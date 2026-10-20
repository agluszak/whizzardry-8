#pragma once

#include "wiz8/data/skills.h"
#include "wiz8/rules/equipment_slots.h"

/* The inputs every derived statistic reads: traits, the modifier blocks, effective
   attributes, skill levels and hand kinds. */

struct GameData;
struct W8Character;
struct W8GameplayModifierBlock;
struct W8MonsterRecord;

bool CharacterHasTrait(const W8Character* character, W8Trait trait);
// Profession, race and gender are set; nothing is derived for a character without them.
bool IsCharacterDefined(const W8Character& character);
float ScaleValueByProfessionLevel(const W8Character* character, W8Trait trait, float base);
float ScaleValueByMonsterLevel(const W8MonsterRecord* record, int trait, float base);

/* Add one modifier block into another: the byte fields sum, the six flags
   latch, and the light and max-combined bytes keep the larger source. */
void ApplyModifierBlock(W8GameplayModifierBlock* target, const W8GameplayModifierBlock* source);
// Fold the worn items' bonuses into one character's equipment bonus block.
void AccumulateEquipmentModifiers(const GameData& data, W8Character* character,
                                  W8GameplayModifierBlock* equipment_bonus);
/* Rebuild one character's derived modifier block from the equipment,
   condition and party blocks without rerunning the derived stats. */
void RebuildCharacterModifierBlock(W8Character* character,
                                   const W8GameplayModifierBlock* party_modifiers);

void InitializeSkillBaseLevels(W8Character* character);
// The skill's points with the profession bonus and the modifier block's adjustment.
int GetEffectiveSkillLevel(const W8Character* character, W8Skill skill_id);
// Set the wield kind of one hand from the items both hands hold.
void SetHandType(const GameData& data, W8Character* character, W8EquipSlot equip_slot);

// Rebuild modifier blocks, effective attributes, skill levels and hand kinds from the
// character's equipment, conditions and the supplied party modifiers.
void EvaluateCharacterInputs(const GameData& data, W8Character& character,
                             const W8GameplayModifierBlock* party_modifiers);
