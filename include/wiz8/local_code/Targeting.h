#pragma once

/* Forget the monster and group the target cycling keys last picked. */
void ResetTargetPicks(void);

#include <optional>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/fight.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/inventory.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/rules/action_choices.h"
#include "wiz8/rules/targeting.h"

struct W8Party;
struct W8MonsterInfo;
struct W8MonsterGroup;
struct W8ItemInstance;

void ResetCombatSlot(W8CombatSlot* slot);

bool TargetSourceIsCharacter(const W8TargetSource* source, int allow_indirect);
bool TargetSourceIsMonster(const W8TargetSource* source, int allow_indirect);

/* Notice colours for combat text: 8 for a party-side source, 9 for a monster
   and 12 for anything else. The target form colours a character target by
   its marching-order slot instead. */
signed char GetSourceNoticeColor(const W8TargetSource* source);
signed char GetTargetNoticeColor(const W8TargetSource* source, const W8CombatSlot* target);

void SetMonsterCombatTarget(W8MonsterInfo* monster_info, int location_id);
bool MonsterTargetMatchesSpell(W8MonsterInfo* monster_info, int spell_id);

void RefreshAllPartyTargets(void);
/* Repick a stored action's target, or a picker's draft target, when the old one no longer
   holds. Both return whether a monster was found; otherwise the target is cleared. */
bool RepickActionTarget(int party_slot, W8TargetingContext stored, int arg);
bool RepickDraftTarget(int party_slot, W8Action& draft);
/* Commit a target and refresh the presentation of the choice: into the action the slot is
   editing (see EditedAction), a stored action, or a picker's draft. Only a committed in-combat
   target is also remembered for its action kind. */
void AimAtTarget(int actor, const W8CombatSlot& target);
void AimStoredAction(int actor, W8TargetingContext stored, const W8CombatSlot& target);
void AimDraft(int actor, W8Action& draft, const W8CombatSlot& target);
// A target naming one party character, directly or indirectly.
W8CombatSlot CharacterTarget(int character_slot, W8TargetKind kind = W8_TARGET_KIND_CHARACTER);
void AimAtMonsterLocation(int party_slot, int location_id, bool allow_single_target);
void AimAtPlace(int actor);
void AimAtGroundTarget(int party_slot);

/* Pick an attack fallback, allowing the character's alternate weapon set when
   the ordinary group selection has no usable monster. */
struct W8WeaponTarget {
    int location_id;
    W8WeaponSwap equipment;
};
std::optional<W8WeaponTarget> ChooseFallbackMonsterTarget(int party_slot, const W8Action& action);
void SetFactionFlag(signed char faction, bool flag);
unsigned char ShowMonsterTargetMarker(W8MonsterInfo* monster_info);
bool IsSpellTargetStillValid(int party_slot, int spell_id, const W8Action& selected);

void SetTargetToCharacter(int character_slot, bool in_combat);

/* The action as the slot can carry it out: nothing while its member cannot participate,
   except a party move. */
W8Action ParticipatingAction(int party_slot, W8Action action);
// The slot's stored committed or executing action, as it can carry it out.
W8Action SelectCombatAction(int party_slot, W8TargetingContext stored);
/* Ask the player to pick a target of `state` (W8_TARGET_NEED_NONE stops asking): the
   pointer's cursor, the party panel and the aiming cone follow it. Game code asks through a
   `TargetingRequested` event instead. */
void SetTargetingMode(W8TargetNeed state);

/* Location id of the nearest hovered live monster, or -1. */
int PickNearestMonsterUnderCursor(int cursor_x, int cursor_y);

bool ActionNeedsExplicitTarget(const W8Action& action, bool conversation_aside);
unsigned int GetActionSpellLikeId(const W8Party& party, const W8Action& selected);
void ResetTargetSource(W8TargetSource* source);
void SetTargetSourceToMonster(const W8MonsterInfo* monster_info, W8TargetSource* source);
bool TargetMatchesNeeded(const W8CombatSlot* target, W8TargetNeed needed);
bool SpellHasAnyValidTarget(int party_slot, int spell_id, bool normalize);
void SetTargetToMonster(int monster_id, bool in_combat);
void SetTargetToGroup(int group_id, bool in_combat);

bool ClearMonsterCombatSlot(W8MonsterInfo* monster_info);

/* Fill the slot's point from where its target is. */
bool ResolveTargetPoint(W8CombatSlot* target, bool sight_probe);
void ApplyTarget(W8CombatSlot* target, bool in_combat);
/* Whether an actor aiming at `target` should drop that aim now
   that `target` has been (re)applied. An out-of-combat application always clears;
   otherwise the actor and the applied target must agree on hostility with the
   action's enemy-aimed flag. */
bool ShouldClearAimForAppliedTarget(W8TargetSource* source, W8CombatSlot* target, bool in_combat,
                                    bool action_targets_enemies);
bool IsTargetStillPresent(const W8CombatSlot* target);
bool IsTargetSourceInRangeOfGroup(const W8TargetSource* source, W8MonsterGroup* group,
                                  W8RangeCategory range);

W8TargetAssessment AssessMonsterTarget(int party_slot, int location_id, const W8Action& selected,
                                       bool allow_single_target, bool sighting = false);
bool CanTargetMonsterGroup(int party_slot, W8MonsterGroup* group, const W8Action& selected,
                           bool sighting = false);
/* Whether the slot's current target satisfies the spell's
   needed-target kind. */
bool IsSpellTargetOfNeededKind(const W8CombatSlot* target, int spell_id, bool needs_item_target);
/* Whether the slot's current target satisfies the item's
   needed-target kind. */
bool IsItemTargetOfNeededKind(const W8CombatSlot* target, const W8ItemInstance* item);
/* Select the party slot the spell-casting view is casting for. */
void SelectSpellCastingPartySlot(int party_slot, W8Action& draft);
/* Set the targeting filter for the spell being aimed. A picker passes its draft, which
   automatic spell targeting aims. */
void ConfigureSpellTargetFilter(W8SpellTargetType target_type, W8TargetNeed needed_kind,
                                W8Action* draft);
void RefreshMonsterTargetCounts(void);
bool AnyMonsterVisible(void);

/* Collect eligible monsters within the radius, with an explicit choice
   between line of sight and geometry-only preview rules. */
void CollectMonstersWithinRadius(const glm::vec3* centre, const glm::vec3* eye,
                                 std::vector<int>& found, float radius, signed char side,
                                 W8AreaSightRule sight_rule);

class Monster;

/* Whether `monster` is within `max_distance` of the player and
   still projects on screen. */
bool IsMonsterVisibleWithinDistance(Monster& monster, float max_distance);

/* Whether `target` lies within the combined radii of `eye` and
   `bonus`, inside the heading and elevation arcs. */
bool TargetInRangeAndArcs(const glm::vec3* target, float bonus, const glm::vec3* eye,
                          float eye_radius, float heading, float elevation, float range);
/* Append the location ids of live targetable monsters inside the
   aim cone to `found`, filtered by disposition (3 admits all); returns how many
   were added. */
int CollectConeMonsterTargets(const W8TargetSource* source, const glm::vec3* eye, float heading,
                              float elevation, std::vector<int>& found, unsigned char disposition,
                              int sight_flag, float range);
/* Whether the pending item use needs a target picked. */
bool ItemUseNeedsTarget(int party_slot);

void ReconcilePartyEquipmentAfterCombat(void);
bool TargetIsInPlay(int party_slot, int hand, const W8Action& selected);

/* Combat action-selection helpers used across the combat units. */
bool CanPartySlotParticipate(int party_slot);
void SetTargetSourceToCharacter(int party_slot, W8TargetSource* source);
/* What the interface has to ask the player to pick for one
   action - a fixed kind for most, the spell's answer for casts and item use. */
W8TargetNeed GetTargetNeededForAction(const W8Party& party, const W8Action& action,
                                      bool needs_item_target = true);
W8TargetNeed GetTargetNeededForItem(const W8ItemInstance* item, bool needs_item_target);

/* After the selected character's action changes, drop back to no
   targeting when its recorded target still fits, or enter the mode the action
   now needs. */
void RevalidateSelectedTarget(int party_slot);
/* Cycle the pick within a group and aim the party slot at it. */
void AimAtMonsterGroupMember(int party_slot, W8MonsterGroup* group);
/* The cycle-target command - advance the pick to the next
   targetable group or monster and commit it. */
void CycleToNextTarget(int party_slot);
/* Whether a combat submenu candidate has anything to aim at. */
bool CandidateHasAnyValidTarget(int party_slot, W8ActionKind candidate);
/* Whether the slot holds a dead character still reachable for a
   targeting mode that admits one. */
bool IsDeadCharacterTargetable(int party_slot);

std::optional<W8Faction> FindFactionByName(const char* name);
void RepickInvalidCombatTargets(void);

// Remember a committed target for the action kind that repeats it.
void RememberActionTarget(ActionChoices& remembered, W8ActionKind kind, const W8CombatSlot& target);

std::optional<int> PickNextTargetableGroupMember(int party_slot, const W8MonsterGroup* group,
                                                 const W8Action& action, bool sighting);
