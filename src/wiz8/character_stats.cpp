#include "wiz8/character_stats.h"

#include <cstring>

#include "wiz8/attack_modes.h"
#include "wiz8/character_skills.h"
#include "wiz8/conditions.h"
#include "wiz8/dice.h"
#include "wiz8/equipment_slots.h"
#include "wiz8/gameplay_modifiers.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/load_category.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/platform/assertion.h"

enum {
    W8_PHYS_COMBAT_WEIGHT_FIGHTER = 4,
    W8_PHYS_COMBAT_WEIGHT_MIXED = 3,
    W8_PHYS_COMBAT_WEIGHT_CASTER = 2
};

const unsigned char gubLocalACPercent[5] = {15, 40, 30, 10, 5};

static const W8Dice g_unarmed_damage_dice[12] = {{0, 1, 2}, {0, 1, 3}, {0, 2, 2}, {0, 2, 3},
                                                 {0, 2, 4}, {0, 3, 3}, {1, 3, 3}, {2, 3, 3},
                                                 {0, 3, 5}, {0, 4, 4}, {2, 4, 4}, {4, 4, 4}};

static int CalcPhysCombatExperience(W8Character* character)
{
    int total = 0;
    int weight = 0;
    unsigned int profession;
    int levels;

    if (character->iProfession == W8_PROFESSION_NONE) {
        return 0;
    }
    if (character == 0) {
        Fail("pPC != NULL");
    }
    if (character->iProfession < W8_PROFESSION_FIGHTER ||
        character->iProfession > W8_PROFESSION_MAGE) {
        Fail("(pPC->iProfession >= 0) && (pPC->iProfession < PROF_COUNT)");
    }

    for (profession = 0; profession < W8_PROFESSION_COUNT; ++profession) {
        levels = character->profession_levels[profession];
        if (levels != 0) {
            switch (profession) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
                weight = W8_PHYS_COMBAT_WEIGHT_FIGHTER;
                break;
            case 7:
            case 8:
            case 9:
                weight = W8_PHYS_COMBAT_WEIGHT_MIXED;
                break;
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
                weight = W8_PHYS_COMBAT_WEIGHT_CASTER;
                break;
            default:
                Fail("FALSE", "CalcPhysCombatExperience: ERROR - Invalid profession");
            }
            total += levels * weight;
        }
    }
    return total;
}

void CalcInitiative(W8Character* character)
{
    character->initiative = ((character->uiExpLevel + 1) >> 1) +
                            character->attributes[W8_ATTRIBUTE_SENSES].effective / 5 - 10 +
                            character->attributes[W8_ATTRIBUTE_SPEED].effective / 5;

    if (character->skills[W8_SKILL_SNAKESPEED].active) {
        character->initiative += character->skills[W8_SKILL_SNAKESPEED].level / 10 + 1;
    }
    character->initiative += character->bonus.damage_bonus;

    switch (character->load_category) {
    case W8_LOAD_NONE:
        break;
    case W8_LOAD_LIGHT:
        character->initiative -= 1;
        break;
    case W8_LOAD_MEDIUM:
        character->initiative -= 2;
        break;
    case W8_LOAD_HEAVY:
        character->initiative -= 4;
        break;
    case W8_LOAD_EXTREME:
        character->initiative -= 8;
        break;
    default:
        Fail("FALSE", "CalcInitiative: ERROR - Invalid load category");
        break;
    }
}

void CalcAttacks(W8Character* character, bool damaged_android)
{
    W8HandAttack* attacks[2];
    W8ItemDatabaseRecord* records[2];
    W8ItemInstance* equipment[2];
    unsigned int hand;
    int physical_experience;
    int load_penalty = 0;

    const auto hands = EvaluateWeaponHands(character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON],
                                           character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON]);
    for (hand = 0; hand < 2; ++hand) {
        attacks[hand] = &character->Hand[hand];
        equipment[hand] = &character->EquippedItem[hand + 6];
        records[hand] =
            equipment[hand]->iItemNo == -1 ? nullptr : &g_item_records[equipment[hand]->iItemNo];
        attacks[hand]->uiHolds = hands[hand].uiHolds;
        attacks[hand]->weapon_skill = hands[hand].weapon_skill;
        attacks[hand]->combat_skill = hands[hand].combat_skill;
        attacks[hand]->in_play = hands[hand].in_play;
    }

    character->dual_wielding = attacks[0]->uiHolds == 1 && attacks[0]->in_play &&
                               attacks[1]->uiHolds == 1 && attacks[1]->in_play;

    physical_experience = CalcPhysCombatExperience(character);
    for (hand = 0; hand < 2; ++hand) {
        W8HandAttack* attack = attacks[hand];
        W8HandAttack* other = attacks[hand == 0];
        int dual_penalty;
        int score;
        unsigned int divisor;

        if (!attack->in_play) {
            continue;
        }

        score = ((character->skills[attack->combat_skill].level +
                  character->skills[attack->weapon_skill].level * 2) *
                 2) /
                3;
        divisor = 20;
        if (other->uiHolds == 1 && other->in_play) {
            score += character->skills[other->weapon_skill].level >> 1;
            divisor = 25;
        }
        if (character->dual_wielding) {
            score += character->skills[W8_SKILL_DUAL_WEAPONS].level;
            divisor += 10;
        }
        attack->combined_skill = score * 10 / divisor;

        switch (character->load_category) {
        case W8_LOAD_NONE:
            load_penalty = 0;
            break;
        case W8_LOAD_LIGHT:
            load_penalty = -15;
            break;
        case W8_LOAD_MEDIUM:
            load_penalty = -30;
            break;
        case W8_LOAD_HEAVY:
            load_penalty = -60;
            break;
        case W8_LOAD_EXTREME:
            load_penalty = -120;
            break;
        default:
            Fail("FALSE", "CalcAttacks: ERROR - Invalid load category");
        }
        if (attack->weapon_skill == W8_SKILL_MODERN_WEAPONS) {
            load_penalty /= 2;
        }

        if (character->dual_wielding) {
            dual_penalty =
                -10 * (hand + 1) - (100 - character->skills[W8_SKILL_DUAL_WEAPONS].level) / 4;
        } else {
            dual_penalty = 0;
        }

        attack->attack_score =
            (dual_penalty + character->attributes[W8_ATTRIBUTE_DEXTERITY].effective / 2 +
             attack->combined_skill * 2 + physical_experience) /
                3 +
            60;
        if (damaged_android) attack->attack_score /= 2;

        score = (((character->attributes[W8_ATTRIBUTE_SPEED].effective +
                   character->attributes[W8_ATTRIBUTE_DEXTERITY].effective) >>
                  1) +
                 dual_penalty + physical_experience + load_penalty + attack->combined_skill) /
                3;
        attack->attacks = 1;
        if (hand == 0) {
            if (score > 49) {
                attack->attacks = 2;
                if (score > 99) {
                    attack->attacks = 3;
                }
            }
        } else if (score > 74) {
            attack->attacks = 2;
        }

        /* Retail reads damage_bonus here before it is reset below, so the swing score sees the
           value left by the previous CalcAttacks pass (0x004ee5ef). */
        score = (character->attributes[W8_ATTRIBUTE_SPEED].effective + attack->damage_bonus * 10 +
                 dual_penalty + physical_experience + load_penalty + attack->combined_skill) /
                3;
        attack->swings = 1;
        if (score > 66) {
            attack->swings = 2;
            if (score > 99) {
                attack->swings = 3;
            }
        }
        if (records[0] != 0 && records[0]->unidentified_name_index == 0x90) {
            attack->swings = 1;
        }

        attack->damage_bonus = 0;
        attack->hit_bonus = 0;
        attack->attack_bonus = 0;
        attack->damage_percent = 0;
        attack->damage_dice.base = 0;
        attack->damage_dice.count = 0;
        attack->damage_dice.sides = 0;
        if (records[hand] != 0) {
            attack->damage_bonus += records[hand]->attack_damage_bonus;
            attack->hit_bonus += records[hand]->attack_hit_bonus;
            if (other->uiHolds == 3 && records[hand == 0] != 0) {
                attack->hit_bonus += records[hand == 0]->attack_hit_bonus;
            }
            if (ItemHasSingledOutGenericName(equipment[hand]->iItemNo)) {
                attack->damage_percent += records[hand]->damage_dice.base * 10;
            }
        } else {
            attack->damage_bonus += attack->combined_skill / 10;
            attack->attack_flags = 0x20;
            if (character->skills[attack->weapon_skill].level > 4) {
                attack->attack_flags = 0x60;
            }
            attack->damage_dice =
                g_unarmed_damage_dice[character->skills[attack->weapon_skill].level / 11];
            if (hand == 0) {
                attack->damage_dice.base += 2;
            }
            memset(attack->condition_chances, 0, sizeof(attack->condition_chances));
            if (character->attributes[W8_ATTRIBUTE_STRENGTH].effective > 49) {
                attack->condition_chances[W8_ATTACK_EFFECT_KNOCK_OUT] =
                    (character->attributes[W8_ATTRIBUTE_STRENGTH].effective - 50) / 5;
            }
        }

        divisor = 1;
        bool strength_applies = true;
        if (records[hand] != 0 && (records[hand]->attack_flags & 0xfe6f) == 0) {
            switch (records[hand]->unidentified_name_index) {
            case 0x68:
            case 0x6e:
            case 0x72:
            case 0x83:
            case 0x90:
                strength_applies = false;
                break;
            default:
                divisor = 2;
            }
        }

        if (!strength_applies) {
            /* These kinds skip the strength terms entirely (0x004ee78a -> 0x004ee7fe). */
        } else if (character->attributes[W8_ATTRIBUTE_STRENGTH].effective < 50) {
            attack->hit_bonus -=
                (50 - character->attributes[W8_ATTRIBUTE_STRENGTH].effective) / (divisor * 10);
            attack->damage_percent -=
                (50 - character->attributes[W8_ATTRIBUTE_STRENGTH].effective) / divisor;
        } else if (character->attributes[W8_ATTRIBUTE_STRENGTH].effective > 50) {
            divisor *= hand + 1;
            attack->hit_bonus +=
                (character->attributes[W8_ATTRIBUTE_STRENGTH].effective - 50) / (divisor * 10);
            attack->damage_percent +=
                (character->attributes[W8_ATTRIBUTE_STRENGTH].effective * 2 - 100) / divisor;
        }

        if (character->attributes[W8_ATTRIBUTE_DEXTERITY].effective < 50) {
            attack->hit_bonus -=
                (50 - character->attributes[W8_ATTRIBUTE_DEXTERITY].effective) / 10;
        } else if (character->attributes[W8_ATTRIBUTE_DEXTERITY].effective > 50) {
            attack->hit_bonus +=
                (character->attributes[W8_ATTRIBUTE_DEXTERITY].effective - 50) / 10;
        }
        if (character->attributes[W8_ATTRIBUTE_SENSES].effective < 30) {
            attack->hit_bonus -= (30 - character->attributes[W8_ATTRIBUTE_SENSES].effective) / 10;
        } else if (character->attributes[W8_ATTRIBUTE_SENSES].effective > 70) {
            attack->hit_bonus += (character->attributes[W8_ATTRIBUTE_SENSES].effective - 70) / 10;
        }

        switch (character->load_category) {
        case W8_LOAD_NONE:
            load_penalty = 0;
            break;
        case W8_LOAD_LIGHT:
            load_penalty = -1;
            break;
        case W8_LOAD_MEDIUM:
            load_penalty = -2;
            break;
        case W8_LOAD_HEAVY:
            load_penalty = -4;
            break;
        case W8_LOAD_EXTREME:
            load_penalty = -8;
            break;
        default:
            break;
        }
        if (hand == 1) {
            load_penalty = load_penalty * 3 / 2;
        }
        if (attack->weapon_skill == W8_SKILL_MODERN_WEAPONS) {
            load_penalty /= 2;
        }
        attack->hit_bonus += load_penalty;
        if (character->skills[W8_SKILL_EAGLE_EYE].active &&
            attack->combat_skill == W8_SKILL_RANGED_COMBAT) {
            attack->hit_bonus += character->skills[W8_SKILL_EAGLE_EYE].level / 20 + 1;
        }
        if (character->skills[W8_SKILL_POWER_STRIKE].active &&
            attack->combat_skill == W8_SKILL_CLOSE_COMBAT) {
            attack->hit_bonus += character->skills[W8_SKILL_POWER_STRIKE].level / 20 + 1;
        }
    }
}

void CalcArmorClasses(W8Character* character, bool defensive_action)
{
    int location_slot = 0;

    unsigned int index;
    for (index = 0; index < 13; ++index) {
        character->armor_class_components[index] = 0;
    }

    for (index = 0; index < 12; ++index) {
        int item_id = character->EquippedItem[index].iItemNo;
        if (index != 0 && index != 4 && index != 5 && index != 8 && index != 9 && index != 10 &&
            index != 11 && item_id != -1) {
            int component =
                g_item_records[item_id].equip_class == W8_ITEM_EQUIP_CLASS_SHIELD ? 3 : 4;
            character->armor_class_components[component] +=
                g_item_records[item_id].armor_class_bonus;
        }
    }

    if (character->highest_condition <= W8_CONDITION_UNCONSCIOUS) {
        if (CharacterHasTrait(character, W8_TRAIT_FAERIE_BASE_ARMOR_CLASS)) {
            character->armor_class_components[W8_AC_COMPONENT_RACE] += 2;
        }
        unsigned int speed = character->attributes[W8_ATTRIBUTE_SPEED].effective;
        if (speed > 79) {
            ++character->armor_class_components[W8_AC_COMPONENT_SPEED];
        }
        if (speed > 89) {
            ++character->armor_class_components[W8_AC_COMPONENT_SPEED];
        }
        if (speed < 20) {
            --character->armor_class_components[W8_AC_COMPONENT_SPEED];
        }
        if (speed < 10) {
            --character->armor_class_components[W8_AC_COMPONENT_SPEED];
        }

        character->armor_class_components[W8_AC_COMPONENT_STEALTH] +=
            character->skills[W8_SKILL_STEALTH].level / 10;
        if (character->skills[W8_SKILL_REFLEXTION].active) {
            character->armor_class_components[W8_AC_COMPONENT_REFLEXTION] +=
                character->skills[W8_SKILL_REFLEXTION].level / 20 + 1;
        }

        int shield = character->armor_class_components[W8_AC_COMPONENT_SHIELD];
        if (shield > 0) {
            int skill_bonus = defensive_action
                                  ? static_cast<int>(character->skills[W8_SKILL_SHIELD].level / 15)
                                  : static_cast<int>(character->skills[W8_SKILL_SHIELD].level / 25);
            int ceiling = defensive_action ? shield * 3 / 2 : shield;
            if (skill_bonus > ceiling) {
                skill_bonus = ceiling;
            }
            character->armor_class_components[W8_AC_COMPONENT_SHIELD] += skill_bonus;
        }

        character->armor_class_components[W8_AC_COMPONENT_MAGIC_SPELLS] +=
            character->bonus.armor_flat;
        character->armor_class_components[W8_AC_COMPONENT_CONDITIONS] +=
            character->bonus.armor_class_adjustment;
        if (defensive_action) {
            character->armor_class_components[W8_AC_COMPONENT_DEFENSIVE_ACTION] += 2;
        }
        switch (character->load_category) {
        case W8_LOAD_MEDIUM:
            character->armor_class_components[W8_AC_COMPONENT_ENCUMBRANCE] -= 1;
            break;
        case W8_LOAD_HEAVY:
            character->armor_class_components[W8_AC_COMPONENT_ENCUMBRANCE] -= 2;
            break;
        case W8_LOAD_EXTREME:
            character->armor_class_components[W8_AC_COMPONENT_ENCUMBRANCE] -= 4;
            break;
        default:
            break;
        }
        character->armor_class_components[W8_AC_COMPONENT_FATIGUE] -=
            FatigueArmorPenalty(character->fatigue_band) / 10;
        character->armor_class_components[W8_AC_COMPONENT_VS_PENETRATION] +=
            character->bonus.armor_matchup;
    }

    character->armor_class_total = 0;
    for (index = 0; index < 12; ++index) {
        if (index != 6) {
            character->armor_class_total += character->armor_class_components[index];
        }
    }
    if (character->bonus.out_of_formation && character->armor_class_total > -5) {
        character->armor_class_total = -5;
    }

    int weighted_total = 0;
    for (index = 0; index < 5; ++index) {
        character->armor_class_by_location[index] = character->armor_class_total;
        character->armor_class_by_location[index] +=
            character->armor_class_components[W8_AC_COMPONENT_VS_PENETRATION];
        switch (index) {
        case 0:
            location_slot = 0;
            break;
        case 1:
            location_slot = 4;
            break;
        case 2:
            location_slot = 10;
            break;
        case 3:
            location_slot = 5;
            break;
        case 4:
            location_slot = 11;
            break;
        default:
            Fail("FALSE", "CalcArmorClasses: ERROR - Invalid AC location");
        }
        int item_id = character->EquippedItem[location_slot].iItemNo;
        if (item_id != -1) {
            character->armor_class_by_location[index] += g_item_records[item_id].armor_class_bonus;
        }
        weighted_total += gubLocalACPercent[index] * character->armor_class_by_location[index];
    }
    character->armor_class_average =
        weighted_total < 0 ? (weighted_total - 50) / 100 : (weighted_total + 50) / 100;
}

bool RecalculateCharacterHitPoints(W8Character* character)
{
    double total = 0.0;
    int profession;
    int hit_points;
    int remaining;

    if (character->iProfession == W8_PROFESSION_NONE) {
        return false;
    }

    for (profession = 0; profession < W8_PROFESSION_COUNT; profession++) {
        unsigned int levels = character->profession_levels[profession];
        if (profession == character->original_profession) {
            levels++;
        }
        if (levels > 0) {
            double vitality = character->attributes[W8_ATTRIBUTE_VITALITY].effective * 0.4;
            total += (vitality * 0.02 + 0.6) * g_profession_hit_point_factors[profession] * levels;
        }
    }

    hit_points = static_cast<int>(total + 0.5) + character->hp_adjustment;
    if (hit_points < 1) {
        hit_points = 1;
    }
    if (hit_points != character->uiHPMax) {
        remaining = (hit_points - character->uiHPMax) + static_cast<int>(character->hp_current);
        if (remaining < 0) {
            remaining = 0;
        }
        character->uiHPMax = hit_points;
        character->hp_current = remaining;
        return remaining == 0;
    }
    return false;
}
