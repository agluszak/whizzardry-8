#include "wiz8/rules/item_rules.h"

#include <array>
#include <stdexcept>

#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/skills.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/item_instance.h"

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
