#include "wiz8/inventory.h"

#include <algorithm>
#include <bitset>
#include <cassert>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "glm/ext/vector_float2.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/cursor.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/fact_ids.h"
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
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

enum { W8_ITEM_USE_TYPE_COUNT = 10 };
// The speech a member makes when a shot weapon or ammunition has no partner to draw from.
enum { W8_SPEECH_NO_AMMUNITION = 39 };

static bool IsHandSlot(unsigned int slot)
{
    return slot == W8_EQUIP_SLOT_PRIMARY_WEAPON || slot == W8_EQUIP_SLOT_SECONDARY_WEAPON;
}

static W8ItemLocation Equipment(unsigned int party_slot, unsigned int equip_slot)
{
    return {W8ItemStorage::Equipment, party_slot, equip_slot};
}

static W8ItemLocation PoolRow(std::size_t row)
{
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(row)};
}

static constexpr W8ItemLocation kHand{W8ItemStorage::Hand};

// ---- Storage

const W8ItemInstance* Inventory::itemAt(W8ItemLocation location) const
{
    static constexpr W8ItemInstance kEmpty{};
    if (location.storage == W8ItemStorage::Hand && !held_) return &kEmpty;
    if (location.storage == W8ItemStorage::Pool && location.slot >= pool_.size())
        return location.slot < kPoolLimit ? &kEmpty : nullptr;
    return const_cast<Inventory&>(*this).slotAt(location);
}

W8ItemInstance* Inventory::slotAt(W8ItemLocation location)
{
    auto& characters = game_.party.characters;
    switch (location.storage) {
    case W8ItemStorage::Hand:
        return held_ ? &held_->item : nullptr;
    case W8ItemStorage::Pool:
        return location.slot < pool_.size() ? &pool_[location.slot] : nullptr;
    case W8ItemStorage::Equipment:
        return location.character < W8_PARTY_SLOT_COUNT && location.slot < W8_EQUIP_SLOT_COUNT
                   ? &characters[location.character].EquippedItem[location.slot]
                   : nullptr;
    case W8ItemStorage::Backpack:
        return location.character < W8_PARTY_SLOT_COUNT &&
                       location.slot < std::size(characters[location.character].backpack)
                   ? &characters[location.character].backpack[location.slot]
                   : nullptr;
    case W8ItemStorage::None:
        return nullptr;
    }
    return nullptr;
}

// Remove an item without settling: a pool row closes up, the hand empties, a slot clears.
void Inventory::remove(W8ItemLocation location)
{
    if (location.storage == W8ItemStorage::Hand)
        held_.reset();
    else if (location.storage == W8ItemStorage::Pool)
        pool_.erase(pool_.begin() + location.slot);
    else if (auto* item = slotAt(location))
        *item = {};
}

/* Exchange the items at two locations, either of which may be empty. The hand remembers
   where its new item came from; emptied pool rows close up. */
void Inventory::exchange(W8ItemLocation first, W8ItemLocation second, int actor)
{
    const auto value = [this](W8ItemLocation location) {
        const auto* item = slotAt(location);
        return item ? *item : W8ItemInstance{};
    };
    auto first_item = value(second);
    auto second_item = value(first);
    first_item.bind_announced = second_item.bind_announced = false;
    const auto write = [&](W8ItemLocation location, W8ItemInstance item, W8ItemLocation other) {
        if (location.storage == W8ItemStorage::Hand) {
            if (item.iItemNo == -1)
                held_.reset();
            else
                held_ = W8HeldItem{item, other, actor};
        } else if (auto* slot = slotAt(location)) {
            *slot = item;
        }
    };
    write(first, first_item, second);
    write(second, second_item, first);
    // Close the later row first so the earlier index stays valid.
    if (first.storage == W8ItemStorage::Pool && second.storage == W8ItemStorage::Pool &&
        first.slot < second.slot)
        std::swap(first, second);
    for (const auto location : {first, second})
        if (location.storage == W8ItemStorage::Pool && location.slot < pool_.size() &&
            pool_[location.slot].iItemNo == -1)
            remove(location);
}

std::optional<W8ItemInstance> Inventory::take(W8ItemLocation location)
{
    const auto* item = slotAt(location);
    if (!item || item->iItemNo == -1) return std::nullopt;
    auto taken = *item;
    remove(location);
    settle({location});
    return taken;
}

bool Inventory::spendUse(W8ItemLocation location, bool remove_exhausted)
{
    auto* item = slotAt(location);
    if (!item || !SpendItemUse(game_.data, *item, remove_exhausted)) return false;
    remove(location);
    return true;
}

void Inventory::editItem(W8ItemLocation location, std::function_ref<void(W8ItemInstance&)> edit)
{
    auto* item = slotAt(location);
    if (!item || item->iItemNo == -1) return;
    edit(*item);
    if (item->iItemNo == -1) throw std::logic_error("Inventory::editItem emptied an item");
}

void Inventory::restore(std::vector<W8ItemInstance> pool, std::optional<W8ItemInstance> held)
{
    // An empty record inside a saved pool holds nothing; the pool stays packed.
    std::erase_if(pool, [](const W8ItemInstance& item) { return item.iItemNo == -1; });
    pool_ = std::move(pool);
    pool_.reserve(kPoolLimit);
    held_.reset();
    if (held && held->iItemNo != -1) held_ = W8HeldItem{*held};
}

// ---- Reading

W8ItemLocation Inventory::find(W8ItemMatch match, W8ItemReach reach) const
{
    const auto& party = game_.party;
    if (held_ && match(held_->item)) return kHand;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value())
            if (const auto found = FindMemberItem(party, slot, match, reach);
                found.storage != W8ItemStorage::None)
                return found;
    if (reach != W8ItemReach::Party) return {};
    const auto found = std::ranges::find_if(pool_, match);
    return found == pool_.end() ? W8ItemLocation{} : PoolRow(found - pool_.begin());
}

bool Inventory::has(int item_id, W8ItemReach reach) const
{
    return find(ItemWithId(item_id), reach).storage != W8ItemStorage::None;
}

/* The held stack counts its size as stored, which is nothing for an item that does not
   stack. */
unsigned int Inventory::count(int item_id) const
{
    const auto& party = game_.party;
    unsigned int total = held_ && held_->item.iItemNo == item_id ? held_->item.stack_count : 0;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value())
            total += CountCharacterItem(party.characters[slot], item_id, W8ItemReach::Carried);
    for (const auto& item : pool_)
        if (item.iItemNo == item_id) total += item.stack_count == 0 ? 1 : item.stack_count;
    return total;
}

W8ItemUse Inventory::useAt(W8ItemLocation location) const
{
    const auto* item = itemAt(location);
    return {-1, location, item ? item->iItemNo : -1};
}

bool Inventory::holds(const W8ItemUse& use) const
{
    const auto* item = itemAt(use.item);
    return item && item->iItemNo != -1 && item->iItemNo == use.item_id;
}

bool Inventory::heldFitsBeside(unsigned int party_slot, W8EquipSlot equip_slot) const
{
    const auto paired = GetPairedEquipSlot(equip_slot);
    if (paired == W8_EQUIP_SLOT_NONE) return true;
    return CanHoldItemsTogether(game_.data, held_ ? held_->item.iItemNo : -1,
                                game_.party.characters[party_slot].EquippedItem[paired].iItemNo);
}

// ---- Settling

/* A committed item use whose item is gone or has moved is abandoned, a change of a weapon
   hand reconciles combat choices, and any changed location is reported. */
void Inventory::settle(std::span<const W8ItemLocation> changed)
{
    auto& party = game_.party;
    std::bitset<W8_PARTY_SLOT_COUNT> changed_hands;
    bool any = false;
    for (const auto location : changed) {
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
            !holds(use))
            DropCharacterFromRound(static_cast<int>(slot));
    }
    SettleActiveParty(game_, changed_hands);
    if (any) game_.events.push(EquipmentSettled{});
}

// A character outside the party is also brought up to date on its own.
void Inventory::settleOutsider(const ItemRecipient& recipient,
                               std::span<const W8ItemLocation> changed)
{
    settle(changed);
    if (!recipient.slot) UpdateStandaloneCharacterStats(game_.data, recipient.character, {});
}

// ---- Placing items

ItemRecipient Inventory::member(unsigned int party_slot)
{
    return {game_.party.characters[party_slot], party_slot};
}

std::optional<Inventory::Placement> Inventory::placeWithCharacter(ItemRecipient recipient,
                                                                  W8ItemInstance& item, bool equip,
                                                                  bool announce, bool stack)
{
    const auto& data = game_.data;
    const std::string name = announce ? FormatItemDisplayName(data, &item, true) : "";
    const auto placed = PlaceWithCharacter(data, recipient.character, item, equip, stack,
                                           game_.status.game_started);
    if (!placed) return std::nullopt;
    auto& character = recipient.character;
    auto& received = placed->storage == W8ItemStorage::Equipment
                         ? character.EquippedItem[placed->slot]
                         : character.backpack[placed->slot];
    W8ItemLocation stored{};
    if (recipient.slot && !placed->merged)
        stored = {placed->storage, *recipient.slot, placed->slot};
    // Equipping straight into a slot acquires nothing and announces nothing.
    if (placed->equipped) return Placement{nullptr, &character, stored};
    if (announce && recipient.slot)
        game_.events.push(
            CharacterNotice{*recipient.slot, fmt::sprintf(data.strings[0x1e8], name)});
    return Placement{&received, &character, stored};
}

std::optional<Inventory::Placement> Inventory::placeInPool(W8ItemInstance& item, bool announce,
                                                           bool stack)
{
    const auto& data = game_.data;
    const std::string name = announce ? FormatItemDisplayName(data, &item, true) : "";
    std::optional<std::size_t> received;
    if (stack && data.items[item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        for (std::size_t row = 0; row < pool_.size(); ++row) {
            if (MergeStack(data, pool_[row], item) != 0 && item.stack_count == 0) {
                item = {};
                received = row;
                break;
            }
        }
    }
    W8ItemLocation stored{};
    if (!received) {
        if (pool_.size() >= kPoolLimit) return std::nullopt;
        pool_.insert(pool_.begin(), std::exchange(item, W8ItemInstance{}));
        pool_.front().bind_announced = false;
        received = 0;
        stored = PoolRow(0);
    }
    if (announce)
        game_.events.push(
            TextNotice{W8_FONT_PALETTE_WHITE, fmt::sprintf(data.strings[0x1e9], name)});
    return Placement{&pool_[*received], nullptr, stored};
}

std::optional<Inventory::Placement> Inventory::placeEither(ItemRecipient recipient,
                                                           W8ItemInstance& item, StoreOrder order,
                                                           bool announce, bool equip)
{
    if (order == StoreOrder::CharacterFirst)
        if (auto placed = placeWithCharacter(recipient, item, equip, announce, true)) return placed;
    if (auto placed = placeInPool(item, announce, true)) return placed;
    if (order == StoreOrder::PoolFirst)
        return placeWithCharacter(recipient, item, equip, announce, true);
    return std::nullopt;
}

/* What acquiring an item sets off: a handful of plot items update their facts, and an
   exceptional one draws a remark, from the receiver or from a random member when the pool
   took it. */
void Inventory::acquire(const std::optional<Placement>& placement)
{
    if (!placement || !placement->received) return;
    UpdateFactsAfterAcquiringItem(placement->received);
    DeliverExceptionalItemReaction(game_, placement->received, placement->receiver == nullptr,
                                   placement->receiver);
}

/* What no store takes is dropped at the party's feet unless it may not be discarded, and
   otherwise goes into an empty hand. Failure keeps the item. */
bool Inventory::dropUnstored(W8ItemInstance& item, W8ItemLocation origin, int actor)
{
    if ((game_.data.items[item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (const auto position = (game_.level.primary ? game_.level.primary->itemDropPosition(
                                                             GetCursorScaledPosition())
                                                       : std::nullopt)) {
            game_.level.loaded().adoptItem(
                game_.createWorldItem(game_.level.loaded(), &item, &*position,
                                      W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            return true;
        }
    }
    if (held_) return false;
    held_ = W8HeldItem{std::exchange(item, W8ItemInstance{}), origin, actor};
    held_->item.bind_announced = false;
    return true;
}

static W8ItemLocation Stored(const auto& placement)
{
    return placement ? placement->stored : W8ItemLocation{};
}

bool Inventory::storeWithCharacter(ItemRecipient recipient, W8ItemInstance& item, bool equip,
                                   bool announce, bool stack)
{
    const auto placed = placeWithCharacter(recipient, item, equip, announce, stack);
    acquire(placed);
    settleOutsider(recipient, {Stored(placed)});
    return placed.has_value();
}

bool Inventory::storeInPool(W8ItemInstance& item, bool announce, bool stack)
{
    const auto placed = placeInPool(item, announce, stack);
    acquire(placed);
    settle({Stored(placed)});
    return placed.has_value();
}

bool Inventory::store(ItemRecipient recipient, W8ItemInstance& item, StoreOrder order,
                      bool announce, bool equip)
{
    const auto placed = placeEither(recipient, item, order, announce, equip);
    acquire(placed);
    settleOutsider(recipient, {Stored(placed)});
    return placed.has_value();
}

bool Inventory::storeOrDrop(W8ItemInstance& item, bool announce)
{
    const auto placed = placeInPool(item, announce, true);
    acquire(placed);
    if (!placed) dropUnstored(item);
    settle({placed ? placed->stored : kHand});
    return placed.has_value();
}

void Inventory::identifyHeld()
{
    if (held_ && !held_->item.identified) PartyAttemptsToIdentifyItem(&held_->item, 0);
}

bool Inventory::storeHeld(unsigned int party_slot)
{
    if (!held_) return false;
    auto item = held_->item;
    const auto placed = placeWithCharacter(member(party_slot), item, false, false, true);
    if (item.iItemNo == -1)
        held_.reset();
    else
        held_->item = item;
    acquire(placed);
    settle({placed ? kHand : W8ItemLocation{}, Stored(placed)});
    return placed.has_value();
}

std::expected<void, InventoryRefusal> Inventory::giveHeld(unsigned int party_slot, StoreOrder order)
{
    assert(held_);
    assert(game_.status.game_started);
    assert(game_.party.members[party_slot].has_value());

    auto item = held_->item;
    const bool eligible = IsPartySlotEligible(static_cast<int>(party_slot));
    std::optional<Placement> placed;
    if (eligible)
        placed = placeEither(member(party_slot), item, order, true, false);
    else if (order == StoreOrder::PoolFirst)
        placed = placeInPool(item, true, true);
    // Whatever was not stored stays in the hand.
    if (item.iItemNo == -1)
        held_.reset();
    else
        held_->item = item;
    acquire(placed);
    if (placed) {
        settle({kHand, placed->stored});
        return {};
    }
    settle({});
    const bool slot_closed = !eligible && order == StoreOrder::CharacterFirst;
    return std::unexpected(InventoryRefusal{game_.data.strings[slot_closed ? 0x1f5 : 0x90c]});
}

Inventory::Receipt Inventory::receiveAll(std::span<const W8ItemInstance> items,
                                         std::function_ref<void()> commit)
{
    const auto& data = game_.data;
    // Plan the whole transfer on a copy, so a refusal changes nothing.
    auto pool = pool_;
    std::vector<std::size_t> received;
    std::vector<W8ItemInstance> overflow;
    for (auto item : items) {
        if (data.items[item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
            for (std::size_t row = 0; row < pool.size() && item.stack_count != 0; ++row)
                if (MergeStack(data, pool[row], item) != 0) received.push_back(row);
            if (item.stack_count == 0) continue;
        }
        if (pool.size() < kPoolLimit) {
            for (auto& row : received) ++row;
            pool.insert(pool.begin(), item);
            received.push_back(0);
        } else {
            overflow.push_back(item);
        }
    }
    std::optional<W8ItemInstance> held;
    std::vector<std::unique_ptr<W8WorldItem>> drops;
    if (!overflow.empty()) {
        if ((data.items[overflow.front().iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
            if (held_ || overflow.size() != 1) return Receipt::Refused;
            held = overflow.front();
        } else {
            const auto position =
                (game_.level.primary
                     ? game_.level.primary->itemDropPosition(GetCursorScaledPosition())
                     : std::nullopt);
            if (!position) return Receipt::Refused;
            for (auto& item : overflow)
                drops.push_back(
                    game_.createWorldItem(game_.level.loaded(), &item, &*position,
                                          W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
        }
    }
    pool_ = std::move(pool);
    commit();
    for (auto& drop : drops) game_.level.loaded().adoptItem(std::move(drop));
    if (held) held_ = W8HeldItem{*held};
    settle({});
    for (const auto row : received) {
        UpdateFactsAfterAcquiringItem(&pool_[row]);
        DeliverExceptionalItemReaction(game_, &pool_[row], true, nullptr);
    }
    return drops.empty() ? Receipt::Stored : Receipt::Dropped;
}

/* Equipment that is bound to its slot stays with the body unless the binding was announced,
   the slot has no paper-doll position, or the member is dead. What the pool cannot take is
   dropped into the world or held when the hand is empty. */
void Inventory::stashDeparting(unsigned int party_slot)
{
    const auto& data = game_.data;
    auto& character = game_.party.characters[party_slot];
    const auto stash = [&](W8ItemInstance& slot, W8ItemLocation location) {
        const int item_id = slot.iItemNo;
        auto item = slot;
        const auto placed = placeInPool(item, false, true);
        acquire(placed);
        if (!placed && dropUnstored(item, location, static_cast<int>(party_slot)))
            game_.events.push(
                TextNotice{W8_FONT_PALETTE_RED,
                           fmt::sprintf(data.strings[0x7d3], data.items[item_id].display_name)});
        slot = item;
    };
    for (unsigned int equip_slot = 0; equip_slot < W8_EQUIP_SLOT_COUNT; ++equip_slot) {
        auto& item = character.EquippedItem[equip_slot];
        if (item.iItemNo != -1 &&
            (data.items[item.iItemNo].binds_on_equip == 0 || item.bind_announced ||
             !HasPaperDollPosition(static_cast<W8EquipSlot>(equip_slot)) ||
             character.uiCondition[W8_CONDITION_DEAD] != 0))
            stash(item, Equipment(party_slot, equip_slot));
    }
    for (unsigned int slot = 0; slot < std::size(character.backpack); ++slot)
        if (character.backpack[slot].iItemNo != -1)
            stash(character.backpack[slot], {W8ItemStorage::Backpack, party_slot, slot});
    settle({});
}

void Inventory::emptyCharacter(W8Character& outsider)
{
    for (auto& item : outsider.EquippedItem) item = {};
    for (auto& item : outsider.backpack) item = {};
    settleOutsider(Outsider(outsider), {});
}

/* The ladder gives the item id and the notice to show; the first tier hands over the
   welcome item outright, and an item the character already holds only gets its id raised
   when it is below the tier's. */
void Inventory::upgradeClassItem(ItemRecipient recipient)
{
    const auto& data = game_.data;
    auto& character = recipient.character;
    const auto notice = [&](unsigned int message, unsigned int pronoun_column) {
        assert(recipient.slot);
        game_.events.push(CharacterNotice{
            *recipient.slot,
            fmt::sprintf(
                data.strings[message],
                data.strings[g_gender_name_message_rows[character.gender][pronoun_column]])});
    };
    int item_id;
    unsigned int message_id;
    switch (character.profession_levels[W8_PROFESSION_GADGETEER]) {
    case 0:
    case 2:
        return settleOutsider(recipient, {});
    case 1: {
        if (FindCharacterItem(character, ItemOfKind(data, 0x83), W8ItemReach::Carried))
            return settleOutsider(recipient, {});
        auto created = MakeItem(data, game_.rng, 599, false, true, true);
        auto placed = placeWithCharacter(recipient, created, false, true, true);
        acquire(placed);
        if (!placed) {
            placed = placeInPool(created, true, true);
            acquire(placed);
        }
        if (placed) notice(0x167, 3);
        return settleOutsider(recipient, {Stored(placed)});
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
    W8ItemInstance* found =
        FindCharacterItem(character, ItemOfKind(data, 0x83), W8ItemReach::Carried);
    W8ItemLocation location{};
    if (found && recipient.slot) {
        const bool equipped =
            found >= std::begin(character.EquippedItem) && found < std::end(character.EquippedItem);
        location = equipped ? Equipment(*recipient.slot,
                                        static_cast<unsigned>(found - character.EquippedItem))
                            : W8ItemLocation{W8ItemStorage::Backpack, *recipient.slot,
                                             static_cast<unsigned>(found - character.backpack)};
    }
    if (!found) {
        location = find(ItemOfKind(data, 0x83));
        found = slotAt(location);
    }
    if (found && found->iItemNo < item_id) {
        found->iItemNo = item_id;
        notice(message_id, 2);
        return settleOutsider(recipient, {location});
    }
    settleOutsider(recipient, {});
}

// ---- Losing items

/* The hand first, then every member's slots, then the pool. When remove_all is clear the
   search stops at the first hit. Even when it is set, only one copy per member is cleared;
   every matching pool row goes. */
bool Inventory::removeById(int item_id, bool remove_all)
{
    std::vector<W8ItemLocation> removed;
    const auto finish = [&] {
        settle(removed);
        return !removed.empty();
    };
    if (held_ && held_->item.iItemNo == item_id) {
        remove(kHand);
        removed.push_back(kHand);
        if (!remove_all) return finish();
    }
    auto& party = game_.party;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value()) continue;
        const auto found = FindMemberItem(party, slot, ItemWithId(item_id), W8ItemReach::Carried);
        if (found.storage == W8ItemStorage::None) continue;
        remove(found);
        removed.push_back(found);
        if (!remove_all) return finish();
    }
    for (std::size_t row = 0; row < pool_.size();) {
        if (pool_[row].iItemNo != item_id) {
            ++row;
            continue;
        }
        remove(PoolRow(row));
        removed.push_back(PoolRow(row));
        if (!remove_all) break;
    }
    return finish();
}

void Inventory::removeQuantity(W8ItemLocation location, unsigned int amount)
{
    auto* item = slotAt(location);
    if (!item || item->iItemNo == -1 || amount == 0) return;
    if (amount >= std::max<unsigned int>(1, item->stack_count)) {
        take(location);
        return;
    }
    item->stack_count -= static_cast<unsigned char>(amount);
    settle({});
}

// ---- The hand

void Inventory::replaceHeld(W8ItemInstance item)
{
    held_.reset();
    if (item.iItemNo != -1) held_ = W8HeldItem{item};
    settle({kHand});
}

bool Inventory::hold(W8ItemInstance& item)
{
    if (held_ || item.iItemNo == -1) return false;
    held_ = W8HeldItem{std::exchange(item, W8ItemInstance{})};
    settle({kHand});
    return true;
}

std::expected<bool, InventoryRefusal> Inventory::dropHeld(const glm::vec2& cursor)
{
    if (!held_) return true;
    if ((game_.data.items[held_->item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0)
        return std::unexpected(InventoryRefusal{game_.data.strings[0x4ef]});
    auto* world = game_.level.primary.get();
    const auto position = world ? world->itemDropPosition(cursor) : std::nullopt;
    if (world && position) {
        auto item = held_->item;
        world->adoptItem(game_.createWorldItem(*world, &item, &*position,
                                               W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
        held_.reset();
    }
    settle({kHand});
    return position.has_value();
}

// ---- The player's moves

/* Taking from a location starts or grows the held stack; splitting the held stack returns
   the rest to where it came from when that still fits, and otherwise to the actor's
   backpack or the end of the pool. */
bool Inventory::pickUp(W8ItemLocation source_location, unsigned int amount, unsigned int actor)
{
    const auto& data = game_.data;
    auto& party = game_.party;
    auto* source = slotAt(source_location);
    if (!source || source->iItemNo < 0 || actor >= W8_PARTY_SLOT_COUNT || amount == 0 ||
        amount > source->stack_count ||
        data.items[source->iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK ||
        (data.items[source->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD))
        return false;
    if (source_location.storage == W8ItemStorage::Equipment && amount == source->stack_count &&
        !CanUnequipSlotItem(data, &party.characters[source_location.character],
                            static_cast<W8EquipSlot>(source_location.slot)))
        return false;
    const bool from_hand = source_location.storage == W8ItemStorage::Hand;
    const bool append = !from_hand && held_;
    if (append && (held_->item.iItemNo != source->iItemNo ||
                   held_->item.stack_count + amount > data.items[source->iItemNo].maximum_quantity))
        return false;
    if (from_hand && amount == source->stack_count) return true;

    W8ItemLocation destination = kHand;
    if (from_hand) {
        destination = held_->origin;
        const auto* original = itemAt(destination);
        const bool equipment = destination.storage == W8ItemStorage::Equipment;
        const bool fits = original && original->iItemNo == -1 &&
                          (destination.storage == W8ItemStorage::Backpack ||
                           (equipment &&
                            CanCharacterUseItem(data, &party.characters[destination.character],
                                                source->iItemNo) &&
                            CanEquipItemInSlot(data, &party.characters[destination.character],
                                               source->iItemNo, destination.slot, false)));
        if (!fits) {
            const auto& backpack = party.characters[actor].backpack;
            const auto free = std::ranges::find(backpack, -1, &W8ItemInstance::iItemNo);
            if (free != std::end(backpack))
                destination = {W8ItemStorage::Backpack, actor,
                               static_cast<unsigned int>(free - std::begin(backpack))};
            else if (pool_.size() < kPoolLimit)
                destination = PoolRow(pool_.size());
            else
                return false;
        }
    }
    const unsigned int moved = from_hand ? source->stack_count - amount : amount;
    auto part = *source;
    part.stack_count = static_cast<unsigned char>(moved);
    source->stack_count -= static_cast<unsigned char>(moved);
    const bool emptied = source->stack_count == 0;
    if (emptied) remove(source_location);
    if (append)
        held_->item.stack_count += static_cast<unsigned char>(moved);
    else if (destination.storage == W8ItemStorage::Hand)
        held_ = W8HeldItem{part, source_location, static_cast<int>(actor)};
    else if (destination.storage == W8ItemStorage::Pool)
        pool_.push_back(part);
    else
        *slotAt(destination) = part;
    settle({emptied ? source_location : W8ItemLocation{}, append ? W8ItemLocation{} : destination});
    return true;
}

bool Inventory::swapWithHand(W8ItemLocation location, unsigned int actor)
{
    const auto& data = game_.data;
    const auto* item = itemAt(location);
    if (!item || location.storage == W8ItemStorage::Hand || actor >= W8_PARTY_SLOT_COUNT ||
        (item->iItemNo == -1 && !held_))
        return false;
    if (location.storage == W8ItemStorage::Equipment) {
        const auto& character = game_.party.characters[location.character];
        if (!CanUnequipSlotItem(data, &character, static_cast<W8EquipSlot>(location.slot)))
            return false;
        if (held_ &&
            (!CanCharacterUseItem(data, &character, held_->item.iItemNo) ||
             !CanEquipItemInSlot(data, &character, held_->item.iItemNo, location.slot, false)))
            return false;
    }
    exchange(location, kHand, static_cast<int>(actor));
    settle({location, kHand});
    supersedeQueuedSwap(location);
    return true;
}

// Changing a weapon hand supersedes a weapon-set swap queued for the round.
void Inventory::supersedeQueuedSwap(W8ItemLocation location)
{
    if (location.storage == W8ItemStorage::Equipment &&
        GetPairedEquipSlot(static_cast<W8EquipSlot>(location.slot)) != W8_EQUIP_SLOT_NONE)
        game_.party.rows[location.character].weapon_swap_pending = false;
}

/* Equip the held item in a hand whose partner it cannot share. An occupied target goes back
   into the hand and the partner into the backpack or the pool; an empty target puts the
   partner itself into the hand. */
bool Inventory::stowPartner(unsigned int actor, W8EquipSlot slot)
{
    const auto& data = game_.data;
    auto& party = game_.party;
    auto& character = party.characters[actor];
    const auto partner_slot = GetPairedEquipSlot(slot);
    if (partner_slot == W8_EQUIP_SLOT_NONE || !held_ ||
        !CanUnequipSlotItem(data, &character, slot) ||
        !CanUnequipSlotItem(data, &character, partner_slot))
        return false;
    auto proposed = character;
    proposed.EquippedItem[partner_slot].iItemNo = -1;
    if (!CanCharacterUseItem(data, &character, held_->item.iItemNo) ||
        !CanEquipItemInSlot(data, &proposed, held_->item.iItemNo, slot, false))
        return false;
    auto& destination = character.EquippedItem[slot];
    auto& partner = character.EquippedItem[partner_slot];
    const bool replace = destination.iItemNo != -1;
    W8ItemLocation storage{};
    if (replace) {
        const auto free = std::ranges::find(character.backpack, -1, &W8ItemInstance::iItemNo);
        if (free != std::end(character.backpack)) {
            storage = {W8ItemStorage::Backpack, actor,
                       static_cast<unsigned int>(free - std::begin(character.backpack))};
        } else {
            auto* npc = actor < 2 ? GetNpcState(party.npcIndex(actor)) : nullptr;
            if ((npc && NpcWantsItem(npc, &partner)) || pool_.size() >= kPoolLimit) return false;
            storage = PoolRow(0);
        }
    }
    // Every destination and gameplay restriction is known before the first write.
    auto displaced = partner;
    displaced.bound = true;
    displaced.bind_announced = false;
    const auto held_origin = replace ? slot : partner_slot;
    auto new_held = replace ? destination : displaced;
    destination = held_->item;
    destination.bind_announced = false;
    partner = {};
    held_.reset();
    if (new_held.iItemNo != -1) {
        new_held.bind_announced = false;
        held_ = W8HeldItem{new_held, Equipment(actor, held_origin), static_cast<int>(actor)};
    }
    std::optional<Placement> stored;
    if (storage.storage == W8ItemStorage::Pool) {
        pool_.insert(pool_.begin(), displaced);
        stored = Placement{&pool_.front(), nullptr, storage};
    } else if (storage.storage == W8ItemStorage::Backpack) {
        auto* slot_item = slotAt(storage);
        *slot_item = displaced;
        stored = Placement{slot_item, &character, storage};
    }
    acquire(stored);
    settle({kHand, Equipment(actor, slot), Equipment(actor, partner_slot), storage});
    return true;
}

std::expected<void, InventoryRefusal> Inventory::placeHeld(unsigned int actor,
                                                           W8ItemLocation location)
{
    assert(held_);
    assert(location.storage == W8ItemStorage::Equipment ||
           location.storage == W8ItemStorage::Backpack);
    auto& character = game_.party.characters[actor];
    DeliverExceptionalItemReaction(game_, &held_->item, held_->actor == -1, &character);
    const auto equip_slot = static_cast<W8EquipSlot>(location.slot);
    if (location.storage == W8ItemStorage::Equipment && !heldFitsBeside(actor, equip_slot)) {
        const auto partner = GetPairedEquipSlot(equip_slot);
        if (!CanUnequipSlotItem(game_.data, &character, partner)) {
            bind(actor, partner);
            return std::unexpected(InventoryRefusal{game_.data.strings[0x916]});
        }
        if (!stowPartner(actor, equip_slot))
            return std::unexpected(InventoryRefusal{game_.data.strings[0x90c]});
    } else {
        swapWithHand(location, actor);
        if (location.storage == W8ItemStorage::Backpack &&
            character.backpack[location.slot].iItemNo != -1 && game_.rng.below(100) < 5)
            QueueCharacterEvent(&character, g_special_event14, 0, kCharacterEventFullVolume);
    }
    supersedeQueuedSwap(location);
    return {};
}

unsigned int Inventory::mergeHeld(W8ItemLocation location)
{
    auto* destination = slotAt(location);
    if (!held_ || !destination || location.storage == W8ItemStorage::Hand) return 0;
    const auto moved = MergeStack(game_.data, *destination, held_->item);
    if (moved == 0) return 0;
    const bool complete = held_->item.stack_count == 0;
    if (complete) held_.reset();
    settle({complete ? kHand : W8ItemLocation{}});
    return moved;
}

Inventory::Deposit Inventory::depositHeld(unsigned int row, bool insert_rest)
{
    Events::Operation outcomes{game_.events};
    if (!held_) return Deposit::Refused;
    unsigned int moved = 0;
    const auto& data = game_.data;
    if (data.items[held_->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        moved = row < pool_.size() ? MergeStack(data, pool_[row], held_->item) : 0;
        for (auto& stack : pool_) {
            if (held_->item.stack_count == 0) break;
            moved += MergeStack(data, stack, held_->item);
        }
        const bool complete = held_->item.stack_count == 0;
        if (complete) held_.reset();
        if (moved != 0) settle({complete ? kHand : W8ItemLocation{}});
        if (complete) return Deposit::Complete;
    }
    if (!insert_rest || row > pool_.size() || pool_.size() >= kPoolLimit)
        return moved ? Deposit::Partial : Deposit::Refused;
    pool_.insert(pool_.begin() + row, held_->item);
    held_.reset();
    settle({kHand, PoolRow(row)});
    return Deposit::Complete;
}

/* Two identified items that some database entry names as its components (in either order)
   are consumed and that entry is made in their place, provided the character has the skill
   it asks for; stacks give up as many units as the smaller one holds. Failing that, two
   items of one kind are stacked together. Practice follows a completed recipe. */
std::expected<bool, InventoryRefusal> Inventory::combineHeld(unsigned int actor,
                                                             W8ItemLocation target)
{
    const auto& data = game_.data;
    auto* destination = slotAt(target);
    if (actor >= W8_PARTY_SLOT_COUNT || !destination || !held_ || destination->iItemNo == -1 ||
        target.storage == W8ItemStorage::Hand)
        return false;
    auto& held = held_->item;
    auto& character = game_.party.characters[actor];
    if (!held.identified || !destination->identified)
        return std::unexpected(InventoryRefusal{data.strings[0x163]});
    for (unsigned int id = 0; id < data.items.size(); ++id) {
        const auto& recipe = data.items[id];
        if (!((recipe.merge_component_a == held.iItemNo &&
               recipe.merge_component_b == destination->iItemNo) ||
              (recipe.merge_component_b == held.iItemNo &&
               recipe.merge_component_a == destination->iItemNo)))
            continue;
        if (recipe.merge_skill != -1 &&
            character.skills[recipe.merge_skill].level < recipe.merge_skill_level)
            return std::unexpected(
                InventoryRefusal{fmt::sprintf(data.strings[0x162], character.name)});
        const bool held_stack = data.items[held.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK;
        const bool target_stack =
            data.items[destination->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK;
        const unsigned int quantity =
            held_stack && target_stack ? std::min(held.stack_count, destination->stack_count) : 1;
        const auto consume = [quantity](W8ItemInstance item, bool stack) {
            if (stack && item.stack_count > quantity)
                item.stack_count -= static_cast<unsigned char>(quantity);
            else
                item = W8ItemInstance{};
            return item;
        };
        auto remaining_held = consume(held, held_stack);
        auto remaining_target = consume(*destination, target_stack);
        auto product = MakeItem(data, game_.rng, static_cast<int>(id), false, false, false);
        if (recipe.quantity_kind == W8_ITEM_QUANTITY_STACK)
            product.stack_count = static_cast<unsigned char>(quantity);
        if (remaining_target.iItemNo == -1)
            remaining_target = product;
        else
            remaining_held = product;
        *destination = remaining_target;
        if (remaining_held.iItemNo == -1)
            held_.reset();
        else
            held = remaining_held;
        settle({kHand, target});
        if (recipe.merge_skill != -1) {
            QueueCharacterEvent(&character, g_learn_sound, 0, kCharacterEventFullVolume);
            PracticeCharacterSkill(game_, &character, static_cast<W8Skill>(recipe.merge_skill),
                                   recipe.merge_skill_level / 10, false);
        }
        return true;
    }
    if (held.iItemNo == destination->iItemNo) return mergeHeld(target) != 0;
    return std::unexpected(InventoryRefusal{data.strings[0x161]});
}

/* Pour one item's uses into another and take that many off the source, one at a time, which
   is what makes the source disappear when it is emptied. Each side counts its quantity the
   way its own record says to. Returns whether the source was used up. */
bool Inventory::pourUses(W8ItemInstance& into, W8ItemLocation from_location)
{
    const auto& data = game_.data;
    const auto& from = *slotAt(from_location);
    const unsigned char available = data.items[from.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
                                        ? from.stack_count
                                        : from.uses_or_charges;
    const unsigned char current = data.items[into.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
                                      ? into.stack_count
                                      : into.uses_or_charges;
    unsigned int moved = data.items[into.iItemNo].initial_quantity.Maximum() - current;
    if (available <= moved) moved = available;
    into.uses_or_charges += static_cast<signed char>(moved);
    bool emptied = false;
    for (; moved != 0 && !emptied; --moved) emptied = spendUse(from_location, false);
    return emptied;
}

void Inventory::pourHeldUses(unsigned int actor, W8ItemLocation destination)
{
    auto* into = slotAt(destination);
    if (actor >= W8_PARTY_SLOT_COUNT || !into || !held_ || into == &held_->item ||
        into->iItemNo == -1 || into->iItemNo != held_->item.iItemNo)
        return;
    settle({pourUses(*into, kHand) ? kHand : W8ItemLocation{}});
    supersedeQueuedSwap(destination);
}

std::expected<void, InventoryRefusal> Inventory::claimEquipped(unsigned int actor, W8EquipSlot slot)
{
    auto& character = game_.party.characters[actor];
    if (!CanUnequipSlotItem(game_.data, &character, slot)) {
        bind(actor, slot);
        return std::unexpected(InventoryRefusal{game_.data.strings[0x90b]});
    }
    character.EquippedItem[slot].bound = true;
    return {};
}

void Inventory::sortPool()
{
    if (!game_.status.game_started) throw std::logic_error("Inventory::sortPool: before play");
    std::sort(pool_.begin(), pool_.end(), [this](const W8ItemInstance& a, const W8ItemInstance& b) {
        return ItemPrecedesInPool(game_.data, a, b);
    });
}

// ---- Weapon sets and ammunition

void Inventory::bind(unsigned int party_slot, W8EquipSlot equip_slot)
{
    if (auto notice = BindEquippedItem(game_.data, game_.party.characters[party_slot], equip_slot))
        game_.events.push(TextNotice{W8_FONT_PALETTE_WHITE, std::move(*notice)});
}

void Inventory::bindHands(unsigned int party_slot)
{
    bind(party_slot, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    bind(party_slot, W8_EQUIP_SLOT_SECONDARY_WEAPON);
}

// Bind the hands' items, then swap each chosen hand with its alternate.
void Inventory::commitWeaponSwap(unsigned int party_slot, W8WeaponSwap choice)
{
    auto& character = game_.party.characters[party_slot];
    bindHands(party_slot);
    for (auto [swap, active, alternate] :
         {std::tuple{choice.primary, W8_EQUIP_SLOT_PRIMARY_WEAPON,
                     W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON},
          std::tuple{choice.secondary, W8_EQUIP_SLOT_SECONDARY_WEAPON,
                     W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON}}) {
        if (!swap) continue;
        auto& first = character.EquippedItem[active];
        auto& second = character.EquippedItem[alternate];
        std::swap(first, second);
        first.bind_announced = second.bind_announced = false;
    }
    const auto hand = [&](bool swapped, W8EquipSlot equip_slot) {
        return swapped ? Equipment(party_slot, equip_slot) : W8ItemLocation{};
    };
    settle({hand(choice.primary, W8_EQUIP_SLOT_PRIMARY_WEAPON),
            hand(choice.primary, W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON),
            hand(choice.secondary, W8_EQUIP_SLOT_SECONDARY_WEAPON),
            hand(choice.secondary, W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON)});
}

/* A member holding nothing at all has nothing to swap and is told so, and a weapon bound to
   its wearer blocks the whole swap until the binding has been announced. The notices go to
   the conversation's text box while the party talks. */
bool Inventory::swapWeaponSets(unsigned int party_slot, bool announce)
{
    const auto& data = game_.data;
    const short context = game_.activity.conversation ? 0 : -1;
    const auto& character = game_.party.characters[party_slot];
    const auto& equipped = character.EquippedItem;
    if (equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == -1) {
        if (announce) game_.events.push(CharacterNotice{party_slot, data.strings[0x1eb], context});
        return false;
    }
    bindHands(party_slot);
    if (!CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        if (announce) game_.events.push(CharacterNotice{party_slot, data.strings[0x1ee], context});
        return false;
    }
    const auto plan = PlanWeaponSetSwap(data, character);
    if (!plan) return false;
    commitWeaponSwap(party_slot, *plan);
    if (announce) game_.events.push(CharacterNotice{party_slot, data.strings[0x1ec], context});
    return true;
}

/* The combat UI only allows this once the turn is really the member's: a free-turn phase or
   a party movement mode refuses the whole request with a notice, and so does the member
   whose attack is being carried out. Otherwise the weapon sets swap, the queued swap
   clears, and the portraits redraw. */
void Inventory::requestWeaponSwap(unsigned int party_slot, bool announce)
{
    const auto& data = game_.data;
    auto& party = game_.party;
    if (game_.combat.has_value()) {
        if (!game_.combat->round_active && !game_.party_movement_mode) {
            game_.events.push(TextNotice{W8_FONT_PALETTE_BEIGE,
                                         data.strings[W8_NOTICE_WEAPON_SWAP_BLOCKED_COMBAT]});
            return;
        }
        if (game_.combat->resolvingTurnOf(PartyActor{static_cast<int>(party_slot)}) &&
            (party.rows[party_slot].executing.kind == W8_ACTION_ATTACK ||
             party.rows[party_slot].executing.kind == W8_ACTION_BERSERK)) {
            game_.events.push(
                TextNotice{W8_FONT_PALETTE_WHITE,
                           fmt::sprintf(data.strings[0x1f7], party.characters[party_slot].name)});
            return;
        }
    }
    if (IsPartySlotEligible(static_cast<int>(party_slot)) && swapWeaponSets(party_slot, announce))
        party.rows[party_slot].weapon_swap_pending = false;
    game_.events.push(CharacterActionChanged{});
}

/* Fill one equipment slot with the item that pairs with the given one. Only the five generic
   name kinds the merge path singles out pair at all. The alternate left hand is the pair to
   take when it holds something compatible; otherwise the caller may name an item that has
   to be found on the member. Neither being available makes the member say so instead.
   Returns where the partner came from, or None when nothing moved. */
W8ItemLocation Inventory::equipPartner(unsigned int party_slot, const W8ItemInstance& item,
                                       int item_id, W8EquipSlot equip_slot)
{
    const auto& data = game_.data;
    auto& party = game_.party;
    auto& character = party.characters[party_slot];
    if (!ItemHasSingledOutGenericName(data, item.iItemNo)) return {};
    if (item.iItemNo == -1) throw std::logic_error("equipPartner: no item to pair");

    W8ItemLocation pair{};
    const int alternate_left =
        character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    if (alternate_left != -1 && CompatiblePartnerItems(data, item.iItemNo, alternate_left))
        pair = Equipment(party_slot, W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON);
    else if (item_id != -1)
        pair = FindMemberItem(party, party_slot, ItemWithId(item_id), W8ItemReach::Carried);
    if (pair.storage == W8ItemStorage::None) {
        game_.events.push(CharacterSpeech{party_slot, W8_SPEECH_NO_AMMUNITION});
        return {};
    }
    exchange(pair, Equipment(party_slot, equip_slot), -1);
    game_.events.push(CharacterNotice{party_slot, data.strings[0x1d4]});
    return pair;
}

/* The same pairing for items that carry a number of uses rather than being worn: the
   compatible-partner table names the kind to look for, and the first one found anywhere on
   the member gives its uses. With no partner the member only voices the attempt. Returns
   the partner's location when its uses ran out. */
W8ItemLocation Inventory::mergePartnerUses(unsigned int party_slot, W8ItemInstance& item)
{
    const auto& data = game_.data;
    const int item_id = item.iItemNo;
    if (item_id == -1 || !ItemUsesShots(data, item_id)) return {};
    short related_kind;
    if (!GetItemMergeKind(data, item_id, &related_kind)) return {};

    const auto partner = FindMemberItem(game_.party, party_slot, ItemOfKind(data, related_kind),
                                        W8ItemReach::Carried);
    if (partner.storage != W8ItemStorage::None) {
        const bool emptied = pourUses(item, partner);
        game_.events.push(CharacterNotice{party_slot, data.strings[0x1d4]});
        return emptied ? partner : W8ItemLocation{};
    }
    game_.events.push(CharacterSpeech{party_slot, W8_SPEECH_NO_AMMUNITION});
    return {};
}

/* Split a stack of thrown weapons across the two hands. The source is the hand opposite the
   requested one and the destination is the requested hand, so the primary hand takes the
   larger half and the off hand the smaller; a lone item moves across as itself, but only
   when the primary hand is the destination. */
void Inventory::splitThrowableStack(unsigned int party_slot, W8EquipSlot equip_slot)
{
    const auto& data = game_.data;
    auto& character = game_.party.characters[party_slot];
    if (equip_slot != W8_EQUIP_SLOT_PRIMARY_WEAPON && equip_slot != W8_EQUIP_SLOT_SECONDARY_WEAPON)
        return;
    const auto source_slot = GetPairedEquipSlot(equip_slot);
    auto& source = character.EquippedItem[source_slot];
    const int item_id = source.iItemNo;
    // Thrown weapons are the only kind that stacks across the two hands.
    if (item_id == -1 || data.items[item_id].equip_class != W8_ITEM_EQUIP_CLASS_THROWN_WEAPON)
        return;

    unsigned char moved = source.stack_count;
    if (equip_slot == W8_EQUIP_SLOT_PRIMARY_WEAPON) {
        if (moved == 1) {
            exchange(Equipment(party_slot, equip_slot), Equipment(party_slot, source_slot), -1);
            return;
        }
        moved -= (moved >> 1);
    } else {
        if (moved == 1) return;
        moved >>= 1;
    }
    auto created = MakeItem(data, game_.rng, item_id, false, source.identified, source.bound);
    source.stack_count -= moved;
    created.stack_count = moved;
    created.bind_announced = false;
    character.EquippedItem[equip_slot] = created;
}

/* Thrown weapons come off their stack and the other hand's stack is shared out, ammunition is
   taken from the paired hand and its partner equipped when it runs out, and an emptied shot
   weapon draws uses from its partner. */
bool Inventory::consumeAttackItems(unsigned int actor, unsigned int hand, W8EquipSlot used,
                                   W8EquipSlot paired)
{
    const auto& data = game_.data;
    auto& character = game_.party.characters[actor];
    const auto used_location = Equipment(actor, used);
    if (paired != W8_EQUIP_SLOT_NONE) {
        const auto paired_location = Equipment(actor, paired);
        const int id = character.EquippedItem[paired].iItemNo;
        const bool emptied = spendUse(paired_location, true);
        W8ItemLocation partner{};
        if (character.EquippedItem[paired].stack_count == 0)
            partner = equipPartner(actor, character.EquippedItem[used], id, paired);
        settle({emptied ? paired_location : W8ItemLocation{},
                partner.storage != W8ItemStorage::None ? paired_location : W8ItemLocation{},
                partner});
        return false;
    }
    auto& item = character.EquippedItem[used];
    if (item.iItemNo == -1 || character.Hand[hand].combat_skill != W8_SKILL_RANGED_COMBAT ||
        data.items[item.iItemNo].spell_id != W8_SPELL_NONE)
        return false;
    const auto& record = data.items[item.iItemNo];
    if (record.equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON) {
        if (record.flags & W8_ITEM_FLAG_NEVER_DEPLETES) return false;
        const bool emptied = spendUse(used_location, true);
        const bool split = emptied || record.quantity_kind != W8_ITEM_QUANTITY_STACK;
        if (split) splitThrowableStack(actor, used);
        const auto other_hand = Equipment(actor, GetPairedEquipSlot(used));
        const bool spent = item.iItemNo == -1 || item.stack_count == 0;
        settle({split ? used_location : W8ItemLocation{}, split ? other_hand : W8ItemLocation{}});
        return spent;
    }
    spendUse(used_location, false);
    W8ItemLocation partner{};
    const bool exhausted = item.uses_or_charges == 0;
    if (exhausted) partner = mergePartnerUses(actor, item);
    // A shot weapon that runs out counts as a change of the hand that holds it.
    settle({exhausted ? used_location : W8ItemLocation{}, partner});
    return false;
}

/* Equip the off-hand partner the primary weapon needs and draw uses for an empty shot
   weapon. */
void Inventory::readyPrimaryWeapon(unsigned int party_slot)
{
    auto& character = game_.party.characters[party_slot];
    auto& weapon = character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON];
    if (weapon.iItemNo == -1) return;
    W8ItemLocation partner{};
    if (character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1)
        partner = equipPartner(party_slot, weapon, -1, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    W8ItemLocation spent{};
    if (weapon.uses_or_charges == 0) spent = mergePartnerUses(party_slot, weapon);
    const auto off_hand = Equipment(party_slot, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    settle({partner.storage != W8ItemStorage::None ? off_hand : W8ItemLocation{}, partner, spent});
}

/* Each unusable piece is stored like a received item: on the member, then in the pool, and
   otherwise at the party's feet. Settling the party calls this, so nothing here settles. */
bool Inventory::unequipUnusable(unsigned int party_slot)
{
    const auto& data = game_.data;
    auto& party = game_.party;
    auto& character = party.characters[party_slot];
    bool hands_changed = false;
    std::bitset<W8_EQUIP_SLOT_COUNT> kept;
    while (const auto equip_slot = FindUnusableEquipment(
               data, character, character.fInParty ? &party.modifiers : nullptr, kept)) {
        // Named while it is still worn, as retail did.
        const auto notice =
            game_.status.game_started
                ? UnusableEquipmentNotice(data, character, character.EquippedItem[*equip_slot])
                : std::string{};
        auto item = std::exchange(character.EquippedItem[*equip_slot], W8ItemInstance{});
        auto placed = placeWithCharacter(member(party_slot), item, false, false, true);
        acquire(placed);
        if (!placed) {
            placed = placeInPool(item, false, true);
            acquire(placed);
        }
        if (!placed && !dropUnstored(item)) {
            character.EquippedItem[*equip_slot] = item;
            kept.set(*equip_slot);
            continue;
        }
        hands_changed |= IsHandSlot(*equip_slot);
        if (!notice.empty()) game_.events.push(CharacterNotice{party_slot, notice});
    }
    return hands_changed;
}

// ---- Gold

/* The gold goes into the purse and into the level's running tally, and announcing it plays
   the coin sound. */
void AddPartyGold(Game& game, int amount, bool announce)
{
    const std::string_view sound_path = "Data\\Sound\\Misc\\ChaChing.wav";
    game.party.gold += amount;
    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT)
        game.status.level_progress[GetLoadedLevelID()].gold_collected += amount;
    if (announce) {
        game.events.push(TextNotice{8, fmt::format("{} {:d} {}.", game.data.strings[0x15f], amount,
                                                   game.data.strings[0x160])});
        if (!wiz8::sound::is_file_playing(sound_path)) wiz8::sound::play(sound_path);
    }
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
Inventory::UseOutcome Inventory::useOnce(unsigned int party_slot, W8ItemLocation location)
{
    auto& game = game_;
    W8Character* character = &game.party.characters[party_slot];
    W8ItemInstance* item = slotAt(location);
    const W8ItemDatabaseRecord* record = &game.data.items[item->iItemNo];
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

    if (!CanCharacterUseItem(game.data, character, item->iItemNo)) {
        game.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game.data.strings[0x164],
                                                     GetItemDisplayName(game.data, item))});
        return {};
    }
    if (!CanCharacterActivateItem(character, item)) {
        game.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game.data.strings[0x165],
                                                     GetItemDisplayName(game.data, item))});
        return {};
    }

    if (record->quantity_kind == W8_ITEM_QUANTITY_CHARGES && item->uses_or_charges == 0) {
        game.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game.data.strings[0x1f1],
                                                     GetItemDisplayName(game.data, item))});
        event_type = g_special_event13;
    } else if (record->quantity_kind == W8_ITEM_QUANTITY_SHOTS && item->uses_or_charges == 0) {
        game.events.push(
            CharacterNotice{party_slot, fmt::sprintf(game.data.strings[0x1f2],
                                                     GetItemDisplayName(game.data, item))});
        event_type = 39;
    } else {
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            character->uiCondition[W8_CONDITION_SILENCED] != 0) {
            game.events.push(CharacterNotice{party_slot, game.data.strings[0x166]});
            return {};
        }

        /* The one item that is not used on anybody: it hands the level's NPC
           over to the fact that says the party has met them. */
        if (item->iItemNo == 0x29f) {
            [[maybe_unused]] W8NpcState* npc = GetNpcState(game.party.npcIndex(party_slot));
            assert(npc != 0);
            assert(npc->name_style == W8_NPC_RFS81_A);
            SetFact(W8_FACT_RFS81_HAS_BEEN_FIXED, 1, false);
            SetFact(W8_FACT_QUEST_ANDROID_BROKEN, 0, false);
            return {{1, std::nullopt}, spendUse(location, true)};
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
            fatigue_cost = game.data.spells[record->spell_id].spell_level * 8 + 0x14;
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
                    CharacterNotice{party_slot, fmt::sprintf(game.data.strings[0x1f3],
                                                             GetItemDisplayName(game.data, item))});
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
                AimStoredAction(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT,
                                CharacterTarget(party_slot));
            }
        }
        target.name_known = true;

        /* A spell-carrying item the world-cursor handler refuses cannot be used
           at all, and the attempt is over before anything is announced. */
        if (record->spell_id != W8_SPELL_NONE &&
            game.data.items[item->iItemNo].spell_id != W8_SPELL_NONE &&
            game.data.items[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_GADGET &&
            (game.data.items[item->iItemNo].equip_class < W8_ITEM_EQUIP_CLASS_FOOD ||
             game.data.items[item->iItemNo].equip_class > W8_ITEM_EQUIP_CLASS_DRINK) &&
            DispatchWorldCursorNodeCommand(0, 4, 1)) {
            return {{0, 0}};
        }

        // Items outside the use-message table are used without an announcement.
        const bool announced = static_cast<int>(record->category) < W8_ITEM_USE_TYPE_COUNT;
        if (announced && TargetSourceIsCharacter(&target, 0)) {
            game.events.push(CharacterNotice{
                static_cast<unsigned int>(target.iChar),
                fmt::format("{} {}", game.data.strings[g_item_use_messages[record->category * 2]],
                            GetItemDisplayName(game.data, item))});
        } else if (announced && TargetSourceIsMonster(&target, 0)) {
            game.events.push(TextNotice{
                9, fmt::format("{} {} {}", GetMonsterName(FindMonster(target.iMonsterID), 0, 0),
                               game.data.strings[g_item_use_messages[record->category * 2]],
                               GetItemDisplayName(game.data, item))});
        }

        if (!g_settings.verbose_combat_messages) {
            game.events.push(TextBoxModeChange{W8_TEXT_BOX_MODE_APPEND, -1});
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
                emptied = spendUse(location, true);
            }
        }
        return {{used, own_cost()}, emptied};
    }

    QueueCharacterEvent(&game.party.characters[party_slot], event_type, 0,
                        kCharacterEventFullVolume);
    return {{used, own_cost()}, emptied};
}

W8ItemUseResult Inventory::use(unsigned int actor, const W8ItemUse& use)
{
    const auto location = use.item;
    if (actor >= W8_PARTY_SLOT_COUNT || !holds(use)) return {};
    const auto outcome = useOnce(actor, location);
    settle({outcome.emptied ? location : W8ItemLocation{}});
    return outcome.result;
}
