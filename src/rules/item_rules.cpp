#include "wiz8/rules/item_rules.h"

#include <array>
#include <stdexcept>
#include <string>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_payloads.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/random.h"

static const short g_compatible_partner_pairs[6][2] = {
    {114, 113}, {110, 132}, {104, 132}, {144, 143}, {145, 146}, {0, 0},
};

/* What a whole stack weighs. An empty slot weighs nothing, and a slot holding
   an item that does not stack still weighs one of it - the count is zero for
   every quantity kind but the stacking one. */
// FUNCTION: WIZ8 0x0051bfd0
unsigned int GetItemStackWeight(const GameData& data, const W8ItemInstance* item)
{
    unsigned int weight;

    weight = 0;
    if (item->iItemNo != -1) {
        weight = data.items[item->iItemNo].weight;
        weight = (item->stack_count > 0 ? item->stack_count : 1) * weight;
    }
    return weight;
}

int WeaponHandKind(const GameData& data, const W8ItemInstance& primary,
                   const W8ItemInstance& secondary, unsigned int hand)
{
    int item_id = (hand == 0 ? primary : secondary).iItemNo;
    int wield_kind;
    if (item_id == -1) {
        wield_kind = 0;
    } else {
        switch (data.items[item_id].equip_class) {
        case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
        case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
        case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
            wield_kind = 1;
            break;
        case W8_ITEM_EQUIP_CLASS_THROWN_WEAPON:
            wield_kind = 1;
            if (hand != 1 || primary.iItemNo == -1 ||
                data.items[primary.iItemNo].unidentified_name_index != 0x83) {
                break;
            }
            [[fallthrough]];
        case W8_ITEM_EQUIP_CLASS_AMMUNITION:
            wield_kind = 3;
            break;
        case W8_ITEM_EQUIP_CLASS_SHIELD:
            wield_kind = 2;
            break;
        default:
            throw std::logic_error("WeaponHandKind: the hand holds an item that is not wielded");
        }
    }
    return wield_kind;
}

std::array<W8HandAttack, 2> EvaluateWeaponHands(const GameData& data, const W8ItemInstance& primary,
                                                const W8ItemInstance& secondary)
{
    std::array<W8HandAttack, 2> hands{};
    W8HandAttack* attacks[2];
    const W8ItemDatabaseRecord* records[2];
    const W8ItemInstance* equipment[2];
    unsigned int hand;
    for (hand = 0; hand < 2; ++hand) {
        hands[hand].uiHolds = WeaponHandKind(data, primary, secondary, hand);
        attacks[hand] = &hands[hand];
        equipment[hand] = (hand == 0 ? &primary : &secondary);
        if (equipment[hand]->iItemNo == -1) {
            records[hand] = 0;
            attacks[hand]->weapon_skill = W8_SKILL_MARTIAL_ARTS;
        } else {
            records[hand] = &data.items[equipment[hand]->iItemNo];
            attacks[hand]->weapon_skill = static_cast<W8Skill>(records[hand]->weapon_skill);
        }
    }

    for (hand = 0; hand < 2; ++hand) {
        W8HandAttack* attack = attacks[hand];
        attack->in_play = true;
        switch (attack->weapon_skill) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 14:
            attack->combat_skill = W8_SKILL_CLOSE_COMBAT;
            break;
        case 7:
        case 8:
        case 9:
            attack->combat_skill = W8_SKILL_RANGED_COMBAT;
            if (ItemHasSingledOutGenericName(data, equipment[hand]->iItemNo) &&
                (equipment[hand == 0]->iItemNo == -1 ||
                 !CompatiblePartnerItems(data, equipment[hand]->iItemNo,
                                         equipment[hand == 0]->iItemNo))) {
                attack->in_play = false;
            }
            if (ItemUsesShots(data, equipment[hand]->iItemNo) &&
                equipment[hand]->uses_or_charges == 0) {
                attack->in_play = false;
            }
            break;
        default:
            attack->combat_skill = W8_SKILL_NONE;
            attack->in_play = false;
        }

        if (hand == 1) {
            if (attacks[0]->combat_skill != attacks[1]->combat_skill) {
                attacks[1]->in_play = false;
            }
            if (attacks[1]->uiHolds == 2 || attacks[1]->uiHolds == 3) {
                attacks[1]->in_play = false;
            }
            if (records[0] != 0 && records[0]->unidentified_name_index == 0x83) {
                attacks[1]->in_play = false;
            }
            if (attacks[1]->uiHolds == HOLDS_NOTHING && attacks[0]->uiHolds != HOLDS_NOTHING) {
                attacks[1]->in_play = false;
            }
        }
    }

    return hands;
}

bool CompatiblePartnerItems(const GameData& data, int ranged_item_id, int other_item_id)
{
    if (static_cast<unsigned int>(ranged_item_id) >= data.items.size()) {
        throw std::logic_error("CompatiblePartnerItems: invalid ranged item");
    }
    if (static_cast<unsigned int>(other_item_id) >= data.items.size()) {
        throw std::logic_error("CompatiblePartnerItems: invalid partner item");
    }

    short ranged_name = static_cast<short>(data.items[ranged_item_id].unidentified_name_index);
    short other_name = static_cast<short>(data.items[other_item_id].unidentified_name_index);

    switch (ranged_name) {
    case 0xb:
    case 0x25:
        if (other_name == 0x16) {
            return true;
        }
        break;
    case 0xc:
        if (other_name == 0x53) {
            return true;
        }
        break;
    case 0xd:
        if (other_name == 0x17) {
            return true;
        }
        break;
    case 0x68:
    case 0x6e:
    case 0x72:
        if (data.items[other_item_id].equip_class > W8_ITEM_EQUIP_CLASS_RANGED_WEAPON ||
            data.items[ranged_item_id].range_category == data.items[other_item_id].range_category) {
            if (ranged_item_id != -1 && g_compatible_partner_pairs[0][0] != 0) {
                int index = 0;
                short name = g_compatible_partner_pairs[0][0];

                while (name != ranged_name) {
                    ++index;
                    name = g_compatible_partner_pairs[index][0];
                    if (name == 0) {
                        return true;
                    }
                }
                if (other_name != g_compatible_partner_pairs[index][1]) {
                    return false;
                }
            }
            return true;
        }
        break;
    case 0x83:
        switch (ranged_item_id) {
        case 599:
            if (other_name == 0x17 || other_name == 0x71) {
                return true;
            }
            break;
        case 600:
        case 0x259:
            if (other_name == 0x17 || other_name == 0x71 || other_name == 0x70) {
                return true;
            }
            break;
        case 0x25a:
        case 0x25b:
        case 0x25c:
        case 0x25d:
            if (other_name == 0x17 || other_name == 0x71 || other_name == 0x70 ||
                other_name == 0x15 || other_name == 0x7e ||
                (data.items[other_item_id].equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON &&
                 other_name == 0)) {
                return true;
            }
            break;
        case 0x25e:
        case 0x25f:
        case 0x260:
        case 0x261:
        case 0x262:
            if (other_name == 0x17 || other_name == 0x71 || other_name == 0x70 ||
                other_name == 0x15 || other_name == 0x7e ||
                (data.items[other_item_id].equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON &&
                 other_name == 0) ||
                other_name == 0x16 || other_name == 0x53) {
                return true;
            }
            return false;
        }
        break;
    }
    return false;
}

bool ItemHasSingledOutGenericName(const GameData& data, int item_id)
{
    if (item_id != -1) {
        switch (data.items[item_id].unidentified_name_index) {
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x25:
        case 0x83:
            return true;
        }
    }
    return false;
}

bool ItemUsesShots(const GameData& data, int item_id)
{
    if (item_id == -1) {
        return false;
    }
    return data.items[item_id].quantity_kind == W8_ITEM_QUANTITY_SHOTS;
}

bool GetItemMergeKind(const GameData& data, int item_id, short* related_kind)
{
    if (related_kind && item_id != -1) {
        for (int index = 0; g_compatible_partner_pairs[index][0] != 0; ++index) {
            if (g_compatible_partner_pairs[index][0] ==
                data.items[item_id].unidentified_name_index) {
                *related_kind = g_compatible_partner_pairs[index][1];
                return true;
            }
        }
    }
    return false;
}

// ---- Making and naming items

W8ItemInstance MakeItem(const GameData& data, Rng& rng, int item_id, bool maximum_quantity,
                        bool force_identified, bool mark_special)
{
    if (item_id < 0 || static_cast<unsigned int>(item_id) >= data.items.size())
        throw std::invalid_argument(fmt::format("MakeItem: invalid item {}", item_id));

    W8ItemInstance item;
    item.iItemNo = item_id;
    const auto& record = data.items[item_id];
    if (record.quantity_kind != W8_ITEM_QUANTITY_NONE) {
        const int maximum = record.initial_quantity.Maximum();
        if (maximum >= 256) throw std::logic_error("MakeItem: the quantity dice exceed a byte");
        const auto quantity =
            maximum_quantity ? static_cast<unsigned char>(maximum)
                             : static_cast<unsigned char>(RollDice(rng, record.initial_quantity));
        if (record.quantity_kind == W8_ITEM_QUANTITY_STACK)
            item.stack_count = quantity;
        else if (record.quantity_kind >= W8_ITEM_QUANTITY_CHARGES &&
                 record.quantity_kind <= W8_ITEM_QUANTITY_SHOTS)
            item.uses_or_charges = quantity;
        else
            throw std::logic_error("MakeItem: invalid quantity kind");
    }
    if (force_identified || (record.flags & W8_ITEM_FLAG_AUTO_IDENTIFY) != 0)
        item.identified = true;
    if (mark_special) item.bound = true;
    return item;
}

const unsigned short g_generic_item_name_notice[W8_GENERIC_ITEM_NAME_COUNT] = {
    0x45a, 0x45b, 0x45c, 0x45d, 0x45e, 0x45f, 0x460, 0x461, 0x462, 0x463, 0x464, 0x465, 0x466,
    0x467, 0x468, 0x469, 0x46a, 0x46b, 0x46c, 0x46d, 0x46e, 0x46f, 0x470, 0x471, 0x472, 0x473,
    0x474, 0x475, 0x476, 0x477, 0x478, 0x479, 0x47a, 0x47b, 0x47c, 0x47d, 0x47e, 0x47f, 0x480,
    0x481, 0x482, 0x483, 0x484, 0x485, 0x486, 0x487, 0x488, 0x489, 0x48a, 0x48b, 0x48c, 0x48d,
    0x48e, 0x48f, 0x490, 0x491, 0x492, 0x493, 0x494, 0x495, 0x496, 0x497, 0x498, 0x499, 0x49a,
    0x49b, 0x49c, 0x49d, 0x49e, 0x49f, 0x4a0, 0x4a1, 0x4a2, 0x4a3, 0x4a4, 0x4a5, 0x4a6, 0x4a7,
    0x4a8, 0x4a9, 0x4aa, 0x4ab, 0x4ac, 0x4ad, 0x4ae, 0x4af, 0x4b0, 0x4b1, 0x4b2, 0x4b3, 0x4b4,
    0x4b5, 0x4b6, 0x4b7, 0x4b8, 0x4b9, 0x4ba, 0x4bb, 0x4bc, 0x4bd, 0x4be, 0x4bf, 0x4c0, 0x4c1,
    0x4c2, 0x4c3, 0x4c4, 0x4c5, 0x4c6, 0x4c7, 0x4c8, 0x4c9, 0x4ca, 0x4cb, 0x4cc, 0x4cd, 0x4ce,
    0x4cf, 0x4d0, 0x459, 0x4d1, 0x4d2, 0x4d3, 0x4d4, 0x4d5, 0x4ab, 0x46f, 0x459, 0x46c, 0x4c1,
    0x4d6, 0x4d7, 0x4d8, 0x4d9, 0x4da, 0x4db, 0x4dc, 0x4dd, 0x4de, 0x4df, 0x4e0, 0x4e1, 0x4e2,
    0x4e3, 0x4e4, 0x4e5, 0x4e6,
};

std::string GetItemDisplayName(const GameData& data, const W8ItemInstance* item)
{
    const auto& record = data.items[item->iItemNo];
    if (item->identified) return record.display_name;
    return fmt::sprintf(data.strings[0x1e7],
                        data.strings[g_generic_item_name_notice[record.unidentified_name_index]]);
}

std::string FormatItemDisplayName(const GameData& data, const W8ItemInstance* item,
                                  bool include_quantity)
{
    auto name = GetItemDisplayName(data, item);
    if (include_quantity && item->stack_count > 1)
        return fmt::format("{} ({:d})", name, static_cast<unsigned int>(item->stack_count));
    return name;
}

/* Any of the nine attack-flag bits, armor class, a resistance, a binding or a
   regeneration bonus. */
bool ItemHasHiddenProperties(const GameData& data, int item_id)
{
    const auto& record = data.items[item_id];
    for (unsigned int index = 0; index < 9; ++index)
        if ((record.attack_flags & (1 << index)) != 0) return true;
    if (record.armor_class_bonus != 0) return true;
    for (unsigned int index = 0; index < 6; ++index)
        if (record.resistance_bonus[index] != 0) return true;
    return record.binds_on_equip != 0 || record.health_regen_bonus != 0 ||
           record.stamina_regen_bonus != 0 || record.spell_regen_bonus != 0;
}

// ---- Equipment slots

W8EquipSlot GetPairedEquipSlot(W8EquipSlot equip_slot)
{
    switch (equip_slot) {
    case W8_EQUIP_SLOT_PRIMARY_WEAPON:
        return W8_EQUIP_SLOT_SECONDARY_WEAPON;
    case W8_EQUIP_SLOT_SECONDARY_WEAPON:
        return W8_EQUIP_SLOT_PRIMARY_WEAPON;
    case W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON:
        return W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
    case W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON:
        return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
    default:
        return W8_EQUIP_SLOT_NONE;
    }
}

bool HasPaperDollPosition(W8EquipSlot equip_slot)
{
    return equip_slot != W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON &&
           equip_slot != W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
}

W8EquipSlot GetItemDefaultEquipSlot(const GameData& data, int item_id, bool game_started)
{
    switch (data.items[item_id].equip_class) {
    case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
    case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
        return W8_EQUIP_SLOT_PRIMARY_WEAPON;
    case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
        return game_started ? W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON : W8_EQUIP_SLOT_PRIMARY_WEAPON;
    case W8_ITEM_EQUIP_CLASS_AMMUNITION:
        return game_started ? W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON
                            : W8_EQUIP_SLOT_SECONDARY_WEAPON;
    case W8_ITEM_EQUIP_CLASS_THROWN_WEAPON:
        return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
    case W8_ITEM_EQUIP_CLASS_MISC:
        return W8_EQUIP_SLOT_MISC_1;
    case W8_ITEM_EQUIP_CLASS_HEAD:
        return W8_EQUIP_SLOT_HEAD;
    case W8_ITEM_EQUIP_CLASS_TORSO:
        return W8_EQUIP_SLOT_TORSO;
    case W8_ITEM_EQUIP_CLASS_LEGS:
        return W8_EQUIP_SLOT_LEGS;
    case W8_ITEM_EQUIP_CLASS_HANDS:
        return W8_EQUIP_SLOT_HANDS;
    case W8_ITEM_EQUIP_CLASS_FEET:
        return W8_EQUIP_SLOT_FEET;
    case W8_ITEM_EQUIP_CLASS_CLOAK:
        return W8_EQUIP_SLOT_CLOAK;
    case W8_ITEM_EQUIP_CLASS_SHIELD:
        return W8_EQUIP_SLOT_SECONDARY_WEAPON;
    default:
        return W8_EQUIP_SLOT_NONE;
    }
}

static constexpr unsigned short SlotBit(int slot)
{
    return static_cast<unsigned short>(1 << slot);
}

/* A two-handed weapon takes a main hand only while that set's off hand is free. A
   one-handed weapon takes either main hand, and an off hand too when the record allows it
   and that set's main hand holds nothing two-handed. Everything else has one home. A
   weapon without a skill is a data error and fits nowhere. */
unsigned short GetItemEquipSlotMask(const GameData& data, int item_id, bool primary_off_hand_free,
                                    bool alternate_off_hand_free, bool primary_main_hand_free,
                                    bool alternate_main_hand_free)
{
    const auto& record = data.items[item_id];
    unsigned short slots = 0;
    switch (record.equip_class) {
    case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
    case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
    case W8_ITEM_EQUIP_CLASS_THROWN_WEAPON:
    case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
        if (record.weapon_skill == -1) return 0;
        if ((record.flags & W8_ITEM_FLAG_TWO_HANDED) != 0) {
            if (primary_off_hand_free) slots = SlotBit(W8_EQUIP_SLOT_PRIMARY_WEAPON);
            if (alternate_off_hand_free) slots |= SlotBit(W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON);
            return slots;
        }
        slots =
            SlotBit(W8_EQUIP_SLOT_PRIMARY_WEAPON) | SlotBit(W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON);
        if ((record.flags & W8_ITEM_FLAG_OFF_HAND_ALLOWED) == 0) return slots;
        if (primary_main_hand_free) slots |= SlotBit(W8_EQUIP_SLOT_SECONDARY_WEAPON);
        break;
    case W8_ITEM_EQUIP_CLASS_AMMUNITION:
    case W8_ITEM_EQUIP_CLASS_SHIELD:
        if (primary_main_hand_free) slots = SlotBit(W8_EQUIP_SLOT_SECONDARY_WEAPON);
        break;
    case W8_ITEM_EQUIP_CLASS_TORSO:
        return SlotBit(W8_EQUIP_SLOT_TORSO);
    case W8_ITEM_EQUIP_CLASS_LEGS:
        return SlotBit(W8_EQUIP_SLOT_LEGS);
    case W8_ITEM_EQUIP_CLASS_HEAD:
        return SlotBit(W8_EQUIP_SLOT_HEAD);
    case W8_ITEM_EQUIP_CLASS_HANDS:
        return SlotBit(W8_EQUIP_SLOT_HANDS);
    case W8_ITEM_EQUIP_CLASS_FEET:
        return SlotBit(W8_EQUIP_SLOT_FEET);
    case W8_ITEM_EQUIP_CLASS_MISC:
        return SlotBit(W8_EQUIP_SLOT_MISC_1) | SlotBit(W8_EQUIP_SLOT_MISC_2);
    case W8_ITEM_EQUIP_CLASS_CLOAK:
        return SlotBit(W8_EQUIP_SLOT_CLOAK);
    default:
        return 0;
    }
    if (alternate_main_hand_free) slots |= SlotBit(W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON);
    return slots;
}

/* A hand counts as free when it is empty, and a main hand also when what it holds is not
   two-handed. */
bool CanEquipItemInSlot(const GameData& data, const W8Character* character, int item_id,
                        unsigned char equip_slot, bool ignore_worn_items)
{
    const auto empty = [&](W8EquipSlot slot) {
        return character->EquippedItem[slot].iItemNo == -1 || ignore_worn_items;
    };
    const auto one_handed = [&](W8EquipSlot slot) {
        const int held = character->EquippedItem[slot].iItemNo;
        return held == -1 || (data.items[held].flags & W8_ITEM_FLAG_TWO_HANDED) == 0 ||
               ignore_worn_items;
    };
    const auto mask = GetItemEquipSlotMask(data, item_id, empty(W8_EQUIP_SLOT_SECONDARY_WEAPON),
                                           empty(W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON),
                                           one_handed(W8_EQUIP_SLOT_PRIMARY_WEAPON),
                                           one_handed(W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON));
    return (SlotBit(equip_slot) & mask) != 0;
}

/* Nothing pairs with a two-handed item. A weapon beside an off-hand item is decided by the
   partner rule, taken in weapon-first order; two things that are not both weapons agree,
   and two weapons have to share a range. */
bool CanHoldItemsTogether(const GameData& data, int first_item_id, int second_item_id)
{
    if (first_item_id == -1 || second_item_id == -1) return true;
    const auto& first = data.items[first_item_id];
    const auto& second = data.items[second_item_id];
    if ((first.flags & W8_ITEM_FLAG_TWO_HANDED) != 0 ||
        (second.flags & W8_ITEM_FLAG_TWO_HANDED) != 0)
        return false;
    if (first.equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON ||
        second.equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION)
        return CompatiblePartnerItems(data, first_item_id, second_item_id);
    if (second.equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON ||
        first.equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION)
        return CompatiblePartnerItems(data, second_item_id, first_item_id);
    if (first.equip_class >= W8_ITEM_EQUIP_CLASS_AMMUNITION) return true;
    if (second.equip_class < W8_ITEM_EQUIP_CLASS_AMMUNITION)
        return first.range_category == second.range_category;
    return true;
}

bool CanUnequipSlotItem(const GameData& data, const W8Character* character, W8EquipSlot equip_slot)
{
    const auto& item = character->EquippedItem[equip_slot];
    return item.iItemNo == -1 || data.items[item.iItemNo].binds_on_equip == 0 ||
           item.bind_announced || !HasPaperDollPosition(equip_slot) ||
           character->uiCondition[W8_CONDITION_DEAD] != 0;
}

// ---- Using items

// The sex mask that admits either sex, and an unused requirement.
enum { W8_ITEM_GENDER_MASK_ANY = 3, W8_ITEM_REQUIREMENT_NONE = 0xff };

bool FindUnmetItemRequirement(const W8Character* character, const W8ItemDatabaseRecord* record,
                              unsigned short* message_id)
{
    for (const auto& requirement : record->attribute_requirements) {
        const int stat = static_cast<signed char>(requirement.stat_id);
        if (requirement.stat_id != W8_ITEM_REQUIREMENT_NONE &&
            character->attributes[stat].effective < requirement.minimum) {
            if (message_id) *message_id = g_character_description_first_ids[stat];
            return true;
        }
    }
    for (const auto& requirement : record->skill_requirements) {
        const int stat = static_cast<signed char>(requirement.stat_id);
        if (requirement.stat_id != W8_ITEM_REQUIREMENT_NONE &&
            character->skills[stat].level < requirement.minimum) {
            if (message_id) *message_id = g_character_skill_name_ids[stat];
            return true;
        }
    }
    return false;
}

bool CanCharacterUseItem(const GameData& data, const W8Character* character, int item_id)
{
    const auto& record = data.items[item_id];
    if ((record.profession_mask & (W8_PROFESSION_LORD << character->iProfession)) == 0 ||
        (record.race_mask & (W8_RACE_ELF << character->iRace)) == 0)
        return false;
    if (record.gender_mask != W8_ITEM_GENDER_MASK_ANY &&
        (record.gender_mask & (W8_GENDER_FEMALE << character->gender)) == 0)
        return false;
    if (FindUnmetItemRequirement(character, &record, nullptr)) return false;

    if (record.category == W8_ITEM_CATEGORY_SPELL_SOURCE) {
        if (record.spell_id == W8_SPELL_NONE)
            throw std::logic_error("CanCharacterUseItem: a spell source without a spell");
        return character->spell_learned[record.spell_id] != 1;
    }
    if (record.category == W8_ITEM_CATEGORY_CASTER_ITEM_6 ||
        record.category == W8_ITEM_CATEGORY_CASTER_ITEM_8) {
        if (record.spell_id == W8_SPELL_NONE)
            throw std::logic_error("CanCharacterUseItem: a casting item without a spell");
        const int profession = record.category == W8_ITEM_CATEGORY_CASTER_ITEM_6
                                   ? W8_PROFESSION_BARD
                                   : W8_PROFESSION_GADGETEER;
        const auto minimum = static_cast<unsigned int>(
            MinimumCasterLevelForSpellLevel(data.spells[record.spell_id].spell_level));
        return static_cast<unsigned int>(character->profession_levels[profession]) >= minimum;
    }
    return true;
}

int MinimumCasterLevelForSpellLevel(int spell_level)
{
    switch (spell_level) {
    case 2:
        return 3;
    case 3:
        return 5;
    case 4:
        return 8;
    case 5:
        return 11;
    case 6:
        return 14;
    case 7:
        return 18;
    default:
        return 1;
    }
}
