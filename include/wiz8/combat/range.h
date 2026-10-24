#pragma once
#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/fight.h"
#include "wiz8/data/records.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/rules/targeting.h"

struct GameData;
struct W8MonsterInfo;
struct W8MonsterRecord;
struct W8CombatSlot;
struct W8Character;
class Monster;

/* Local Code\Combat Range.cpp: the party's own world position and the trace
   wrapper that decides whether a line of sight counts as unobstructed. */
unsigned char TraceModeRejectsNoHit(int mode);

/* How many formation rows between `party_slot` and the monster block a short
   reach: zero when they share a row, otherwise occupants ahead of the monster
   and (when the gap is exactly two rows) the front rank. */

/* Shorten the action range by intervening formation rows in combat. */

/* Whether `party_slot` may aim at `monster_info`: live threat, the
   resolved action's range category (less the rows between in combat), the
   info's aim flag for the action's ranged-ness, and the band distance. Assessment
   returns a reason; the controller decides whether to report it. */

/* Whether the front rank stands between two formation positions. */

W8RangeCategory MonsterActionRange(const GameData& data, const W8MonsterInfo& monster,
                                   const W8MonsterRecord& record, unsigned int attack);
/* The base missile speed a range category grants `source`, in world units. */
float CalcRangeDistance(W8RangeCategory range_category, W8TargetSource* source);
/* The sight-condition slot a range band needs the observer's sight flags
   checked under: zero inside long range, the current condition beyond it. */
bool RangeCategoryUsesSightCondition(const W8MonsterInfo* monster, W8RangeCategory range_category);
bool MonsterActionReachesTarget(W8MonsterInfo* monster_info, unsigned int attack,
                                W8CombatSlot* target);
/* The location id of the nearest live, in-combat group member visible under
   the selected condition, or -1 when none qualify. */
int FindNearestVisibleGroupMonster(W8MonsterInfo* monster_info, int group_id,
                                   W8VisibilityCondition condition);
/* The furthest range band the monster can act at: its attacks first, then its
   castable spells. `skip_capability_checks` (callers pass 1 for reach/info)
   bypasses prefer-ranged/flee/usability gates; otherwise those gates apply.
   `out_sight` receives the sight-condition slot the band's target needs. */
W8RangeCategory GetMonsterBestRangeCategory(W8MonsterInfo* monster_info,
                                            bool skip_capability_checks, int* out_sight);
/* The range category the slot's chosen action works at. */

float MonsterChooseTarget(W8MonsterInfo* monster_info, W8CombatSlot* out,
                          W8VisibilityCondition condition);
float GetGroundTargetRange(void);
float GetMonsterEngagementRange(void);

void InitializeMonsterRangeCapabilities(W8MonsterInfo* monster_info, const W8MonsterRecord* record);
W8RangeCategory GetBestMonsterAttackRange(const W8MonsterRecord* record, bool close_quarters_only);
float CalcRangeDistance(W8RangeCategory range_category);
/* Same band steps as CalcRangeDistance, then add the party navigator's
   movement collision_radius (camera/party radius offset used by the world cursor). */
float CalcRangeDistanceFromParty(W8RangeCategory range_category);
/* The furthest range category any of the character's hands can
   reach at. */

/* Whether the slot's chosen action in `context` still reaches its
   selected target - character range and front-rank screen, monster aim, place
   distance and line of sight, or group reach. `hand` selects the attack side;
   2 asks the range helpers to use the better hand. */

/* The slot-vs-slot form the target-list builder uses: whether the
   slot's chosen action in `context` can strike `target_slot`. */

/* Collect the party slots the slot could reach and strike under
   `relationship`, and pick one at random; -1 when none qualify. */
int PickReachableSlotByDisposition(int party_slot, W8Disposition relationship);
/* Write `out` the offset from the monster's feet to the origin of
   its attack for `kind` - 1 the projectile muzzle, 3 the spell hand. */
glm::vec3 GetMonsterAttackSourceOffset(Monster& monster, int kind);
/* Whether `source`'s queued action still reaches `target` -
   characters check their own current target, monsters check `target`. */
bool SourceActionReachesTarget(W8TargetSource* source, W8CombatSlot* target,
                               const W8Action& action);
/* Whether the slot has any attack of `category` that reaches a
   valid target in the scanned groups for `hand`; the condition interrupt uses
   it to tell usable attacks from merely reachable ones. `flag` == 1 skips the
   monster scan. */
bool CanPartySlotAttackAnyTarget(int party_slot, int flag, bool hand);
