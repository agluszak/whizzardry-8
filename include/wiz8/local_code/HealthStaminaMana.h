#pragma once

#include <cstdint>
#include "wiz8/layouts/gameplay_databases.h"


#include "surrender/srMath.h"
#include "wiz8/layouts/targeting.h"

class W8Monster;
struct W8Character;
struct W8CombatSlot;
struct W8Dice;
struct W8MonsterInfo;
struct W8SpellEffectResult;

W8Character* FindPartyMemberWithLowestResistance4(void);
std::uint32_t FindPartySlotWithLowestHitPoints(void);
std::uint32_t FindPartySlotWithLowestSpellPoints(void);

void HealCharacter(std::int32_t party_slot, std::int32_t amount, bool announce);
void RestoreCharacterStamina(std::int32_t party_slot, std::int32_t amount, bool announce);
void DrainCharacterSpellPoints(std::int32_t party_slot, std::uint32_t amount, bool announce);
void RestoreCharacterSpellPointsEvenly(std::int32_t party_slot, std::int32_t amount);
void RestoreCharacterRealmSpellPoints(std::int32_t party_slot, W8SpellRealm realm, std::int32_t amount);
void FatigueCharacter(std::int32_t party_slot, std::int32_t amount, bool scale_by_load,
                      W8SpellEffectResult* report_to);
/* Run one queued fatigue op. The op is a combat slot: a
   character target fatigues that party slot, a monster target resolves the
   monster from its location id. The third argument is unused. */
void ApplyQueuedFatigue(W8CombatSlot* op, std::uint32_t amount, std::int32_t);
std::uint32_t CharacterActionFatigueCost(std::int32_t party_slot, W8ActionKind action_kind);
void DamageCharacter(std::int32_t party_slot, std::uint32_t damage, bool announce);
void DrainCharacterRealmSpellPoints(std::int32_t party_slot, W8SpellRealm realm, std::uint32_t amount,
                                    bool announce);
void DrainPartySpellPoints(std::int32_t amount, bool announce);
std::int32_t CalculateMonsterFatigueBand(std::int32_t current, std::int32_t maximum);
std::uint32_t FatigueArmorPenalty(std::int32_t fatigue_band);
std::int32_t SpellCastFatigueCost(std::int32_t spell_id, std::int32_t result);
void SpendCharacterSpellPoints(std::int32_t party_slot, W8SpellRealm realm, std::int32_t amount);
std::int32_t MonsterActionFatigueCost(const W8MonsterInfo* monster_info);
void FatigueMonster(W8MonsterInfo* monster_info, std::uint32_t amount,
                    W8SpellEffectResult* report_to);
void HealMonster(W8MonsterInfo* monster_info, std::uint32_t amount, bool announce);
void RestoreMonsterStamina(W8MonsterInfo* monster_info, std::int32_t amount, bool announce);
/* The monster-side effect application pass the aging producer
   drives for both sign directions. The result block, when given, collects
   the damage dealt; the applied amount is also returned. `quiet` marks
   non-provoking damage such as a poison tick: it picks the notice strings,
   skips the condition-target bookkeeping, and silences the struck reaction. */
std::uint32_t ApplyDamageToMonster(W8MonsterInfo* monster_info, std::uint32_t amount,
                                  struct W8TargetSource* source, bool quiet,
                                  std::uint8_t in_combat, char a,
                                  W8SpellEffectResult* result_stats, bool c);
/* The character-side counterpart - damage absorbed by the
   slot-2 enchantment first, two thirds of the rest fatigue the character, the
   remainder comes off hit points and can kill. The sixth parameter receives a
   kill-counting result block.
   `quiet` marks non-provoking damage such as a poison tick: it picks the
   shield/notice strings and the poison suffix, and keeps a sleeping character
   asleep. `announce` gates the damage notice, `short_notice` selects the terse
   ShowNotice form over the verbose PostCharacterNotice one, and `detailed`
   includes the character name in the message. */
std::uint32_t ApplyDamageToCharacter(std::int32_t party_slot, std::uint32_t amount, bool quiet, bool announce,
                                    bool short_notice, W8SpellEffectResult* result_stats,
                                    bool detailed);
extern char g_poison_suffix[];
/* How a monster answers being struck - the struck cycle, a
   possible condition knock-on, and the hostility check toward the attacker. */
void MonsterReactsToBeingStruck(W8MonsterInfo* monster_info, W8TargetSource* attacker, bool quiet);
void CharacterDies(std::int32_t party_slot);
void ApplyRolledHealthChangeToParty(const W8Dice* dice, W8SpellEffectResult* result, std::int32_t announce);
/* Roll the dice once for every live monster within the radius of
   a point and apply each roll as damage. */
void DamageMonstersInRadius(const srVector3T<float>& center, float radius, const W8Dice* dice,
                            struct W8TargetSource* source, W8SpellEffectResult* result);
void RestorePartyStaminaByDice(std::uint8_t count, std::uint8_t sides, std::int16_t base);
void HealPartyByDice(std::uint8_t count, std::uint8_t sides, std::int16_t base);
void RestorePartySpellPoints(std::int32_t amount);
void RecalculateCharacterHitPoints(W8Character* character);
std::int32_t __cdecl CompareSpellPointDeficits(const void* first, const void* second);
std::int32_t SumCharacterSpellPoints(const W8Character* character);
std::int32_t SumCharacterSpellPointsLeft(const W8Character* character);
std::int32_t GetCharacterRealmSpellPoints(const W8Character* character, W8SpellRealm realm);
void RecalculateCharacterStamina(W8Character* character);
void RecalculateRealmSpellPoints(W8Character* character);
std::int32_t RebuildRealmSpellPointCeilings(W8Character* character);
