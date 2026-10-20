#pragma once

#include <string>

#include "wiz8/data/skills.h"

struct W8Character;
struct W8SkillNoticePayload;
struct W8MonsterRecord;

/* 0x00553F10: add usage points to one skill, roll its increase, and report
   the result; suppress_notification keeps silent practice calls silent. */
void PracticeCharacterSkill(W8Character* character, W8Skill skill_id, int usage_points,
                            bool suppress_notification);

unsigned int GetCharacterSkillNoticeValue(W8Character* character, W8Skill skill_id);
void PostSkillIncreaseNotices(const W8SkillNoticePayload* notices);

/* 0x00554170: append one "race-icon Name's skill +level" clause to a notice
   buffer; when continue_line is set, insert a line break first. */
std::string FormatSkillIncreaseNoticeText(int party_slot, bool continue_line, W8Skill skill_id);

/* 0x005542E0: drain the deferred per-slot skill-increase flags into one or
   more queued skill-notice portrait messages. */
void FlushDeferredSkillNotices(void);
/* 0x0068506D: a deferred skill-increase notice is queued for the text box. */

/* 0x00547A50: the cheat-death trait's revival - notice, unconsciousness, and
   hit points rolled back up from a profession-level-scaled share. */
void CheatDeathRevive(int party_slot);
/* 0x00548E60: the alchemist's brew - rolls a level-banded item for a character
   with the MAKE_POTIONS trait, then re-arms the brew cooldown. */
void BrewAlchemistPotion(W8Character* character);
/* 0x00547BF0: whether the slot has the priest/bishop turn-undead trait, its
   combat-state use flag is clear, and a live hostile undead monster (record
   kind 0x14) is in play. */
bool CanPartySlotTurnUndead(int party_slot);
/* 0x00547F40: whether the slot has the priest's pray trait, its combat-state
   use flag is clear, and a live hostile monster is in play. */
bool CanPartySlotPray(int party_slot);
float ScaleValueByProfessionLevel(W8Character* character, W8Trait trait, float base);
float ScaleValueByMonsterLevel(W8MonsterRecord* record, int trait, float base);
bool IsCharacterSkillAvailable(W8Character* character, W8Skill skill_id,
                               const bool* expert_realm_flags);
void InvalidateAndRecalculateCharacterClassData(W8Character* character);
bool CharacterHasTrait(const W8Character* character, W8Trait trait);
int RevealCharacterItemBindingsByProfession(int party_slot, unsigned int target_slot);
/* 0x00553AD0: propagate a changed attribute base value - the at-maximum
   pseudo-skill flag, the effective value, equipment and derived state. */
void ApplyAttributeChange(W8Character* character, W8Attribute attribute);
/* 0x00553C10: propagate a changed skill investment - availability rescan,
   the level rebuild, equipment and derived state. */
void ApplySkillChange(W8Character* character, W8Skill skill_id);
void InitializeSkillBaseLevels(W8Character* character);
void RefreshCharacterSkillAvailability(W8Character* character);
unsigned int GetSkillQuarterValue(W8Character* character, W8Skill skill_id);

// Profession, race and gender are set; nothing is derived for a character without them.
bool IsCharacterDefined(const W8Character& character);
struct W8GameplayModifierBlock;
// Rebuild modifier blocks, effective attributes, skill levels and hand kinds from the
// character's equipment, conditions and the supplied party modifiers.
void EvaluateCharacterInputs(W8Character&, const W8GameplayModifierBlock*);
