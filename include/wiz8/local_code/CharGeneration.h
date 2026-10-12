#pragma once

#include <cstdint>
#include "wiz8/character_skills.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "compat/compiler.h"

struct W8Character;

/* CharGeneration.cpp's transient editing state, embedded by the Character
   screen and passed through its four pages and commit helpers. */
struct W8CharacterCreationState {
    std::int32_t attribute_points_remaining;
    std::int32_t attribute_points_total;
    std::int32_t attribute_values[7];
    std::int32_t attribute_step_limit;
    std::int32_t attribute_limits[7];
    bool attributes_complete; /* set once every attribute choice validates */
    std::uint8_t padding_045[3];
    std::int32_t attribute_baselines[7];
    std::int32_t skill_points_remaining;
    std::int32_t skill_points_total;
    std::int32_t skill_points_spent[0x29];
    std::int32_t skill_step_limit;
    std::int32_t skill_limits[0x29];
    bool skills_complete; /* set once every skill choice validates */
    std::uint8_t padding_1b9[3];
    /* The per-skill counterpart of attribute_baselines. The
       level-up reset zeroes it, and a profession change refunds whatever each
       entry holds before the new profession's skill points are assigned. */
    std::int32_t skill_baselines[0x29];
    std::int32_t spell_points_remaining;
    std::int32_t spell_points_total;
    std::int32_t magic_skill_bonus;
    bool spells_complete;
    std::uint8_t padding_26d[3];
};

void InitializeCharacterCreation(W8Character*, W8CharacterCreationState*);
void InitializeCharacterLevelUp(W8Character*, W8CharacterCreationState*);
void ApplyRaceProfessionTables(W8Character*, W8CharacterCreationState*);
void RebuildLevelUpPoolsForProfession(W8Character*, W8CharacterCreationState*, W8Profession);
void SetCharacterRace(W8Character*, W8CharacterCreationState*, W8Race);
void SetCharacterGender(W8Character*, W8CharacterCreationState*, W8Gender);
void PayDownAttributeDebt(W8Character*, W8CharacterCreationState*);
void DetermineEligibleProfessions(W8Character*, W8CharacterCreationState*, std::uint8_t*);
void FinalizeCreatedCharacter(W8Character*, W8CharacterCreationState*, bool);
/* Hand out the starting equipment table the race or profession
   selects, then the profession's own extra item. */
void AddCharacterStartingEquipment(W8Character*);
/* The six starting item ids each profession hands out, with the faerie
   race's own row last; -1 is an empty slot. */
extern std::int32_t g_starting_equipment[0x10][6];
void RecomputeAttributeLimits(W8Character*, W8CharacterCreationState*);
void ClampAttributesToBudget(W8Character*, W8CharacterCreationState*);
void ApplyProfessionMinimumAttributes(W8Character*, W8CharacterCreationState*);
void AdjustAllocatedAttribute(W8Character*, W8CharacterCreationState*, W8Attribute, std::int32_t);
void RefundAllocatedAttributes(W8Character*, W8CharacterCreationState*);
void RecomputeSkillLimits(W8Character*, W8CharacterCreationState*);
void InitializeLevelUpAttributePool(W8Character*, W8CharacterCreationState*, std::uint32_t, std::int32_t);
void ResetSkillContribution(W8Character*, W8CharacterCreationState*, W8Skill);
void RefundSkillAllocation(W8Character*, W8CharacterCreationState*, W8Skill);
void RebuildSkillAllocations(W8Character*, W8CharacterCreationState*);
void ClampSkillsToBudget(W8Character*, W8CharacterCreationState*);
void RefundAllSkillPoints(W8Character*, W8CharacterCreationState*);
void FinalizeSpellPointPool(W8Character*, W8CharacterCreationState*);
std::int32_t CountRemainingSpellPoints(W8Character*, W8CharacterCreationState*);
std::int32_t ComputeLevelUpSpellPointAward(W8Character*, W8CharacterCreationState*);
void SelectCreationSpell(W8Character*, W8CharacterCreationState*, std::uint32_t spell);
void DeselectCreationSpell(W8Character*, W8CharacterCreationState*, std::uint32_t spell);
void ResetSpellSelections(W8Character*, W8CharacterCreationState*);
std::int32_t ComputeRealmSkillDebt(W8Character* original, W8Character* edited);
std::int32_t ComputeStartingEquipmentCost(W8Character*);
bool CanAffordStartingEquipment(W8Character*);
