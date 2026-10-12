#pragma once

#include <cstdint>
#include <string_view>

#include "wiz8/layouts/targeting.h"

struct W8MonsterRecord;

/* A friendly NPC's combat-entry script notice is still owed. */

void ChooseAction(std::int32_t party_slot, W8ActionKind action, std::int32_t detail, const W8ActionDetailBlock* data,
                  bool defer_execution, std::int32_t notify);
void ChooseCombatAction(std::int32_t party_slot, W8TargetingContext context, W8ActionKind* out_kind,
                        std::int32_t* out_action, W8CombatSlot** out_target,
                        W8ActionDetailBlock** out_detail);
void SetCharacterCombatAction(std::int32_t party_slot, W8ActionKind action_kind, std::int32_t action_detail,
                              const W8ActionDetailBlock* data, std::int32_t notify);
void EndCombat(bool forced_cleanup);
void BeginCombatExecution(void);
void AssignCombatPhases(void);
void UpdateCombat(void);
void SwitchCharacterTo(std::int32_t party_slot, W8ActionKind action);
void ApplyCombatEndEffects(void);
bool CombatHasContinuingEffects(void);
/* Whether continuous-combat stance may advance past the pending
   NPC-script / engagement gate. */
bool CombatMayAdvanceContinuously(void);
bool QueueNpcCombatScript(void);
/* The combat turn scheduler - picks the next party slot or
   monster whose action phase arrived, starts party-movement phases, arms the
   action pacing clock, and rolls the round counter forward until someone
   can act or the round ends. */
void ScheduleCombatActor(void);
std::int32_t CheckCombatEnd(std::uint32_t end_if_no_hostiles);
void AdvanceCombatRound(void);
void RollCombatSurprise(char party_cannot_be_surprised);
/* Enter combat mode; queues a friendly NPC's combat-entry script
   notice when one is still owed and declines while dialogue or a script event
   defers it. */
bool StartCombat(std::int32_t surprise);
struct W8Character;
struct W8MonsterInfo;
struct W8CombatCharacterRow;

void CatchUpCombatActor(W8CombatCharacterRow* row);
bool AnyCharacterEngaged(void);
/* Whether the character knows any spell whose realm's sp_left
   still covers its spell_point_cost. */
bool CharacterHasCastableSpell(W8Character* character);
void PointCameraAtCombatTarget(W8TargetSource* source, W8CombatSlot* target);
/* Run one party slot's committed combat action - commit the
   chosen block, check conditions that can redirect or cancel it, dispatch on
   the action kind, and charge the fatigue. */
void ExecuteCharacterAction(std::int32_t party_slot);
/* Run one monster's committed combat action - the monster-side
   counterpart of ExecuteCharacterAction. */
void ExecuteMonsterAction(W8MonsterInfo* monster_info, W8MonsterRecord* record);
/* Pick the hand the slot's attack lands with this round and
   derive the row's next action phase from the stored attack values. */
void ComputeCharacterActionPhase(std::int32_t party_slot);
/* Set one monster's turn up, once - phase and speed by action. */
void SetUpMonsterTurn(W8MonsterInfo* monster_info);
/* The condition interrupt that fires before the actor's action;
   returns the interrupt kind, or -1 when none fires. */
std::int32_t GetConditionInterrupt(W8TargetSource* source);
/* The monster's breathe/special-attack action step. */
bool MonsterFleeAction(W8MonsterInfo* monster_info, W8MonsterRecord* record);
/* Run the monster's committed special attack against its marker
   lists and every monster hostile to it; returns the action outcome code. */
std::int32_t ExecuteMonsterSpecialAttack(W8MonsterInfo* monster_info, W8MonsterRecord* record);
/* The character's breathe/special-attack action step. */
bool CreateCharacterBreathEffect(std::int32_t party_slot);
/* The character's committed breath attack against the marker
   lists plus every hostile monster; returns the action outcome code. */
std::int32_t ExecuteCharacterSpecialAttack(std::int32_t party_slot);
void ApplyPartyCombatAction(std::int32_t party_slot, W8ActionKind action, std::int32_t detail,
                            const W8ActionDetailBlock* data, std::int32_t notify);
std::int32_t IsPartyEngaged(void);
W8PartyAction GetEffectivePartyAction(void);
void RecordCharacterDeath(std::int32_t party_slot);
void DropCharacterFromRound(std::int32_t party_slot);
/* Whether one party slot may switch to the given targeting
   context, in the two forms the target-refresh pass asks. */
bool CharacterCanSwitchTo(std::int32_t party_slot, W8TargetingContext context, bool allow_missing_target,
                          bool allow_equipment_changes);
bool TryCharacterAction(std::int32_t party_slot, W8ActionKind action, bool commit);
void NotifyNearbyMonsters(std::int32_t what);
void CombatLog(std::string_view text);
void BeginCombatRound(void);
void EndMonsterTurn(W8MonsterInfo* monster_info);
void EndMonsterAttack(W8MonsterInfo* monster_info);
/* Face the monster toward whatever its combat slot targets before
   the attack starts; `alternate` picks the immediate versus animated turn. */
void OrientMonsterTowardTarget(W8MonsterInfo* monster_info, bool alternate);
void AimMonsterBreathAtTarget(W8MonsterInfo* monster_info);
void SetSlotAction(std::int32_t party_slot, W8ActionKind action_kind, std::int32_t action_detail);
bool CanCharReBreathe(std::int32_t party_slot);
bool TryPanicWoundedCharacter(const W8CombatSlot* target);
std::int16_t GetCombatActionProgress(std::int32_t* out_total);
