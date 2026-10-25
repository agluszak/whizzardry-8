#pragma once

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/records.h"
#include "wiz8/rules/targeting.h"

class Monster;
class Rng;
struct MonsterAction;
struct DamageOptions;
struct W8Character;
struct W8CombatSlot;
struct W8Dice;
struct W8MonsterInfo;
struct W8SpellEffectResult;

unsigned int FindPartySlotWithLowestHitPoints(void);
unsigned int FindPartySlotWithLowestSpellPoints(void);

void HealCharacter(int party_slot, int amount, bool announce);
void RestoreCharacterStamina(int party_slot, int amount, bool announce);
void DrainCharacterSpellPoints(int party_slot, unsigned int amount, bool announce);
void RestoreCharacterSpellPointsEvenly(int party_slot, int amount);
void RestoreCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, int amount);

/* Run one queued fatigue op. The op is a combat slot: a
   character target fatigues that party slot, a monster target resolves the
   monster from its location id. The third argument is unused. */
void ApplyQueuedFatigue(W8CombatSlot* op, unsigned int amount, int);
unsigned int CharacterActionFatigueCost(int party_slot, W8ActionKind action_kind);
void DamageCharacter(int party_slot, unsigned int damage, bool announce);
void DrainCharacterRealmSpellPoints(int party_slot, W8SpellRealm realm, unsigned int amount,
                                    bool announce);
void DrainPartySpellPoints(int amount, bool announce);
int SpellCastFatigueCost(int spell_id, int result);
void SpendCharacterSpellPoints(int party_slot, W8SpellRealm realm, int amount);
int MonsterActionFatigueCost(Rng& rng, MonsterAction action, bool diseased);
void HealMonster(W8MonsterInfo* monster_info, unsigned int amount, bool announce);
void RestoreMonsterStamina(W8MonsterInfo* monster_info, int amount, bool announce);
// Returns the post-shield magnitude, including overkill, not merely HP removed.
unsigned int ApplyDamageToMonster(W8MonsterInfo& monster, unsigned int amount,
                                  const W8TargetSource& source, DamageOptions options,
                                  W8SpellEffectResult* result_stats = nullptr);

extern char g_poison_suffix[];
/* How a monster answers being struck - the struck cycle, a
   possible condition knock-on, and the hostility check toward the attacker. */
void MonsterReactsToBeingStruck(W8MonsterInfo* monster_info, const W8TargetSource* attacker,
                                bool quiet);
void ApplyRolledHealthChangeToParty(const W8Dice* dice, W8SpellEffectResult* result, int announce);
/* Roll the dice once for every live monster within the radius of
   a point and apply each roll as damage. */
void DamageMonstersInRadius(const glm::vec3& center, float radius, const W8Dice* dice,
                            struct W8TargetSource* source, W8SpellEffectResult* result);
void RestorePartyStaminaByDice(unsigned char count, unsigned char sides, short base);
void HealPartyByDice(unsigned char count, unsigned char sides, short base);
void RestorePartySpellPoints(int amount);
int SumCharacterSpellPoints(const W8Character* character);
int SumCharacterSpellPointsLeft(const W8Character* character);
int GetCharacterRealmSpellPoints(const W8Character* character, W8SpellRealm realm);
