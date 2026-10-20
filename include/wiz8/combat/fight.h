#pragma once

#include <string_view>

#include "wiz8/rules/targeting.h"

class Game;

void ChooseAction(int party_slot, W8ActionKind action, int detail, const W8ActionDetailBlock* data,
                  bool defer_execution, int notify);

void SetCharacterCombatAction(int party_slot, W8Action action, int notify);
/* In combat, a member's committed action follows a change of the weapons in hand: an
   attack keeps a reachable target, and the alternate-hand toggle flips so the
   hand-attack values are rebuilt. Outside combat it does nothing. */
void ReconcileCombatHandChange(Game& game, unsigned int party_slot);
/* Run a party slot's action, in a fight (its committed choice, when its turn comes) or in
   the world (its executing action): a condition interrupt can cancel or redirect it
   first; the action kind is dispatched and the fatigue charged. */
void ExecuteCharacterAction(Game& game, int party_slot);
void BeginCombatExecution(void);
void AssignCombatPhases(void);
void SwitchCharacterTo(int party_slot, W8ActionKind action);
void ApplyCombatEndEffects(void);
bool CombatHasContinuingEffects(void);
/* Whether continuous-combat stance may advance past the pending
   NPC-script / engagement gate. */
bool CombatMayAdvanceContinuously(void);
bool QueueNpcCombatScript(void);
int CheckCombatEnd(unsigned int end_if_no_hostiles);
void AdvanceCombatRound(void);
/* While the auto-advance option is on and nothing else captures input, move the
   selection to the next live member without a combat action. */
void AutoAdvanceSelectedCharacter(void);
void RollCombatSurprise(signed char party_cannot_be_surprised);
struct W8Character;
struct W8MonsterInfo;
struct W8CombatCharacterRow;

void CatchUpCombatActor(W8CombatCharacterRow* row);
bool AnyCharacterEngaged(void);
/* Whether the character knows any spell whose realm's sp_left
   still covers its spell_point_cost. */
bool CharacterHasCastableSpell(W8Character* character);
void PointCameraAtCombatTarget(W8TargetSource* source, const W8CombatSlot* target);
/* Set one monster's turn up, once - phase and speed by action. */
void SetUpMonsterTurn(W8MonsterInfo* monster_info);
/* The condition interrupt that fires before the actor's action;
   returns the interrupt kind, or -1 when none fires. */
int GetConditionInterrupt(W8TargetSource* source);
void ApplyPartyCombatAction(int party_slot, W8ActionKind action, int detail,
                            const W8ActionDetailBlock* data, int notify);
bool IsPartyEngaged(void);
W8PartyAction GetEffectivePartyAction(void);
void RecordCharacterDeath(int party_slot);
void DropCharacterFromRound(int party_slot);
/* Whether one party slot may switch to the given targeting
   context, in the two forms the target-refresh pass asks. */
bool CharacterCanSwitchTo(int party_slot, W8TargetingContext context, bool allow_missing_target,
                          bool allow_equipment_changes);
bool TryCharacterAction(int party_slot, W8ActionKind action, bool commit);
void NotifyNearbyMonsters(int what);
void CombatLog(std::string_view text);
void BeginCombatRound(void);
void EndMonsterTurn(W8MonsterInfo* monster_info);
void EndMonsterAttack(W8MonsterInfo* monster_info);
/* Face the monster toward whatever its combat slot targets before
   the attack starts; `alternate` picks the immediate versus animated turn. */
void OrientMonsterTowardTarget(W8MonsterInfo* monster_info, bool alternate);
bool CanCharReBreathe(int party_slot);
bool TryPanicWoundedCharacter(const W8CombatSlot* target);
short GetCombatActionProgress(int* out_total);
