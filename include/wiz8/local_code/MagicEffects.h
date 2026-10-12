#pragma once

#include <cstdint>
#include "wiz8/local_code/Factions.h"

#include "wiz8/layouts/gameplay_databases.h"

#include "wiz8/monster_spell_icons.h"
#include "wiz8/conditions.h"
#include "surrender/srMath.h"
#include "wiz8/dice.h"
#include "wiz8/local_code/SpellEffect.h"

struct W8CombatSlot;
struct W8MonsterInfo;
struct W8TargetSource;
struct W8SpellEffectEntry;
struct W8Character;
struct W8EffectSlot;
struct W8MonsterInfo;

std::uint32_t RollEffectMagnitude(W8SpellEffectDefinition* definition);
std::uint32_t RollEffectDuration(W8SpellEffectDefinition* definition);
W8EnchantmentSlot GetConditionDisplaySlot(std::int32_t spell_id);

bool IsScreenBusy(void);

/* Shrink a rolled magnitude by the share of it the target's
   resistance in the realm turns aside; a permanent magnitude is left alone. */
void ReduceMagnitudeByResistance(std::uint32_t* magnitude, W8CombatSlot* target, W8SpellRealm realm,
                                 std::int32_t power_level);
void AnnounceEffectResisted(W8CombatSlot* target);
void ClearMonsterCharm(W8MonsterInfo* monster_info);
void ApplyEffectAndAnnounce(std::uint32_t* result, W8CombatSlot* target, W8SpellRealm realm,
                            std::int32_t power_level);
bool ResolveAttackOnTarget(const W8TargetSource* source, W8CombatSlot* target,
                           W8Condition condition_id, W8SpellRealm realm, std::uint32_t power_level,
                           std::int32_t argument, std::int32_t magnitude, bool announce_resistance,
                           bool announce_condition, std::int32_t duration);
/* The saving throw against a condition. A dead target is beyond
   reach and counts as resisting. */
bool TargetResistsCondition(W8CombatSlot* target, W8SpellRealm realm, std::uint32_t power_level,
                            W8Condition condition_id);
/* Land a condition whose saving throw failed. `source_character`
   is part of the call but nothing in the body reads it. */
char InflictConditionOnTarget(W8CombatSlot* target, W8Condition condition_id, W8SpellRealm realm,
                              std::uint32_t power_level, std::int32_t argument, std::uint32_t magnitude,
                              std::int32_t source_character, std::int32_t duration, bool announce);
/* Each scales the value by seven fifths or three fifths on the easy and hard settings and leaves
   it alone on normal; which way round depends on the character's condition
   thirteen or the monster's allegiance flag. */
void ScaleValueForCharacterDifficulty(std::int32_t party_slot, std::int32_t* value);
void ScaleValueForMonsterDifficulty(W8MonsterInfo* monster_info, std::int32_t* value);
void ProcessSpellEffectTargets(W8SpellEffectEntry* effect);
void FinishSpellEffectTargets(W8SpellEffectEntry* effect);
/* The queued-effect helpers ProcessSpellEffectTargets dispatches to. */
char TryCureConditionOnTargets(W8SpellEffectEntry* effect, W8Condition condition, bool force);
void ApplyConditionToTargets(W8SpellEffectEntry* effect, W8EnchantmentSlot slot);
void ApplyRandomAfflictionToTarget(W8SpellEffectEntry* effect);
void ReportSpellEffectResult(W8SpellEffectEntry* effect);
void ApplyDamageToTargets(W8SpellEffectEntry* effect);
void DrainTargetsLife(W8SpellEffectEntry* effect);
char HealTargets(W8SpellEffectEntry* effect);
char RestoreTargetsStamina(W8SpellEffectEntry* effect);
void FatigueTargets(W8SpellEffectEntry* effect);
void InflictConditionAttack(W8SpellEffectEntry* effect, W8Condition condition, std::int32_t chance,
                            std::int32_t argument);
/* The target's own turns left on a condition; condition seven
   also hands its argument back through `argument`. */
std::uint32_t GetTargetConditionTurns(W8SpellEffectEntry* effect, W8Condition condition,
                                     std::int32_t* argument);
/* Heading from a world point toward the nearest live monster, or
   the camera-facing yaw for a hostile disposition - also the fallback when
   no monster is near. */
float HeadingTowardNearestMonster(srVector3T<float> point, W8Disposition disposition,
                                  std::int32_t exclusion);

/* Eight bytes per effect id; the leading dword names the HUD
   effect icon and the second the monster visual resource, -1 means none. */
struct W8EffectVisual {
    std::int32_t hud_icon;
    W8MonsterSpellIconId monster_icon;
};
extern W8EffectVisual g_effect_visual_table[150];

void TickCombatEffectSlots(W8EffectSlot* slots, W8CombatSlot* target);
void TickRadiusBlastEffectSlots(W8EffectSlot* slots);
void ApplyMonsterControlToNearbyMonsters(W8SpellEffectEntry* effect);
void ClearEffectSlot(W8MonsterInfo* monster_info, W8EffectSlot* slot);
void ResetPartyEffectBlock(W8EffectSlot* slot);

void ResetCombatEffects(void);
void RecalculateCharacterResistances(W8Character* character);

/* Enchantment- and flat-amount damage applied outside the announced attack
   path: each rolls or takes its amount, runs it through the target's damage
   reduction, applies it, and feeds the running combat totals and the pending
   damage-report queue inside g_combat_state. */
void ApplyDiceDamageToCharacter(std::int32_t party_slot, W8TargetSource* source, W8Enchantment* enchantment);
void ApplyDiceDamageToMonster(W8MonsterInfo* monster_info, W8TargetSource* source,
                              W8Enchantment* enchantment);
void ApplyDirectDamageToCharacter(std::int32_t party_slot, W8TargetSource* source, std::int32_t damage);
void ApplyDirectDamageToMonster(W8MonsterInfo* monster_info, W8TargetSource* source, std::int32_t damage);
