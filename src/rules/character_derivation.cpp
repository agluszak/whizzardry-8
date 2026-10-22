#include "wiz8/rules/character_derivation.h"

#include <algorithm>
#include <stdexcept>

#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"

/* Whether the character has the trait through profession, race, or - for
   trait 0x1c - a set second enchantment slot. Profession id -1 and race id
   -1 both mean absent and skip their tables. Read-only, so callers agree
   on a const character. */
bool CharacterHasTrait(const W8Character* character, W8Trait trait)
{
    unsigned int index;

    if (character == 0) {
        return false;
    }
    if (character->iProfession != W8_PROFESSION_NONE) {
        const W8Trait* abilities = g_profession_abilities[character->iProfession].ability_ids;
        for (index = 0; index < 3; ++index) {
            if (abilities[index] == trait) {
                return true;
            }
        }
    }
    if (character->iRace != W8_RACE_NONE) {
        const W8Trait* abilities = g_race_abilities[character->iRace].ability_ids;
        for (index = 0; index < 5; ++index) {
            if (abilities[index] == trait) {
                return true;
            }
        }
    }
    if (trait == W8_TRAIT_BREATHE &&
        character->enchantments[W8_ENCHANTMENT_DRACON_BREATH].turns != 0) {
        return true;
    }
    return false;
}

bool IsCharacterDefined(const W8Character& character)
{
    return character.iProfession < W8_PROFESSION_COUNT && character.iRace < W8_RACE_COUNT &&
           character.gender != W8_GENDER_UNSET;
}

static float ScaleValueByLevel(unsigned int level, float base)
{
    if (level > 0x14) {
        return base;
    }
    float scaled = (level * 2.0f + 60.0f) * base;
    return scaled * 0.01f;
}

/* Scale a trait's flat value by the character's level in their current
   profession: full value above twenty levels, sixty percent at zero and two
   percent per level in between. The trait id is carried by the call but the
   body never reads it. */
float ScaleValueByProfessionLevel(const W8Character* character, W8Trait, float base)
{
    return ScaleValueByLevel(character->profession_levels[character->iProfession], base);
}

float ScaleValueByMonsterLevel(const W8MonsterRecord* record, int, float base)
{
    return ScaleValueByLevel(record->effective_level, base);
}

void ApplyModifierBlock(W8GameplayModifierBlock* target, const W8GameplayModifierBlock* source)
{
    unsigned int index;

    target->damage_bonus += source->damage_bonus;
    target->hit_bonus += source->hit_bonus;
    target->attack_bonus += source->attack_bonus;
    target->damage_percent += source->damage_percent;
    target->armor_class_adjustment += source->armor_class_adjustment;
    target->armor_flat += source->armor_flat;
    target->armor_matchup += source->armor_matchup;
    target->damage_reduction_adjustment += source->damage_reduction_adjustment;
    target->resistance_bonus_all += source->resistance_bonus_all;
    target->damage_per_minute += source->damage_per_minute;
    target->health_regen_adjustment += source->health_regen_adjustment;
    target->stamina_regen_adjustment += source->stamina_regen_adjustment;
    target->spell_regen_adjustment += source->spell_regen_adjustment;
    for (index = 0; index < 7; ++index) {
        target->attribute_adjustments[index] += source->attribute_adjustments[index];
    }
    for (index = 0; index < 0x29; ++index) {
        target->skill_bonus[index] += source->skill_bonus[index];
    }
    for (index = 0; index < 6; ++index) {
        target->resistance_bonus[index] += source->resistance_bonus[index];
    }
    if (source->boost_health_regen != 0) {
        target->boost_health_regen = 1;
    }
    if (source->boost_stamina_regen != 0) {
        target->boost_stamina_regen = 1;
    }
    if (source->boost_spell_regen != 0) {
        target->boost_spell_regen = 1;
    }
    if (source->out_of_formation != 0) {
        target->out_of_formation = 1;
    }
    if (source->detect_secrets != 0) {
        target->detect_secrets = 1;
    }
    if (source->sight_override != 0) {
        target->sight_override = 1;
    }
    if (target->light < source->light) {
        target->light = source->light;
    }
    if (target->value < source->value) {
        target->value = source->value;
    }
    if (target->missile_deflect_chance < source->missile_deflect_chance) {
        target->missile_deflect_chance = source->missile_deflect_chance;
    }
}

/* Fold the worn items into one character's equipment bonus block: the twelve
   equipment slots are walked, the two alternate hand slots are skipped, the
   two hand slots contribute no attack values, and the six per-item
   resistance bytes are summed and clamped into the block's resistance run. */
void AccumulateEquipmentModifiers(const GameData& data, W8Character* character,
                                  W8GameplayModifierBlock* equipment_bonus)
{
    int resistance_totals[6] = {0};
    unsigned int slot;
    int index;

    for (slot = 0; slot < 12; ++slot) {
        int item_id = character->EquippedItem[slot].iItemNo;
        if (slot == 8 || slot == 9 || item_id == -1) {
            continue;
        }
        const W8ItemDatabaseRecord* record = &data.items[item_id];
        if (slot != 6 && slot != 7) {
            equipment_bonus->damage_bonus += record->attack_damage_bonus;
            equipment_bonus->hit_bonus += record->attack_hit_bonus;
        }
        equipment_bonus->health_regen_adjustment += record->health_regen_bonus;
        equipment_bonus->stamina_regen_adjustment += record->stamina_regen_bonus;
        equipment_bonus->spell_regen_adjustment += record->spell_regen_bonus;
        if (record->modifier_0b1_index != -1) {
            equipment_bonus->skill_bonus[record->modifier_0b1_index] += record->modifier_0b1_value;
        }
        if (record->modifier_0b3_index != -1) {
            equipment_bonus->attribute_adjustments[record->modifier_0b3_index] +=
                record->modifier_0b3_value;
        }
        for (index = 0; index < 6; ++index) {
            resistance_totals[index] += record->resistance_bonus[index];
        }
    }

    for (index = 0; index < 6; ++index) {
        int value = resistance_totals[index];
        if (value < -0x7c) {
            value = -0x7d;
        } else if (value > 0x7c) {
            value = 0x7d;
        }
        equipment_bonus->resistance_bonus[index] = static_cast<signed char>(value);
    }
}

static void RebuildCharacterModifierBlock(W8Character* character,
                                          const W8GameplayModifierBlock* party_modifiers)
{
    character->bonus = {};
    ApplyModifierBlock(&character->bonus, &character->equipment_bonus);
    ApplyModifierBlock(&character->bonus, &character->condition_modifiers);
    if (party_modifiers) ApplyModifierBlock(&character->bonus, party_modifiers);
}

/* Average the two attribute values g_skill_attributes names for every skill
   into the skill's 0x0a base level. 0x00557D80 and 0x00557B20 seed the
   profession skill levels from these. */
void InitializeSkillBaseLevels(W8Character* character)
{
    for (int index = 0; index < 0x29; ++index) {
        W8Attribute first = g_skill_attributes[index].attribute_1;
        W8Attribute second = g_skill_attributes[index].attribute_2;
        character->skills[index].base_level =
            (character->attributes[first].base + character->attributes[second].base) >> 1;
    }
}

/* Rebuild each attribute's effective value from its base and the modifier
   block's seven adjustment bytes, clamped to one through 125, and then every
   skill's base level from the attribute pair g_skill_attributes names. The
   complete values are available before equipment requirements are checked. */
// FUNCTION: WIZ8 0x005539e0
static void ResetCharacterAttributes(W8Character* character)
{
    unsigned int index;

    for (index = 0; index < 7; ++index) {
        int value =
            character->bonus.attribute_adjustments[index] + character->attributes[index].base;
        value = std::clamp(value, 1, 0x7d);
        character->attributes[index].effective = value;
    }
    InitializeSkillBaseLevels(character);
}

int GetEffectiveSkillLevel(const W8Character* character, W8Skill skill_id)
{
    int level = character->skills[skill_id].points;
    if (skill_id == g_profession_bonus_skills[character->iProfession]) {
        unsigned int bonus = static_cast<unsigned int>(level * 0x19) / 100;
        if (bonus == 0) {
            bonus = 1;
        }
        level += bonus;
    }
    level += character->bonus.skill_bonus[skill_id];
    level = std::clamp(level, 0, 0x7d);
    return level;
}

/* Rebuild every skill level from the value already spent on it, the
   profession's bonus skill and the race and profession adjustment bytes the
   modifier block carries, clamped to zero through 125. Equipment requirements are checked after all skills have been evaluated. */
// FUNCTION: WIZ8 0x00553a60
static void ResetCharacterSkills(W8Character* character)
{
    unsigned int index;

    for (index = 0; index < 0x29; ++index) {
        character->skills[index].level =
            GetEffectiveSkillLevel(character, static_cast<W8Skill>(index));
    }
}

/* Set the wield kind for one primary hand from the item it holds. An empty
   hand wields nothing; otherwise the item's equipment class selects the kind,
   with class two in the off hand deferring to the primary hand's item unless
   that item carries the paired flag. */
void SetHandType(const GameData& data, W8Character* character, W8EquipSlot slot)
{
    if (character == 0) {
        throw std::logic_error("SetHandType needs a character");
    }
    if (static_cast<unsigned int>(slot) > W8_EQUIP_SLOT_FEET) {
        throw std::logic_error("SetHandType: the equipment slot is out of range");
    }
    int hand;
    if (slot == W8_EQUIP_SLOT_PRIMARY_WEAPON) {
        hand = 0;
    } else {
        if (slot != W8_EQUIP_SLOT_SECONDARY_WEAPON) {
            throw std::logic_error("SetHandType: the equipment slot is not a weapon hand");
        }
        hand = 1;
    }
    character->Hand[hand].uiHolds =
        WeaponHandKind(data, character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON],
                       character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON], hand);
}

void EvaluateCharacterInputs(const GameData& data, W8Character& character,
                             const W8GameplayModifierBlock* party_modifiers)
{
    character.uiExpLevel = std::min(character.uiExpLevel, 50u);
    for (auto& level : character.profession_levels)
        if (static_cast<unsigned int>(level) > 50) level = 50;
    character.equipment_bonus = {};
    AccumulateEquipmentModifiers(data, &character, &character.equipment_bonus);
    RebuildCharacterModifierBlock(&character, party_modifiers);
    ResetCharacterAttributes(&character);
    ResetCharacterSkills(&character);
    SetHandType(data, &character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    SetHandType(data, &character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
}
