#include "wiz8/inventory.h"

#include <bitset>
#include <cassert>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/levels.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/notices.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/inventory.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"

enum { W8_ITEM_USE_TYPE_COUNT = 10 };

static bool IsHandSlot(unsigned int slot)
{
    return slot == W8_EQUIP_SLOT_PRIMARY_WEAPON || slot == W8_EQUIP_SLOT_SECONDARY_WEAPON;
}

Inventory::Inventory(Game& game)
    : game_(game), storage_(game.data, game.party, game.rng, game.events)
{
}

PlayPhase Inventory::phase() const
{
    return game_.status.game_started ? PlayPhase::Play : PlayPhase::Creation;
}

// ---- Consequences of an edit

/* The consequences of one live inventory edit. The edit names the locations whose item it
   replaced or emptied; a change of quantity alone is not a replacement. A committed item
   use whose item is gone or has moved is abandoned, a weapon hand change reconciles combat
   choices, and a changed hand is reported. */
void Inventory::finishEdit(std::span<const W8ItemLocation> replaced)
{
    auto& party = game_.party;
    std::bitset<W8_PARTY_SLOT_COUNT> changed_hands;
    bool any = false;
    for (const auto location : replaced) {
        if (location.storage == W8ItemStorage::None) continue;
        any = true;
        if (location.storage == W8ItemStorage::Equipment && IsHandSlot(location.slot))
            changed_hands.set(location.character);
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& action = party.rows[slot].committed;
        const auto& use = action.detail_block.item_use;
        if (game_.status.game_started && party.members[slot].has_value() &&
            action.kind == W8_ACTION_USE_ITEM && use.item.storage != W8ItemStorage::None &&
            !ResolveItemUse(party, use))
            DropCharacterFromRound(slot);
    }
    SettleActiveParty(game_, changed_hands);
    if (any) game_.events.push(EquipmentSettled{});
}

void Inventory::finishEdit(std::initializer_list<W8ItemLocation> replaced)
{
    finishEdit(std::span{replaced.begin(), replaced.size()});
}

void Inventory::finish(const InventoryEdit& edit)
{
    if (edit.settle) finishEdit(edit.replaced);
}

/* An edit of a character outside the party, such as a draft in party creation, also brings
   that character's own statistics up to date. */
void Inventory::finishCharacterEdit(ItemRecipient recipient,
                                    std::initializer_list<W8ItemLocation> replaced)
{
    finishEdit(replaced);
    if (!recipient.slot) UpdateStandaloneCharacterStats(game_.data, recipient.character, {});
}

// ---- Acquiring items

/* What acquiring an item sets off: a handful of plot items update their facts, and an
   exceptional one draws a remark, from the receiver or from a random member when the pool
   took it. A placement into equipment acquires nothing. */
/* A party location names its own receiver: a member, or nobody for the pool. */
void Inventory::acquire(const std::optional<ItemPlacement>& placement, W8Character* receiver)
{
    if (!placement || !placement->received) return;
    const auto& received = *placement->received;
    if (received.location) {
        const auto location = *received.location;
        receiver = location.storage == W8ItemStorage::Pool
                       ? nullptr
                       : &game_.party.characters[location.character];
    }
    acquire(received.get(game_.party), receiver);
}

void Inventory::acquire(W8ItemInstance& item, W8Character* receiver)
{
    UpdateFactsAfterAcquiringItem(&item);
    reactToExceptionalItem(item, receiver);
}

// Character creation (screen 5) deliberately suppresses every reaction.
void Inventory::reactToExceptionalItem(W8ItemInstance& item, W8Character* receiver)
{
    const bool choose_character = receiver == nullptr;
    W8Character* character = receiver;
    int message;

    if (CurrentScreenState().id == W8_SCREEN_PARTY_SELECTION) {
        return;
    }
    if (choose_character) {
        int party_slot = GetRandomCharacter(0, 0, -1, -1);
        if (party_slot == -1) {
            return;
        }
        character = &game_.party.characters[party_slot];
    }
    if ((item.effect_used & 1) != 0) {
        return;
    }

    switch (item.iItemNo) {
    case 0x239:
        message = 132;
        break;
    case 0x242:
        message = 46;
        break;
    case 0x243:
        message = 47;
        break;
    case 0x244:
        message = game_.story.factions[W8_FACTION_MOOK].disposition_score != 0 ? 48 : 49;
        break;
    case 0x264:
        message = 66;
        break;
    case 0x27c: {
        SetFact(W8_FACT_DIAMOND_FOUND, 1, false);
        if (!NpcLeadHasNameStyle(W8_NPC_MYLES) || !GetFact(W8_FACT_QUEST_MYLES_FIND_DIAMOND)) {
            return;
        }
        W8NpcState* npc = GetNpcStateByKind(7);
        if (npc == 0) {
            return;
        }
        signed char npc_slot = npc->group_index;
        QueueCharacterEvent(&game_.party.characters[npc_slot], 65, 0, kCharacterEventFullVolume);
        return;
    }
    default:
        if (choose_character || !CanCharacterUseItem(game_.data, character, item.iItemNo) ||
            game_.data.items[item.iItemNo].value / character->uiExpLevel <= 500 ||
            !item.identified) {
            return;
        }
        message = 55;
        break;
    }

    QueueCharacterEvent(character, message, 0, kCharacterEventFullVolume);
    item.effect_used |= 1;
}

std::optional<ItemPlacement> Inventory::storeWithCharacter(ItemRecipient recipient,
                                                           ItemReference source,
                                                           bool equip_if_possible, bool announce,
                                                           bool skip_stacking)
{
    const auto placement = storage_.placeWithCharacter(recipient, source, equip_if_possible,
                                                       announce, skip_stacking, phase());
    acquire(placement, &recipient.character);
    return placement;
}

std::optional<ItemPlacement> Inventory::storeInPool(ItemReference source, bool announce,
                                                    bool skip_stacking)
{
    const auto placement = storage_.placeInPool(source, announce, skip_stacking);
    acquire(placement, nullptr);
    return placement;
}

/* Put an item somewhere it will fit. The flag decides which of the character and the party
   pool is tried first; the other is tried after. */
std::optional<ItemPlacement>
Inventory::storeWithCharacterOrPoolFirst(ItemRecipient recipient, ItemReference source,
                                         bool party_first, bool announce, bool equip_if_possible)
{
    const auto placement = storage_.placeWithCharacterOrPool(recipient, source, party_first,
                                                             announce, equip_if_possible, phase());
    acquire(placement, &recipient.character);
    return placement;
}

// The source's location when a store took the item, and where a new entry went.
static W8ItemLocation From(const ItemReference& source, bool stored)
{
    return stored && source.location ? *source.location : W8ItemLocation{};
}

static W8ItemLocation To(const std::optional<ItemPlacement>& placement)
{
    return placement ? placement->stored : W8ItemLocation{};
}

/* What no store takes is dropped at the party's feet unless it may not be discarded, and
   otherwise goes into an empty hand. Failure leaves the source intact. */
bool Inventory::dropUnstored(ItemReference source)
{
    if ((game_.data.items[source.get(game_.party).iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (const auto position = FindItemDropPosition()) {
            AddWorldItem(CreateWorldItem(&source.get(game_.party), &*position,
                                         W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            return true;
        }
    }
    return storage_.placeInEmptyHand(source);
}

ItemRecipient Inventory::member(unsigned int party_slot)
{
    return {game_.party.characters[party_slot], party_slot};
}

bool Inventory::addToCharacter(ItemRecipient recipient, ItemReference source,
                               bool equip_if_possible, bool announce, bool skip_stacking)
{
    const auto stored =
        storeWithCharacter(recipient, source, equip_if_possible, announce, skip_stacking);
    finishCharacterEdit(recipient, {From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

bool Inventory::addToPool(ItemReference source, bool announce, bool skip_stacking)
{
    const auto stored = storeInPool(source, announce, skip_stacking);
    finishEdit({From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

/* Put an item in the party pool, and when the pool will not take it, drop it at the party's
   feet or into an empty hand. */
bool Inventory::addToPoolOrDrop(ItemReference source, bool announce)
{
    const auto stored = storeInPool(source, announce, false);
    if (!stored) dropUnstored(source);
    finishEdit({From(source, true), stored ? stored->stored : W8ItemLocation{W8ItemStorage::Hand}});
    return stored.has_value();
}

bool Inventory::storeWithCharacterOrPool(ItemRecipient recipient, ItemReference source,
                                         bool party_first, bool announce, bool equip_if_possible)
{
    const auto stored =
        storeWithCharacterOrPoolFirst(recipient, source, party_first, announce, equip_if_possible);
    finishCharacterEdit(recipient, {From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

/* The character and the party pool are tried in the order the flag asks for, with the other
   tried after - and when the slot cannot take items, only the pool. */
bool Inventory::give(unsigned int party_slot, ItemReference source, bool party_first)
{
    assert(game_.status.game_started);
    assert(game_.party.members[party_slot].has_value());

    std::optional<ItemPlacement> destination;
    if (!IsPartySlotEligible(static_cast<int>(party_slot))) {
        if (party_first) destination = storeInPool(source, true, false);
    } else {
        destination =
            storeWithCharacterOrPoolFirst(member(party_slot), source, party_first, true, false);
    }
    // A stored item has left its source; a refused one is untouched.
    finishEdit({From(source, destination.has_value()), To(destination)});
    return destination.has_value();
}

/* Identify the held item first, then use the same transfer as every other party
   item. Placement owns source consumption; a refused transfer keeps its remainder. */
std::expected<void, InventoryRefusal> Inventory::giveHeld(unsigned int party_slot, bool party_first)
{
    auto& held = game_.party.held.item;
    assert(held.iItemNo != -1);
    if (!held.identified) PartyAttemptsToIdentifyItem(&held, 0);
    const bool slot_closed = !IsPartySlotEligible(static_cast<int>(party_slot)) && !party_first;
    if (give(party_slot, PartyItem({W8ItemStorage::Hand}), party_first)) return {};
    return std::unexpected(InventoryRefusal{game_.data.strings[slot_closed ? 0x1f5 : 0x90c]});
}

/* Equipment that is bound to its slot stays with the body unless the binding was announced,
   the slot has no paper-doll position, or the member is dead. What the pool cannot take is
   dropped into the world or held when the hand is empty. */
void Inventory::stashDepartingMember(unsigned int party_slot)
{
    auto& character = game_.party.characters[party_slot];
    const auto stash = [&](W8ItemLocation location) {
        const int item_id = game_.party.itemAt(location)->iItemNo;
        const auto source = PartyItem(location);
        if (storeInPool(source, false, false)) return;
        if (!dropUnstored(source)) return;
        game_.events.push(
            TextNotice{W8_FONT_PALETTE_RED, fmt::sprintf(game_.data.strings[0x7d3],
                                                         game_.data.items[item_id].display_name)});
    };
    for (unsigned int equip_slot = 0; equip_slot < W8_EQUIP_SLOT_COUNT; ++equip_slot) {
        const auto& item = character.EquippedItem[equip_slot];
        if (item.iItemNo != -1 &&
            (game_.data.items[item.iItemNo].binds_on_equip == 0 || item.bind_announced ||
             !HasPaperDollPosition(static_cast<W8EquipSlot>(equip_slot)) ||
             character.uiCondition[W8_CONDITION_DEAD] != 0))
            stash({W8ItemStorage::Equipment, party_slot, equip_slot});
    }
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (character.backpack[slot].iItemNo != -1)
            stash({W8ItemStorage::Backpack, party_slot, slot});
    finishEdit({});
}

void Inventory::emptyOutsider(W8Character& outsider)
{
    for (auto& item : outsider.EquippedItem) item = {};
    for (auto& item : outsider.backpack) item = {};
    finishCharacterEdit(Outsider(outsider), {});
}

/* The ladder answers with the item id and the notice to show; the first tier hands over the
   welcome item outright, and an item the character already holds only gets its id raised
   when it is below the tier's. Returns the location whose item was replaced. */
W8ItemLocation Inventory::upgradeClassItemIn(ItemRecipient recipient)
{
    auto& character = recipient.character;
    const auto notice = [&](unsigned int message, unsigned int pronoun_column) {
        assert(recipient.slot);
        game_.events.push(CharacterNotice{
            *recipient.slot,
            fmt::sprintf(
                game_.data.strings[message],
                game_.data.strings[g_gender_name_message_rows[character.gender][pronoun_column]])});
    };
    int item_id;
    unsigned int message_id;
    switch (character.profession_levels[W8_PROFESSION_GADGETEER]) {
    case 0:
    case 2:
        return {};
    case 1: {
        if (FindCharacterItem(character, ItemOfKind(game_.data, 0x83), W8ItemReach::Carried))
            return {};
        auto created = MakeItem(game_.data, game_.rng, 599, false, true, true);
        auto stored = storeWithCharacter(recipient, OwnedItem(created), false, true, false);
        if (!stored) stored = storeInPool(OwnedItem(created), true, false);
        if (!stored) return {};
        notice(0x167, 3);
        return stored->stored;
    }
    case 3:
    case 4:
        item_id = 600;
        message_id = 0x168;
        break;
    case 5:
    case 6:
        item_id = 0x259;
        message_id = 0x169;
        break;
    case 7:
    case 8:
        item_id = 0x25a;
        message_id = 0x16a;
        break;
    case 9:
    case 10:
        item_id = 0x25b;
        message_id = 0x16b;
        break;
    case 0xb:
    case 0xc:
        item_id = 0x25c;
        message_id = 0x16c;
        break;
    case 0xd:
    case 0xe:
    case 0xf:
        item_id = 0x25d;
        message_id = 0x16d;
        break;
    case 0x10:
    case 0x11:
        item_id = 0x25e;
        message_id = 0x16e;
        break;
    case 0x12:
    case 0x13:
    case 0x14:
        item_id = 0x25f;
        message_id = 0x16f;
        break;
    case 0x15:
    case 0x16:
        item_id = 0x260;
        message_id = 0x170;
        break;
    case 0x17:
    case 0x18:
    case 0x19:
        item_id = 0x261;
        message_id = 0x171;
        break;
    default:
        item_id = 0x262;
        message_id = 0x172;
        break;
    }

    // The character's own class item, in equipment slot order and then backpack order.
    W8ItemInstance* found = nullptr;
    W8ItemLocation location{};
    const auto search = [&](std::span<W8ItemInstance> slots, W8ItemStorage storage) {
        for (unsigned int index = 0; index < slots.size() && !found; ++index) {
            if (!ItemIsOfKind(game_.data, slots[index], 0x83)) continue;
            found = &slots[index];
            if (recipient.slot) location = {storage, *recipient.slot, index};
        }
    };
    search(character.EquippedItem, W8ItemStorage::Equipment);
    search(character.backpack, W8ItemStorage::Backpack);
    if (!found) {
        location = FindPartyItem(game_.party, ItemOfKind(game_.data, 0x83), W8ItemReach::Party);
        found = game_.party.itemAt(location);
    }
    if (found && found->iItemNo < item_id) {
        found->iItemNo = item_id;
        notice(message_id, 2);
        return location;
    }
    return {};
}

void Inventory::upgradeClassItem(ItemRecipient recipient)
{
    finishCharacterEdit(recipient, {upgradeClassItemIn(recipient)});
}

bool Inventory::removeItemById(int item_id, bool remove_all)
{
    const auto edit = storage_.removeItemById(item_id, remove_all);
    finish(edit);
    return !edit.replaced.empty();
}

void Inventory::removeQuantity(W8ItemLocation location, unsigned amount)
{
    if (const auto edit = storage_.removeQuantity(location, amount)) finish(*edit);
}

W8ItemInstance Inventory::take(W8ItemLocation location)
{
    auto item = storage_.take(location);
    if (!item) return {};
    finishEdit({location});
    return *item;
}

/* The gold goes into the purse and into the level's running tally, and announcing it plays
   the coin sound. */
void Inventory::addGold(int amount, bool announce)
{
    const std::string_view sound_path = "Data\\Sound\\Misc\\ChaChing.wav";
    game_.party.gold += amount;
    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT)
        game_.status.level_progress[GetLoadedLevelID()].gold_collected += amount;
    if (announce) {
        game_.events.push(TextNotice{8, fmt::format("{} {:d} {}.", game_.data.strings[0x15f],
                                                    amount, game_.data.strings[0x160])});
        if (!wiz8::sound::is_file_playing(sound_path)) wiz8::sound::play(sound_path);
    }
}

// ---- The hand

void Inventory::replaceHeld(W8ItemInstance item)
{
    game_.party.held = {.item = item};
    finishEdit({{W8ItemStorage::Hand}});
}

bool Inventory::placeInHand(int item_id, bool maximum_quantity, bool force_identified,
                            bool mark_special)
{
    if (game_.party.held.item.iItemNo != -1) return false;
    replaceHeld(
        MakeItem(game_.data, game_.rng, item_id, maximum_quantity, force_identified, mark_special));
    return true;
}

void Inventory::replaceHeldWithNew(int item_id, bool quality)
{
    game_.party.take({W8ItemStorage::Hand});
    auto created = MakeItem(game_.data, game_.rng, item_id, true, quality, false);
    storage_.placeInEmptyHand(OwnedItem(created));
    finishEdit({{W8ItemStorage::Hand}});
}

bool Inventory::receiveInHand(W8ItemInstance& item)
{
    auto& party = game_.party;
    if (party.held.item.iItemNo != -1 || item.iItemNo == -1) return false;
    party.held = {std::exchange(item, W8ItemInstance{}), {}, -1};
    finishEdit({{W8ItemStorage::Hand}});
    return true;
}

// An item that may not be discarded is refused.
std::expected<void, InventoryRefusal> Inventory::dropHeld(int unused)
{
    if ((game_.data.items[game_.party.held.item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0)
        return std::unexpected(InventoryRefusal{game_.data.strings[0x4ef]});
    DropHeldItem(unused);
    finishEdit({{W8ItemStorage::Hand}});
    return {};
}

// ---- Player edits

bool Inventory::splitStack(W8ItemLocation source, unsigned amount, unsigned actor)
{
    const auto edit = storage_.splitStack(source, amount, actor);
    if (!edit) return false;
    finish(*edit);
    return true;
}

bool Inventory::exchange(W8ItemLocation first, W8ItemLocation second, unsigned actor)
{
    const auto edit = storage_.exchange(first, second, actor);
    if (!edit) return false;
    finish(*edit);
    return true;
}

bool Inventory::equipHeldAndStowPartner(unsigned actor, W8EquipSlot slot)
{
    const auto npc_keeps = [&](const W8ItemInstance& partner) {
        auto* npc = actor < 2 ? GetNpcState(game_.party.npcIndex(actor)) : nullptr;
        return npc && NpcWantsItem(npc, &partner);
    };
    const auto stowed = storage_.equipHeldAndStowPartner(actor, slot, npc_keeps);
    if (!stowed) return false;
    const bool pool = stowed->stored && stowed->stored->stored.storage == W8ItemStorage::Pool;
    acquire(stowed->stored, pool ? nullptr : &game_.party.characters[actor]);
    finish(stowed->edit);
    return true;
}

StackMergeResult Inventory::mergeStacks(W8ItemLocation destination, W8ItemLocation source)
{
    const auto merge = storage_.mergeStacks(destination, source);
    if (!merge) return {};
    finish(merge->edit);
    return {merge->complete, true};
}

bool Inventory::insertHeldInPool(unsigned index)
{
    const auto edit = storage_.insertHeldInPool(index);
    if (!edit) return false;
    finish(*edit);
    return true;
}

// Practice follows a completed recipe as a phase of its own.
std::expected<bool, InventoryRefusal> Inventory::combine(unsigned actor, W8ItemLocation target)
{
    const auto combination = storage_.combine(actor, target);
    if (!combination) return std::unexpected(combination.error());
    if (!*combination) return false;
    const auto& made = **combination;
    finish(made.edit);
    if (made.practised_skill) {
        auto* character = &game_.party.characters[actor];
        QueueCharacterEvent(character, g_learn_sound, 0, kCharacterEventFullVolume);
        PracticeCharacterSkill(game_, character, *made.practised_skill, made.practice, false);
    }
    return made.merged;
}

void Inventory::mergeUses(unsigned actor, W8ItemLocation destination, W8ItemLocation source)
{
    if (const auto edit = storage_.mergeUses(actor, destination, source)) finish(*edit);
}

// The screens sort only once play has started.
void Inventory::sortPool()
{
    if (!game_.status.game_started) throw std::logic_error("Inventory::sortPool: before play");
    storage_.sortPool();
}

// ---- Weapon sets and ammunition

void Inventory::bind(W8Character& character, W8EquipSlot equip_slot)
{
    storage_.bind(character, equip_slot);
}

void Inventory::commitWeaponSetSwap(unsigned int party_slot, W8WeaponSwap choice)
{
    finish(storage_.commitWeaponSetSwap(party_slot, choice));
}

/* The notices go to the conversation's text box while the party talks. */
bool Inventory::swapWeaponSets(unsigned int party_slot, bool announce)
{
    const short notice_context = game_.activity.conversation ? 0 : -1;
    const auto plan = storage_.prepareWeaponSetSwap(party_slot, announce, notice_context);
    if (!plan) return false;
    commitWeaponSetSwap(party_slot, *plan);
    if (announce)
        game_.events.push(CharacterNotice{party_slot, game_.data.strings[0x1ec], notice_context});
    return true;
}

/* The combat UI only allows this once the turn is really the member's: a free-turn phase or
   a party movement mode refuses the whole request with a notice, and so does the member
   whose attack is being carried out. Otherwise the weapon sets swap, the queued swap
   clears, and the portraits redraw. */
void Inventory::requestWeaponSetSwap(unsigned int party_slot, bool announce)
{
    if (game_.combat.has_value()) {
        if (!game_.combat->round_active && !game_.party_movement_mode) {
            game_.events.push(TextNotice{W8_FONT_PALETTE_BEIGE,
                                         game_.data.strings[W8_NOTICE_WEAPON_SWAP_BLOCKED_COMBAT]});
            return;
        }
        if (game_.combat->resolvingTurnOf(PartyActor{static_cast<int>(party_slot)}) &&
            (game_.party.rows[party_slot].executing.kind == W8_ACTION_ATTACK ||
             game_.party.rows[party_slot].executing.kind == W8_ACTION_BERSERK)) {
            game_.events.push(TextNotice{
                W8_FONT_PALETTE_WHITE,
                fmt::sprintf(game_.data.strings[0x1f7], game_.party.characters[party_slot].name)});
            return;
        }
    }

    if (IsPartySlotEligible(static_cast<int>(party_slot)) && swapWeaponSets(party_slot, announce))
        game_.party.rows[party_slot].weapon_swap_pending = false;
    game_.events.push(CharacterActionChanged{});
}

bool Inventory::consumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used,
                                   W8EquipSlot paired)
{
    const auto spent = storage_.consumeAttackItems(actor, hand, used, paired);
    finish(spent.edit);
    return spent.thrown_spent;
}

void Inventory::readyPrimaryWeapon(unsigned party_slot)
{
    finish(storage_.readyPrimaryWeapon(party_slot));
}

// ---- Equipment rule

/* Each unusable piece is stored like a received item: on the member, then in the pool, and
   otherwise at the party's feet. */
bool Inventory::unequipUnusable(unsigned int party_slot)
{
    bool hands_changed = false;
    std::bitset<W8_EQUIP_SLOT_COUNT> kept;
    while (auto removed = storage_.takeUnusableEquipment(party_slot, phase(), kept)) {
        const auto source = OwnedItem(removed->item);
        if (!storeWithCharacter(member(party_slot), source, false, false, false) &&
            !storeInPool(source, false, false) && !dropUnstored(source)) {
            game_.party.characters[party_slot].EquippedItem[removed->equip_slot] = removed->item;
            kept.set(removed->equip_slot);
            continue;
        }
        hands_changed |= removed->weapon_hand;
        if (!removed->notice.empty())
            game_.events.push(CharacterNotice{party_slot, std::move(removed->notice)});
    }
    return hands_changed;
}

// ---- Using items

/* Use one item as a character's action. The attempts that cannot happen at all
   come first, each with its own notice and character event: a spent charge
   stack, a spent dose, a class-0x0d item whose influence the character is still
   under, and the one item that resolves to an NPC fact instead of a spell.
   Everything else aims the item and casts the spell it carries, which is where
   the spell's presentation skill and the character's own level in it decide how
   hard the attempt is. `out_uses` receives the fatigue cost of the attempt, and
   stays -1 when nothing was attempted; a casting aid's attempt costs eight per
   level of its spell plus twenty, even when it fails. */
Inventory::ItemUseOutcome Inventory::useOnce(unsigned int party_slot, W8ItemLocation location)
{
    W8Character* character = &game_.party.characters[party_slot];
    W8ItemInstance* item = game_.party.itemAt(location);
    const W8ItemDatabaseRecord* record = &game_.data.items[item->iItemNo];
    unsigned int power;
    unsigned int chance;
    W8Skill skill;
    int event_type;
    int fatigue_cost = -1;
    bool emptied = false;
    const auto own_cost = [&] {
        return fatigue_cost < 0 ? std::nullopt : std::optional<int>{fatigue_cost};
    };
    W8TargetSource target;
    // Retail returned an unset byte for a spent item; nothing was used.
    unsigned char used = 0;

    if (!CanCharacterUseItem(game_.data, character, item->iItemNo)) {
        game_.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game_.data.strings[0x164],
                                                     GetItemDisplayName(game_.data, item))});
        return {};
    }
    if (!CanCharacterActivateItem(character, item)) {
        game_.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game_.data.strings[0x165],
                                                     GetItemDisplayName(game_.data, item))});
        return {};
    }

    if (record->quantity_kind == W8_ITEM_QUANTITY_CHARGES && item->uses_or_charges == 0) {
        game_.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game_.data.strings[0x1f1],
                                                     GetItemDisplayName(game_.data, item))});
        event_type = g_special_event13;
    } else if (record->quantity_kind == W8_ITEM_QUANTITY_SHOTS && item->uses_or_charges == 0) {
        game_.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game_.data.strings[0x1f2],
                                                     GetItemDisplayName(game_.data, item))});
        event_type = 39;
    } else {
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            character->uiCondition[W8_CONDITION_SILENCED] != 0) {
            game_.events.push(CharacterNotice{party_slot, game_.data.strings[0x166]});
            return {};
        }

        /* The one item that is not used on anybody: it hands the level's NPC
           over to the fact that says the party has met them. */
        if (item->iItemNo == 0x29f) {
            [[maybe_unused]] W8NpcState* npc = GetNpcState(game_.party.npcIndex(party_slot));
            assert(npc != 0);
            assert(npc->name_style == W8_NPC_RFS81_A);
            SetFact(W8_FACT_RFS81_HAS_BEEN_FIXED, 1, false);
            SetFact(W8_FACT_QUEST_ANDROID_BROKEN, 0, false);
            return {{1, std::nullopt}, storage_.consumeUse(location, true)};
        }

        if (record->equip_class != W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            record->equip_class != W8_ITEM_EQUIP_CLASS_GADGET) {
            /* An ordinary item carries the spell's own power, and the one use
               kind whose presentation skill is nine costs a flat ten. */
            if (GetItemSpellPresentation(record) == 9) {
                fatigue_cost = 10;
            }
            power = record->spell_power;
        } else {
            /* A casting aid has to beat the difficulty of the character's own
               level in the skill that presents the spell. Each attempt that
               fails is retried one power lower, and the first roll that
               succeeds ends the search. Bug: spells 0x58 and 0x74 have no
               presentation skill, and still index skills[-1] here. */
            skill = GetItemSpellPresentation(record);
            /* The attempt costs fatigue by the carried spell's level, whether
               or not it succeeds. */
            fatigue_cost = game_.data.spells[record->spell_id].spell_level * 8 + 0x14;
            power = 7;
            do {
                if (GetItemUseDifficulty(character, skill, character->skills[skill].level,
                                         record->spell_id, power) > 0xf) {
                    --power;
                    continue;
                }
                chance = power * 7;
                while (power != 0 && chance > game_.rng.below(100)) {
                    --power;
                    chance -= 7;
                }
                break;
            } while (power != 0);

            if (power == 0) {
                game_.events.push(CharacterNotice{
                    party_slot,
                    fmt::sprintf(game_.data.strings[0x1f3], GetItemDisplayName(game_.data, item))});
                PracticeCharacterSkill(game_, character, skill, 1, false);
                return {{0, own_cost()}};
            }
        }

        /* The targeting classes aim through the slot's own out-of-combat
           target; everything else is used on the character acting. */
        SetTargetSourceToCharacter(party_slot, &target);
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_POTION ||
            (record->equip_class > W8_ITEM_EQUIP_CLASS_SCROLL &&
             record->equip_class < W8_ITEM_EQUIP_CLASS_KEY)) {
            if (!game_.combat.has_value()) {
                W8CombatSlot* aimed = &game_.party.rows[party_slot].executing.target;
                if (aimed->iType == W8_TARGET_KIND_CHARACTER) {
                    SetTargetSourceToCharacter(aimed->iChar, &target);
                } else if (aimed->iType == W8_TARGET_KIND_MONSTER) {
                    SetTargetSourceToMonster(FindMonster(aimed->iMonsterID), &target);
                }
            } else {
                AimStoredAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT,
                                CharacterTarget(party_slot));
            }
        }
        target.name_known = true;

        /* A spell-carrying item the world-cursor handler refuses cannot be used
           at all, and the attempt is over before anything is announced. */
        if (record->spell_id != W8_SPELL_NONE &&
            game_.data.items[item->iItemNo].spell_id != W8_SPELL_NONE &&
            game_.data.items[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_GADGET &&
            (game_.data.items[item->iItemNo].equip_class < W8_ITEM_EQUIP_CLASS_FOOD ||
             game_.data.items[item->iItemNo].equip_class > W8_ITEM_EQUIP_CLASS_DRINK) &&
            DispatchWorldCursorNodeCommand(0, 4, 1)) {
            return {{0, 0}};
        }

        // Items outside the use-message table are used without an announcement.
        const bool announced = static_cast<int>(record->category) < W8_ITEM_USE_TYPE_COUNT;
        if (announced && TargetSourceIsCharacter(&target, 0)) {
            game_.events.push(CharacterNotice{
                static_cast<unsigned int>(target.iChar),
                fmt::format("{} {}", game_.data.strings[g_item_use_messages[record->category * 2]],
                            GetItemDisplayName(game_.data, item))});
        } else if (announced && TargetSourceIsMonster(&target, 0)) {
            game_.events.push(TextNotice{
                9, fmt::format("{} {} {}", GetMonsterName(FindMonster(target.iMonsterID), 0, 0),
                               game_.data.strings[g_item_use_messages[record->category * 2]],
                               GetItemDisplayName(game_.data, item))});
        }

        if (g_settings.verbose_combat_messages == 0) {
            game_.events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        // A usable item without a spell does nothing.
        if (record->spell_id == W8_SPELL_NONE) {
            used = 0;
            fatigue_cost = 0;
        } else {
            used = CastItemSpell(character, item, power);
            if (!used) {
                fatigue_cost = 0;
            } else {
                emptied = storage_.consumeUse(location, true);
            }
        }
        return {{used, own_cost()}, emptied};
    }

    QueueCharacterEvent(&game_.party.characters[party_slot], event_type, 0,
                        kCharacterEventFullVolume);
    return {{used, own_cost()}, emptied};
}

W8ItemUseResult Inventory::use(unsigned actor, const W8ItemUse& item_use)
{
    const auto location = item_use.item;
    if (actor >= W8_PARTY_SLOT_COUNT || !ResolveItemUse(game_.party, item_use)) return {};
    const auto outcome = useOnce(actor, location);
    finishEdit({outcome.emptied ? location : W8ItemLocation{}});
    return outcome.result;
}
