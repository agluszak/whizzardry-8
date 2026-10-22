#pragma once

#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"

/* The constant profession, race, skill and portrait tables. */

enum W8SkillImportPolicy {
    W8_SKILL_IMPORT_POLICY_0 = 0,
    W8_SKILL_IMPORT_POLICY_1 = 1,
    W8_SKILL_IMPORT_PROFESSION = 2,
    W8_SKILL_IMPORT_DISABLED = 3,
};

struct W8SkillAttributes {
    W8SkillCategory category;
    W8SkillImportPolicy import_policy;
    /* Retail offset 0x08/0x0c: governing attributes averaged into base_level and
       listed in the stat-info dialog. */
    W8Attribute attribute_1;
    W8Attribute attribute_2;
};

extern const W8RaceResistanceProfile g_race_resistance_profiles[];
extern const int g_profession_skill_availability[0x29][W8_PROFESSION_COUNT];
extern const W8Skill g_profession_bonus_skills[W8_PROFESSION_COUNT];
extern const W8SkillAttributes g_skill_attributes[0x29];

/* Profession and race trait sets consulted by CharacterHasTrait. Each entry is
   only its id list: three profession abilities, five race abilities. */
struct W8ProfessionAbilitySet {
    W8Trait ability_ids[3];
};

struct W8RaceAbilitySet {
    W8Trait ability_ids[5];
};

extern const W8ProfessionAbilitySet g_profession_abilities[W8_PROFESSION_COUNT];
extern const W8RaceAbilitySet g_race_abilities[W8_RACE_COUNT];
extern const W8Skill g_profession_skills[W8_PROFESSION_COUNT][4];
extern const int g_profession_magic_level_offsets[W8_PROFESSION_COUNT];
extern const float g_profession_hit_point_factors[W8_PROFESSION_COUNT];

enum class W8PortraitMirrorSide : unsigned char { none, left, right };

struct W8PortraitDescriptor {
    int group;
    int unknown_004;
    int unknown_008;
    W8PortraitMirrorSide mirror_side;
};

extern const W8PortraitDescriptor g_portrait_descriptors[80];

/* Message ids naming what character sheets, notices and dialogs print about a
   character: attributes by attribute id, skills by skill id. */
extern const unsigned short g_character_description_first_ids[22];
extern const unsigned short g_character_skill_name_ids[84];
/* One row per gender; the third entry is the possessive item notices print. */
extern const unsigned short g_gender_name_message_rows[4][4];
// The inline text colour of a member's notices, by marching-order position.
unsigned char GetPartyOrderTextColor(signed char party_order);
