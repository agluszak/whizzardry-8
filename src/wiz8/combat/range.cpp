#include "wiz8/combat/range.h"

#include <cassert>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "sight_state.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/world.h"

int g_event_sight_blocked = kFirstRemappedEvent + 30;

/* The range categories are W8RangeCategory, declared with the spell record
   that shares the domain. Above W8_RANGE_SHORT an attack is out of the
   close-quarters band the melee rule restricts itself to. */

/* Whether the slot has any attack of `category` that reaches a valid target
   in the scanned groups for `hand`; the condition interrupt uses it to tell
   usable attacks from merely reachable ones. `flag` == 1 skips the monster
   scan and only the out-of-combat and plain-attack categories also scan the
   party for an opposing-side member the chosen action could actually
   strike. */
bool CanPartySlotAttackAnyTarget(int party_slot, int flag, bool hand)
{
    signed char side;
    int first = party_slot;

    if (!CurrentGame().combat.has_value() ||
        !CurrentGame().combat->characters[party_slot].berserk) {
        side = hand;
    } else {
        side = 1;
    }
    side = (side != 0) + 1;
    bool live_groups = CombatAllowsLiveGroups();
    if (flag != 1) {
        unsigned int index;
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            W8MonsterInfo* monster_info = GetMonsterListEntry(index);
            if (monster_info->fActive && monster_info->hp_current != 0 &&
                monster_info->highest_condition < W8_CONDITION_DEAD &&
                (live_groups || CurrentGame().party.dispositionToward(
                                    first, (monster_info)->ubDisposition) == side)) {
                for (unsigned int reach_hand = 0; reach_hand < 2; ++reach_hand) {
                    if ((GetCharAttackRange(CurrentGame().data,
                                            &CurrentGame().party.characters[first],
                                            reach_hand) != W8_RANGE_NONE) &&
                        CurrentGame().assessActionReach(
                            first, reach_hand,
                            (W8Action{W8_ACTION_ATTACK, -1, {}, {}})
                                .aimedAt(W8CombatSlot::monster(
                                    monster_info ? monster_info->location_id : -1)))) {
                        return true;
                    }
                }
            }
        }
    }
    {
        W8Character* character = &CurrentGame().party.characters[first];
        for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            if (slot != first && CurrentGame().party.participates(slot) &&
                CurrentGame().party.dispositionBetween(first, slot) == side) {
                for (unsigned int reach_hand = 0; reach_hand < 2; ++reach_hand) {
                    if (!(GetCharAttackRange(CurrentGame().data,
                                             &CurrentGame().party.characters[first],
                                             reach_hand) != W8_RANGE_NONE)) {
                        continue;
                    }
                    if (static_cast<signed char>(first) == slot) {
                        return true;
                    }
                    const auto range = GetCharAttackRange(Data(), character, reach_hand);
                    if (range == W8_RANGE_NONE) {
                        continue;
                    }
                    if (range != W8_RANGE_TOUCH) {
                        return true;
                    }
                    if (!FormationScreens(CurrentGame().party, first, slot)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/* Whether the slot's chosen action reaches the target it was aimed at:
   `hand` selects the attack side (2 asks for the better hand). A monster
   must be aimable, another party member takes the action's range (with the
   front-rank screen on melee), a point on the ground must sit inside the
   spell's distance and its line of sight, and a group target defers to the
   shared group range test. */

/* Whether `party_slot` may aim at `monster_info`. The monster must be a live
   threat; with no targeting/combat/spell/item mode outstanding the pick is a
   plain sighting and the ranged aim flag applies, otherwise the slot's chosen
   action supplies the range category (a combat melee band loses the formation
   rows between slot and monster) and selects the aim flag. An allowed aim
   then has to pass the band distance to the monster. */

/* The slot-vs-slot reach check the target-list builders share: the slot's
   chosen action has to have a range, and melee range additionally has to get
   past the front rank. */

/* Whether `party_slot`'s action can reach any member of `group_id` at all.
   The character becomes the source for the shared range test; a miss can
   queue the character's complaint event when `notify` asks for it. */

/* The range category the slot's chosen action works at. Attacks take the
   weapon's reach for the asked hand, a move acts at long range, spells and
   item uses ask their spell record, and anything unknown has no range. */

/* The furthest range category any of this character's hands can reach at. */

/* Whether the first lighting condition applies at distant or extreme range. */
bool RangeCategoryUsesSightCondition(const W8MonsterInfo* monster, W8RangeCategory range_category)
{
    if (range_category >= W8_RANGE_LONG && range_category <= W8_RANGE_EXTREME) {
        return monster->has_projectile_origin;
    }
    return false;
}

namespace {
unsigned int MonsterActionSight(const W8MonsterInfo& monster, const W8MonsterRecord& record,
                                unsigned int attack, MonsterAction action)
{
    switch (action.kind) {
    case W8_MONSTER_ACTION_ATTACK:
        return RangeCategoryUsesSightCondition(
            &monster, static_cast<W8RangeCategory>(record.attacks[attack].range_category));
    case W8_MONSTER_ACTION_SPELL:
        return 2u + monster.has_spell_origin;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        return 2;
    default:
        return 0;
    }
}

W8RangeCategory ActionRange(const GameData& data, const W8MonsterInfo& monster,
                            const W8MonsterRecord& record, unsigned int attack,
                            MonsterAction action)
{
    switch (action.kind) {
    case W8_MONSTER_ACTION_ATTACK:
        assert(attack < W8_MAX_MONSTER_ATTACKS);
        assert(record.attacks[attack].fHasAttack != 0);
        return MonsterAttackRange(monster, record, attack);
    case W8_MONSTER_ACTION_SPELL:
        return data.spells[action.detail].range_category;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        return W8_RANGE_LONG;
    case W8_MONSTER_ACTION_PROTECT:
        return W8_RANGE_TOUCH;
    default:
        return W8_RANGE_NONE;
    }
}
}

bool Game::monsterReachesPartyMember(const W8MonsterInfo& monster, unsigned int attack, int slot,
                                     MonsterAction action) const
{
    if (monster.player_visibility.sight_state != W8_SIGHT_SEEN) return false;
    const auto& record = data.monsters[monster.monster_species];
    if (!monster.player_visibility.los_flags[MonsterActionSight(monster, record, attack, action)])
        return false;
    auto range = ActionRange(data, monster, record, attack, action);
    if (range == W8_RANGE_NONE) return false;
    if (combat)
        range = ShortenFormationReach(range,
                                      BlockingFormationRows(party, slot, monsterQuadrant(monster)));
    return range != W8_RANGE_NONE &&
           monster.entity->GetDistanceToPlayer() <= CalcRangeDistance(range);
}

bool W8MonsterInfo::reaches(const W8MonsterInfo& target, const GameData& data, unsigned int attack,
                            MonsterAction action) const
{
    const auto& record = data.monsters[monster_species];
    if (record.untargetable != 0) return false;
    if (&target == this) return true;
    if (!monster_visibility) return false;
    const auto found = monster_visibility->find(target.location_id);
    if (found == monster_visibility->end()) return false;
    const auto& visibility = found->second;
    if (visibility.sight_state != W8_SIGHT_SEEN ||
        !visibility.los_flags[MonsterActionSight(*this, record, attack, action)])
        return false;
    const auto range = ActionRange(data, *this, record, attack, action);
    return range != W8_RANGE_NONE &&
           entity->GetDistanceToMonster(*target.entity) <= CalcRangeDistance(range);
}

/* The furthest range category among a monster's three attacks. Asking for the
   close-quarters band only considers the two categories inside it. */
W8RangeCategory GetBestMonsterAttackRange(const W8MonsterRecord* record, bool close_quarters_only)
{
    W8RangeCategory best = W8_RANGE_NONE;
    int attack;
    unsigned char category;

    for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
        if (record->attacks[attack].fHasAttack != 0) {
            category = record->attacks[attack].range_category;
            if ((!close_quarters_only || category < W8_RANGE_LONG) &&
                static_cast<int>(category) > best) {
                best = static_cast<W8RangeCategory>(category);
            }
        }
    }
    return best;
}

/* Furthest attack band, optionally raised to long for certain special-attack
   kinds, then the furthest castable spell band when the gates allow. */
W8RangeCategory GetMonsterBestRangeCategory(W8MonsterInfo* monster_info,
                                            bool skip_capability_checks, int* out_sight)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    W8RangeCategory best = W8_RANGE_NONE;
    int attack;
    int spell;

    for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
        if (record->attacks[attack].fHasAttack != 0 &&
            static_cast<int>(record->attacks[attack].range_category) > best) {
            best = static_cast<W8RangeCategory>(record->attacks[attack].range_category);
        }
    }

    if (best >= W8_RANGE_LONG && best <= W8_RANGE_EXTREME) {
        *out_sight = monster_info->has_projectile_origin;
    } else {
        *out_sight = 0;
    }

    if (!skip_capability_checks && record->prefer_ranged_actions != 1) {
        return best;
    }
    if (skip_capability_checks || record->flee_chance >= 0x50) {
        if (record->special_attack_kind != 0 &&
            g_special_attack_table[record->special_attack_kind][0] != 6 &&
            (skip_capability_checks || CanMonsterFlee(monster_info, record, true))) {
            if (best < W8_RANGE_LONG) {
                best = W8_RANGE_LONG;
                *out_sight = W8_RANGE_LONG;
            }
        }
    }
    if (!skip_capability_checks &&
        (record->prefer_ranged_actions != 1 || record->spell_chance < 0x50)) {
        return best;
    }

    for (spell = 0; spell < 10; ++spell) {
        unsigned int spell_id = record->spells[spell];

        if (!SpellTargetsEnemies(CurrentGame().data, spell_id)) {
            continue;
        }
        if (!skip_capability_checks &&
            !IsSpellUsableByMonster(monster_info, static_cast<int>(spell_id), true)) {
            continue;
        }
        if (static_cast<int>(best) < static_cast<int>(Data().spells[spell_id].range_category)) {
            best = Data().spells[spell_id].range_category;
            *out_sight = GetMonsterSpellSightIndex(monster_info);
        }
    }
    return best;
}

/* The range category one monster action works at. A spell takes the range off
   the spell record; two of the actions have a fixed answer and the rest have
   none. A plain attack takes it from the attack itself, which has to exist. */
W8RangeCategory MonsterActionRange(const GameData& data, const W8MonsterInfo& monster,
                                   const W8MonsterRecord& record, unsigned int attack)
{
    return ActionRange(data, monster, record, attack, monster.action);
}

/* How far a range category actually is. The four categories step 2, 4, 25, 50
   before the world scale multiplies them; no range at all is zero distance. */
float CalcRangeDistance(W8RangeCategory range_category)
{
    unsigned int steps = 0;

    switch (range_category) {
    case W8_RANGE_NONE:
        steps = 0;
        break;
    case W8_RANGE_TOUCH:
        steps = 2;
        break;
    case W8_RANGE_SHORT:
        steps = 4;
        break;
    case W8_RANGE_LONG:
        steps = 25;
        break;
    case W8_RANGE_EXTREME:
        steps = 50;
        break;
    default:
        return 0.0f;
    }
    return steps * kWorldScale;
}

// The source's collision radius is geometry, independent of the action's range rule.
float World::sourceRadius(const W8TargetSource& source) const
{
    if (TargetSourceIsCharacter(&source, 0)) {
        return party.collisionSphere().radius;
    }
    if (TargetSourceIsMonster(&source, 0)) {
        assert(source.iMonsterID != -1);
        W8MonsterInfo* monster_info = population->findMonster(source.iMonsterID);
        return monster_info->entity->nav.movement.alternate_radius;
    }
    return 0.0f;
}

/* Party-relative action range for the world cursor: same band steps as
   CalcRangeDistance, then add the startup navigator's movement collision_radius. */
float CalcRangeDistanceFromParty(W8RangeCategory range_category)
{
    return CalcRangeDistance(range_category) +
           GetWorld()->party.navigator.movement.collision_radius;
}

/* Shrink a short-range category by the formation rows CountRowsBetween says
   stand between the monster and `party_slot`. Exhausting the category marks it
   unreachable (-1). */

/* Whether anybody standing ahead of this position is still in formation. */

/* How many formation rows between `party_slot` and the monster block a short
   reach. Same row answers zero; otherwise one when the monster's row is
   occupied, plus one more when the gap is exactly two rows and either that
   row or the front rank screens. */

/* Whether the front rank stands between two positions. Only positions exactly
   two rows apart can be screened, and the fifth row is never in the way. */

/* Every other seated, living party member whose side matches `relationship`
   and that the slot's chosen action could actually strike is collected; one of
   them is picked at random, or -1 when nobody qualifies. The berserk and
   turncoat paths use it to pick a victim. */
int PickReachableSlotByDisposition(int party_slot, W8Disposition relationship)
{
    int candidates[8];
    int* next = candidates;
    int count = 0;
    for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
        if (slot == party_slot || !CurrentGame().party.participates(slot) ||
            CurrentGame().party.dispositionBetween(party_slot, slot) != relationship) {
            continue;
        }
        for (unsigned int hand = 0; hand < 2; ++hand) {
            if (!(GetCharAttackRange(CurrentGame().data,
                                     &CurrentGame().party.characters[party_slot],
                                     hand) != W8_RANGE_NONE)) {
                continue;
            }
            if (!CurrentGame().assessActionReach(
                    party_slot, hand,
                    (CurrentGame().party.action(party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT))
                        .aimedAt(W8CombatSlot::character(slot)))) {
                continue;
            }
            ++count;
            *next = slot;
            ++next;
            break;
        }
    }
    if (count == 0) {
        return -1;
    }
    return candidates[CurrentGame().rng.below(count)];
}

/* Two seven-byte constant readers the range rules share. */
float GetMonsterEngagementRange(void)
{
    return 2.5e+04f;
}

float GetGroundTargetRange(void)
{
    return 1.25e+04f;
}

/* Cache which origin points this monster can actually provide. Projectile
   attacks need a usable launch point only when their best range is long or
   extreme; spell casting also requires the spell cycle. */
void InitializeMonsterRangeCapabilities(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    monster_info->unknown_379 = 1;
    monster_info->unknown_37b = 1;

    W8RangeCategory best_range = W8_RANGE_NONE;
    for (const auto& attack : record->attacks) {
        if (attack.fHasAttack != 0 && best_range < attack.range_category) {
            best_range = static_cast<W8RangeCategory>(attack.range_category);
        }
    }

    glm::vec3 position;
    monster_info->has_projectile_origin =
        best_range > W8_RANGE_SHORT && monster_info->entity->GetProjectilePosition(&position);

    monster_info->has_spell_origin = record->spell_chance != 0 &&
                                     monster_info->entity->IsCycleSupported(0x19) &&
                                     monster_info->entity->GetSpellPosition(&position);
}

unsigned char TraceModeRejectsNoHit(int mode)
{
    switch (mode) {
    case 0:
    case 1:
        return 0;
    case 2:
    case 3:
        return 1;
    default:
        return static_cast<unsigned char>(mode);
    }
}

/* Choose what one monster aims at: the player when it can see them, otherwise
   the nearest hostile visible monster. Answers the chosen distance and fills
   the target kind and monster id when applicable. */
// FUNCTION: WIZ8 0x0051ac30
float MonsterChooseTarget(W8MonsterInfo* monster_info, W8CombatSlot* out,
                          W8VisibilityCondition condition)
{
    float best = 999999.0f;

    out->iType = W8_TARGET_KIND_NONE;
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
        IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, condition) &&
        (best = monster_info->entity->GetDistanceToPlayer(), best < 999999.0f)) {
        out->iType = W8_TARGET_KIND_CHARACTER;
    }
    if (out->iType == W8_TARGET_KIND_NONE || !monster_info->pCombat->reconsider_action) {
        auto count = CurrentLevelPopulation().monster_roster.live().size();

        for (unsigned int index = 0; index < count; ++index) {
            W8MonsterInfo* other = GetMonsterListEntry(index);

            if (other != monster_info && other->fActive && other->hp_current != 0 &&
                other->pCombat &&
                MonsterHostility(CurrentGame().data, *monster_info, *other) ==
                    W8_DISPOSITION_HOSTILE) {
                W8VisibilityRecord* row = FindMonToMonVisibility(monster_info, other);
                if (IsVisibleUnderConditions(monster_info, row, condition)) {
                    float distance = monster_info->entity->GetDistanceToMonster(*other->entity);
                    if (distance < best) {
                        out->iType = W8_TARGET_KIND_MONSTER;
                        out->iMonsterID = other->location_id;
                        best = distance;
                    }
                }
            }
            count = CurrentLevelPopulation().monster_roster.live().size();
        }
    }
    return best;
}
/* Whether the monster's current action still reaches the slot `target`
   carries: its own line of sight has to hold for a character or the party, the
   target monster has to be reachable, and a group target is checked against
   every member. */
bool Game::monsterActionReachesTarget(W8MonsterInfo* monster_info, unsigned int attack,
                                      W8CombatSlot* target, MonsterAction action)
{

    if (target->iType == W8_TARGET_KIND_CHARACTER) {
        int party_slot = target->iChar;
        return monsterReachesPartyMember(*monster_info, attack, party_slot, action);
    } else if (target->iType == W8_TARGET_KIND_MONSTER) {
        W8MonsterInfo* target_monster = level.loaded().population->findMonster(target->iMonsterID);
        return monster_info->reaches(*target_monster, data, attack, action);
    } else if (target->iType == W8_TARGET_KIND_PARTY) {
        int party_slot = party.randomSlot(rng, 1, 1, -1, -1);
        return monsterReachesPartyMember(*monster_info, attack, party_slot, action);
    } else if (target->iType == W8_TARGET_KIND_GROUP) {
        W8TargetSource source;
        SetTargetSourceToMonster(monster_info, &source);
        W8MonsterGroup* group = level.loaded().population->findGroup(target->iGroupID);
        // Retirement can leave a body alive after its identity leaves the population.
        if (!level.loaded().population->findEntity(monster_info->location_id)) return false;
        const auto range = ActionRange(data, *monster_info,
                                       data.monsters[monster_info->monster_species], 0, action);
        return level.loaded().sourceReachesGroup(source, *group, CalcRangeDistance(range));
    }
    return true;
}

// FUNCTION: WIZ8 0x0051ad60
int FindNearestVisibleGroupMonster(W8MonsterInfo* monster_info, int group_id,
                                   W8VisibilityCondition condition)
{
    int best_id = -1;
    float best = 999999.0f;
    W8MonsterGroup* group = FindMonsterGroup(group_id);
    for (unsigned int index = 0; index < group->MemberIds().size(); ++index) {
        int location_id = group->MemberLocationId(index);
        W8MonsterInfo* target = FindMonster(location_id);
        if (target->fActive && target->hp_current != 0 && target->pCombat) {
            W8VisibilityRecord* row = FindMonToMonVisibility(monster_info, target);
            if (IsVisibleUnderConditions(monster_info, row, condition)) {
                float distance = monster_info->entity->GetDistanceToMonster(*target->entity);
                if (distance < best) {
                    best_id = target->location_id;
                    best = distance;
                }
            }
        }
    }
    return best_id;
}

/* The attack-origin offset for `kind` 1 (projectile) or 3 (spell): the
   monster's height offset, shifted by the delta between its navigator
   position and the muzzle/hand point when that point resolves. */
glm::vec3 GetMonsterAttackSourceOffset(Monster& monster, int kind)
{
    // A model without the origin vertex attacks from its height offset.
    glm::vec3 origin(0.0f, monster.nav.movement.height_offset, 0.0f);
    if ((kind == 1 && monster.GetProjectilePosition(&origin)) ||
        (kind == 3 && monster.GetSpellPosition(&origin))) {
        return origin - monster.nav.GetPosition();
    }
    return origin;
}
