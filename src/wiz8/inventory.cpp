#include "wiz8/inventory.h"

#include <bitset>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/fact_ids.h"
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
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
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
#include "wiz8/platform/assertion.h"
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
#include "wiz8/world/level_session.h"

enum { W8_ITEM_USE_TYPE_COUNT = 10 };

static bool IsHandSlot(unsigned int slot)
{
    return slot == W8_EQUIP_SLOT_PRIMARY_WEAPON || slot == W8_EQUIP_SLOT_SECONDARY_WEAPON;
}

// ---- Consequences of an edit

/* The consequences of one live inventory edit. The edit names the locations whose item it
   replaced or emptied; a change of quantity alone is not a replacement. A committed item
   use whose item is gone or has moved is abandoned, a weapon hand change reconciles combat
   choices, and a changed hand is reported. */
static void FinishInventoryEdit(Game& game, std::span<const W8ItemLocation> replaced)
{
    auto& party = game.party;
    std::bitset<W8_PARTY_SLOT_COUNT> changed_hands;
    bool hand = false;
    bool any = false;
    for (const auto location : replaced) {
        if (location.storage == W8ItemStorage::None) continue;
        any = true;
        hand |= location.storage == W8ItemStorage::Hand;
        if (location.storage == W8ItemStorage::Equipment && IsHandSlot(location.slot))
            changed_hands.set(location.character);
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        const auto& action = party.rows[slot].committed;
        const auto& use = action.detail_block.item_use;
        if (game.status.game_started && party.rows[slot].fOccupied &&
            action.kind == W8_ACTION_USE_ITEM && use.item.storage != W8ItemStorage::None &&
            !ResolveItemUse(party, use))
            DropCharacterFromRound(slot);
    }
    SettleActiveParty(game, changed_hands);
    if (hand) game.events.push(HeldItemChanged{});
    if (any) game.events.push(EquipmentSettled{});
}

static void FinishInventoryEdit(Game& game, std::initializer_list<W8ItemLocation> replaced)
{
    FinishInventoryEdit(game, std::span{replaced.begin(), replaced.size()});
}

static void Finish(Game& game, const InventoryEdit& edit)
{
    if (edit.settle) FinishInventoryEdit(game, edit.replaced);
}

/* An edit of a character outside the party, such as a draft in party creation, also brings
   that character's own statistics up to date. */
static void FinishCharacterInventoryEdit(Game& game, ItemRecipient recipient,
                                         std::initializer_list<W8ItemLocation> replaced)
{
    FinishInventoryEdit(game, replaced);
    if (!recipient.slot) UpdateStandaloneCharacterStats(Data(), recipient.character, {});
}

// ---- Acquiring items

/* What acquiring an item sets off: a handful of plot items update their facts, and an
   exceptional one draws a remark, from the receiver or from a random member when the pool
   took it. A placement into equipment acquires nothing. */
static void Acquire(const std::optional<ItemPlacement>& placement, W8Character* receiver)
{
    if (!placement || !placement->received) return;
    UpdateFactsAfterAcquiringItem(placement->received);
    DeliverExceptionalItemReaction(placement->received, receiver == nullptr, receiver);
}

static std::optional<ItemPlacement> StoreWithCharacter(Game& game, ItemRecipient recipient,
                                                       ItemSource source, bool equip_if_possible,
                                                       bool announce, bool skip_stacking)
{
    const auto placement = PlaceWithCharacter(game.rules(), recipient, source, equip_if_possible,
                                              announce, skip_stacking);
    Acquire(placement, &recipient.character);
    return placement;
}

static std::optional<ItemPlacement> StoreInPool(Game& game, ItemSource source, bool announce,
                                                bool skip_stacking)
{
    const auto placement = PlaceInPool(game.rules(), source, announce, skip_stacking);
    Acquire(placement, nullptr);
    return placement;
}

/* Put an item somewhere it will fit. The flag decides which of the character and the party
   pool is tried first; the other is tried after. */
static std::optional<ItemPlacement> StoreWithCharacterOrPool(Game& game, ItemRecipient recipient,
                                                             ItemSource source, bool party_first,
                                                             bool announce, bool equip_if_possible)
{
    std::optional<ItemPlacement> stored;
    if (!party_first)
        stored = StoreWithCharacter(game, recipient, source, equip_if_possible, announce, false);
    if (!stored) stored = StoreInPool(game, source, announce, false);
    if (!stored && party_first)
        stored = StoreWithCharacter(game, recipient, source, equip_if_possible, announce, false);
    return stored;
}

// The source's location when a store took the item, and where a new entry went.
static W8ItemLocation From(const ItemSource& source, bool stored)
{
    return stored && source.location ? *source.location : W8ItemLocation{};
}

static W8ItemLocation To(const std::optional<ItemPlacement>& placement)
{
    return placement ? placement->stored : W8ItemLocation{};
}

/* What no store takes is dropped at the party's feet unless it may not be discarded, and
   otherwise goes into an empty hand. A drop leaves the source as it was. */
static void DropUnstoredItem(Game& game, ItemSource source)
{
    if ((Data().items[source.item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (const auto position = FindItemDropPosition()) {
            AddWorldItem(CreateWorldItem(&source.item, &*position,
                                         W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            return;
        }
    }
    PlaceInEmptyHand(game.rules(), source);
}

ItemRecipient PartyMember(Game& game, unsigned int party_slot)
{
    return {game.party.characters[party_slot], party_slot};
}

bool AddItemToCharacter(Game& game, ItemRecipient recipient, ItemSource source,
                        bool equip_if_possible, bool announce, bool skip_stacking)
{
    const auto stored =
        StoreWithCharacter(game, recipient, source, equip_if_possible, announce, skip_stacking);
    FinishCharacterInventoryEdit(game, recipient, {From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

bool AddItemToParty(Game& game, ItemSource source, bool announce, bool skip_stacking)
{
    const auto stored = StoreInPool(game, source, announce, skip_stacking);
    FinishInventoryEdit(game, {From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

/* Put an item in the party pool, and when the pool will not take it, drop it at the party's
   feet or into an empty hand. */
bool AddItemToPartyOrDrop(Game& game, ItemSource source, bool announce)
{
    const auto stored = StoreInPool(game, source, announce, false);
    if (!stored) DropUnstoredItem(game, source);
    FinishInventoryEdit(
        game, {From(source, true), stored ? stored->stored : W8ItemLocation{W8ItemStorage::Hand}});
    return stored.has_value();
}

bool StoreItemWithCharacterOrParty(Game& game, ItemRecipient recipient, ItemSource source,
                                   bool party_first, bool announce, bool equip_if_possible)
{
    const auto stored =
        StoreWithCharacterOrPool(game, recipient, source, party_first, announce, equip_if_possible);
    FinishCharacterInventoryEdit(game, recipient, {From(source, stored.has_value()), To(stored)});
    return stored.has_value();
}

/* The character and the party pool are tried in the order the flag asks for, with the other
   tried after - and when the slot cannot take items, only the pool. */
bool GiveItemToCharacterOrParty(Game& game, unsigned int party_slot, ItemSource source,
                                bool party_first)
{
    if (!game.status.game_started) Fail("gStatus.fGameStarted");
    if (!game.party.rows[party_slot].fOccupied) Fail("fCHAR_OCCUPIED(uiChar)");

    std::optional<ItemPlacement> destination;
    if (!IsPartySlotEligible(static_cast<int>(party_slot))) {
        if (party_first) destination = StoreInPool(game, source, true, false);
    } else {
        destination = StoreWithCharacterOrPool(game, PartyMember(game, party_slot), source,
                                               party_first, true, false);
    }
    // A stored item has left its source; a refused one is untouched.
    FinishInventoryEdit(game, {From(source, destination.has_value()), To(destination)});
    return destination.has_value();
}

/* The held item is first offered to everybody who could identify it, and then stored with
   the member or the party pool in the order the flag asks for. The hand is cleared before
   the outcome is tested, and a store that fails is refused: the notice names the slot when
   neither destination was open, and the item otherwise. */
std::expected<void, InventoryRefusal>
GiveHeldItemToCharacterOrParty(Game& game, unsigned int party_slot, bool party_first)
{
    auto& held = game.party.held.item;
    bool stored = party_first;
    if (held.iItemNo == -1) Fail("gStatus.fItemInCursor");
    if (!held.identified) PartyAttemptsToIdentifyItem(&held, 0);
    if (!game.status.game_started) Fail("gStatus.fGameStarted");
    if (!game.party.rows[party_slot].fOccupied) Fail("fCHAR_OCCUPIED(uiChar)");

    const auto source = PartyItem(game.party, {W8ItemStorage::Hand});
    const bool eligible = IsPartySlotEligible(static_cast<int>(party_slot));
    std::optional<ItemPlacement> destination;
    if (eligible) {
        destination = StoreWithCharacterOrPool(game, PartyMember(game, party_slot), source,
                                               party_first, true, false);
        if (destination) stored = true;
    } else if (stored) {
        destination = StoreInPool(game, source, true, false);
        stored = destination.has_value();
    }

    // Only a stored item releases the hand; a refused one stays held.
    if (stored) {
        game.party.take({W8ItemStorage::Hand});
        FinishInventoryEdit(game, {{W8ItemStorage::Hand}, To(destination)});
        return {};
    }

    FinishInventoryEdit(game, {});
    const bool slot_closed = !eligible && !party_first;
    return std::unexpected(InventoryRefusal{Data().strings[slot_closed ? 0x1f5 : 0x90c]});
}

/* Equipment that is bound to its slot stays with the body unless the binding was announced,
   the slot has no paper-doll position, or the member is dead. What the pool cannot take is
   dropped into the world or held when the hand is empty. */
void StashDepartingCharacterItems(Game& game, unsigned int party_slot)
{
    auto& character = game.party.characters[party_slot];
    const auto stash = [&](W8ItemLocation location) {
        const int item_id = game.party.itemAt(location)->iItemNo;
        const auto source = PartyItem(game.party, location);
        if (StoreInPool(game, source, false, false)) return;
        DropUnstoredItem(game, source);
        game.events.push(
            TextNotice{W8_FONT_PALETTE_RED,
                       fmt::sprintf(Data().strings[0x7d3], Data().items[item_id].display_name)});
    };
    for (unsigned int equip_slot = 0; equip_slot < W8_EQUIP_SLOT_COUNT; ++equip_slot) {
        const auto& item = character.EquippedItem[equip_slot];
        if (item.iItemNo != -1 &&
            (Data().items[item.iItemNo].binds_on_equip == 0 || item.bind_announced ||
             !HasPaperDollPosition(static_cast<W8EquipSlot>(equip_slot)) ||
             character.uiCondition[W8_CONDITION_DEAD] != 0))
            stash({W8ItemStorage::Equipment, party_slot, equip_slot});
    }
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (character.backpack[slot].iItemNo != -1)
            stash({W8ItemStorage::Backpack, party_slot, slot});
    FinishInventoryEdit(game, {});
}

void EmptyAllCarriedItems(Game& game, W8Character& outsider)
{
    for (auto& item : outsider.EquippedItem) item = {};
    for (auto& item : outsider.backpack) item = {};
    FinishCharacterInventoryEdit(game, Outsider(outsider), {});
}

/* The ladder answers with the item id and the notice to show; the first tier hands over the
   welcome item outright, and an item the character already holds only gets its id raised
   when it is below the tier's. Returns the location whose item was replaced. */
static W8ItemLocation UpgradeClassItem(Game& game, ItemRecipient recipient)
{
    auto& character = recipient.character;
    const auto notice = [&](unsigned int message, unsigned int pronoun_column) {
        if (!recipient.slot) Fail("pPC->fInParty", "the class item notice names a member");
        game.events.push(CharacterNotice{
            *recipient.slot,
            fmt::sprintf(
                Data().strings[message],
                Data().strings[g_gender_name_message_rows[character.gender][pronoun_column]])});
    };
    int item_id;
    unsigned int message_id;
    switch (character.profession_levels[W8_PROFESSION_GADGETEER]) {
    case 0:
    case 2:
        return {};
    case 1: {
        if (FindCharacterItem(character, ItemOfKind(Data(), 0x83), W8ItemReach::Carried)) return {};
        auto created = MakeItem(Data(), game.rng, 599, false, true, true);
        auto stored = StoreWithCharacter(game, recipient, OwnedItem(created), false, true, false);
        if (!stored) stored = StoreInPool(game, OwnedItem(created), true, false);
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
            if (!ItemIsOfKind(Data(), slots[index], 0x83)) continue;
            found = &slots[index];
            if (recipient.slot) location = {storage, *recipient.slot, index};
        }
    };
    search(character.EquippedItem, W8ItemStorage::Equipment);
    search(character.backpack, W8ItemStorage::Backpack);
    if (!found) {
        location = FindPartyItem(game.party, ItemOfKind(Data(), 0x83), W8ItemReach::Party);
        found = game.party.itemAt(location);
    }
    if (found && found->iItemNo < item_id) {
        found->iItemNo = item_id;
        notice(message_id, 2);
        return location;
    }
    return {};
}

void UpgradeProfessionClassItem(Game& game, ItemRecipient recipient)
{
    FinishCharacterInventoryEdit(game, recipient, {UpgradeClassItem(game, recipient)});
}

bool RemovePartyItemByID(Game& game, int item_id, bool remove_all)
{
    const auto edit = RemovePartyItemByID(game.rules(), item_id, remove_all);
    Finish(game, edit);
    return !edit.replaced.empty();
}

void RemoveInventoryQuantity(Game& game, W8ItemLocation location, unsigned amount)
{
    if (const auto edit = RemoveInventoryQuantity(game.rules(), location, amount))
        Finish(game, *edit);
}

W8ItemInstance TakeInventoryItem(Game& game, W8ItemLocation location)
{
    auto item = TakeInventoryItem(game.rules(), location);
    if (!item) return {};
    FinishInventoryEdit(game, {location});
    return *item;
}

/* The gold goes into the purse and into the level's running tally, and announcing it plays
   the coin sound. */
void AddPartyGold(Game& game, int amount, bool announce)
{
    const std::string_view sound_path = "Data\\Sound\\Misc\\ChaChing.wav";
    game.party.gold += amount;
    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT)
        game.status.level_progress[GetLoadedLevelID()].gold_collected += amount;
    if (announce) {
        game.events.push(TextNotice{
            8, fmt::format("{} {:d} {}.", Data().strings[0x15f], amount, Data().strings[0x160])});
        if (!wiz8::sound::is_file_playing(sound_path)) wiz8::sound::play(sound_path);
    }
}

// ---- The hand

void ReplaceHeldItem(Game& game, W8ItemInstance item)
{
    game.party.held = {.item = item};
    FinishInventoryEdit(game, {{W8ItemStorage::Hand}});
}

bool PlaceItemOnCursor(Game& game, int item_id, bool maximum_quantity, bool force_identified,
                       bool mark_special)
{
    if (game.party.held.item.iItemNo != -1) return false;
    ReplaceHeldItem(game, MakeItem(Data(), game.rng, item_id, maximum_quantity, force_identified,
                                   mark_special));
    return true;
}

void ReplaceHeldItemWithNew(Game& game, int item_id, bool quality)
{
    game.party.take({W8ItemStorage::Hand});
    auto created = MakeItem(Data(), game.rng, item_id, true, quality, false);
    PlaceInEmptyHand(game.rules(), OwnedItem(created));
    FinishInventoryEdit(game, {{W8ItemStorage::Hand}});
}

bool ReceiveItemInHand(Game& game, W8ItemInstance& item)
{
    auto& party = game.party;
    if (party.held.item.iItemNo != -1 || item.iItemNo == -1) return false;
    party.held = {std::exchange(item, W8ItemInstance{}), {}, -1};
    FinishInventoryEdit(game, {{W8ItemStorage::Hand}});
    return true;
}

// An item that may not be discarded is refused.
std::expected<void, InventoryRefusal> DropItemInHand(Game& game, int unused)
{
    if ((Data().items[game.party.held.item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0)
        return std::unexpected(InventoryRefusal{Data().strings[0x4ef]});
    DropHeldItem(unused);
    FinishInventoryEdit(game, {{W8ItemStorage::Hand}});
    return {};
}

// ---- Player edits

bool SplitItemStack(Game& game, W8ItemLocation source, unsigned amount, unsigned actor)
{
    const auto edit = SplitItemStack(game.rules(), source, amount, actor);
    if (!edit) return false;
    Finish(game, *edit);
    return true;
}

bool ExchangeInventoryItems(Game& game, W8ItemLocation first, W8ItemLocation second, unsigned actor)
{
    const auto edit = ExchangeInventoryItems(game.rules(), first, second, actor);
    if (!edit) return false;
    Finish(game, *edit);
    return true;
}

bool EquipHeldItemAndStowPartner(Game& game, unsigned actor, W8EquipSlot slot)
{
    const auto npc_keeps = [&](const W8ItemInstance& partner) {
        auto* npc = actor < 2 ? GetNpcState(game.party.rows[actor].npc_index) : nullptr;
        return npc && NpcWantsItem(npc, &partner);
    };
    const auto stowed = EquipHeldItemAndStowPartner(game.rules(), actor, slot, npc_keeps);
    if (!stowed) return false;
    const bool pool = stowed->stored && stowed->stored->stored.storage == W8ItemStorage::Pool;
    Acquire(stowed->stored, pool ? nullptr : &game.party.characters[actor]);
    Finish(game, stowed->edit);
    return true;
}

StackMergeResult MergeInventoryStacks(Game& game, W8ItemLocation destination, W8ItemLocation source)
{
    const auto merge = MergeInventoryStacks(game.rules(), destination, source);
    if (!merge) return {};
    Finish(game, merge->edit);
    return {merge->complete, true};
}

bool InsertHeldItemInPool(Game& game, unsigned index)
{
    const auto edit = InsertHeldItemInPool(game.rules(), index);
    if (!edit) return false;
    Finish(game, *edit);
    return true;
}

// Practice follows a completed recipe as a phase of its own.
std::expected<bool, InventoryRefusal> MergeItems(Game& game, unsigned actor, W8ItemLocation target)
{
    const auto combination = MergeItems(game.rules(), actor, target);
    if (!combination) return std::unexpected(combination.error());
    if (!*combination) return false;
    const auto& made = **combination;
    Finish(game, made.edit);
    if (made.practised_skill) {
        auto* character = &game.party.characters[actor];
        QueueCharacterEvent(character, g_learn_sound, 0, kCharacterEventFullVolume);
        PracticeCharacterSkill(game, character, *made.practised_skill, made.practice, false);
    }
    return made.merged;
}

void MergeItemUses(Game& game, unsigned actor, W8ItemLocation destination, W8ItemLocation source)
{
    if (const auto edit = MergeItemUses(game.rules(), actor, destination, source))
        Finish(game, *edit);
}

// ---- Weapon sets and ammunition

void CommitWeaponSetSwap(Game& game, unsigned int party_slot, W8WeaponSwap choice)
{
    Finish(game, CommitWeaponSetSwap(game.rules(), party_slot, choice));
}

/* The notices go to the conversation's text box while the party talks. */
bool SwapWeaponSetSlots(Game& game, unsigned int party_slot, bool announce)
{
    const short notice_context = game.activity.conversation ? 0 : -1;
    const auto plan = PrepareWeaponSetSwap(game.rules(), party_slot, announce, notice_context);
    if (!plan) return false;
    CommitWeaponSetSwap(game, party_slot, *plan);
    if (announce)
        game.events.push(CharacterNotice{party_slot, Data().strings[0x1ec], notice_context});
    return true;
}

/* The combat UI only allows this once the turn is really the member's: a free-turn phase or
   a party movement mode refuses the whole request with a notice, and so does the member
   whose attack is being carried out. Otherwise the weapon sets swap, the queued swap
   clears, and the portraits redraw. */
void BindCharacterItems(Game& game, unsigned int party_slot, bool announce)
{
    if (game.combat.has_value()) {
        if (!game.combat->round_active && !game.party_movement_mode) {
            game.events.push(TextNotice{W8_FONT_PALETTE_BEIGE,
                                        Data().strings[W8_NOTICE_WEAPON_SWAP_BLOCKED_COMBAT]});
            return;
        }
        if (game.combat->resolvingTurnOf(PartyActor{static_cast<int>(party_slot)}) &&
            (game.party.rows[party_slot].executing.kind == W8_ACTION_ATTACK ||
             game.party.rows[party_slot].executing.kind == W8_ACTION_BERSERK)) {
            game.events.push(TextNotice{
                W8_FONT_PALETTE_WHITE,
                fmt::sprintf(Data().strings[0x1f7], game.party.characters[party_slot].name)});
            return;
        }
    }

    if (IsPartySlotEligible(static_cast<int>(party_slot)) &&
        SwapWeaponSetSlots(game, party_slot, announce))
        game.party.rows[party_slot].weapon_swap_pending = false;
    game.events.push(CharacterActionChanged{});
}

bool ConsumeAttackItems(Game& game, unsigned actor, unsigned hand, W8EquipSlot used,
                        W8EquipSlot paired)
{
    const auto spent = ConsumeAttackItems(game.rules(), actor, hand, used, paired);
    Finish(game, spent.edit);
    return spent.thrown_spent;
}

void ReadyPrimaryWeapon(Game& game, unsigned party_slot)
{
    Finish(game, ReadyPrimaryWeapon(game.rules(), party_slot));
}

// ---- Equipment rule

/* Each unusable piece is stored like a received item: on the member, then in the pool, and
   otherwise at the party's feet. */
bool UnequipUnusableItems(Game& game, unsigned int party_slot)
{
    bool hands_changed = false;
    while (auto removed = TakeUnusableEquipment(game.rules(), party_slot)) {
        hands_changed |= removed->weapon_hand;
        const auto source = OwnedItem(removed->item);
        if (!StoreWithCharacter(game, PartyMember(game, party_slot), source, false, false, false) &&
            !StoreInPool(game, source, false, false))
            DropUnstoredItem(game, source);
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
namespace {
struct ItemUseOutcome {
    W8ItemUseResult result;
    bool emptied = false;
};
}

static ItemUseOutcome UseItemOnce(Game& game, unsigned int party_slot, W8ItemLocation location)
{
    W8Character* character = &game.party.characters[party_slot];
    W8ItemInstance* item = game.party.itemAt(location);
    const W8ItemDatabaseRecord* record = &Data().items[item->iItemNo];
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

    if (!CanCharacterUseItem(Data(), character, item->iItemNo)) {
        game.events.push(CharacterNotice{
            party_slot, fmt::sprintf(Data().strings[0x164], GetItemDisplayName(Data(), item))});
        return {};
    }
    if (!CanCharacterActivateItem(character, item)) {
        game.events.push(CharacterNotice{
            party_slot, fmt::sprintf(Data().strings[0x165], GetItemDisplayName(Data(), item))});
        return {};
    }

    if (record->quantity_kind == W8_ITEM_QUANTITY_CHARGES && item->uses_or_charges == 0) {
        game.events.push(CharacterNotice{
            party_slot, fmt::sprintf(Data().strings[0x1f1], GetItemDisplayName(Data(), item))});
        event_type = g_special_event13;
    } else if (record->quantity_kind == W8_ITEM_QUANTITY_SHOTS && item->uses_or_charges == 0) {
        game.events.push(CharacterNotice{
            party_slot, fmt::sprintf(Data().strings[0x1f2], GetItemDisplayName(Data(), item))});
        event_type = 39;
    } else {
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            character->uiCondition[W8_CONDITION_SILENCED] != 0) {
            game.events.push(CharacterNotice{party_slot, Data().strings[0x166]});
            return {};
        }

        /* The one item that is not used on anybody: it hands the level's NPC
           over to the fact that says the party has met them. */
        if (item->iItemNo == 0x29f) {
            W8NpcState* npc = GetNpcState(game.party.rows[party_slot].npc_index);
            if (npc == 0) {
                Fail("pNPC");
            }
            if (npc->name_style != W8_NPC_RFS81_A) {
                Fail("pNPC->ubNPCDBaseID == NPC_RFS81A");
            }
            SetFact(W8_FACT_RFS81_HAS_BEEN_FIXED, 1, false);
            SetFact(W8_FACT_QUEST_ANDROID_BROKEN, 0, false);
            return {{1, std::nullopt}, ConsumeItemUse(game.rules(), location, true)};
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
            fatigue_cost = Data().spells[record->spell_id].spell_level * 8 + 0x14;
            power = 7;
            do {
                if (GetItemUseDifficulty(character, skill, character->skills[skill].level,
                                         record->spell_id, power) > 0xf) {
                    --power;
                    continue;
                }
                chance = power * 7;
                while (power != 0 && chance > game.rng.below(100)) {
                    --power;
                    chance -= 7;
                }
                break;
            } while (power != 0);

            if (power == 0) {
                game.events.push(
                    CharacterNotice{party_slot, fmt::sprintf(Data().strings[0x1f3],
                                                             GetItemDisplayName(Data(), item))});
                PracticeCharacterSkill(game, character, skill, 1, false);
                return {{0, own_cost()}};
            }
        }

        /* The targeting classes aim through the slot's own out-of-combat
           target; everything else is used on the character acting. */
        SetTargetSourceToCharacter(party_slot, &target);
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_POTION ||
            (record->equip_class > W8_ITEM_EQUIP_CLASS_SCROLL &&
             record->equip_class < W8_ITEM_EQUIP_CLASS_KEY)) {
            if (!game.combat.has_value()) {
                W8CombatSlot* aimed = &game.party.rows[party_slot].executing.target;
                if (aimed->iType == W8_TARGET_KIND_CHARACTER) {
                    SetTargetSourceToCharacter(aimed->iChar, &target);
                } else if (aimed->iType == W8_TARGET_KIND_MONSTER) {
                    SetTargetSourceToMonster(FindMonster(aimed->iMonsterID), &target);
                }
            } else {
                AimAtCharacter(party_slot, party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT);
            }
        }
        target.name_known = true;

        if (static_cast<int>(record->category) >= W8_ITEM_USE_TYPE_COUNT) {
            Fail("pItemDB->ubUseType < USE_TYPE_COUNT");
        }
        /* A spell-carrying item the world-cursor handler refuses cannot be used
           at all, and the attempt is over before anything is announced. */
        if (record->spell_id != W8_SPELL_NONE &&
            Data().items[item->iItemNo].spell_id != W8_SPELL_NONE &&
            Data().items[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_GADGET &&
            (Data().items[item->iItemNo].equip_class < W8_ITEM_EQUIP_CLASS_FOOD ||
             Data().items[item->iItemNo].equip_class > W8_ITEM_EQUIP_CLASS_DRINK) &&
            DispatchWorldCursorNodeCommand(0, 4, 1)) {
            return {{0, 0}};
        }

        if (TargetSourceIsCharacter(&target, 0)) {
            game.events.push(CharacterNotice{
                static_cast<unsigned int>(target.iChar),
                fmt::format("{} {}", Data().strings[g_item_use_messages[record->category * 2]],
                            GetItemDisplayName(Data(), item))});
        } else if (TargetSourceIsMonster(&target, 0)) {
            game.events.push(TextNotice{
                9, fmt::format("{} {} {}", GetMonsterName(FindMonster(target.iMonsterID), 0, 0),
                               Data().strings[g_item_use_messages[record->category * 2]],
                               GetItemDisplayName(Data(), item))});
        } else {
            Fail("FALSE");
        }

        if (g_settings.verbose_combat_messages == 0) {
            game.events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
        }
        if (record->spell_id == W8_SPELL_NONE) {
            std::string message = fmt::format(
                "UseItem: ERROR - Usable item {:d} doesn't do anything!", item->iItemNo);
            Fail("FALSE", message);
            used = 0;
            fatigue_cost = 0;
        } else {
            used = CastItemSpell(character, item, power);
            if (!used) {
                fatigue_cost = 0;
            } else {
                emptied = ConsumeItemUse(game.rules(), location, true);
            }
        }
        return {{used, own_cost()}, emptied};
    }

    QueueCharacterEvent(&game.party.characters[party_slot], event_type, 0,
                        kCharacterEventFullVolume);
    return {{used, own_cost()}, emptied};
}

W8ItemUseResult UseItem(Game& game, unsigned actor, const W8ItemUse& use)
{
    const auto location = use.item;
    if (actor >= W8_PARTY_SLOT_COUNT || !ResolveItemUse(game.party, use)) return {};
    const auto outcome = UseItemOnce(game, actor, location);
    FinishInventoryEdit(game, {outcome.emptied ? location : W8ItemLocation{}});
    return outcome.result;
}
