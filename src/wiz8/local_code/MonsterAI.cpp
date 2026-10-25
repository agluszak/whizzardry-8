#include "wiz8/local_code/MonsterAI.h"

#include <cassert>
#include <cmath>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "monster_cycles.h"
#include "sight_state.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/group_attacks.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kGroupEngagementProbeScale = 333.33333f;
constexpr float kMonsterEngagementRangeFloor = 7500.0f;
constexpr float kReinforcementDistanceSlack = 1.15f;

/* Gates the out-of-combat check that gives a group whose leader is still up a
   nudge. Only UpdateMonsterGroups reads it. */
static bool g_out_of_combat_rtai_enabled = true;

/* The frame counter the staggered group update rotates through the group
   list, one fifth per frame with a full-distance pass every twentieth. */
static unsigned int g_monster_group_tick = 0;

static W8MonsterInfo* GetGroupMemberInfo(W8MonsterGroup* group, unsigned int index);

/* 125000, the cap on how far a monster will walk to investigate a heard
   noise. */
int g_noise_investigate_radius_cap = 125000;

/* Two dwords per special-attack kind; only the leading dword is
   read here. */
const int g_special_attack_table[32][2] = {
    {0, 0}, {1, 0}, {1, 0}, {2, 3}, {4, 0}, {1, 1}, {1, 1}, {1, 0}, {5, 4}, {5, 0}, {1, 5},
    {1, 1}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 1}, {1, 5}, {1, 0},
    {1, 1}, {1, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {5, 0}, {6, 0},
};

/* The timed sight service. The dirty flag is raised by the condition and
   enchantment writers whenever what monsters can see of the party may have
   changed; out of combat sight is refreshed unconditionally, in combat only
   when the flag says so, and detection runs while the combat state permits. */
void Game::refreshPendingMonsterSight()
{
    if (!combat.has_value() || sight_refresh_pending) {
        if (sight_refresh_pending) {
            sight_refresh_pending = false;
        }
        refreshOutwardSight();
        if (combat.has_value() && combat->round_count == 0) {
            CheckMonsterGroupsEnterCombat();
        }
    }
}

/* The per-frame monster group pass. When `staggered` is set the sight block
   is left to UpdateMonsterSight and the maintenance below is spread across
   the group list a fifth at a time, with a full-distance check every
   twentieth frame; groups further than twice the far clip are detached and
   groups close enough get their members loaded. */
void UpdateMonsterGroups(bool staggered)
{
    W8MonsterGroup* monster_group;
    W8MonsterInfo* monster_info;
    glm::vec3 camera_position;
    float nearest_distance;
    unsigned int group_list_index;

    if (!(CurrentGame().party.participantCount() != 0)) {
        return;
    }
    if ((CurrentLevelPopulation().groups().empty())) {
        return;
    }
    ++g_monster_group_tick;
    if (!staggered) {
        CurrentGame().refreshPendingMonsterSight();
    }
    camera_position = GetWorld()->party.eye();
    for (group_list_index = 0; group_list_index < CurrentLevelPopulation().groups().size();
         ++group_list_index) {
        if (staggered && group_list_index % 5 != g_monster_group_tick % 5) {
            continue;
        }
        monster_group = CurrentLevelPopulation().groups()[group_list_index].get();
        if (CurrentLevelPopulation().allMembersDying(*monster_group)) {
            continue;
        }
        monster_info = FindMonster(monster_group->leader_location_id);
        if (monster_info == 0 || !monster_info->entity) {
            continue;
        }
        nearest_distance = GetGroupNearestDistance(monster_group);
        if (staggered && group_list_index % 20 != g_monster_group_tick % 20 &&
            (WorldGetFarClip(GetWorld()) < nearest_distance ||
             group_list_index % 5 != g_monster_group_tick % 5)) {
            continue;
        }
        if (!monster_group->members_active) {
            if (nearest_distance < WorldGetFarClip(GetWorld()) * 1.5f) {
                CurrentGame().activateGroup(*monster_group);
            }
        }
        if (monster_group->members_active && !monster_group->fInCombat) {
            double far_clip;
            if (monster_group->leader_group_id == 0) {
                RefreshMonsterGroupHostility(monster_group);
            }
            far_clip = WorldGetFarClip(GetWorld());
            if (nearest_distance <= far_clip + far_clip) {
                if (!CurrentGame().combat.has_value() && MonsterGroupCanEngage(monster_group)) {
                    MonsterGroupEnterCombat(monster_group);
                }
            } else {
                DetachMonsterGroup(monster_group);
            }
        }
        if (g_out_of_combat_rtai_enabled && !CurrentGame().combat.has_value()) {
            assert(monster_group != 0);
            if (monster_group->leader_group_id == 0) {
                monster_info = FindMonster(monster_group->leader_location_id);
                if (!monster_info->entity->IsDying()) {
                    DoMonsterRTAI(monster_info, true);
                }
            }
        }
    }
}

/* How aware the group is of the party: zero while the global gates say the
   group cannot react or its representative record is untargetable, one the
   moment any live member's party-visibility state is one, and two while the
   best state a live member reports is two. Members with no live entry, a
   dying body, no hit points or a condition at 0x0c or higher do not count. */
W8SightState GetMonsterGroupPartySightState(W8MonsterGroup* monster_group)
{
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;
    unsigned int index;
    W8SightState result;

    result = W8_SIGHT_UNSEEN;
    if (CurrentGame().status.world_suspended) {
        return W8_SIGHT_UNSEEN;
    }
    monster_info = FindMonster(monster_group->leader_location_id);
    record = GetMonsterDataForInfo(monster_info);
    if (record != 0 && record->untargetable != 0) {
        return W8_SIGHT_UNSEEN;
    }
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        int location_id;

        location_id = monster_group->MemberLocationId(index);
        monster_info = FindMonster(location_id);
        if (monster_info->fActive && !monster_info->entity->IsDying() &&
            monster_info->hp_current != 0 && monster_info->highest_condition < W8_CONDITION_BLIND) {
            if (monster_info->player_visibility.sight_state == W8_SIGHT_SEEN) {
                return W8_SIGHT_SEEN;
            }
            if (monster_info->player_visibility.sight_state == W8_SIGHT_RECENT) {
                result = W8_SIGHT_RECENT;
            }
        }
    }
    return result;
}

/* The real-time AI decision for one monster. The `engage` call form is the
   trigger path: while sight is overridden and the monster still has its
   ambush flag set it moves to within its longest out-of-reach attack band of
   the party and activates its group. Otherwise the disposition picks
   a mode: the scripted-orders helper for dispositions zero and two, a
   hit-point retreat check for one. The chosen mode is committed through
   ApplyMonsterRTAIDecision when it differs from what ai_mode already holds. */
void DoMonsterRTAI(W8MonsterInfo* monster_info, bool engage)
{
    bool update;
    unsigned char decision;

    update = false;
    decision = W8_RT_AI_IDLE;
    if (engage && CurrentGame().accelerated_calendar &&
        monster_info->ai_mode == W8_RT_AI_CHARGE_PARTY) {
        const W8MonsterRecord* record;
        W8MonsterGroup* monster_group;
        Navigator* navigator;
        unsigned char best_range;
        unsigned int attack;

        if (monster_info->pCombat) {
            return;
        }
        assert(monster_info != 0);
        record = GetMonsterDataForInfo(monster_info);
        best_range = 0;
        for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
            if (CurrentGame().rateMonsterAttack(*monster_info, attack, false) ==
                    W8_MONSTER_ATTACK_OUT_OF_REACH &&
                best_range < record->attacks[attack].range_category) {
                best_range = record->attacks[attack].range_category;
            }
        }
        if (monster_info->entity->ApproachStartupNavigator(
                CalcRangeDistance(static_cast<W8RangeCategory>(best_range)) * 0.9f) == 0) {
            return;
        }
        monster_group = FindMonsterGroup(monster_info->monster_group_id);
        MonsterGroupEnterCombat(monster_group);
        navigator = monster_info->entity->nav.linked_navigator;
        if (navigator != 0) {
            navigator->group_linked = true;
        } else {
            monster_info->entity->nav.group_linked = true;
        }
        return;
    }
    if (monster_info->pCombat) {
        return;
    }
    if (monster_info->hp_current != 0 && monster_info->highest_condition < W8_CONDITION_WEBBED) {
        if (monster_info->control_state == W8_MONSTER_CONTROL_LURED) {
            decision = W8_RT_AI_FOLLOW_LURE;
        } else {
            W8SightState alert;

            alert = W8_SIGHT_UNSEEN;
            if (engage) {
                alert = GetMonsterGroupPartySightState(
                    FindMonsterGroup(monster_info->monster_group_id));
            }
            if (alert == W8_SIGHT_UNSEEN) {
                if (!monster_info->entity->orders_finished) {
                    decision = W8_RT_AI_IDLE;
                } else {
                    update = ChooseMonsterRTAIMode(monster_info, &decision);
                }
            } else if (alert <= W8_SIGHT_RECENT) {
                switch (monster_info->ubDisposition) {
                case W8_DISPOSITION_NEUTRAL:
                    if (!monster_info->entity->orders_finished) {
                        decision = W8_RT_AI_IDLE;
                    } else {
                        update = ChooseMonsterRTAIMode(monster_info, &decision);
                    }
                    break;
                case W8_DISPOSITION_HOSTILE:
                    decision = static_cast<unsigned char>(
                                   monster_info->hp_current * 100 / monster_info->uiHPMax <= 0x14) +
                               1;
                    if (monster_info->entity->nav.movement_stopped) {
                        update = true;
                    }
                    break;
                case W8_DISPOSITION_FRIENDLY:
                    if (!monster_info->entity->orders_finished) {
                        decision = W8_RT_AI_IDLE;
                    } else {
                        update = ChooseMonsterRTAIMode(monster_info, &decision);
                    }
                    break;
                default:
                    return;
                }
            }
        }
    }
    if ((monster_info->ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
        unsigned char mode;

        mode = monster_info->ai_mode & W8_MONSTER_AI_MODE_MASK;
        if (mode != W8_RT_AI_PATROL_AREA &&
            (!monster_info->entity->face_party || monster_info->pathing_cooldown != 0 ||
             mode != W8_RT_AI_FACE_DIRECTION || !monster_info->player_visibility.line_of_sight ||
             glm::length((((monster_info->entity->nav.movement.position -
                            monster_info->entity->owner_world.party.position())))) >= 5e+03f)) {
            monster_info->ai_mode &= ~W8_MONSTER_AI_REAPPLY_MODE;
        }
        if (decision <= W8_RT_AI_LINK_TO_PARTY || decision == W8_RT_AI_FOLLOW_LURE) {
            update = true;
        }
    }
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE ||
        monster_info->entity->script_wait != MONSCR_WALKTO || decision != W8_RT_AI_IDLE) {
        if (decision != monster_info->ai_mode || update) {
            assert(engage || decision != W8_RT_AI_CHARGE_PARTY);
            ApplyMonsterRTAIDecision(monster_info, decision);
        }
    }
}

/* The orders-driven half of the real-time decision. A monster that should
   face the party and can see it takes mode 0xa straight away; a group with a
   downed member takes nothing. A latched mode keeps running until its patrol
   point is reached, a fresh look-around delay just counts down, and a heard
   noise is investigated while the path to it still looks cheap enough. */
bool ChooseMonsterRTAIMode(W8MonsterInfo* monster_info, unsigned char* decision)
{
    bool changed;
    signed char mode;

    Monster& monster = *monster_info->entity;
    mode = W8_RT_AI_IDLE;
    changed = false;
    if (monster.face_party && monster_info->pathing_cooldown == 0 &&
        monster_info->player_visibility.line_of_sight) {
        glm::vec3 delta;

        delta = monster.nav.movement.position - monster.owner_world.party.position();
        if (glm::length(delta) < 5e+03f) {
            glm::vec3 camera;
            glm::vec3 position;

            camera = cameraLocation();
            position = monster.nav.GetPosition();
            *decision = W8_RT_AI_FACE_DIRECTION;
            monster.move_direction = camera - position;
            return true;
        }
    }
    if (MonsterGroupHasIncapacitatedMember(monster_info->monster_group_id)) {
        *decision = W8_RT_AI_IDLE;
        return false;
    }
    if ((monster_info->ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
        if (monster.order_mode == W8_MONSTER_ORDER_POINT_PATROL ||
            monster.order_mode == W8_MONSTER_ORDER_RANDOM_POINT_PATROL) {
            glm::vec3 delta;
            glm::vec3 patrol;

            monster.GetPatrolPoint(&patrol);
            delta = patrol - monster.nav.GetPosition();
            if (glm::length(delta) >= 1500.0) {
                *decision = monster_info->ai_mode;
                return true;
            }
        } else {
            *decision = monster_info->ai_mode;
            return true;
        }
    }
    if (monster_info->pathing_cooldown != 0) {
        --monster_info->pathing_cooldown;
        if (monster_info->pathing_cooldown == 0) {
            monster_info->heard_noise_margin = 0;
        }
        return false;
    }
    if (monster_info->ai_mode == W8_RT_AI_FACE_NOISE &&
        fabsf(monster.nav.movement.target_yaw - monster.nav.movement.yaw) >=
            g_camera_transition_epsilon) {
        mode = W8_RT_AI_FACE_NOISE;
    } else {
        if (monster_info->heard_noise_radius > 0 && monster.nav.movement_stopped &&
            monster_info->ai_mode == W8_RT_AI_INVESTIGATE_NOISE) {
            glm::vec3 delta;

            delta = monster_info->heard_noise_position - monster.nav.GetPosition();
            if (glm::length(delta) < 1500.0) {
                monster_info->heard_noise_radius = 0;
            }
        }
        if (monster_info->heard_noise_radius > 0) {
            if (!monster.nav.movement_stopped &&
                monster_info->ai_mode == W8_RT_AI_INVESTIGATE_NOISE) {
                mode = W8_RT_AI_INVESTIGATE_NOISE;
            } else {
                glm::vec3 noise_position;
                glm::vec3 position;
                int radius;

                noise_position = monster_info->heard_noise_position;
                radius = monster_info->heard_noise_radius * 3 / 2;
                if (radius >= g_noise_investigate_radius_cap) {
                    radius = g_noise_investigate_radius_cap;
                }
                position = monster.nav.GetPosition();
                const auto path = GetWorldSpatialIndex()->measureNoisePath(
                    position, noise_position, static_cast<float>(radius));
                if (path &&
                    NoiseHearingMargin(monster_info->heard_noise_radius,
                                       static_cast<int>(path->length), path->door_hops) > 0) {
                    mode = W8_RT_AI_INVESTIGATE_NOISE;
                    changed = true;
                } else {
                    mode = W8_RT_AI_FACE_NOISE;
                }
            }
        } else if (monster_info->pause_time != 0) {
            if (CurrentGame().rng.below(0x14) == 0) {
                double angle;

                mode = W8_RT_AI_FACE_DIRECTION;
                angle =
                    (CurrentGame().rng.below(0x168) << 1) * g_camera_pi * kInverseFullTurnDegrees;
                monster.move_direction.x = static_cast<float>(cos(angle) * 500.0);
                monster.move_direction.y = 0.0f;
                monster.move_direction.z = static_cast<float>(sin(angle) * 500.0);
            } else {
                mode = W8_RT_AI_IDLE;
            }
        } else {
            switch (monster.order_mode) {
            case W8_MONSTER_ORDER_NONE:
                break;
            case W8_MONSTER_ORDER_GUARD: {
                glm::vec3 delta;
                glm::vec3 patrol;

                monster.GetPatrolPoint(&patrol);
                delta = patrol - monster.nav.GetPosition();
                if (glm::length(delta) < 1500.0) {
                    mode = W8_RT_AI_IDLE;
                } else {
                    mode = W8_RT_AI_MOVE_TO_PATROL_POINT;
                }
                break;
            }
            case W8_MONSTER_ORDER_PATROL:
                mode = W8_RT_AI_PATROL_AREA;
                if (monster.nav.movement_stopped) {
                    changed = true;
                }
                break;
            case W8_MONSTER_ORDER_POINT_PATROL:
            case W8_MONSTER_ORDER_RANDOM_POINT_PATROL: {
                glm::vec3 delta;
                glm::vec3 patrol;
                int count;
                int next;

                mode = W8_RT_AI_MOVE_TO_PATROL_POINT;
                monster.GetPatrolPoint(&patrol);
                delta = patrol - monster.nav.GetPosition();
                if (glm::length(delta) >= 1500.0) {
                    break;
                }
                count = static_cast<int>(monster.vector.size());
                if (monster.order_mode == W8_MONSTER_ORDER_POINT_PATROL) {
                    next = monster.patrol_index + 1;
                    if (next < count) {
                        monster.patrol_index = static_cast<signed char>(next);
                    } else {
                        monster.patrol_index = 0;
                    }
                } else if (count < 2) {
                    monster.patrol_index = 0;
                } else if ((monster_info->ai_mode & W8_MONSTER_AI_MODE_MASK) ==
                           W8_RT_AI_MOVE_TO_PATROL_POINT) {
                    do {
                        next = static_cast<signed char>(CurrentGame().rng.below(count));
                    } while (next == monster.patrol_index);
                    monster.patrol_index = static_cast<signed char>(next);
                } else {
                    monster.patrol_index = static_cast<signed char>(CurrentGame().rng.below(count));
                }
                changed = true;
                break;
            }
            case W8_MONSTER_ORDER_FACE_DIRECTION:
                monster.move_direction =
                    glm::vec3(monster.direction_x, monster.direction_y, monster.direction_z);
                mode = W8_RT_AI_FACE_DIRECTION;
                changed = true;
                break;
            }
        }
    }
    if (mode != static_cast<signed char>(monster_info->ai_mode)) {
        changed = true;
    }
    *decision = static_cast<unsigned char>(mode);
    return changed;
}

static void HandleMonsterPatrolFailure(W8MonsterInfo* monster_info)
{
    W8MonsterGroup* monster_group = FindMonsterGroup(monster_info->monster_group_id);
    if (monster_group->encounter_registered) {
        MarkMonsterGroupForRemoval(monster_info->monster_group_id);
    } else {
        monster_info->pathing_cooldown = 0x14;
        if (monster_info->movement_stall_ticks < 2) {
            monster_info->movement_stall_ticks = 2;
        }
    }
}

/* Carry out the real-time mode ChooseMonsterRTAIMode picked. Each case does
   the movement or aiming that mode needs; when a mode cannot run the decision
   is folded back to zero so ai_mode records what actually happened. Bit 0x80
   of the decision rides in alongside the mode and only case 6 consumes it. */
void ApplyMonsterRTAIDecision(W8MonsterInfo* monster_info, unsigned char decision)
{
    Monster& monster = *monster_info->entity;
    W8MonsterGroup* monster_group;
    const W8MonsterRecord* record;
    W8MonsterInfo* member;
    W8SpellEffectEntry* effect;
    glm::vec3 position;
    glm::vec3 patrol_point;
    unsigned char best_range;
    unsigned int attack;
    unsigned int index;

    switch (decision & W8_MONSTER_AI_MODE_MASK) {
    case W8_RT_AI_CHARGE_PARTY:
        best_range = 0;
        assert(monster_info != 0);
        record = GetMonsterDataForInfo(monster_info);
        for (attack = 0; attack < 3; ++attack) {
            if (CurrentGame().rateMonsterAttack(*monster_info, attack, false) == 3 &&
                best_range < record->attacks[attack].range_category) {
                best_range = record->attacks[attack].range_category;
            }
        }
        if (monster.ApproachStartupNavigator(
                CalcRangeDistance(static_cast<W8RangeCategory>(best_range)) * 0.9f) == 1) {
            if (CurrentGame().accelerated_calendar) {
                monster_group = FindMonsterGroup(monster_info->monster_group_id);
                MonsterGroupEnterCombat(monster_group);
            }
            break;
        }
        decision = W8_RT_AI_IDLE;
        [[fallthrough]];
    case W8_RT_AI_IDLE:
        ClearMonsterPathAndResume(monster_info);
        break;
    case W8_RT_AI_LINK_TO_PARTY:
        monster.LinkToStartupNavigator();
        break;
    case W8_RT_AI_APPROACH_PARTY:
        assert(monster_info->ubDisposition != W8_DISPOSITION_HOSTILE);
        monster.ApproachStartupNavigator(3000.0);
        break;
    case W8_RT_AI_INVESTIGATE_NOISE:
        position = monster_info->heard_noise_position;
        monster.nav.ConfigureMovementToPosition(&position);
        break;
    case W8_RT_AI_PATROL_AREA:
        if ((decision & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
            monster.nav.SetHeightRange(monster.patrol_distance, monster.patrol_variation);
            monster_info->ai_mode &= ~W8_MONSTER_AI_REAPPLY_MODE;
            break;
        }
        if (monster.formation == glm::vec3(0)) {
            monster.formation = monster.nav.GetPosition();
        }
        position = monster.formation;
        if (monster.nav.StartPatrol(&position, monster.patrol_distance, monster.patrol_variation)) {
            break;
        }
        HandleMonsterPatrolFailure(monster_info);
        decision = W8_RT_AI_IDLE;
        break;
    case W8_RT_AI_MOVE_TO_PATROL_POINT:
        if (!monster.GetPatrolPoint(&patrol_point)) {
            decision = W8_RT_AI_IDLE;
            break;
        }
        if (monster.nav.ConfigureMovementToPosition(&patrol_point) != 0) {
            break;
        }
        if (!monster.IsWithinWorldRange() &&
            monster_info->party_threat.sight_state == W8_SIGHT_SEEN) {
            monster_group = FindMonsterGroup(monster_info->monster_group_id);
            position = patrol_point;
            if (CurrentGame().placeMonsterGroup(monster_group, &position, monster.nav.GetYaw(),
                                                false, true, false, false)) {
                break;
            }
        }
        HandleMonsterPatrolFailure(monster_info);
        decision = W8_RT_AI_IDLE;
        break;
    case W8_RT_AI_FACE_NOISE:
        monster.nav.flags &= ~0x20000000;
        monster.nav.ClearMovement();
        position = monster_info->heard_noise_position;
        monster_info->heard_noise_radius = 0;
        monster_info->pathing_cooldown = 0x1e;
        monster_group = FindMonsterGroup(monster_info->monster_group_id);
        for (index = 0; index < monster_group->MemberIds().size(); ++index) {
            member = FindMonster(monster_group->MemberLocationId(index));
            member->entity->nav.AimAtPosition(&position);
        }
        break;
    case W8_RT_AI_FOLLOW_LURE:
        effect = monster.owner_world.monsterControlEffect();
        if (!effect) break;
        if (monster.nav.SetMovementTargetToAnchor(effect->target.point, 2500.0) == 0) {
            position = effect->target.point;
            monster.nav.AimAtPosition(&position);
        }
        break;
    case W8_RT_AI_FACE_DIRECTION:
        monster.nav.flags &= ~0x20000000;
        monster.nav.ClearMovement();
        position = monster.nav.GetPosition();
        position += monster.move_direction;
        monster.nav.AimAtPosition(&position);
        break;
    }
    monster_info->ai_mode ^= (monster_info->ai_mode ^ decision) & W8_MONSTER_AI_MODE_MASK;
}

/* Run the decision over every monster that is in the fight and still alive. */
void UpdateAllMonsterAI(void)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->pCombat && monster_info->hp_current != 0) {
            CurrentGame().updateMonsterDecision(monster_info);
        }
    }
}

/* Whether a hostile group still has a member able to engage the party. Only
   hostile groups can; of those, the group engages when a live member has a
   visible target, and a member counts when it can attack, cast one of its
   spells, flee, or - for a touch/short attack with the party close - reach
   the party along the waypoints. The waypoint probe runs once, against the
   group's named member, and is reused for the rest of the sweep. */
bool MonsterGroupCanEngage(W8MonsterGroup* monster_group)
{
    W8MonsterInfo* member;
    W8MonsterInfo* leader;
    const W8MonsterRecord* record;
    unsigned int index;
    unsigned int spell;
    float distance;
    bool waypoint_checked = false;
    unsigned char waypoint_result = 0;
    glm::vec3 source;
    glm::vec3 destination;

    assert(monster_group != 0);
    if (monster_group->ubDisposition == W8_DISPOSITION_HOSTILE) {
        for (index = 0; index < monster_group->MemberIds().size(); ++index) {
            member = GetGroupMemberInfo(monster_group, index);
            if (member->fActive && member->hp_current != 0 &&
                member->highest_condition < W8_CONDITION_DEAD &&
                MonsterHasVisibleTarget(member, true, W8_VISIBLE_TARGET_HOSTILE, true)) {
                goto members;
            }
        }
    }
    return false;
members:
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        member = FindMonster(monster_group->MemberLocationId(index));
        record = GetMonsterDataForInfo(member);
        if (!member->fActive || member->fMotionless || member->entity->IsDying() ||
            member->hp_current == 0 || member->highest_condition >= W8_CONDITION_BLIND) {
            continue;
        }
        if (CurrentGame().rateMonsterBestAttack(*member, false) == 0) {
            return true;
        }
        if (record->spell_chance != 0) {
            if (member->entity->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                for (spell = 0; spell < 10; ++spell) {
                    if (SpellTargetsEnemies(CurrentGame().data, record->spells[spell]) &&
                        CurrentGame().canMonsterCastSpell(member, record->spells[spell], true)) {
                        return true;
                    }
                }
            }
        }
        if (CurrentGame().canMonsterUseSpecial(member, true)) {
            return true;
        }
        if (GetBestMonsterAttackRange(record, false) <= W8_RANGE_SHORT) {
            distance = member->entity->GetDistanceToPlayer();
            if (GetMonsterCombatMoveRange(member) * kGroupEngagementProbeScale > distance) {
                if (!waypoint_checked) {
                    leader = FindMonster(monster_group->leader_location_id);
                    if (leader != 0 && leader->fActive) {
                        destination = leader->entity->owner_world.party.position();
                        source = leader->entity->nav.GetPosition();
                        waypoint_result =
                            GetWorldSpatialIndex()
                                ->pathing->tracePathSpan(source, destination, false, false)
                                .reachable;
                    }
                    waypoint_checked = true;
                }
                if (waypoint_result != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* The in-combat sweep: refreshes what each monster can see, then drops combat
   for groups that have nothing left to fight. A neutral group stays only
   while a reinforcement is near; a hostile group leaves when no live member
   has a visible enemy, and - when the leader cannot reach - when the nearest
   member sits outside the leader's reach with nothing the group can engage.
   Once no group still has a member fighting, every group leaves at once. */
void CheckMonsterGroupsLeaveCombat(void)
{
    W8MonsterGroup* group;
    W8MonsterInfo* member;
    W8MonsterInfo* leader;
    W8MonsterInfo* other;
    unsigned int group_index;
    unsigned int index;
    unsigned int member_index;
    int engaged = 0;
    int sight;
    float nearest;
    float reach;
    float minimum;

    CurrentGame().refreshOutwardSight();
    for (group_index = 0; group_index < CurrentLevelPopulation().groups().size(); ++group_index) {
        group = CurrentLevelPopulation().groups()[group_index].get();
        if (!group->members_active || !group->fInCombat ||
            CurrentLevelPopulation().allMembersDying(*group)) {
            continue;
        }
        if (group->ubDisposition == W8_DISPOSITION_NEUTRAL) {
            if (!MonsterGroupHasReinforcement(group)) {
                CurrentGame().endGroupCombat(*group);
            }
            continue;
        }
        assert(group != 0);
        for (index = 0; index < group->MemberIds().size(); ++index) {
            member = FindMonster(group->MemberLocationId(index));
            if (!member->fActive || member->hp_current == 0 ||
                member->highest_condition >= W8_CONDITION_DEAD ||
                MonsterHasNoVisibleEnemy(member, false)) {
                continue;
            }
            if (group->ubDisposition != W8_DISPOSITION_HOSTILE) {
                break;
            }
            leader = FindMonster(group->leader_location_id);
            if (group->engagement && leader != 0 && leader->fActive) {
                nearest = GetGroupNearestDistance(group);
                reach = CalcRangeDistance(GetMonsterBestRangeCategory(leader, true, &sight)) +
                        GetMonsterCombatMoveRange(leader) * 1e+03f;
                minimum = GetMonsterEngagementRange() + kMonsterEngagementRangeFloor;
                if (reach <= minimum) {
                    reach = minimum;
                }
                if (reach < nearest && !MonsterGroupCanEngage(group)) {
                    CurrentGame().endGroupCombat(*group);
                    break;
                }
            }
            assert(group != 0);
            for (member_index = 0; member_index < group->MemberIds().size(); ++member_index) {
                other = FindMonster(group->MemberLocationId(member_index));
                if (other->fActive && other->hp_current != 0 &&
                    other->highest_condition < W8_CONDITION_DEAD &&
                    !MonsterHasNoVisibleEnemy(other, true)) {
                    ++engaged;
                    break;
                }
            }
            break;
        }
        if (index >= group->MemberIds().size()) {
            CurrentGame().endGroupCombat(*group);
        }
    }
    if (engaged != 0) {
        return;
    }
    for (group_index = 0; group_index < CurrentLevelPopulation().groups().size(); ++group_index) {
        group = CurrentLevelPopulation().groups()[group_index].get();
        if (group->members_active && group->fInCombat &&
            !CurrentLevelPopulation().allMembersDying(*group)) {
            CurrentGame().endGroupCombat(*group);
        }
    }
}

/* Whether the monster sees no living enemy. A raised party-threat flag
   answers at once; a visible hostile party member counts, and `party_only`
   zero also scans the monsters it is hostile to that it can see. */
bool MonsterHasNoVisibleEnemy(W8MonsterInfo* monster_info, bool party_only)
{
    unsigned int index;
    W8MonsterInfo* other;
    W8VisibilityRecord* record;

    if (monster_info->party_threat.sight_state != W8_SIGHT_UNSEEN) {
        return false;
    }
    if (monster_info->player_visibility.sight_state != W8_SIGHT_UNSEEN) {
        for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (CurrentGame().party.participates(index) &&
                CurrentGame().party.dispositionToward(index, (monster_info)->ubDisposition) ==
                    W8_DISPOSITION_HOSTILE) {
                return false;
            }
        }
    }
    if (!party_only) {
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            other = GetMonsterListEntry(index);
            if (other != monster_info && other->fActive && other->hp_current != 0 &&
                other->highest_condition < W8_CONDITION_DEAD && other->pCombat &&
                MonsterHostility(CurrentGame().data, *monster_info, *other) ==
                    W8_DISPOSITION_HOSTILE &&
                (record = FindMonToMonVisibility(monster_info, other)) != 0 &&
                record->sight_state != W8_SIGHT_UNSEEN) {
                return false;
            }
        }
    }
    return true;
}

/* Whether any live member of the group has a visible target; the arguments
   forward to MonsterHasVisibleTarget. */
bool MonsterGroupHasVisibleTarget(W8MonsterGroup* monster_group, bool party_only,
                                  W8VisibleTargetFilter hostility, bool within_reach)
{
    unsigned int index;
    W8MonsterInfo* member;

    assert(monster_group != 0);
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        member = GetGroupMemberInfo(monster_group, index);
        if (member->fActive && member->hp_current > 0 &&
            member->highest_condition < W8_CONDITION_DEAD &&
            MonsterHasVisibleTarget(member, party_only, hostility, within_reach)) {
            return true;
        }
    }
    return false;
}

/* Whether the monster has a living target it can see. `party_only` skips the
   monster scan, `hostility` selects the class - three accepts anything and
   four anything non-neutral - and `within_reach` also requires the target
   inside the engagement range computed from the monster's best attack. */
bool MonsterHasVisibleTarget(W8MonsterInfo* monster_info, bool party_only,
                             W8VisibleTargetFilter hostility, bool within_reach)
{
    float reach;
    int sight;
    unsigned int index;
    W8MonsterInfo* other;
    W8VisibilityRecord* record;
    W8Disposition disposition;

    if (within_reach) {
        reach = CalcRangeDistance(GetMonsterBestRangeCategory(monster_info, true, &sight)) +
                GetMonsterCombatMoveRange(monster_info) * 1e+03f;
        if (reach <= GetMonsterEngagementRange() + kMonsterEngagementRangeFloor) {
            reach = GetMonsterEngagementRange() + kMonsterEngagementRangeFloor;
        }
    }
    if (monster_info->player_visibility.sight_state == W8_SIGHT_SEEN &&
        monster_info->player_visibility.los_flags[2] &&
        (!within_reach || monster_info->entity->GetDistanceToPlayer() <= reach)) {
        for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (CurrentGame().party.participates(index)) {
                disposition =
                    CurrentGame().party.dispositionToward(index, (monster_info)->ubDisposition);
                if (hostility == W8_VISIBLE_TARGET_ANY) {
                    return true;
                }
                if (static_cast<W8VisibleTargetFilter>(disposition) == hostility) {
                    return true;
                }
                if (hostility == W8_VISIBLE_TARGET_NON_NEUTRAL &&
                    disposition != W8_DISPOSITION_NEUTRAL) {
                    return true;
                }
            }
        }
    }
    if (!party_only) {
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            other = GetMonsterListEntry(index);
            if (other != monster_info && other->fActive && other->hp_current != 0 &&
                other->highest_condition < W8_CONDITION_DEAD && other->pCombat) {
                disposition = MonsterHostility(CurrentGame().data, *monster_info, *other);
                if (hostility == W8_VISIBLE_TARGET_ANY ||
                    static_cast<W8VisibleTargetFilter>(disposition) == hostility ||
                    (hostility == W8_VISIBLE_TARGET_NON_NEUTRAL &&
                     disposition != W8_DISPOSITION_NEUTRAL)) {
                    record = FindMonToMonVisibility(monster_info, other);
                    if (record != 0 && record->sight_state == W8_SIGHT_SEEN &&
                        record->los_flags[2]) {
                        if (!within_reach ||
                            monster_info->entity->GetDistanceToMonster(*other->entity) <= reach) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

/* How near the nearest member of a group has come. The search starts at
   999999 and keeps the smallest player distance any member reports. */
float GetGroupNearestDistance(W8MonsterGroup* group)
{
    unsigned int index;
    int location_id;
    W8MonsterInfo* monster_info;
    float furthest = 999999.0f;
    float distance;

    assert(group != 0);

    for (index = 0; index < group->MemberIds().size(); ++index) {
        location_id = group->MemberLocationId(index);
        monster_info = FindMonster(location_id);
        distance = monster_info->entity->GetDistanceToPlayer();
        if (distance < furthest) {
            furthest = distance;
        }
    }
    return furthest;
}

/* Follow the saved gameplay anchor even when no lure visual could be created. */
short IsMonsterControlPointInRange(W8MonsterInfo* monster_info)
{
    const auto* effect = monster_info->entity->owner_world.monsterControlEffect();
    if (!effect) return 0;
    const auto& anchor = effect->target.point;
    auto& nav = monster_info->entity->nav;
    const auto in_range = nav.SetMovementTargetToAnchor(anchor, 2500.0);
    if (!in_range) nav.AimAtPosition(&anchor);
    return in_range;
}

/* Whether a live hostile monster already fighting stands nearer to one of
   the group's members than to the party: such a reinforcement keeps a
   neutral group out of the leave sweep and brings it into combat. The group
   only counts members already flagged or whose record cannot idle, and only
   while an encounter list exists. */
bool MonsterGroupHasReinforcement(W8MonsterGroup* monster_group)
{
    unsigned int index;
    unsigned int other_index;
    W8MonsterInfo* member;
    W8MonsterInfo* other;
    float member_distance;
    float other_distance;
    float monster_distance;

    if (CurrentLevelPopulation().monster_roster.hostileCombatCount() == 0) {
        return false;
    }
    for (index = 0; index < monster_group->MemberIds().size(); ++index) {
        member = FindMonster(monster_group->MemberLocationId(index));
        if ((member->ubDisposition == W8_DISPOSITION_NEUTRAL &&
             GetMonsterDataForInfo(member)->camouflage1 != 0) ||
            !member->party_threat.los_flags[1]) {
            continue;
        }
        member_distance = member->entity->GetDistanceToPlayer();
        if (member_distance > GetMonsterEngagementRange()) {
            continue;
        }
        for (other_index = 0; other_index < CurrentLevelPopulation().monster_roster.live().size();
             ++other_index) {
            other = GetMonsterListEntry(other_index);
            if (other != member && other->party_threat.los_flags[1] && other->fActive &&
                other->pCombat && other->hp_current != 0 &&
                other->ubDisposition == W8_DISPOSITION_HOSTILE) {
                other_distance = other->entity->GetDistanceToPlayer();
                monster_distance = member->entity->GetDistanceToMonster(*other->entity);
                if (monster_distance + member_distance <
                        other_distance * kReinforcementDistanceSlack &&
                    (member_distance < other_distance || monster_distance < other_distance)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Member info by group list position: entry id at the index, resolved through
   the location index to the script part. Assert lines 3199/3204 pin it late
   in the original file. */
static W8MonsterInfo* GetGroupMemberInfo(W8MonsterGroup* group, unsigned int index)
{
    assert(group != 0);
    return FindMonster(group->MemberLocationId(index));
}

/* Recompute each unled hostile group's engagement byte once the combat state
   has settled: a member still finding its feet (settle_ticks under three) in the
   group or an allied group drops it, and past that the group's own counter
   decides. */
void UpdateMonsterGroupEngagement(void)
{
    W8MonsterGroup* group;
    W8MonsterGroup* allied;
    W8MonsterInfo* member;
    unsigned int group_index;
    unsigned int index;
    int allied_index;
    int member_starting;

    if (CurrentGame().combat->combat_update_count <= 2) {
        return;
    }
    for (group_index = 0; group_index < CurrentLevelPopulation().groups().size(); ++group_index) {
        group = CurrentLevelPopulation().groups()[group_index].get();
        if (!group->members_active || !group->fInCombat ||
            group->ubDisposition != W8_DISPOSITION_HOSTILE || group->leader_group_id != 0) {
            continue;
        }
        member_starting = 0;
        for (index = 0; index < group->MemberIds().size(); ++index) {
            member = FindMonster(group->MemberLocationId(index));
            if (member->fActive && member->pCombat &&
                static_cast<unsigned int>(member->pCombat->settle_ticks) < 3) {
                member_starting = 1;
                break;
            }
        }
        for (allied_index = 0; member_starting == 0 && allied_index < 4; ++allied_index) {
            if (group->allied_group_ids[allied_index] != 0) {
                allied = FindMonsterGroup(group->allied_group_ids[allied_index]);
                for (index = 0; index < allied->MemberIds().size(); ++index) {
                    member = FindMonster(allied->MemberLocationId(index));
                    if (member->fActive && member->pCombat &&
                        static_cast<unsigned int>(member->pCombat->settle_ticks) < 3) {
                        member_starting = 1;
                        break;
                    }
                }
            }
        }
        SetMonsterGroupEngagementState(group->group_id,
                                       member_starting == 0 ? group->engagement_ticks < 3 : 0);
    }
}

/* The out-of-combat sweep: refreshes sight, alerts the faction groups of
   every group already fighting, then enters combat for the groups that
   should - promoting a neutral group's disposition when its record says the
   encounter turns it hostile. */
void CheckMonsterGroupsEnterCombat(void)
{
    W8MonsterGroup* group;
    const W8MonsterRecord* record;
    unsigned int index;

    CurrentGame().refreshSight();
    for (index = 0; index < CurrentLevelPopulation().groups().size(); ++index) {
        group = CurrentLevelPopulation().groups()[index].get();
        if (group->fInCombat && group->ubDisposition == W8_DISPOSITION_HOSTILE) {
            CurrentGame().alertFactionGroups(CurrentGame().level.loaded(), group);
        }
    }
    for (index = 0; index < CurrentLevelPopulation().groups().size(); ++index) {
        group = CurrentLevelPopulation().groups()[index].get();
        if (!group->fInCombat) {
            if (ShouldMonsterGroupEnterCombat(group)) {
                MonsterGroupEnterCombat(group);
                if (group->ubDisposition == W8_DISPOSITION_NEUTRAL) {
                    record = MonsterGroupGetRecord(group);
                    if ((record->flags & W8_MONSTER_FLAG_NPC) == 0 &&
                        record->faction_id == W8_FACTION_UNALIGNED &&
                        record->hostility_radius != 0 && record->hostility_radius != -1) {
                        CurrentGame().setGroupHostility(CurrentGame().level.loaded(), group,
                                                        W8_DISPOSITION_HOSTILE, false);
                    }
                }
            }
        }
    }
}

/* Whether a group outside combat should join it. Neutral groups need a
   reinforcement; hostile ones need a member with a visible non-neutral
   target inside the leader's reach, the leader itself close enough to walk
   to the party, or a rendered member already near the party. */
bool ShouldMonsterGroupEnterCombat(W8MonsterGroup* monster_group)
{
    W8MonsterInfo* leader;
    W8MonsterInfo* member;
    unsigned int index;
    int sight;
    float reach;
    float minimum;
    float path_distance;
    glm::vec3 party;

    if (CurrentLevelPopulation().allMembersDying(*monster_group) ||
        !monster_group->members_active) {
        return false;
    }
    if (monster_group->leader_location_id == -0x32323233 ||
        (leader = FindMonster(monster_group->leader_location_id)) == 0 || !leader->entity) {
        return false;
    }
    if (!CurrentGame().combat.has_value()) {
        return false;
    }
    if (monster_group->ubDisposition == W8_DISPOSITION_NEUTRAL) {
        if (MonsterGroupHasReinforcement(monster_group)) {
            return true;
        }
    } else {
        for (index = 0; index < monster_group->MemberIds().size(); ++index) {
            member = GetGroupMemberInfo(monster_group, index);
            if (member->fActive && member->hp_current != 0 &&
                member->highest_condition < W8_CONDITION_DEAD &&
                MonsterHasVisibleTarget(member, false, W8_VISIBLE_TARGET_NON_NEUTRAL, true)) {
                reach = CalcRangeDistance(GetMonsterBestRangeCategory(leader, true, &sight)) +
                        GetMonsterCombatMoveRange(leader) * 1e+03f;
                minimum = GetMonsterEngagementRange() + kMonsterEngagementRangeFloor;
                if (reach <= minimum) {
                    reach = minimum;
                }
                party = leader->entity->owner_world.party.position();
                if (glm::length((leader->entity->nav.movement.position - party)) <= reach &&
                    leader->entity->nav.FindNavigatorPathDistance(750.0f, &path_distance) &&
                    path_distance <= reach) {
                    return true;
                }
                break;
            }
        }
        if (monster_group->ubDisposition == W8_DISPOSITION_HOSTILE &&
            MonsterGroupHasRenderableMember(monster_group, true)) {
            if (GetGroupNearestDistance(monster_group) <= CalcRangeDistance(W8_RANGE_EXTREME)) {
                return true;
            }
        }
    }
    return false;
}
