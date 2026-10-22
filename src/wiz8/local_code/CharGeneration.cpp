#include "wiz8/local_code/CharGeneration.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>

#include "wiz8/character_skills.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/ProfRaceInfoDialog.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/character_stats.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"

/* Refund every spent skill point through the per-skill commit helper. */
bool CharacterDraft::attributesComplete() const
{
    if (allocation.attribute_points_remaining <= 0) return true;
    for (int index = 0; index < 7; ++index)
        if (allocation.attribute_values[index] < allocation.attribute_limits[index]) return false;
    return true;
}

bool CharacterDraft::skillsComplete() const
{
    if (allocation.skill_points_remaining <= 0) return true;
    for (int index = 0; index < W8_SKILL_COUNT; ++index)
        if (allocation.skill_points_spent[index] < allocation.skill_limits[index]) return false;
    return true;
}

void CharacterDraft::refreshSkillAvailability()
{
    const auto changed = RefreshCharacterSkillAvailability(game.rules().data, &character);
    for (int index = 0; index < W8_SKILL_COUNT; ++index) {
        if (!changed[index]) continue;
        const auto skill = static_cast<W8Skill>(index);
        if (character.skills[index].active)
            resetSkillContribution(skill);
        else
            refundSkillAllocation(skill);
    }
}

void CharacterDraft::refundAllSkillPoints()
{
    int index;
    int spent;

    for (index = 0; index < 0x29; ++index) {
        spent = allocation.skill_points_spent[index];
        if (spent > 0) {
            adjustSkill(index, -spent);
        }
    }
}

/* Sums the realm skill costs the edited character still owes: skills the
   original holds but the edited build lacks, or the original's own set when
   it has no caster level to compare against. */
int ComputeRealmSkillDebt(W8Character* original, W8Character* edited)
{
    int total = 0;

    if (GetProfessionCasterLevel(edited, W8_PROFESSION_NONE) != 0) {
        int index;

        for (index = 0x18; index <= 0x1b; ++index) {
            if (!original->skills[index].active) {
                continue;
            }
            if (edited->skills[index].active) {
                continue;
            }
            total += original->skill_costs[index - 0x18];
        }
        return total;
    }
    if (GetProfessionCasterLevel(original, W8_PROFESSION_NONE) < 1) {
        return 0;
    }
    {
        int index;

        for (index = 0x18; index <= 0x1b; ++index) {
            if (!original->skills[index].active) {
                continue;
            }
            total += original->skill_costs[index - 0x18];
        }
        total += original->magic_bonus_pool;
    }
    return total;
}

void CharacterDraft::initializeCharacterPointPools()
{
    const bool creating = character.uiExpLevel == 1;
    allocation.attribute_points_total =
        character.iProfession != W8_PROFESSION_NONE && character.iRace != W8_RACE_NONE
            ? (creating ? 60 : 6)
            : 0;
    allocation.attribute_points_remaining = allocation.attribute_points_total;
    recomputeAttributeLimits();
    allocation.skill_points_total = creating ? 15 : 9;
    allocation.skill_points_remaining = allocation.skill_points_total;
    recomputeSkillLimits();
}

/* A fresh creation: zero the character and the editing state, install the
   sentinel values, and hand out the level-one pools. */
void CharacterDraft::beginCreation()
{
    gender_was_forced = false;
    character = W8Character{};
    character.gender = W8_GENDER_UNSET;
    character.iProfession = W8_PROFESSION_NONE;
    character.iRace = W8_RACE_NONE;
    character.uiExpLevel = 1;
    character.level_band_base = 0;
    character.highest_condition = W8_CONDITION_NONE;
    character.enchantment_top = W8_ENCHANTMENT_NONE;
    character.portrait_index = -1;
    character.unknown_007d = -1;
    character.personality = -1;
    allocation = {};
    EmptyAllCarriedItems(game, character);

    initializeCharacterPointPools();
}

/* One level gained: raise the level and profession counter, reset the editing
   state, and re-derive every pool the character's new level entitles them
   to. */
void CharacterDraft::beginLevelUp()
{
    gender_was_forced = false;
    ++character.uiExpLevel;
    ++character.profession_levels[character.iProfession];
    allocation = {};

    initializeCharacterPointPools();

    if (character.attribute_point_deficit < 0) {
        payDownAttributeDebt();
    }
    CalcCharacterLevelBand(&character);
    RecalculateEditedCharacterStats(game, character);
    finalizeSpellPointPool();
}

/* Reset one skill's contribution to the editing state: set its reset byte,
   refund its spent points, and let a realm skill that was just opened up reach its
   minimum five. */
void CharacterDraft::resetSkillContribution(W8Skill skill_id)
{
    /* Retail stores to byte 1 of the skill record, not to the
       active flag at byte 0. */
    character.skills[skill_id].reset_flag = true;
    allocation.skill_points_spent[skill_id] = 0;

    if (skill_id >= W8_SKILL_SPELLBOOK_WIZARDRY && skill_id < W8_SKILL_FIRE_MAGIC) {
        int offset = g_profession_magic_level_offsets[character.iProfession];
        if (offset < 0 && offset > -0xff &&
            character.profession_levels[character.iProfession] + offset == 1) {
            int missing = 5 - character.skills[skill_id].points;
            if (missing > 0) {
                character.skills[skill_id].points += missing;
                character.skills[skill_id].level += missing;
                allocation.skill_baselines[skill_id] += missing;
            }
        }
    }
}

/* Refund every point spent on one skill and leave its level at the base
   value the assignment gave it. */
void CharacterDraft::refundSkillAllocation(W8Skill skill_id)
{
    int spent = allocation.skill_points_spent[skill_id];
    if (spent > 0) {
        allocation.skill_points_remaining += spent;
        character.skills[skill_id].points -= spent;
        allocation.skill_points_spent[skill_id] = 0;
    }
    character.skills[skill_id].level = character.skills[skill_id].points;
}

/* Every point the edited character has already committed is spent from the
   pool in one burst. */
void CharacterDraft::refundAllocatedAttributes()
{
    for (int index = 0; index < 7; ++index) {
        if (allocation.attribute_values[index] > 0) {
            adjustAttribute(static_cast<W8Attribute>(index), -allocation.attribute_values[index]);
        }
    }
}

/* Attribute limits and the step ceiling for the current pool, then the
   attribute-point reconciliations. */
void CharacterDraft::recomputeAttributeLimits()
{
    int points = allocation.attribute_points_total;
    int step;
    int index;

    if (points < 0) {
        step = 0;
    } else {
        for (index = 0; index < 7; ++index) {
            points += allocation.attribute_baselines[index];
        }
        step = (points + 2) / 3;
        if (step < 3) {
            step = points < 4 ? points : 3;
        }
    }
    allocation.attribute_step_limit = step;

    for (index = 0; index < 7; ++index) {
        int limit = step - allocation.attribute_baselines[index];
        int cap = (allocation.attribute_values[index] - character.attributes[index].base) + 100;
        int value = limit;
        if (cap <= limit) {
            value = cap;
        }
        if (value < 0) {
            limit = 0;
        } else if (cap <= limit) {
            limit = cap;
        }
        allocation.attribute_limits[index] = limit;
    }

    clampAttributesToBudget();
}

/* Clamp the allocated attribute values to their limits, then take points back
   round-robin while the character is over budget. */
void CharacterDraft::clampAttributesToBudget()
{
    int index;

    for (index = 0; index < 7; ++index) {
        int value = allocation.attribute_values[index];
        int limit = allocation.attribute_limits[index];
        if (value > limit) {
            allocation.attribute_values[index] = value - (value - limit);
        }
    }

    int total = 0;
    for (index = 0; index < 7; ++index) {
        total += allocation.attribute_values[index];
    }
    if (total > 0 && allocation.attribute_points_total > 0 &&
        allocation.attribute_points_total < total) {
        index = 0;
        while (allocation.attribute_points_total < total) {
            if (allocation.attribute_values[index] > 0) {
                --allocation.attribute_values[index];
                --character.attributes[index].base;
                --total;
            }
            if (++index == 7) {
                index = 0;
            }
        }
    }
    allocation.attribute_points_remaining = allocation.attribute_points_total - total;
}

/* Top the character's attributes up to the profession's minimums, or hand the
   shortfall to 0x00557200 when the pool cannot cover it. A level-one character
   also gets the value/baseline bookkeeping the creation flow expects. */
void CharacterDraft::applyProfessionMinimumAttributes()
{
    int deficits[7];
    int index;

    character.attribute_point_deficit = 0;
    for (index = 0; index < 7; ++index) {
        int deficit = g_profession_attribute_minimums[character.iProfession].values[index] -
                      character.attributes[index].base;
        deficits[index] = deficit;
        if (deficit > 0) {
            character.attribute_point_deficit -= deficit;
        }
    }

    if (allocation.attribute_points_total + character.attribute_point_deficit < 0) {
        /* Paying down the debt still falls through to the pool refresh and
           limit recompute below. */
        payDownAttributeDebt();
    } else {
        character.attribute_point_deficit = 0;
        character.level_band_base = 0;
        for (index = 0; index < 7; ++index) {
            int deficit = deficits[index];
            if (deficit < 1) {
                if (character.uiExpLevel > 1) {
                    int minimum =
                        g_profession_attribute_minimums[character.iProfession].values[index];
                    int value = allocation.attribute_values[index];
                    while (character.attributes[index].base - value <
                           static_cast<unsigned int>(minimum)) {
                        allocation.attribute_values[index] = value - 1;
                        --allocation.attribute_points_total;
                        ++allocation.attribute_baselines[index];
                        value = allocation.attribute_values[index];
                    }
                }
            } else {
                character.attributes[index].base += deficit;
                character.attributes[index].effective += deficit;
                allocation.attribute_points_total -= deficit;
                if (character.uiExpLevel > 1) {
                    allocation.attribute_baselines[index] = deficit;
                }
            }
        }
    }
    allocation.attribute_points_remaining = allocation.attribute_points_total;
    recomputeAttributeLimits();
}

/* Draw the level-up attribute debt back down one point at a time, preferring
   the largest remaining deficit, until the pool or the debt runs out. */
void CharacterDraft::payDownAttributeDebt()
{
    std::array<std::array<int, 2>, 7> deficits;
    int index;

    int total = 0;
    for (index = 0; index < 7; ++index) {
        unsigned int minimum = g_profession_attribute_minimums[character.iProfession].values[index];
        int deficit = 0;
        if (character.attributes[index].base < minimum) {
            deficit = minimum - character.attributes[index].base;
        }
        deficits[index][0] = deficit;
        deficits[index][1] = index;
        total -= deficit;
    }
    std::ranges::sort(deficits, std::greater{}, [](const auto& row) { return row[0]; });
    if (character.attribute_point_deficit != total) {
        character.attribute_point_deficit = total;
    }
    index = 0;
    while (character.attribute_point_deficit < 0 && allocation.attribute_points_total > 0) {
        if (deficits[index][0] < 1 ||
            (index != 6 && deficits[index][0] <= deficits[index + 1][0])) {
            index = (index + 1) % 7;
        } else {
            int attribute = deficits[index][1];
            ++character.attributes[attribute].base;
            ++character.attributes[attribute].effective;
            total = deficits[index][0];
            ++character.attribute_point_deficit;
            int available = allocation.attribute_points_total;
            deficits[index][0] = total - 1;
            allocation.attribute_points_total = available - 1;
            index = 0;
        }
    }
    if (character.attribute_point_deficit >= 0) {
        if (allocation.attribute_points_total < allocation.attribute_points_remaining) {
            allocation.attribute_points_remaining = allocation.attribute_points_total;
        }
        return;
    }
    allocation.attribute_points_total = character.attribute_point_deficit;
    character.level_band_base = character.uiExpLevel;
}

/* Which of the fifteen professions the current attribute budget can still
   reach: the character's own profession is always eligible, profession two
   needs the matching sex, and every other one has to clear the
   profession's minimums within the remaining pool and limits. */
bool CharacterDraft::canChooseProfession(W8Profession profession) const
{
    if (profession == character.iProfession) return true;
    if (profession == W8_PROFESSION_VALKYRIE && character.gender != W8_GENDER_FEMALE) return false;
    int deficit = 0;
    for (int attribute = 0; attribute < 7; ++attribute) {
        const int difference = g_profession_attribute_minimums[profession].values[attribute] -
                               character.attributes[attribute].base;
        if (difference > 0) deficit -= difference;
    }
    if (allocation.attribute_points_total + deficit < 0) return false;
    for (int attribute = 0; attribute < 7; ++attribute) {
        if (static_cast<unsigned int>(allocation.attribute_limits[attribute] -
                                      allocation.attribute_values[attribute]) +
                character.attributes[attribute].base <
            static_cast<unsigned int>(
                g_profession_attribute_minimums[profession].values[attribute]))
            return false;
    }
    return true;
}

/* Apply one attribute adjustment through the same clamps 0x005579E0 uses for
   the creation flow: never below zero, never above the pool or the limit. */
void CharacterDraft::adjustAttribute(W8Attribute attribute, int modifier)
{
    int value = allocation.attribute_values[attribute];
    if (value + modifier < 0) {
        assert(modifier < 0);
        modifier = -value;
    }
    if (modifier > allocation.attribute_points_remaining) {
        modifier = allocation.attribute_points_remaining;
    }
    if (value + modifier > allocation.attribute_limits[attribute]) {
        modifier = allocation.attribute_limits[attribute] - value;
    }
    character.attributes[attribute].base += modifier;
    character.attributes[attribute].effective += modifier;
    allocation.attribute_points_remaining -= modifier;
    allocation.attribute_values[attribute] += modifier;

    RecalculateEditedCharacterStats(game, character);
    if (character.uiExpLevel == 1) {
        rebuildSkillAllocations();
    }
    recountSpellPoints();
    refreshSkillAvailability();
    recomputeSkillLimits();
}

/* Skill limits from the pool plus the base levels the attribute pairs give
   every skill. */
void CharacterDraft::recomputeSkillLimits()
{
    InitializeSkillBaseLevels(&character);
    int step = (allocation.skill_points_total + 2) / 3;
    allocation.skill_step_limit = step;

    for (int index = 0; index < 0x29; ++index) {
        if (!character.skills[index].active) {
            allocation.skill_limits[index] = 0;
            continue;
        }
        int limit = (allocation.skill_points_spent[index] - character.skills[index].points) + 0x4b;
        int value = step;
        if (limit <= step) {
            value = limit;
        }
        if (value < 0) {
            allocation.skill_limits[index] = 0;
        } else {
            if (step < limit) {
                limit = step;
            }
            allocation.skill_limits[index] = limit;
        }
    }
    clampSkillsToBudget();
}

/* Clamp the committed skill points to their limits, refund round-robin while
   over budget, and report whether every skill still has room. */
void CharacterDraft::clampSkillsToBudget()
{
    int index;

    for (index = 0; index < 0x29; ++index) {
        int spent = allocation.skill_points_spent[index];
        int excess = spent - allocation.skill_limits[index];
        if (excess > 0) {
            allocation.skill_points_spent[index] = spent - excess;
            character.skills[index].points -= excess;
            character.skills[index].level -= excess;
        }
    }
    int total = 0;
    for (index = 0; index < 0x29; ++index) {
        total += allocation.skill_points_spent[index];
    }
    if (total > 0 && allocation.skill_points_total > 0 && allocation.skill_points_total < total) {
        index = 0;
        while (allocation.skill_points_total < total) {
            if (allocation.skill_points_spent[index] > 0) {
                --allocation.skill_points_spent[index];
                --character.skills[index].points;
                --character.skills[index].level;
                --total;
            }
            if (++index == 0x29) {
                index = 0;
            }
        }
    }
    allocation.skill_points_remaining = allocation.skill_points_total - total;
}

/* Finalize the spell-point pool the level-up flow presents: settle the realm
   skills against the spent spell points, then trim any learned spell the
   character can no longer hold. */
void CharacterDraft::finalizeSpellPointPool()
{
    int spent = allocation.spell_points_total - allocation.spell_points_remaining;
    if (GetProfessionCasterLevel(&character, W8_PROFESSION_NONE) < 1) {
        allocation.spell_points_total = 0;
    } else {
        if (character.uiExpLevel == 1) {
            allocation.magic_skill_bonus = 2;
        } else {
            allocation.magic_skill_bonus = 1;
        }

        int total = 0;
        if (GetProfessionCasterLevel(&character, W8_PROFESSION_NONE) > 0) {
            for (unsigned int realm = 0x18; realm < 0x1c; ++realm) {
                if (character.skills[realm].active) {
                    total += character.skill_costs[realm - 0x18];
                }
            }
            total += character.magic_bonus_pool;
        }
        allocation.spell_points_total = total + allocation.magic_skill_bonus;
    }

    if (allocation.spell_points_total < spent) {
        for (unsigned int index = 0; index < 0x72; ++index) {
            if (character.spell_learned[index] == 2) {
                character.spell_learned[index] = 0;
                --spent;
                if (spent == allocation.spell_points_total) {
                    break;
                }
            }
        }
    }
    allocation.spell_points_remaining = allocation.spell_points_total - spent;
    if (allocation.spell_points_remaining > 0) {
        return;
    }
}

/* Re-checks every spell against the character's current realm skills and
   reports how many points still remain to be allocated. */
int CharacterDraft::recountSpellPoints()
{
    int available = 0;
    int index;
    int spent = allocation.spell_points_total - allocation.spell_points_remaining;

    if (allocation.spell_points_total < 1) {
        for (index = 0; index < 0x72; ++index) {
            if (character.spell_learned[index] == -1 || character.spell_learned[index] == 2) {
                character.spell_learned[index] = 0;
            }
        }
    } else {
        for (index = 0; index < 4; ++index) {
            int skill = 0x18 + index;
            int points = allocation.skill_points_spent[skill];
            character.skills[skill].points -= points;
            character.skills[skill].level -= points;
        }
        for (index = 0; index < 6; ++index) {
            int skill = 0x1c + index;
            int points = allocation.skill_points_spent[skill];
            character.skills[skill].points -= points;
            character.skills[skill].level -= points;
        }
        for (index = 0; index < 0x72; ++index) {
            if (character.spell_learned[index] != 1) {
                if (!CanCharacterLearnSpell(&character, index)) {
                    if (character.spell_learned[index] == 2) {
                        --spent;
                        ++allocation.spell_points_remaining;
                    }
                    character.spell_learned[index] = 0;
                } else {
                    if (character.spell_learned[index] != 2) {
                        character.spell_learned[index] = -1;
                    }
                    ++available;
                }
            }
        }
        for (index = 0; index < 4; ++index) {
            int skill = 0x18 + index;
            int points = allocation.skill_points_spent[skill];
            character.skills[skill].points += points;
            character.skills[skill].level += points;
        }
        for (index = 0; index < 6; ++index) {
            int skill = 0x1c + index;
            int points = allocation.skill_points_spent[skill];
            character.skills[skill].points += points;
            character.skills[skill].level += points;
        }
    }

    if (available < allocation.spell_points_total) {
        allocation.spell_points_total = available;
        int remaining = available - spent;
        if (remaining < 0) {
            remaining = 0;
        }
        allocation.spell_points_remaining = remaining;
    } else {
        allocation.spell_points_remaining = allocation.spell_points_total - spent;
    }
    if (allocation.spell_points_total != 0 && allocation.spell_points_remaining != 0) {
        return available;
    }
    return available;
}

/* Reset every skill to the attribute-derived base, hand the profession's
   skills and bonus skill their share of the step pool, then add the points
   already committed. */
void CharacterDraft::rebuildSkillAllocations()
{
    int index;

    for (index = 0; index < 0x29; ++index) {
        character.skills[index].points = 0;
        character.skills[index].level = 0;
    }
    InitializeSkillBaseLevels(&character);

    W8Profession profession = character.iProfession;
    int count = 1;
    for (index = 0; index < 4; ++index) {
        if (g_profession_skills[profession][index] != -1) {
            ++count;
        }
    }
    int step = 0x3c / count;
    if (allocation.attribute_points_total < 0) {
        step -= static_cast<unsigned int>(allocation.attribute_points_total * step * -2) / 100;
    }

    for (index = 0; index < 4; ++index) {
        W8Skill skill = g_profession_skills[profession][index];
        if (skill != W8_SKILL_NONE) {
            int value = (character.skills[skill].base_level * step) / 100;
            character.skills[skill].points = value;
            character.skills[skill].level = value;
        }
    }
    W8Skill bonus_skill = g_profession_bonus_skills[profession];
    int value = (character.skills[bonus_skill].base_level * step) / 100;
    character.skills[bonus_skill].points = value;
    character.skills[bonus_skill].level = value;
    for (index = 0; index < 0x29; ++index) {
        character.skills[index].points += allocation.skill_points_spent[index];
        character.skills[index].level = character.skills[index].points;
    }
    character.skills[bonus_skill].level += GetSkillQuarterValue(&character, bonus_skill);
}

/* Compute how many spell points the level-up summary can award from the
   character's six realm pools, saving and restoring the learned-spell flags
   around the trial assignment. */
int CharacterDraft::computeLevelUpSpellPointAward()
{
    int total = 0;
    int index;
    int saved[0x72];

    if (character.uiExpLevel <= 1) {
        if (allocation.spell_points_total > 0) {
            if (allocation.spell_points_remaining > 0) {
                for (index = 0; index < 0x72; ++index) {
                    saved[index] = character.spell_learned[index];
                }
                /* Retail clears 0x72 bytes, not 0x72 entries: the first 28
                   flags and the low half of the 29th. The trial only tests
                   flags for 1 or 2, which a half-cleared flag never equals,
                   so clearing the first 29 flags is the same trial. */
                std::fill_n(character.spell_learned, 29, 0);
                static const int trial_spells[6] = {0xc, 6, 7, 5, 3, 0xb};
                for (index = 0; index < 6; ++index) {
                    if (CanCharacterLearnSpell(&character, trial_spells[index])) {
                        character.spell_learned[trial_spells[index]] = 1;
                    }
                }
            }
            RecalculateEditedCharacterStats(game, character);
            std::array<std::array<int, 2>, 6> pools;
            for (index = 0; index < 6; ++index) {
                pools[index][0] = character.sp_max[index];
                pools[index][1] = index;
            }
            std::ranges::sort(pools, std::greater{}, [](const auto& row) { return row[0]; });
            int count = allocation.spell_points_total < 7 ? allocation.spell_points_total : 6;
            for (index = 0; index < count; ++index) {
                total += character.sp_max[pools[index][1]];
            }
            if (allocation.spell_points_remaining > 0) {
                for (index = 0; index < 0x72; ++index) {
                    character.spell_learned[index] = saved[index];
                }
                RecalculateEditedCharacterStats(game, character);
            }
        }
        return total;
    }
    for (index = 0; index < 6; ++index) {
        total += character.sp_max[index];
    }
    return total;
}

/* Spend one creation spell point on a pick: the spell goes to selected, and
   either end of the linked 0x49/0x4b pair drags its partner along unless the
   character already knows it. An emptied pool marks the page complete. */
void CharacterDraft::selectCreationSpell(unsigned int spell)
{
    if (allocation.spell_points_remaining > 0) {
        character.spell_learned[spell] = 2;
        --allocation.spell_points_remaining;
        if (spell == 0x4b) {
            if (character.spell_learned[0x49] != 1) {
                character.spell_learned[0x49] = 2;
            }
        } else if (spell == 0x49) {
            if (character.spell_learned[0x4b] != 1) {
                character.spell_learned[0x4b] = 2;
            }
        }
    }
    if (allocation.spell_points_remaining == 0) {
    }
    RecalculateEditedCharacterStats(game, character);
}

/* Return one creation spell pick: the spell goes to declined, an auto-added
   partner of the 0x49/0x4b pair follows only while it is still marked
   selected, and the point goes back to the pool. */
void CharacterDraft::deselectCreationSpell(unsigned int spell)
{
    if (character.spell_learned[spell] != 0) {
        character.spell_learned[spell] = -1;
        if (spell == 0x4b) {
            if (character.spell_learned[0x49] == 2) {
                character.spell_learned[0x49] = -1;
            }
        } else if (spell == 0x49) {
            if (character.spell_learned[0x4b] == 2) {
                character.spell_learned[0x4b] = -1;
            }
        }
        ++allocation.spell_points_remaining;
        RecalculateEditedCharacterStats(game, character);
    }
}

/* Back out the in-progress spell picks before the pool is rebuilt: every
   tentative selection (state two) returns to unlearned, the point pool is
   topped back up and the page is marked incomplete. */
void CharacterDraft::resetSpellSelections()
{
    for (int index = 0; index < 0x72; ++index) {
        if (character.spell_learned[index] == 2) {
            character.spell_learned[index] = -1;
        }
    }
    allocation.spell_points_remaining = allocation.spell_points_total;
}

/* Price the profession's starting gear: stage the equipment, sum every
   carried stack's value, then empty the character back out. */
int ComputeStartingEquipmentCost(Game& game, W8Character* character)
{
    int total;
    int count;

    total = 0;
    AddCharacterStartingEquipment(game, character);
    for (count = 0; count < 0xc; ++count) {
        if (character->EquippedItem[count].iItemNo != -1) {
            total += GetItemStackValue(&character->EquippedItem[count]);
        }
    }
    for (count = 0; count < 8; ++count) {
        if (character->backpack[count].iItemNo != -1) {
            total += GetItemStackValue(&character->backpack[count]);
        }
    }
    EmptyAllCarriedItems(game, *character);
    return total;
}

/* Whether the party's gold covers the new character's starting gear. */
bool CanAffordStartingEquipment(Game& game, W8Character* character)
{
    return static_cast<unsigned int>(ComputeStartingEquipmentCost(game, character)) <=
           game.party.gold;
}

/* Recompute the level-up pools after a profession change, refunding every
   baseline the previous profession's assignment had granted. */
void CharacterDraft::setProfession(W8Profession profession)
{
    int index;

    if (character.iProfession != W8_PROFESSION_NONE) {
        --character.profession_levels[character.iProfession];
    }
    ++character.profession_levels[profession];
    character.iProfession = profession;

    if (profession == W8_PROFESSION_VALKYRIE) {
        if (character.gender == W8_GENDER_MALE) {
            gender_was_forced = true;
            character.gender = W8_GENDER_FEMALE;
        }
    } else if (gender_was_forced) {
        gender_was_forced = false;
        character.gender = W8_GENDER_MALE;
    }

    if (character.uiExpLevel > 1) {
        for (index = 0; index < 7; ++index) {
            int baseline = allocation.attribute_baselines[index];
            if (baseline > 0) {
                character.attributes[index].base -= baseline;
                character.attributes[index].effective -= baseline;
                allocation.attribute_points_total += baseline;
                allocation.attribute_baselines[index] = 0;
            }
        }
        for (index = 0; index < 0x29; ++index) {
            int baseline = allocation.skill_baselines[index];
            if (baseline > 0) {
                character.skills[index].points -= baseline;
                character.skills[index].level -= baseline;
                allocation.skill_baselines[index] = 0;
            }
        }
        W8Skill bonus_skill = g_profession_bonus_skills[character.iProfession];
        int base = character.skills[bonus_skill].points;
        int missing = (allocation.skill_points_spent[bonus_skill] - base) + 5;
        if (missing > 0) {
            character.skills[bonus_skill].points = base + missing;
            character.skills[bonus_skill].level += missing;
            allocation.skill_baselines[bonus_skill] += missing;
        }
        for (index = 0; index < 4; ++index) {
            W8Skill skill = g_profession_skills[character.iProfession][index];
            int value = character.skills[skill].points;
            int missing = (allocation.skill_points_spent[skill] - value) + 5;
            if (missing > 0) {
                character.skills[skill].points = value + missing;
                character.skills[skill].level += missing;
                allocation.skill_baselines[skill] += missing;
            }
        }
    }
    applyRaceProfessionTables();
}

/* Assign the chosen race and rebuild the race/profession tables. */
void CharacterDraft::setRace(W8Race race)
{
    character.iRace = race;
    applyRaceProfessionTables();
}

/* Assign the chosen sex and rebuild the race/profession tables. */
void CharacterDraft::setGender(W8Gender gender)
{
    character.gender = gender;
    applyRaceProfessionTables();
}

/* Apply the race and profession tables once race, profession and sex are
   all set, rebuilding every derived pool on the way. */
void CharacterDraft::applyRaceProfessionTables()
{
    int index;
    if (character.iRace == W8_RACE_NONE) {
        if (character.iProfession != W8_PROFESSION_NONE) {
            for (index = 0; index < 7; ++index) {
                unsigned int minimum =
                    g_profession_attribute_minimums[character.iProfession].values[index];
                character.attributes[index].base = minimum;
                character.attributes[index].effective = minimum;
            }
        }
    } else {
        if (character.uiExpLevel == 1) {
            for (index = 0; index < 7; ++index) {
                unsigned int minimum = g_race_attribute_minimums[character.iRace].values[index];
                character.attributes[index].base = minimum;
                character.attributes[index].effective = minimum;
            }
        }
        if (character.iProfession != W8_PROFESSION_NONE) {
            if (character.uiExpLevel == 1) {
                allocation.attribute_points_total = 60;
                allocation.attribute_points_remaining = 60;
                recomputeAttributeLimits();
                character.original_profession = character.iProfession;
                applyProfessionMinimumAttributes();
                CalcCharacterLevelBand(&character);
                for (index = 0; index < 7; ++index) {
                    character.attributes[index].base += allocation.attribute_values[index];
                }
            } else {
                applyProfessionMinimumAttributes();
            }
            refreshSkillAvailability();
            finalizeSpellPointPool();
            RecountLearnedSpellsByRealm(game.rules().data, &character);
            recountSpellPoints();
            refreshSkillAvailability();
            recomputeSkillLimits();
        }
        if (character.uiExpLevel == 1) {
            for (index = 0; index < 7; ++index) {
                character.attributes[index].effective = character.attributes[index].base;
            }
        }
    }
    RecalculateEditedCharacterStats(game, character);
    if (character.uiExpLevel == 1) {
        if (character.gender != W8_GENDER_UNSET && character.iProfession != W8_PROFESSION_NONE &&
            character.iRace != W8_RACE_NONE) {
            rebuildSkillAllocations();
        }
        character.portrait_index = -1;
        character.unknown_007d = -1;
        character.personality = -1;
        character.voice = 0;
    }
}

/* The level-up attribute editor's pool is fixed by the profession minimums,
   so the deficit sweep runs before the skill pass. */
void CharacterDraft::adjustSkill(unsigned int skill, int modifier)
{
    int spent = allocation.skill_points_spent[skill];
    if (spent + modifier < 0) {
        assert(modifier < 0);
        modifier = -spent;
    }
    if (allocation.skill_points_remaining < modifier) {
        modifier = allocation.skill_points_remaining;
    }
    if (allocation.skill_limits[skill] < spent + modifier) {
        modifier = allocation.skill_limits[skill] - spent;
    }
    character.skills[skill].points += modifier;
    character.skills[skill].level += modifier;
    allocation.skill_points_remaining -= modifier;
    allocation.skill_points_spent[skill] += modifier;

    recountSpellPoints();
    refreshSkillAvailability();
    recomputeSkillLimits();
    RecalculateEditedCharacterStats(game, character);
}

/* Finalize a created character: recompute the derived stats, hand out the
   starting equipment when the caller asked for it, settle the attribute magic
   skill bonus against the skill costs of the chosen spells, turn every chosen
   spell into a learned one, reset the ones left over, rebuild the spell-point
   ceilings and re-derive the experience goal and level band. The spell the
   creation flow marks as chosen but does not charge for is skipped by its
   retail index, not by anything a record says. */
void CharacterDraft::finish(bool give_starting_equipment)
{
    unsigned int realm;
    unsigned int spell;

    RecalculateCharacterDerivedStats(game, character);
    if (give_starting_equipment) {
        AddCharacterStartingEquipment(game, &character);
    }

    if (character.iProfession == W8_PROFESSION_BISHOP) {
        character.magic_bonus_pool += allocation.magic_skill_bonus;
    } else {
        for (realm = 0x18; realm <= 0x1b; ++realm) {
            if (character.skills[realm].active) {
                character.skill_costs[realm - 0x18] += allocation.magic_skill_bonus;
                break;
            }
        }
    }

    for (spell = 0; spell < 0x72; ++spell) {
        if (character.spell_learned[spell] != 2) {
            continue;
        }
        character.spell_learned[spell] = 1;
        if (spell == 0x49) {
            continue;
        }
        for (realm = 0x18; realm <= 0x1b; ++realm) {
            switch (realm) {
            case 0x18:
                if (game.rules().data.spells[spell].wizardry_spell == 0) continue;
                break;
            case 0x19:
                if (game.rules().data.spells[spell].divinity_spell == 0) continue;
                break;
            case 0x1a:
                if (game.rules().data.spells[spell].alchemy_spell == 0) continue;
                break;
            case 0x1b:
                if (game.rules().data.spells[spell].psionics_spell == 0) continue;
                break;
            }
            if (character.skills[realm].active && character.skill_costs[realm - 0x18] > 0) {
                --character.skill_costs[realm - 0x18];
                goto spell_done;
            }
        }
        if (character.magic_bonus_pool > 0) {
            --character.magic_bonus_pool;
        } else if (character.iProfession == W8_PROFESSION_BISHOP) {
            for (realm = 0x18; realm <= 0x1b; ++realm) {
                if (character.skills[realm].active && character.skill_costs[realm - 0x18] > 0) {
                    --character.skill_costs[realm - 0x18];
                    break;
                }
            }
        }
    spell_done:;
    }

    for (spell = 0; spell < 0x72; ++spell) {
        if (character.spell_learned[spell] == 2) {
            character.spell_learned[spell] = 1;
        }
    }

    const auto spell_point_ceilings = std::to_array(character.sp_max);
    RebuildRealmSpellPointCeilings(game.rules().data, &character);
    FollowSpellPointCeilings(character, spell_point_ceilings);
    character.experience_previous_goal = character.experience_goal;
    CalcXPGoal(&character);
    CalcCharacterLevelBand(&character);
}

/* The six starting item ids each profession hands out, with the faerie race's
   own row last; -1 is an empty slot. */
int g_starting_equipment[0x10][6] = {
    {143, 153, 203, 246, -1, -1},  {35, 1, 158, 204, 247, 210},  {75, 170, 197, 246, 215, -1},
    {110, 125, 157, 188, 247, -1}, {38, 49, 165, 192, 246, -1},  {105, 230, 180, 209, 253, -1},
    {79, 105, 165, 192, 246, -1},  {0, 0, 99, 156, 187, 247},    {132, 156, 187, 246, -1, -1},
    {769, 116, 132, 156, 187, -1}, {165, 192, 246, 335, -1, -1}, {28, 165, 192, 246, 350, -1},
    {82, 165, 192, 246, 223, -1},  {0, 165, 192, 246, 347, -1},  {28, 165, 192, 246, 385, -1},
    {63, 169, 196, 335, 358, 0},
};

/* Hand out the race or profession's six-item starting set and then the extra
   item the profession's own skill pair selects, and settle the character's
   modifiers once the equipment is on. */
void AddCharacterStartingEquipment(Game& game, W8Character* character)
{
    W8ItemInstance item;
    unsigned int slot;
    unsigned int set;

    EmptyAllCarriedItems(game, *character);

    set = character->iRace == W8_RACE_FAERIE ? 15 : character->iProfession;
    for (slot = 0; slot < 6; ++slot) {
        if (g_starting_equipment[set][slot] == -1) {
            continue;
        }
        item = MakeItem(game.rules().data, game.rng, g_starting_equipment[set][slot], true, true,
                        true);
        AddItemToCharacter(game, Outsider(*character), OwnedItem(item), true, false, false);
    }

    switch (character->iProfession) {
    case W8_PROFESSION_PRIEST:
        if (character->skills[W8_SKILL_MACE_FLAIL].level >
            character->skills[W8_SKILL_STAFF_WAND].level) {
            item = MakeItem(game.rules().data, game.rng, 0x16, true, true, true);
        } else {
            item = MakeItem(game.rules().data, game.rng, 0x52, true, true, true);
        }
        break;
    case W8_PROFESSION_FIGHTER:
        if (character->skills[W8_SKILL_AXE].level > character->skills[W8_SKILL_SWORD].level) {
            item = MakeItem(game.rules().data, game.rng, 0x12, true, true, true);
        } else {
            item = MakeItem(game.rules().data, game.rng, 7, true, true, true);
        }
        break;
    case W8_PROFESSION_GADGETEER:
        item = MakeItem(game.rules().data, game.rng, 599, true, true, true);
        break;
    case W8_PROFESSION_BARD:
        item = MakeItem(game.rules().data, game.rng, 0x144, true, true, true);
        break;
    default:
        RecalculateCharacterDerivedStats(game, *(character));
        return;
    }
    AddItemToCharacter(game, Outsider(*character), OwnedItem(item), true, false, false);
    RecalculateCharacterDerivedStats(game, *(character));
}
