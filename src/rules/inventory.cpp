#include "wiz8/rules/inventory.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <expected>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/events.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/rule_context.h"
#include "wiz8/rules/targeting.h"

// The speech a member makes when a shot weapon or ammunition has no partner to draw from.
enum { W8_SPEECH_NO_AMMUNITION = 39 };

// ---- Storage helpers

static bool IsHandSlot(unsigned int slot)
{
    return slot == W8_EQUIP_SLOT_PRIMARY_WEAPON || slot == W8_EQUIP_SLOT_SECONDARY_WEAPON;
}

static bool IsCarried(W8ItemLocation location)
{
    return location.storage == W8ItemStorage::Equipment ||
           location.storage == W8ItemStorage::Backpack;
}

ItemSource PartyItem(W8Party& party, W8ItemLocation location)
{
    return {*party.itemAt(location), location};
}

// Empty a source: a party location gives its item up, closing up the pool; a value clears.
static void Discard(W8Party& party, ItemSource source)
{
    if (source.location)
        party.take(*source.location);
    else
        source.item = {};
}

/* Move a complete item into an empty destination and empty its source. Between two party
   locations this is an exchange; moving a value into the hand starts a held item with no
   origin. */
static void MoveInto(W8Party& party, W8ItemInstance& destination,
                     std::optional<W8ItemLocation> destination_location, ItemSource source)
{
    if (destination_location && source.location) {
        party.exchange(*destination_location, *source.location,
                       IsCarried(*source.location) ? static_cast<int>(source.location->character)
                                                   : -1);
        return;
    }
    destination = source.location ? party.take(*source.location)
                                  : std::exchange(source.item, W8ItemInstance{});
    destination.bind_announced = false;
    if (destination_location && destination_location->storage == W8ItemStorage::Hand)
        party.held = {destination, {}, -1};
}

// The location of a recipient's equipment or backpack slot; None outside the party.
static W8ItemLocation RecipientLocation(const ItemRecipient& recipient, W8ItemStorage storage,
                                        unsigned int slot)
{
    if (!recipient.slot) return {};
    return {storage, *recipient.slot, slot};
}

/* The most of one item a character can hold at once: the record's own quantity dice taken
   at their maximum. */
static int MaximumQuantity(const GameData& data, int item_id)
{
    return data.items[item_id].initial_quantity.Maximum();
}

/* Normalize a stack and split every full overflow stack into the party pool. Quantity kinds
   two through four live in uses_or_charges instead. */
static void NormalizeItemStack(const GameData& data, W8Party& party, W8ItemInstance& item)
{
    if (item.iItemNo == -1) return;

    const auto& record = data.items[item.iItemNo];
    if (record.quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (item.uses_or_charges != 0) {
            if (item.stack_count == 0) item.stack_count = item.uses_or_charges;
            item.uses_or_charges = 0;
        }
    } else if (record.quantity_kind >= W8_ITEM_QUANTITY_CHARGES &&
               record.quantity_kind <= W8_ITEM_QUANTITY_SHOTS && item.stack_count != 0) {
        if (item.uses_or_charges == 0) item.uses_or_charges = item.stack_count;
        item.stack_count = 0;
    }

    if (record.quantity_kind != W8_ITEM_QUANTITY_STACK) return;
    if (record.maximum_quantity == 0) {
        item.stack_count = 0;
        return;
    }
    while (item.stack_count > record.maximum_quantity) {
        W8ItemInstance split = item;
        unsigned char quantity = item.stack_count - record.maximum_quantity;
        if (quantity > record.maximum_quantity) quantity = record.maximum_quantity;
        split.stack_count = quantity;
        if (!party.insertIntoPool(party.item_count, split)) {
            item.stack_count = record.maximum_quantity;
            return;
        }
        item.stack_count -= quantity;
    }
}

/* Merge as much of one stack as fits in another. A completely consumed source is emptied
   and reported; a partial merge is reported through `partially_merged`. */
static bool MergeStack(const GameData& data, W8Party& party, W8ItemInstance& destination,
                       ItemSource source, bool* partially_merged)
{
    if (destination.iItemNo == -1) return false;

    NormalizeItemStack(data, party, destination);
    const auto& record = data.items[destination.iItemNo];
    if (record.quantity_kind != W8_ITEM_QUANTITY_STACK) return false;
    if (destination.stack_count > record.maximum_quantity)
        destination.stack_count = record.maximum_quantity;
    if (destination.iItemNo != source.item.iItemNo ||
        destination.identified != source.item.identified)
        return false;

    const unsigned char room = record.maximum_quantity - destination.stack_count;
    if (room == 0) return false;
    const unsigned char moved = source.item.stack_count < room ? source.item.stack_count : room;
    destination.stack_count += moved;
    source.item.stack_count -= moved;
    if (source.item.stack_count == 0) {
        Discard(party, source);
        return true;
    }
    if (partially_merged) *partially_merged = true;
    return false;
}

/* Take one of an item's uses away, and when that was the last one, empty the source. A
   stack counts down its count and a charged item its charges; an already exhausted charged
   item is cleared only when the caller allows it, and one whose kind has hidden properties
   is kept even then. */
static bool ConsumeUse(const GameData& data, W8Party& party, ItemSource source,
                       bool remove_exhausted)
{
    auto& item = source.item;
    const int item_id = item.iItemNo;
    if (item_id == -1) return false;
    const auto& record = data.items[item_id];
    if (record.quantity_kind == W8_ITEM_QUANTITY_NONE &&
        record.equip_class != W8_ITEM_EQUIP_CLASS_DRINK)
        return false;

    if (record.quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (item.stack_count != 0) --item.stack_count;
        if (item.stack_count != 0) return false;
    } else if (item.uses_or_charges == 0) {
        if (!remove_exhausted) return false;
    } else {
        --item.uses_or_charges;
        if (item.uses_or_charges != 0) return false;
        if (ItemHasHiddenProperties(data, item_id)) return false;
    }

    Discard(party, source);
    return true;
}

bool ConsumeItemUse(const RuleContext& rules, W8ItemLocation location, bool remove_exhausted)
{
    return ConsumeUse(rules.data, rules.party, PartyItem(rules.party, location), remove_exhausted);
}

// ---- Searching

bool ItemIsOfKind(const GameData& data, const W8ItemInstance& item, short item_kind)
{
    return item.iItemNo != -1 && data.items[item.iItemNo].unidentified_name_index == item_kind;
}

W8ItemInstance* FindCharacterItem(W8Character& character, W8ItemMatch match, W8ItemReach reach)
{
    return const_cast<W8ItemInstance*>(FindCharacterItem(std::as_const(character), match, reach));
}

const W8ItemInstance* FindCharacterItem(const W8Character& character, W8ItemMatch match,
                                        W8ItemReach reach)
{
    if (auto* item = std::ranges::find_if(character.EquippedItem, match);
        item != std::end(character.EquippedItem))
        return item;
    if (reach == W8ItemReach::Worn) return nullptr;
    auto* item = std::ranges::find_if(character.backpack, match);
    return item != std::end(character.backpack) ? item : nullptr;
}

/* Where a member carries the first match: equipment slot order, then backpack order. */
static W8ItemLocation FindMemberItem(const W8Party& party, unsigned int slot, W8ItemMatch match,
                                     W8ItemReach reach)
{
    const auto& character = party.characters[slot];
    const auto equipped = std::ranges::find_if(character.EquippedItem, match);
    if (equipped != std::end(character.EquippedItem))
        return {W8ItemStorage::Equipment, slot,
                static_cast<unsigned int>(equipped - std::begin(character.EquippedItem))};
    if (reach == W8ItemReach::Worn) return {};
    const auto carried = std::ranges::find_if(character.backpack, match);
    if (carried != std::end(character.backpack))
        return {W8ItemStorage::Backpack, slot,
                static_cast<unsigned int>(carried - std::begin(character.backpack))};
    return {};
}

W8ItemLocation FindPartyItem(const W8Party& party, W8ItemMatch match, W8ItemReach reach)
{
    if (party.held.item.iItemNo != -1 && match(party.held.item)) return {W8ItemStorage::Hand};
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value())
            if (const auto found = FindMemberItem(party, slot, match, reach);
                found.storage != W8ItemStorage::None)
                return found;
    if (reach != W8ItemReach::Party) return {};
    const auto pool = std::span{party.item_pool}.first(party.item_count);
    const auto found = std::ranges::find_if(pool, match);
    if (found == pool.end()) return {};
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(found - pool.begin())};
}

/* How many of one item a character holds, counting a stack as its count and anything else
   as one. */
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach)
{
    const auto units = [](const W8ItemInstance& item) {
        return item.stack_count == 0 ? 1u : item.stack_count;
    };
    unsigned int total = 0;
    for (const auto& item : character.EquippedItem)
        if (item.iItemNo == item_id) total += units(item);
    if (reach == W8ItemReach::Worn) return total;
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (character.backpack[slot].iItemNo == item_id)
            /* Bug: this counts the equipped slot with the same index. */
            total += units(character.EquippedItem[slot]);
    return total;
}

/* The whole party: the item in hand, every occupied slot's carried items and the pool. The
   held stack counts its size as stored. */
unsigned int CountPartyItem(const W8Party& party, int item_id)
{
    unsigned int total = 0;
    if (party.held.item.iItemNo != -1 && party.held.item.iItemNo == item_id)
        total = party.held.item.stack_count;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value())
            total += CountCharacterItem(party.characters[slot], item_id, W8ItemReach::Carried);
    for (const auto& item : std::span{party.item_pool}.first(party.item_count))
        if (item.iItemNo == item_id) total += item.stack_count == 0 ? 1 : item.stack_count;
    return total;
}

bool EveryCharacterHasItem(const W8Party& party, int item_id, W8ItemReach reach)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value() &&
            !FindCharacterItem(party.characters[slot], ItemWithId(item_id), reach))
            return false;
    return true;
}

int GetItemInHand(const W8Party& party)
{
    return party.held.item.iItemNo;
}

W8ItemUse ItemUseAt(const W8Party& party, W8ItemLocation location)
{
    const auto* item = party.itemAt(location);
    return {-1, location, item ? item->iItemNo : -1};
}

W8ItemInstance* ResolveItemUse(W8Party& party, const W8ItemUse& use)
{
    if (use.item.storage == W8ItemStorage::Pool && use.item.slot >= party.item_count)
        return nullptr;
    auto* item = party.itemAt(use.item);
    return item && item->iItemNo != -1 && item->iItemNo == use.item_id ? item : nullptr;
}

// ---- Placing items

/* Pick the equipment slot a newly acquired item goes into. Before play has started the two
   hand pairs are special-cased: a dual-wield-capable item can displace the right hand's
   item into the left hand, a ranged weapon fills a free pair before anything else, and
   ammunition looks for a ranged weapon to go with and otherwise fails. */
static W8EquipSlot ChooseCharacterEquipSlot(const RuleContext& rules, W8Character& character,
                                            int item_id)
{
    const auto& data = rules.data;
    W8EquipSlot slot = GetItemDefaultEquipSlot(data, item_id, rules.game_started);
    if (rules.game_started ||
        (slot != W8_EQUIP_SLOT_PRIMARY_WEAPON && slot != W8_EQUIP_SLOT_SECONDARY_WEAPON))
        return slot;

    auto& equipped = character.EquippedItem;
    const int primary_right = equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    const int alternate_right = equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo;
    const int primary_left = equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
    const int alternate_left = equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    const auto one_handed_melee = [&](int held) {
        return (data.items[held].equip_class == W8_ITEM_EQUIP_CLASS_SHORT_WEAPON ||
                data.items[held].equip_class == W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON) &&
               (data.items[held].flags & W8_ITEM_FLAG_TWO_HANDED) == 0;
    };
    // Within one character an exchange with an empty slot moves the item.
    const auto move = [](W8ItemInstance& to, W8ItemInstance& from) {
        std::swap(to, from);
        to.bind_announced = from.bind_announced = false;
    };
    switch (data.items[item_id].equip_class) {
    case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
    case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
        if (character.skills[W8_SKILL_DUAL_WEAPONS].points != 0) {
            if (CanEquipItemInSlot(data, &character, item_id, 7, false)) {
                if (primary_right != -1 && primary_left == -1 && one_handed_melee(primary_right))
                    slot = W8_EQUIP_SLOT_SECONDARY_WEAPON;
                if (alternate_right != -1 && alternate_left == -1 &&
                    one_handed_melee(alternate_right))
                    return W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
            } else {
                if (primary_right != -1 && primary_left == -1 &&
                    CanEquipItemInSlot(data, &character, primary_right, 7, false)) {
                    move(equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON],
                         equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON]);
                    return W8_EQUIP_SLOT_PRIMARY_WEAPON;
                }
                if (alternate_right != -1 && alternate_left == -1 &&
                    CanEquipItemInSlot(data, &character, alternate_right, 9, false)) {
                    move(equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON],
                         equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON]);
                    return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
                }
            }
        }
        break;
    case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
        if (primary_right == -1 && primary_left == -1) return W8_EQUIP_SLOT_PRIMARY_WEAPON;
        if (alternate_right == -1 && alternate_left == -1)
            return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
        break;
    case W8_ITEM_EQUIP_CLASS_AMMUNITION:
        if (primary_right != -1 &&
            data.items[primary_right].equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON)
            return W8_EQUIP_SLOT_SECONDARY_WEAPON;
        if (alternate_right != -1 &&
            data.items[alternate_right].equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON)
            return W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
        return W8_EQUIP_SLOT_NONE;
    default:
        break;
    }
    return slot;
}

/* Equipment is tried first when requested; otherwise compatible stacks are coalesced before
   the first empty backpack slot is used. The source item is consumed only after a
   destination accepts it, so the caller can still fall back to the pool. */
std::optional<ItemPlacement> PlaceWithCharacter(const RuleContext& rules, ItemRecipient recipient,
                                                ItemSource source, bool equip_if_possible,
                                                bool announce, bool skip_stacking)
{
    auto& data = rules.data;
    auto& party = rules.party;
    auto& character = recipient.character;

    if (equip_if_possible && CanCharacterUseItem(data, &character, source.item.iItemNo)) {
        const W8EquipSlot equip_slot =
            ChooseCharacterEquipSlot(rules, character, source.item.iItemNo);
        if (equip_slot != W8_EQUIP_SLOT_NONE) {
            auto& destination = character.EquippedItem[equip_slot];
            const auto location =
                RecipientLocation(recipient, W8ItemStorage::Equipment, equip_slot);
            /* A refused merge leaves the equipment untouched; retail skips the
               acquisition for it (0x0051c379). */
            if (destination.iItemNo == -1) {
                MoveInto(party, destination,
                         recipient.slot ? std::optional{location} : std::nullopt, source);
                return ItemPlacement{location, nullptr};
            }
            if (MergeStack(data, party, destination, source, nullptr))
                return ItemPlacement{{}, nullptr};
        }
    }

    // Named before a merge can empty the source.
    const std::string display_name =
        announce ? FormatItemDisplayName(data, &source.item, true) : "";
    W8ItemInstance* stored_item = nullptr;
    unsigned int stored_index = 0;
    bool placed = false;
    if (data.items[source.item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK && !skip_stacking) {
        for (unsigned int index = 0; index < 12 && !stored_item; ++index) {
            if (MergeStack(data, party, character.EquippedItem[index], source, nullptr)) {
                stored_item = &character.EquippedItem[index];
                stored_index = index;
            }
        }
        for (unsigned int index = 0; index < 8 && !stored_item; ++index) {
            if (MergeStack(data, party, character.backpack[index], source, nullptr)) {
                stored_item = &character.backpack[index];
                stored_index = index;
            }
        }
    }

    W8ItemLocation stored{};
    if (!stored_item) {
        while (stored_index < 8 && character.backpack[stored_index].iItemNo != -1) ++stored_index;
        if (stored_index == 8) return std::nullopt;
        stored_item = &character.backpack[stored_index];
        stored = RecipientLocation(recipient, W8ItemStorage::Backpack, stored_index);
        MoveInto(party, *stored_item, recipient.slot ? std::optional{stored} : std::nullopt,
                 source);
        placed = true;
    }

    if (announce && recipient.slot)
        rules.events.push(
            CharacterNotice{*recipient.slot, fmt::sprintf(data.strings[0x1e8], display_name)});
    /* Retail always reports the backpack cell at the stored index, even when the stack was
       merged into the equipped slot with that index. */
    return ItemPlacement{placed ? stored : W8ItemLocation{}, &character.backpack[stored_index]};
}

std::optional<ItemPlacement> PlaceInPool(const RuleContext& rules, ItemSource source, bool announce,
                                         bool skip_stacking)
{
    auto& data = rules.data;
    auto& party = rules.party;
    // Named before a merge can empty the source.
    const std::string display_name =
        announce ? FormatItemDisplayName(data, &source.item, true) : "";
    bool partially_merged = false;
    unsigned int index = 0;
    bool merged = false;

    if (data.items[source.item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK && !skip_stacking) {
        for (; index < party.item_count; ++index) {
            if (MergeStack(data, party, party.item_pool[index], source, &partially_merged)) {
                merged = true;
                break;
            }
        }
    }

    if (!merged) {
        if (party.item_count >= std::size(party.item_pool)) return std::nullopt;
        index = 0;
        W8ItemInstance entry{};
        MoveInto(party, entry, std::nullopt, source);
        party.insertIntoPool(0, entry);
    }

    if (announce)
        rules.events.push(
            TextNotice{W8_FONT_PALETTE_WHITE, fmt::sprintf(data.strings[0x1e9], display_name)});
    return ItemPlacement{merged ? W8ItemLocation{} : W8ItemLocation{W8ItemStorage::Pool, 0, 0},
                         &party.item_pool[index]};
}

bool PlaceInEmptyHand(const RuleContext& rules, ItemSource source)
{
    auto& party = rules.party;
    if (party.held.item.iItemNo != -1) return false;
    MoveInto(party, party.held.item, W8ItemLocation{W8ItemStorage::Hand}, source);
    return true;
}

std::optional<UnusableEquipment> TakeUnusableEquipment(const RuleContext& rules,
                                                       unsigned int party_slot,
                                                       std::bitset<W8_EQUIP_SLOT_COUNT> kept)
{
    auto& data = rules.data;
    auto& party = rules.party;
    auto& character = party.characters[party_slot];
    EvaluateCharacterInputs(data, character, character.fInParty ? &party.modifiers : nullptr);
    auto* item = std::ranges::find_if(character.EquippedItem, [&](const auto& candidate) {
        const auto slot = static_cast<unsigned int>(&candidate - character.EquippedItem);
        return !kept[slot] && candidate.iItemNo != -1 &&
               !CanCharacterUseItem(data, &character, candidate.iItemNo);
    });
    if (item == std::end(character.EquippedItem)) return std::nullopt;
    const auto equip_slot = static_cast<unsigned int>(item - character.EquippedItem);
    std::string notice;
    if (rules.game_started) {
        unsigned short message_id = 0;
        const auto& pronoun = data.strings[g_gender_name_message_rows[character.gender][2]];
        notice = FindUnmetItemRequirement(&character, &data.items[item->iItemNo], &message_id)
                     ? fmt::sprintf(data.strings[0x1ef], pronoun,
                                    FormatItemDisplayName(data, item, true), pronoun,
                                    data.strings[message_id])
                     : fmt::sprintf(data.strings[0x1f0], pronoun,
                                    FormatItemDisplayName(data, item, true));
    }
    return UnusableEquipment{party.take({W8ItemStorage::Equipment, party_slot, equip_slot}),
                             IsHandSlot(equip_slot), equip_slot, std::move(notice)};
}

// ---- Losing items and gold

/* The hand first, then every member's slots, then the pool. When remove_all is clear the
   search stops at the first hit. Even when it is set, only one copy per member is cleared;
   the pool walk does compact every matching entry. */
InventoryEdit RemovePartyItemByID(const RuleContext& rules, int item_id, bool remove_all)
{
    auto& party = rules.party;
    InventoryEdit edit;
    auto& removed = edit.replaced;

    if (party.held.item.iItemNo != -1 && party.held.item.iItemNo == item_id) {
        party.take({W8ItemStorage::Hand});
        removed.push_back({W8ItemStorage::Hand});
        if (!remove_all) return edit;
    }
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (!party.members[slot].has_value()) continue;
        const auto found = FindMemberItem(party, slot, ItemWithId(item_id), W8ItemReach::Carried);
        if (found.storage == W8ItemStorage::None) continue;
        removed.push_back(found);
        party.take(found);
        if (!remove_all) return edit;
    }
    for (unsigned int index = 0; index < party.item_count; ++index) {
        if (party.item_pool[index].iItemNo != item_id) continue;
        removed.push_back({W8ItemStorage::Pool, 0, index});
        party.take(removed.back());
        if (!remove_all) return edit;
        /* The tail moved down into this index, so it is visited again. */
        --index;
    }
    return edit;
}

std::optional<InventoryEdit> RemoveInventoryQuantity(const RuleContext& rules,
                                                     W8ItemLocation location, unsigned amount)
{
    auto* item = rules.party.itemAt(location);
    if (!item || item->iItemNo == -1 || amount == 0) return std::nullopt;
    if (amount >= std::max<unsigned>(1, item->stack_count)) {
        if (!TakeInventoryItem(rules, location)) return std::nullopt;
        return InventoryEdit{{location}};
    }
    item->stack_count -= static_cast<unsigned char>(amount);
    return InventoryEdit{};
}

std::optional<W8ItemInstance> TakeInventoryItem(const RuleContext& rules, W8ItemLocation location)
{
    auto& party = rules.party;
    if (location.storage == W8ItemStorage::Pool && location.slot >= party.item_count)
        return std::nullopt;
    if (!party.itemAt(location)) return std::nullopt;
    return party.take(location);
}

void SpendPartyGold(W8Party& party, unsigned int amount)
{
    party.gold = amount > party.gold ? 0 : party.gold - amount;
}

// ---- Player edits

/* Split a stack into the hand, or put part of the held stack back. Taking from a location
   starts or grows the held stack; splitting the held stack returns the remainder to where it
   came from when that still fits, and otherwise to the actor's backpack or the pool. */
std::optional<InventoryEdit> SplitItemStack(const RuleContext& rules,
                                            W8ItemLocation source_location, unsigned amount,
                                            unsigned actor)
{
    auto& data = rules.data;
    auto& party = rules.party;
    if (source_location.storage == W8ItemStorage::Pool && source_location.slot >= party.item_count)
        return std::nullopt;
    auto* source = party.itemAt(source_location);
    if (!source || source->iItemNo < 0 || actor >= W8_PARTY_SLOT_COUNT || amount == 0 ||
        amount > source->stack_count ||
        data.items[source->iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK ||
        (data.items[source->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD))
        return std::nullopt;
    if (source_location.storage == W8ItemStorage::Equipment && amount == source->stack_count &&
        !CanUnequipSlotItem(data, &party.characters[source_location.character],
                            static_cast<W8EquipSlot>(source_location.slot)))
        return std::nullopt;
    const bool from_hand = source_location.storage == W8ItemStorage::Hand;
    const bool append = !from_hand && party.held.item.iItemNo != -1;
    if (append &&
        (party.held.item.iItemNo != source->iItemNo ||
         party.held.item.stack_count + amount > data.items[source->iItemNo].maximum_quantity))
        return std::nullopt;
    if (from_hand && amount == source->stack_count) return InventoryEdit{{}, false};

    W8ItemLocation destination_location{W8ItemStorage::Hand};
    if (from_hand) {
        destination_location = party.held.origin;
        auto* original = party.itemAt(destination_location);
        const bool equipment = destination_location.storage == W8ItemStorage::Equipment;
        if (!original || original->iItemNo != -1 ||
            (destination_location.storage != W8ItemStorage::Backpack && !equipment) ||
            (equipment &&
             (!CanCharacterUseItem(data, &party.characters[destination_location.character],
                                   source->iItemNo) ||
              !CanEquipItemInSlot(data, &party.characters[destination_location.character],
                                  source->iItemNo, destination_location.slot, false)))) {
            destination_location = {};
            for (unsigned slot = 0; slot < 8; ++slot) {
                if (party.characters[actor].backpack[slot].iItemNo == -1) {
                    destination_location = {W8ItemStorage::Backpack, actor, slot};
                    break;
                }
            }
            if (destination_location.storage == W8ItemStorage::None) {
                if (party.item_count == std::size(party.item_pool)) return std::nullopt;
                destination_location = {W8ItemStorage::Pool, 0, party.item_count};
            }
        }
    }
    const unsigned transferred = from_hand ? source->stack_count - amount : amount;
    auto part = *source;
    part.stack_count = static_cast<unsigned char>(transferred);
    source->stack_count -= static_cast<unsigned char>(transferred);
    if (append) {
        party.held.item.stack_count += static_cast<unsigned char>(transferred);
    } else if (destination_location.storage == W8ItemStorage::Hand) {
        party.held = {part, source_location, static_cast<int>(actor)};
    } else if (destination_location.storage == W8ItemStorage::Pool) {
        party.insertIntoPool(destination_location.slot, part);
    } else {
        *party.itemAt(destination_location) = part;
    }
    const bool emptied = source->stack_count == 0;
    if (emptied) party.take(source_location);
    return InventoryEdit{{emptied ? source_location : W8ItemLocation{},
                          append ? W8ItemLocation{} : destination_location}};
}

/* Exchange two locations, either of which may be empty, as the player does by moving an
   item. Equipment slots accept only what their wearer may equip, and a bound item stays
   where it is. */
std::optional<InventoryEdit> ExchangeInventoryItems(const RuleContext& rules,
                                                    W8ItemLocation first_location,
                                                    W8ItemLocation second_location, unsigned actor)
{
    auto& data = rules.data;
    auto& party = rules.party;
    if (second_location.storage == W8ItemStorage::Pool && second_location.slot >= party.item_count)
        return std::nullopt;
    if (first_location.storage == W8ItemStorage::Pool && first_location.slot >= party.item_count)
        return std::nullopt;
    auto* first = party.itemAt(first_location);
    auto* second = party.itemAt(second_location);
    if (!first || !second || first == second || actor >= W8_PARTY_SLOT_COUNT) return std::nullopt;
    for (auto [location, incoming] :
         {std::pair{first_location, second}, std::pair{second_location, first}}) {
        if (location.storage != W8ItemStorage::Equipment) continue;
        auto* character = &party.characters[location.character];
        if (!CanUnequipSlotItem(data, character, static_cast<W8EquipSlot>(location.slot)))
            return std::nullopt;
        if (incoming->iItemNo != -1 &&
            (!CanCharacterUseItem(data, character, incoming->iItemNo) ||
             !CanEquipItemInSlot(data, character, incoming->iItemNo, location.slot, false)))
            return std::nullopt;
    }
    party.exchange(first_location, second_location, static_cast<int>(actor));
    return InventoryEdit{{first_location, second_location}};
}

/* An occupied target goes back into the hand and the partner into the backpack or the pool;
   an empty target puts the partner itself into the hand. */
std::optional<StowedPartner>
EquipHeldItemAndStowPartner(const RuleContext& rules, unsigned actor, W8EquipSlot slot,
                            std::function_ref<bool(const W8ItemInstance&)> npc_keeps)
{
    auto& data = rules.data;
    auto& party = rules.party;
    if (actor >= W8_PARTY_SLOT_COUNT) return std::nullopt;
    auto& character = party.characters[actor];
    const auto partner_slot = GetPairedEquipSlot(slot);
    if (partner_slot == W8_EQUIP_SLOT_NONE || party.held.item.iItemNo == -1 ||
        !CanUnequipSlotItem(data, &character, slot) ||
        !CanUnequipSlotItem(data, &character, partner_slot))
        return std::nullopt;
    auto proposed = character;
    proposed.EquippedItem[partner_slot].iItemNo = -1;
    if (!CanCharacterUseItem(data, &character, party.held.item.iItemNo) ||
        !CanEquipItemInSlot(data, &proposed, party.held.item.iItemNo, slot, false))
        return std::nullopt;
    auto& destination = character.EquippedItem[slot];
    auto& partner = character.EquippedItem[partner_slot];
    const bool replace = destination.iItemNo != -1;
    W8ItemLocation storage{};
    if (replace) {
        for (unsigned index = 0; index < 8; ++index) {
            if (character.backpack[index].iItemNo == -1) {
                storage = {W8ItemStorage::Backpack, actor, index};
                break;
            }
        }
        if (storage.storage == W8ItemStorage::None) {
            if (npc_keeps(partner) || party.item_count >= std::size(party.item_pool))
                return std::nullopt;
            storage = {W8ItemStorage::Pool, 0, 0};
        }
    }
    // Every destination and gameplay restriction is known before the first write.
    auto displaced = partner;
    displaced.bound = true;
    displaced.bind_announced = false;
    const auto held_origin = replace ? slot : partner_slot;
    const auto new_held = replace ? destination : displaced;
    destination = party.held.item;
    destination.bind_announced = false;
    partner = {};
    party.held = {new_held,
                  {W8ItemStorage::Equipment, actor, static_cast<unsigned>(held_origin)},
                  static_cast<int>(actor)};
    party.held.item.bind_announced = false;
    if (storage.storage == W8ItemStorage::Pool)
        party.insertIntoPool(0, displaced);
    else if (storage.storage == W8ItemStorage::Backpack)
        *party.itemAt(storage) = displaced;

    StowedPartner result{
        InventoryEdit{{{W8ItemStorage::Hand},
                       {W8ItemStorage::Equipment, actor, static_cast<unsigned>(slot)},
                       {W8ItemStorage::Equipment, actor, static_cast<unsigned>(partner_slot)},
                       storage}},
        std::nullopt};
    if (storage.storage != W8ItemStorage::None)
        result.stored = ItemPlacement{storage, party.itemAt(storage)};
    return result;
}

std::optional<StackMerge> MergeInventoryStacks(const RuleContext& rules,
                                               W8ItemLocation destination_location,
                                               W8ItemLocation source_location)
{
    auto& data = rules.data;
    auto& party = rules.party;
    if (source_location.storage == W8ItemStorage::Pool && source_location.slot >= party.item_count)
        return std::nullopt;
    if (destination_location.storage == W8ItemStorage::Pool &&
        destination_location.slot >= party.item_count)
        return std::nullopt;
    auto* destination = party.itemAt(destination_location);
    auto* source = party.itemAt(source_location);
    if (!destination || !source || destination == source || destination->iItemNo == -1 ||
        source->iItemNo != destination->iItemNo || source->identified != destination->identified ||
        data.items[source->iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK)
        return std::nullopt;
    const unsigned maximum = data.items[source->iItemNo].maximum_quantity;
    if (destination->stack_count >= maximum) return std::nullopt;
    const unsigned amount =
        std::min<unsigned>(source->stack_count, maximum - destination->stack_count);
    if (!amount) return std::nullopt;
    destination->stack_count += static_cast<unsigned char>(amount);
    source->stack_count -= static_cast<unsigned char>(amount);
    const bool complete = source->stack_count == 0;
    if (complete) party.take(source_location);
    return StackMerge{complete, InventoryEdit{{complete ? source_location : W8ItemLocation{}}}};
}

std::optional<InventoryEdit> InsertHeldItemInPool(const RuleContext& rules, unsigned index)
{
    auto& party = rules.party;
    if (party.held.item.iItemNo == -1 || index > party.item_count ||
        party.item_count >= std::size(party.item_pool))
        return std::nullopt;
    party.insertIntoPool(index, party.take({W8ItemStorage::Hand}));
    return InventoryEdit{{{W8ItemStorage::Hand}, {W8ItemStorage::Pool, 0, index}}};
}

/* Two identified items that some database entry names as its components (in either order)
   are consumed and that entry is made in their place, provided the character has the skill
   it asks for; stacks give up as many units as the smaller one holds. Failing that, two
   items of one kind are stacked together. */
std::expected<std::optional<ItemCombination>, InventoryRefusal>
MergeItems(const RuleContext& rules, unsigned actor, W8ItemLocation target)
{
    auto& data = rules.data;
    auto& party = rules.party;
    auto* destination = party.itemAt(target);
    auto& held = party.held.item;
    if (actor >= W8_PARTY_SLOT_COUNT || !destination || held.iItemNo == -1 ||
        destination->iItemNo == -1)
        return std::nullopt;
    const auto& character = party.characters[actor];
    if (!held.identified || !destination->identified)
        return std::unexpected(InventoryRefusal{data.strings[0x163]});
    for (unsigned id = 0; id < data.items.size(); ++id) {
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
        const unsigned quantity =
            held_stack && target_stack ? std::min(held.stack_count, destination->stack_count) : 1;
        auto remaining_held = held;
        auto remaining_target = *destination;
        auto consume = [quantity](W8ItemInstance& item, bool stack) {
            if (stack && item.stack_count > quantity)
                item.stack_count -= static_cast<unsigned char>(quantity);
            else
                item = W8ItemInstance{};
        };
        consume(remaining_held, held_stack);
        consume(remaining_target, target_stack);
        auto product = MakeItem(data, rules.rng, static_cast<int>(id), false, false, false);
        if (recipe.quantity_kind == W8_ITEM_QUANTITY_STACK)
            product.stack_count = static_cast<unsigned char>(quantity);
        if (remaining_target.iItemNo == -1)
            remaining_target = product;
        else
            remaining_held = product;
        held = remaining_held;
        *destination = remaining_target;
        if (held.iItemNo == -1) party.held = {};
        ItemCombination made{.merged = true,
                             .edit = InventoryEdit{{{W8ItemStorage::Hand}, target}}};
        if (recipe.merge_skill != -1) {
            made.practised_skill = static_cast<W8Skill>(recipe.merge_skill);
            made.practice = recipe.merge_skill_level / 10;
        }
        return made;
    }
    if (held.iItemNo == destination->iItemNo) {
        bool partial = false;
        const bool merged = MergeStack(data, party, *destination,
                                       PartyItem(party, {W8ItemStorage::Hand}), &partial);
        return ItemCombination{.merged = merged || partial,
                               .edit = InventoryEdit{{merged ? W8ItemLocation{W8ItemStorage::Hand}
                                                             : W8ItemLocation{}}}};
    }
    return std::unexpected(InventoryRefusal{data.strings[0x161]});
}

/* Pour one item's uses into another and take that many off the source, one at a time, which
   is what makes the source disappear when it is emptied. Each side counts its quantity the
   way its own record says to. Returns whether the source was used up. */
static bool MergeUses(const GameData& data, W8Party& party, W8ItemInstance& into, ItemSource from)
{
    const unsigned char available =
        data.items[from.item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
            ? from.item.stack_count
            : from.item.uses_or_charges;
    const unsigned char held = data.items[into.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
                                   ? into.stack_count
                                   : into.uses_or_charges;
    unsigned int moved = MaximumQuantity(data, into.iItemNo) - held;
    if (available <= moved) moved = available;
    into.uses_or_charges += static_cast<signed char>(moved);
    bool emptied = false;
    for (; moved != 0 && !emptied; --moved) emptied = ConsumeUse(data, party, from, false);
    return emptied;
}

std::optional<InventoryEdit> MergeItemUses(const RuleContext& rules, unsigned actor,
                                           W8ItemLocation destination, W8ItemLocation source)
{
    auto& party = rules.party;
    auto* into = party.itemAt(destination);
    auto* from = party.itemAt(source);
    if (actor >= W8_PARTY_SLOT_COUNT || !into || !from || into == from || into->iItemNo == -1 ||
        into->iItemNo != from->iItemNo)
        return std::nullopt;
    return InventoryEdit{{MergeUses(rules.data, party, *into, PartyItem(party, source))
                              ? source
                              : W8ItemLocation{}}};
}

void AddItemUses(const GameData& data, W8ItemInstance& item, signed char uses)
{
    const unsigned char total = item.uses_or_charges + uses;
    const int maximum = MaximumQuantity(data, item.iItemNo);
    item.uses_or_charges = static_cast<int>(static_cast<unsigned int>(total)) < maximum
                               ? total
                               : static_cast<unsigned char>(maximum);
}

/* Both entries hold something. Every comparison is the reverse of the usual sense, so the
   pool ends up in descending order. */
bool ItemPrecedesInPool(const GameData& data, const W8ItemInstance& a, const W8ItemInstance& b)
{
    if (a.iItemNo == -1 || b.iItemNo == -1)
        throw std::logic_error("ItemPrecedesInPool: an empty pool entry");
    const auto& ra = data.items[a.iItemNo];
    const auto& rb = data.items[b.iItemNo];
    if (rb.equip_class < ra.equip_class) return false;
    if (ra.equip_class < rb.equip_class) return true;
    if (rb.unidentified_name_index < ra.unidentified_name_index) return false;
    if (ra.unidentified_name_index < rb.unidentified_name_index) return true;
    if (!a.identified) return false;
    if (!b.identified) return true;
    if (ra.value < rb.value) return false;
    if (rb.value < ra.value) return true;
    if (a.stack_count < b.stack_count) return false;
    if (b.stack_count < a.stack_count) return true;
    if (a.uses_or_charges < b.uses_or_charges) return false;
    return b.uses_or_charges < a.uses_or_charges;
}

// A pool of one is left alone.
void SortPartyItemPool(const RuleContext& rules)
{
    if (!rules.game_started) throw std::logic_error("SortPartyItemPool: before play");
    auto& party = rules.party;
    if (party.item_count > 1)
        std::sort(party.item_pool, party.item_pool + party.item_count,
                  [&](const W8ItemInstance& a, const W8ItemInstance& b) {
                      return ItemPrecedesInPool(rules.data, a, b);
                  });
}

// ---- Weapon sets and ammunition

void BindEquippedItem(const RuleContext& rules, W8Character& character, W8EquipSlot equip_slot)
{
    auto& item = character.EquippedItem[equip_slot];
    if (item.iItemNo == -1) return;
    if (rules.data.items[item.iItemNo].binds_on_equip == 0 || item.bind_announced) {
        if (HasPaperDollPosition(equip_slot) && !item.bound) item.bound = true;
        return;
    }
    if (HasPaperDollPosition(equip_slot) && !item.bound) {
        item.bound = true;
        rules.events.push(TextNotice{W8_FONT_PALETTE_WHITE,
                                     fmt::sprintf(rules.data.strings[0x1ea],
                                                  FormatItemDisplayName(rules.data, &item, true))});
    }
}

std::optional<W8WeaponSwap> PlanWeaponSetSwap(const GameData& data, const W8Character& character)
{
    const auto& equipped = character.EquippedItem;
    if (!CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_SECONDARY_WEAPON))
        return std::nullopt;
    if (std::ranges::all_of(std::array{W8_EQUIP_SLOT_PRIMARY_WEAPON, W8_EQUIP_SLOT_SECONDARY_WEAPON,
                                       W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON,
                                       W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON},
                            [&](int slot) { return equipped[slot].iItemNo == -1; }))
        return std::nullopt;
    const int primary_right = equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    const int primary_left = equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
    const int alternate_right = equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo;
    const int alternate_left = equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    bool blocked_primary = false;
    bool blocked_alternate = false;
    if (alternate_right == -1 && alternate_left != -1)
        blocked_primary = CanHoldItemsTogether(data, primary_right, alternate_left);
    if (alternate_left == -1 && alternate_right != -1)
        blocked_alternate = CanHoldItemsTogether(data, primary_left, alternate_right);
    return W8WeaponSwap{(primary_right != -1 || alternate_right != -1) && !blocked_primary,
                        (primary_left != -1 || alternate_left != -1) && !blocked_alternate};
}

/* Bind the hands' items, then swap each chosen hand with its alternate. */
InventoryEdit CommitWeaponSetSwap(const RuleContext& rules, unsigned int party_slot,
                                  W8WeaponSwap choice)
{
    auto& character = rules.party.characters[party_slot];
    BindEquippedItem(rules, character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(rules, character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
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
        return swapped ? W8ItemLocation{W8ItemStorage::Equipment, party_slot,
                                        static_cast<unsigned int>(equip_slot)}
                       : W8ItemLocation{};
    };
    return InventoryEdit{{hand(choice.primary, W8_EQUIP_SLOT_PRIMARY_WEAPON),
                          hand(choice.primary, W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON),
                          hand(choice.secondary, W8_EQUIP_SLOT_SECONDARY_WEAPON),
                          hand(choice.secondary, W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON)}};
}

/* A member holding nothing at all has nothing to swap and is told so, and a weapon bound to
   its wearer blocks the whole swap until the binding has been announced. */
std::optional<W8WeaponSwap> PrepareWeaponSetSwap(const RuleContext& rules, unsigned int party_slot,
                                                 bool announce, short notice_context)
{
    auto& data = rules.data;
    auto& character = rules.party.characters[party_slot];
    const auto& equipped = character.EquippedItem;
    if (equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == -1 &&
        equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == -1) {
        if (announce)
            rules.events.push(CharacterNotice{party_slot, data.strings[0x1eb], notice_context});
        return std::nullopt;
    }

    BindEquippedItem(rules, character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(rules, character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (!CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        if (announce)
            rules.events.push(CharacterNotice{party_slot, data.strings[0x1ee], notice_context});
        return std::nullopt;
    }

    return PlanWeaponSetSwap(data, character);
}

/* Fill one equipment slot with the item that pairs with the given one. Only the five generic
   name kinds the merge path singles out pair at all. The alternate left hand is the pair to
   take when it holds something compatible; otherwise the caller may name an item that has
   to be found on the member. Neither being available makes the member say so instead.
   Returns where the partner came from, or None when nothing moved. */
static W8ItemLocation EquipPartner(const RuleContext& rules, unsigned int party_slot,
                                   const W8ItemInstance& item, int item_id, W8EquipSlot equip_slot)
{
    auto& data = rules.data;
    auto& party = rules.party;
    auto& character = party.characters[party_slot];
    if (!ItemHasSingledOutGenericName(data, item.iItemNo)) return {};
    if (item.iItemNo == -1) throw std::logic_error("EquipPartner: no item to pair");

    W8ItemLocation pair{};
    const int alternate_left =
        character.EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    if (alternate_left != -1 && CompatiblePartnerItems(data, item.iItemNo, alternate_left))
        pair = {W8ItemStorage::Equipment, party_slot, W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON};
    else if (item_id != -1)
        pair = FindMemberItem(party, party_slot, ItemWithId(item_id), W8ItemReach::Carried);
    if (pair.storage == W8ItemStorage::None) {
        rules.events.push(CharacterSpeech{party_slot, W8_SPEECH_NO_AMMUNITION});
        return {};
    }

    party.exchange(
        pair, {W8ItemStorage::Equipment, party_slot, static_cast<unsigned int>(equip_slot)}, -1);
    rules.events.push(CharacterNotice{party_slot, data.strings[0x1d4]});
    return pair;
}

/* The same pairing for items that carry a number of uses rather than being worn: the
   compatible-partner table names the kind to look for, and the first one found anywhere on
   the member gives its uses. With no partner the member only voices the attempt. Returns
   the partner's location when its uses ran out. */
static W8ItemLocation MergePartnerUses(const RuleContext& rules, unsigned int party_slot,
                                       W8ItemInstance& item)
{
    auto& data = rules.data;
    auto& party = rules.party;
    const int item_id = item.iItemNo;
    if (item_id == -1 || !ItemUsesShots(data, item_id)) return {};
    short related_kind;
    if (!GetItemMergeKind(data, item_id, &related_kind)) return {};

    const auto partner =
        FindMemberItem(party, party_slot, ItemOfKind(data, related_kind), W8ItemReach::Carried);
    if (partner.storage != W8ItemStorage::None) {
        const bool emptied = MergeUses(data, party, item, PartyItem(party, partner));
        rules.events.push(CharacterNotice{party_slot, data.strings[0x1d4]});
        return emptied ? partner : W8ItemLocation{};
    }
    rules.events.push(CharacterSpeech{party_slot, W8_SPEECH_NO_AMMUNITION});
    return {};
}

/* Split a stack of thrown weapons across the two hands. The source is the hand opposite the
   requested one and the destination is the requested hand, so the primary hand takes the
   larger half and the off hand the smaller; a lone item moves across as itself, but only
   when the primary hand is the destination. */
static void SplitThrowableStack(const RuleContext& rules, unsigned int party_slot,
                                W8EquipSlot equip_slot)
{
    auto& data = rules.data;
    auto& party = rules.party;
    auto& character = party.characters[party_slot];
    if (equip_slot != W8_EQUIP_SLOT_PRIMARY_WEAPON && equip_slot != W8_EQUIP_SLOT_SECONDARY_WEAPON)
        return;
    const auto source_slot = GetPairedEquipSlot(equip_slot);
    auto& source = character.EquippedItem[source_slot];
    const int item_id = source.iItemNo;
    // Thrown weapons are the only kind that stacks across the two hands.
    if (item_id == -1 || data.items[item_id].equip_class != W8_ITEM_EQUIP_CLASS_THROWN_WEAPON)
        return;

    const W8ItemLocation destination{W8ItemStorage::Equipment, party_slot,
                                     static_cast<unsigned int>(equip_slot)};
    unsigned char moved = source.stack_count;
    if (equip_slot == W8_EQUIP_SLOT_PRIMARY_WEAPON) {
        if (moved == 1) {
            party.exchange(
                destination,
                {W8ItemStorage::Equipment, party_slot, static_cast<unsigned int>(source_slot)}, -1);
            return;
        }
        moved -= (moved >> 1);
    } else {
        if (moved == 1) return;
        moved >>= 1;
    }

    auto created = MakeItem(data, rules.rng, item_id, false, source.identified, source.bound);
    source.stack_count -= moved;
    created.stack_count = moved;
    MoveInto(party, character.EquippedItem[equip_slot], destination, OwnedItem(created));
}

/* Thrown weapons come off their stack and the other hand's stack is shared out, ammunition is
   taken from the paired hand and its partner equipped when it runs out, and an emptied shot
   weapon draws uses from its partner. */
AttackItemsSpent ConsumeAttackItems(const RuleContext& rules, unsigned actor, unsigned hand,
                                    W8EquipSlot used, W8EquipSlot paired)
{
    auto& data = rules.data;
    auto& character = rules.party.characters[actor];
    const W8ItemLocation used_location{W8ItemStorage::Equipment, actor,
                                       static_cast<unsigned>(used)};
    if (paired != W8_EQUIP_SLOT_NONE) {
        const W8ItemLocation paired_location{W8ItemStorage::Equipment, actor,
                                             static_cast<unsigned>(paired)};
        const int id = character.EquippedItem[paired].iItemNo;
        const bool emptied = ConsumeItemUse(rules, paired_location, true);
        W8ItemLocation partner{};
        if (character.EquippedItem[paired].stack_count == 0)
            partner = EquipPartner(rules, actor, character.EquippedItem[used], id, paired);
        return {false, InventoryEdit{{emptied ? paired_location : W8ItemLocation{},
                                      partner.storage != W8ItemStorage::None ? paired_location
                                                                             : W8ItemLocation{},
                                      partner}}};
    }
    auto& item = character.EquippedItem[used];
    const AttackItemsSpent nothing{false, InventoryEdit{{}, false}};
    if (item.iItemNo == -1 || character.Hand[hand].combat_skill != W8_SKILL_RANGED_COMBAT ||
        data.items[item.iItemNo].spell_id != W8_SPELL_NONE)
        return nothing;
    const auto& record = data.items[item.iItemNo];
    if (record.equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON) {
        if (record.flags & W8_ITEM_FLAG_NEVER_DEPLETES) return nothing;
        const bool emptied = ConsumeItemUse(rules, used_location, true);
        const bool split = emptied || record.quantity_kind != W8_ITEM_QUANTITY_STACK;
        if (split) SplitThrowableStack(rules, actor, used);
        const W8ItemLocation other_hand{W8ItemStorage::Equipment, actor,
                                        static_cast<unsigned>(GetPairedEquipSlot(used))};
        return {item.iItemNo == -1 || item.stack_count == 0,
                InventoryEdit{{split ? used_location : W8ItemLocation{},
                               split ? other_hand : W8ItemLocation{}}}};
    }
    ConsumeItemUse(rules, used_location, false);
    W8ItemLocation partner{};
    const bool exhausted = item.uses_or_charges == 0;
    if (exhausted) partner = MergePartnerUses(rules, actor, item);
    // A shot weapon that runs out counts as a change of the hand that holds it.
    return {false, InventoryEdit{{exhausted ? used_location : W8ItemLocation{}, partner}}};
}

/* Equip the off-hand partner the primary weapon needs and draw uses for an empty shot
   weapon. */
InventoryEdit ReadyPrimaryWeapon(const RuleContext& rules, unsigned party_slot)
{
    auto& character = rules.party.characters[party_slot];
    auto& weapon = character.EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON];
    if (weapon.iItemNo == -1) return InventoryEdit{{}, false};
    W8ItemLocation partner{};
    if (character.EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1)
        partner = EquipPartner(rules, party_slot, weapon, -1, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    W8ItemLocation spent{};
    if (weapon.uses_or_charges == 0) spent = MergePartnerUses(rules, party_slot, weapon);
    const W8ItemLocation off_hand{W8ItemStorage::Equipment, party_slot,
                                  W8_EQUIP_SLOT_SECONDARY_WEAPON};
    return InventoryEdit{
        {partner.storage != W8ItemStorage::None ? off_hand : W8ItemLocation{}, partner, spent}};
}
