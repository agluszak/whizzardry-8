#pragma once
#include <bitset>

#include "wiz8/data/skills.h"

struct GameData;
class Game;
struct W8Character;
struct W8Party;
struct W8SkillNoticePayload;

/* 0x00553F10: add usage points to one skill, roll its increase, and report
   the result; suppress_notification asks for the increase notice to wait. */
void PracticeCharacterSkill(Game& game, W8Character* character, W8Skill skill_id, int usage_points,
                            bool suppress_notification);

unsigned int GetCharacterSkillNoticeValue(const W8Character* character, W8Skill skill_id);
void PostSkillIncreaseNotices(Game& game, const W8SkillNoticePayload& notices);

/* Show a member's skill increase as a portrait message now, or keep it in the slot's
   per-skill flags for FlushDeferredSkillNotices. */
void AnnounceSkillIncrease(Game& game, unsigned int party_slot, W8Skill skill_id, bool now);
/* 0x005542E0: drain the deferred per-slot skill-increase flags into one or
   more queued skill-notice portrait messages. */
void FlushDeferredSkillNotices(Game& game);

/* 0x00548E60: the alchemist's brew - rolls a level-banded item for a character
   with the MAKE_POTIONS trait, then re-arms the brew cooldown. */
void BrewAlchemistPotion(Game& game, W8Character* character);
/* 0x00547BF0: whether the slot has the priest/bishop turn-undead trait, its
   combat-state use flag is clear, and a live hostile undead monster (record
   kind 0x14) is in play. */
bool CanPartySlotTurnUndead(int party_slot);
/* 0x00547F40: whether the slot has the priest's pray trait, its combat-state
   use flag is clear, and a live hostile monster is in play. */
bool CanPartySlotPray(int party_slot);
bool IsCharacterSkillAvailable(W8Character* character, W8Skill skill_id,
                               const bool* expert_realm_flags);
void InvalidateAndRecalculateCharacterClassData(W8Character* character);
int RevealCharacterItemBindingsByProfession(const W8Party& party, int party_slot,
                                            unsigned int target_slot);
/* 0x00553AD0: propagate a changed attribute base value - the at-maximum
   pseudo-skill flag, the effective value, equipment and derived state. */
void ApplyAttributeChange(Game& game, W8Character* character, W8Attribute attribute);
/* 0x00553C10: propagate a changed skill investment - availability rescan,
   the level rebuild, equipment and derived state. */
void ApplySkillChange(Game& game, W8Character* character, W8Skill skill_id);
// Update native availability and return the skill rows that changed.
std::bitset<W8_SKILL_COUNT> RefreshCharacterSkillAvailability(const GameData& data,
                                                              W8Character* character);
unsigned int GetSkillQuarterValue(W8Character* character, W8Skill skill_id);
