#include "wiz8/rules/inventory.h"

#include <algorithm>
#include <bitset>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <fmt/printf.h>

#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/skills.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"

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

W8ItemLocation FindMemberItem(const W8Party& party, unsigned int slot, W8ItemMatch match,
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

bool EveryCharacterHasItem(const W8Party& party, int item_id, W8ItemReach reach)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.members[slot].has_value() &&
            !FindCharacterItem(party.characters[slot], ItemWithId(item_id), reach))
            return false;
    return true;
}

// ---- Quantities

unsigned int MergeStack(const GameData& data, W8ItemInstance& destination, W8ItemInstance& source)
{
    if (&destination == &source || destination.iItemNo == -1 ||
        destination.iItemNo != source.iItemNo || destination.identified != source.identified)
        return 0;
    const auto& record = data.items[destination.iItemNo];
    if (record.quantity_kind != W8_ITEM_QUANTITY_STACK) return 0;
    const unsigned int room = destination.stack_count < record.maximum_quantity
                                  ? record.maximum_quantity - destination.stack_count
                                  : 0;
    const unsigned int moved = std::min<unsigned int>(source.stack_count, room);
    destination.stack_count += static_cast<unsigned char>(moved);
    source.stack_count -= static_cast<unsigned char>(moved);
    return moved;
}

bool SpendItemUse(const GameData& data, W8ItemInstance& item, bool remove_exhausted)
{
    if (item.iItemNo == -1) return false;
    const auto& record = data.items[item.iItemNo];
    if (record.quantity_kind == W8_ITEM_QUANTITY_NONE &&
        record.equip_class != W8_ITEM_EQUIP_CLASS_DRINK)
        return false;
    if (record.quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (item.stack_count != 0) --item.stack_count;
        return item.stack_count == 0;
    }
    if (item.uses_or_charges == 0) return remove_exhausted;
    --item.uses_or_charges;
    return item.uses_or_charges == 0 && !ItemHasHiddenProperties(data, item.iItemNo);
}

void AddItemUses(const GameData& data, W8ItemInstance& item, signed char uses)
{
    const unsigned char total = item.uses_or_charges + uses;
    const int maximum = data.items[item.iItemNo].initial_quantity.Maximum();
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

// ---- Placing an item with a character

/* Pick the equipment slot a newly acquired item goes into. Before play has started the two
   hand pairs are special-cased: a dual-wield-capable item can displace the right hand's
   item into the left hand, a ranged weapon fills a free pair before anything else, and
   ammunition looks for a ranged weapon to go with and otherwise fails. */
static W8EquipSlot ChooseCharacterEquipSlot(const GameData& data, W8Character& character,
                                            int item_id, bool game_started)
{
    W8EquipSlot slot = GetItemDefaultEquipSlot(data, item_id, game_started);
    if (game_started ||
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

/* Merge the rest of an item into a slot and report whether it is all gone. */
static bool MergeAll(const GameData& data, W8ItemInstance& destination, W8ItemInstance& item)
{
    if (MergeStack(data, destination, item) == 0 || item.stack_count != 0) return false;
    item = {};
    return true;
}

std::optional<CharacterPlacement> PlaceWithCharacter(const GameData& data, W8Character& character,
                                                     W8ItemInstance& item, bool equip, bool stack,
                                                     bool game_started)
{
    if (item.iItemNo == -1) return std::nullopt;
    if (equip && CanCharacterUseItem(data, &character, item.iItemNo)) {
        const auto slot = ChooseCharacterEquipSlot(data, character, item.iItemNo, game_started);
        if (slot != W8_EQUIP_SLOT_NONE) {
            auto& destination = character.EquippedItem[slot];
            if (destination.iItemNo == -1) {
                destination = std::exchange(item, W8ItemInstance{});
                destination.bind_announced = false;
                return CharacterPlacement{W8ItemStorage::Equipment, static_cast<unsigned>(slot),
                                          true};
            }
            if (MergeAll(data, destination, item))
                return CharacterPlacement{W8ItemStorage::Equipment, static_cast<unsigned>(slot),
                                          true, true};
        }
    }

    if (stack && data.items[item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        for (unsigned int index = 0; index < W8_EQUIP_SLOT_COUNT; ++index)
            if (MergeAll(data, character.EquippedItem[index], item))
                return CharacterPlacement{W8ItemStorage::Equipment, index, false, true};
        for (unsigned int index = 0; index < std::size(character.backpack); ++index)
            if (MergeAll(data, character.backpack[index], item))
                return CharacterPlacement{W8ItemStorage::Backpack, index, false, true};
    }

    const auto free = std::ranges::find(character.backpack, -1, &W8ItemInstance::iItemNo);
    if (free == std::end(character.backpack)) return std::nullopt;
    *free = std::exchange(item, W8ItemInstance{});
    free->bind_announced = false;
    return CharacterPlacement{W8ItemStorage::Backpack,
                              static_cast<unsigned int>(free - std::begin(character.backpack))};
}

// ---- Equipment

std::optional<unsigned int> FindUnusableEquipment(const GameData& data, W8Character& character,
                                                  const W8GameplayModifierBlock* modifiers,
                                                  std::bitset<W8_EQUIP_SLOT_COUNT> kept)
{
    EvaluateCharacterInputs(data, character, modifiers);
    for (unsigned int slot = 0; slot < W8_EQUIP_SLOT_COUNT; ++slot) {
        const auto& item = character.EquippedItem[slot];
        if (!kept[slot] && item.iItemNo != -1 &&
            !CanCharacterUseItem(data, &character, item.iItemNo))
            return slot;
    }
    return std::nullopt;
}

std::string UnusableEquipmentNotice(const GameData& data, const W8Character& character,
                                    const W8ItemInstance& item)
{
    unsigned short message_id = 0;
    const auto& pronoun = data.strings[g_gender_name_message_rows[character.gender][2]];
    return FindUnmetItemRequirement(&character, &data.items[item.iItemNo], &message_id)
               ? fmt::sprintf(data.strings[0x1ef], pronoun,
                              FormatItemDisplayName(data, &item, true), pronoun,
                              data.strings[message_id])
               : fmt::sprintf(data.strings[0x1f0], pronoun,
                              FormatItemDisplayName(data, &item, true));
}

std::optional<std::string> BindEquippedItem(const GameData& data, W8Character& character,
                                            W8EquipSlot equip_slot)
{
    auto& item = character.EquippedItem[equip_slot];
    if (item.iItemNo == -1 || !HasPaperDollPosition(equip_slot) || item.bound) return std::nullopt;
    item.bound = true;
    if (data.items[item.iItemNo].binds_on_equip == 0 || item.bind_announced) return std::nullopt;
    return fmt::sprintf(data.strings[0x1ea], FormatItemDisplayName(data, &item, true));
}

std::optional<W8WeaponSwap> PlanWeaponSetSwap(const GameData& data, const W8Character& character)
{
    const auto& equipped = character.EquippedItem;
    if (!CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(data, &character, W8_EQUIP_SLOT_SECONDARY_WEAPON))
        return std::nullopt;
    const int primary_right = equipped[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    const int primary_left = equipped[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
    const int alternate_right = equipped[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo;
    const int alternate_left = equipped[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    if (primary_right == -1 && primary_left == -1 && alternate_right == -1 && alternate_left == -1)
        return std::nullopt;
    bool blocked_primary = false;
    bool blocked_alternate = false;
    if (alternate_right == -1 && alternate_left != -1)
        blocked_primary = CanHoldItemsTogether(data, primary_right, alternate_left);
    if (alternate_left == -1 && alternate_right != -1)
        blocked_alternate = CanHoldItemsTogether(data, primary_left, alternate_right);
    return W8WeaponSwap{(primary_right != -1 || alternate_right != -1) && !blocked_primary,
                        (primary_left != -1 || alternate_left != -1) && !blocked_alternate};
}
