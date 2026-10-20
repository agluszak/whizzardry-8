#include "wiz8/local_code/MonsterAI.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "monster_actions.h"
#include "monster_cycles.h"
#include "sight_state.h"
#include "wiz8/character_skills.h"
#include "wiz8/checked_integer.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GroupAttacks.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

constexpr float kGroupEngagementProbeScale = 333.33333f;
constexpr float kMonsterEngagementRangeFloor = 7500.0f;
constexpr float kReinforcementDistanceSlack = 1.15f;

/* The special-attack table row value that marks a summon; flee and control
   rules treat those rows differently. */
enum { W8_SPECIAL_ATTACK_EFFECT_SUMMON = 6 };

/* Gates the out-of-combat check that gives a group whose leader is still up a
   nudge. Only UpdateMonsterGroups reads it. */
static bool g_out_of_combat_rtai_enabled = true;

/* The frame counter the staggered group update rotates through the group
   list, one fifth per frame with a full-distance pass every twentieth. */
static unsigned int g_monster_group_tick = 0;

static W8MonsterInfo* GetGroupMemberInfo(W8MonsterGroup* group, unsigned int index);
static void QueueMonsterAction(W8MonsterInfo* monster_info, W8MonsterActionKind action_kind,
                               int action_detail, int attack_index, W8TargetKind target_kind,
                               int target_value);

/* 125000, the cap on how far a monster will walk to investigate a heard
   noise. */
int g_noise_investigate_radius_cap = 125000;

/* Two dwords per special-attack kind; only the leading dword is
   read here. */
int g_special_attack_table[32][2] = {
    {0, 0}, {1, 0}, {1, 0}, {2, 3}, {4, 0}, {1, 1}, {1, 1}, {1, 0}, {5, 4}, {5, 0}, {1, 5},
    {1, 1}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 1}, {1, 5}, {1, 0},
    {1, 1}, {1, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {6, 0}, {5, 0}, {6, 0},
};

/* The spell that fills each being effect slot, walked by
   MonsterSpellTargetOK to tell whether a buff is already running; the
   monster side indexes effect_slots, the party side the matching rows
   in CurrentGame().status. */
const int g_being_effect_slot_spells[12] = {
    W8_SPELL_ARMORPLATE, W8_SPELL_CHAMELEON,    W8_SPELL_DETECT_SECRETS, W8_SPELL_ENCHANTED_BLADE,
    W8_SPELL_LIGHT,      W8_SPELL_MAGIC_SCREEN, W8_SPELL_MISSILE_SHIELD, W8_SPELL_SHADOW_HOUND,
    W8_SPELL_X_RAY,      W8_SPELL_NONE,         W8_SPELL_NONE,           W8_SPELL_NONE,
};

/* The combat-state spell per effect slot, walked against
   W8CombatState::effect_slots and the monster's combat_effects. */
const int g_combat_effect_slot_spells[9] = {
    W8_SPELL_ARMORMELT, W8_SPELL_ACID_BOMB,   W8_SPELL_TOXIC_CLOUD,
    W8_SPELL_FIRESTORM, W8_SPELL_DEATH_CLOUD, W8_SPELL_DRAINING_CLOUD,
    W8_SPELL_NONE,      W8_SPELL_NONE,        W8_SPELL_NONE,
};

/* The same mapping for the second combat effect block, indexed against
   W8CombatState::effect_slots0 and the monster's combat_effects_2. One
   retail array: [0..6) are spell ids, [6..23) — the retail sub-table at
   0x00616DF0 — is the spell-point budget/failure percentage indexed by cost
   band that Magic.cpp's failure and power-level readers consume. */
const int g_combat_effect_slot_spells_and_cast_success[23] = {
    W8_SPELL_BLESS,
    W8_SPELL_ELEMENT_SHIELD,
    W8_SPELL_SOUL_SHIELD,
    W8_SPELL_RING_OF_FIRE,
    W8_SPELL_NONE,
    W8_SPELL_NONE,
    30,
    40,
    50,
    58,
    64,
    70,
    76,
    81,
    86,
    90,
    94,
    97,
    100,
    102,
    105,
    107,
    110,
};

/* The per-slot weights ChooseMonsterSpell rolls against. */
static int g_spell_cast_weights[10] = {5, 5, 5, 10, 10, 10, 10, 15, 15, 15};

/* Reported once, so a monster missing its special-attack cycle does not flood
   the log. */
static bool g_special_attack_cycle_error_reported;

/* The timed sight service. The dirty flag is raised by the condition and
   enchantment writers whenever what monsters can see of the party may have
   changed; out of combat sight is refreshed unconditionally, in combat only
   when the flag says so, and detection runs while the combat state permits. */
void UpdateMonsterSight(void)
{
    if (!CurrentGame().combat.active() || CurrentGame().sight_refresh_pending) {
        if (CurrentGame().sight_refresh_pending) {
            CurrentGame().sight_refresh_pending = false;
        }
        RefreshOutwardSightForAllMonsters();
        if (CurrentGame().combat.active() && CurrentGame().combat.state()->round_count == 0) {
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

    if (!AnyCharacterActive()) {
        return;
    }
    if ((CurrentLevelPopulation().monster_groups.empty())) {
        return;
    }
    ++g_monster_group_tick;
    if (!staggered) {
        UpdateMonsterSight();
    }
    camera_position = cameraLocation(*GetWorld());
    for (group_list_index = 0; group_list_index < CurrentLevelPopulation().monster_groups.size();
         ++group_list_index) {
        if (staggered && group_list_index % 5 != g_monster_group_tick % 5) {
            continue;
        }
        monster_group = CurrentLevelPopulation().monster_groups[group_list_index].get();
        if (MonsterGroupAllMembersDying(monster_group)) {
            continue;
        }
        monster_info = FindMonster(monster_group->leader_location_id);
        if (monster_info == 0 || monster_info->p3D == 0) {
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
                LoadMonsterGroupMembers(monster_group);
            }
        }
        if (monster_group->members_active && !monster_group->fInCombat) {
            double far_clip;
            if (monster_group->leader_group_id == 0) {
                RefreshMonsterGroupHostility(monster_group);
            }
            far_clip = WorldGetFarClip(GetWorld());
            if (nearest_distance <= far_clip + far_clip) {
                if (!CurrentGame().combat.active() && MonsterGroupCanEngage(monster_group)) {
                    MonsterGroupEnterCombat(monster_group);
                }
            } else {
                DetachMonsterGroup(monster_group);
            }
        }
        if (g_out_of_combat_rtai_enabled && !CurrentGame().combat.active()) {
            if (monster_group == 0) {
                Fail("pMonsterGroup");
            }
            if (monster_group->leader_group_id == 0) {
                monster_info = FindMonster(monster_group->leader_location_id);
                if (!monster_info->p3D->IsDying()) {
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
        if (monster_info->fActive && !monster_info->p3D->IsDying() &&
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
    if (engage && IsSightRangeOverridden() && monster_info->ai_mode == W8_RT_AI_CHARGE_PARTY) {
        const W8MonsterRecord* record;
        W8MonsterGroup* monster_group;
        Navigator* navigator;
        unsigned char best_range;
        unsigned int attack;

        if (monster_info->pCombat) {
            return;
        }
        if (monster_info == 0) {
            Fail("pMonsterInfo");
        }
        record = GetMonsterDataForInfo(monster_info);
        best_range = 0;
        for (attack = 0; attack < W8_MAX_MONSTER_ATTACKS; ++attack) {
            if (RateMonsterAttack(monster_info, record, attack, 0, false) ==
                    W8_MONSTER_ATTACK_OUT_OF_REACH &&
                best_range < record->attacks[attack].range_category) {
                best_range = record->attacks[attack].range_category;
            }
        }
        if (monster_info->p3D->ApproachStartupNavigator(
                CalcRangeDistance(static_cast<W8RangeCategory>(best_range)) * 0.9f) == 0) {
            return;
        }
        monster_group = FindMonsterGroup(monster_info->monster_group_id);
        MonsterGroupEnterCombat(monster_group);
        navigator = monster_info->p3D->nav.linked_navigator;
        if (navigator != 0) {
            navigator->group_linked = true;
        } else {
            monster_info->p3D->nav.group_linked = true;
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
                if (!monster_info->p3D->orders_finished) {
                    decision = W8_RT_AI_IDLE;
                } else {
                    update = ChooseMonsterRTAIMode(monster_info, &decision);
                }
            } else if (alert <= W8_SIGHT_RECENT) {
                switch (monster_info->ubDisposition) {
                case W8_DISPOSITION_NEUTRAL:
                    if (!monster_info->p3D->orders_finished) {
                        decision = W8_RT_AI_IDLE;
                    } else {
                        update = ChooseMonsterRTAIMode(monster_info, &decision);
                    }
                    break;
                case W8_DISPOSITION_HOSTILE:
                    decision = static_cast<unsigned char>(
                                   monster_info->hp_current * 100 / monster_info->uiHPMax <= 0x14) +
                               1;
                    if (monster_info->p3D->nav.movement_stopped) {
                        update = true;
                    }
                    break;
                case W8_DISPOSITION_FRIENDLY:
                    if (!monster_info->p3D->orders_finished) {
                        decision = W8_RT_AI_IDLE;
                    } else {
                        update = ChooseMonsterRTAIMode(monster_info, &decision);
                    }
                    break;
                default:
                    Fail("FALSE", "DoMonsterRTAI: ERROR - Invalid disposition");
                    return;
                }
            }
        }
    }
    if ((monster_info->ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
        unsigned char mode;

        mode = monster_info->ai_mode & W8_MONSTER_AI_MODE_MASK;
        if (mode != W8_RT_AI_PATROL_AREA &&
            (!monster_info->p3D->face_party || monster_info->pathing_cooldown != 0 ||
             mode != W8_RT_AI_FACE_DIRECTION || !monster_info->player_visibility.line_of_sight ||
             glm::length((((monster_info->p3D->nav.movement.position -
                            PartyNavigator()->GetPosition())))) >= 5e+03f)) {
            monster_info->ai_mode &= ~W8_MONSTER_AI_REAPPLY_MODE;
        }
        if (decision <= W8_RT_AI_LINK_TO_PARTY || decision == W8_RT_AI_FOLLOW_LURE) {
            update = true;
        }
    }
    if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE ||
        monster_info->p3D->script_wait != MONSCR_WALKTO || decision != W8_RT_AI_IDLE) {
        if (decision != monster_info->ai_mode || update) {
            if (!engage && decision == W8_RT_AI_CHARGE_PARTY) {
                Fail("ubAIDecision != RT_AI_MODE_CHARGE_PARTY");
            }
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
    Monster* monster;
    bool changed;
    char mode;

    monster = monster_info->p3D;
    mode = W8_RT_AI_IDLE;
    changed = false;
    if (monster->face_party && monster_info->pathing_cooldown == 0 &&
        monster_info->player_visibility.line_of_sight) {
        glm::vec3 delta;

        delta = monster->nav.movement.position - PartyNavigator()->GetPosition();
        if (glm::length(delta) < 5e+03f) {
            glm::vec3 camera;
            glm::vec3 position;

            camera = cameraLocation();
            position = monster->nav.GetPosition();
            *decision = W8_RT_AI_FACE_DIRECTION;
            monster->move_direction = camera - position;
            return true;
        }
    }
    if (MonsterGroupHasIncapacitatedMember(monster_info->monster_group_id)) {
        *decision = W8_RT_AI_IDLE;
        return false;
    }
    if ((monster_info->ai_mode & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
        if (monster->order_mode == W8_MONSTER_ORDER_POINT_PATROL ||
            monster->order_mode == W8_MONSTER_ORDER_RANDOM_POINT_PATROL) {
            glm::vec3 delta;
            glm::vec3 patrol;

            monster->GetPatrolPoint(&patrol);
            delta = patrol - monster->nav.GetPosition();
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
        fabsf(monster->nav.movement.target_yaw - monster->nav.movement.yaw) >=
            g_camera_transition_epsilon) {
        mode = W8_RT_AI_FACE_NOISE;
    } else {
        if (monster_info->heard_noise_radius > 0 && monster->nav.movement_stopped &&
            monster_info->ai_mode == W8_RT_AI_INVESTIGATE_NOISE) {
            glm::vec3 delta;

            delta = monster_info->heard_noise_position - monster->nav.GetPosition();
            if (glm::length(delta) < 1500.0) {
                monster_info->heard_noise_radius = 0;
            }
        }
        if (monster_info->heard_noise_radius > 0) {
            if (!monster->nav.movement_stopped &&
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
                position = monster->nav.GetPosition();
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
                monster->move_direction.x = static_cast<float>(cos(angle) * 500.0);
                monster->move_direction.y = 0.0f;
                monster->move_direction.z = static_cast<float>(sin(angle) * 500.0);
            } else {
                mode = W8_RT_AI_IDLE;
            }
        } else {
            switch (monster->order_mode) {
            case W8_MONSTER_ORDER_NONE:
                break;
            case W8_MONSTER_ORDER_GUARD: {
                glm::vec3 delta;
                glm::vec3 patrol;

                monster->GetPatrolPoint(&patrol);
                delta = patrol - monster->nav.GetPosition();
                if (glm::length(delta) < 1500.0) {
                    mode = W8_RT_AI_IDLE;
                } else {
                    mode = W8_RT_AI_MOVE_TO_PATROL_POINT;
                }
                break;
            }
            case W8_MONSTER_ORDER_PATROL:
                mode = W8_RT_AI_PATROL_AREA;
                if (monster->nav.movement_stopped) {
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
                monster->GetPatrolPoint(&patrol);
                delta = patrol - monster->nav.GetPosition();
                if (glm::length(delta) >= 1500.0) {
                    break;
                }
                count = static_cast<int>(monster->vector.size());
                if (monster->order_mode == W8_MONSTER_ORDER_POINT_PATROL) {
                    next = monster->patrol_index + 1;
                    if (next < count) {
                        monster->patrol_index = static_cast<signed char>(next);
                    } else {
                        monster->patrol_index = 0;
                    }
                } else if (count < 2) {
                    monster->patrol_index = 0;
                } else if ((monster_info->ai_mode & W8_MONSTER_AI_MODE_MASK) ==
                           W8_RT_AI_MOVE_TO_PATROL_POINT) {
                    do {
                        next = static_cast<signed char>(CurrentGame().rng.below(count));
                    } while (next == monster->patrol_index);
                    monster->patrol_index = static_cast<signed char>(next);
                } else {
                    monster->patrol_index =
                        static_cast<signed char>(CurrentGame().rng.below(count));
                }
                changed = true;
                break;
            }
            case W8_MONSTER_ORDER_FACE_DIRECTION:
                monster->move_direction =
                    glm::vec3(monster->direction_x, monster->direction_y, monster->direction_z);
                mode = W8_RT_AI_FACE_DIRECTION;
                changed = true;
                break;
            }
        }
    }
    if (mode != static_cast<char>(monster_info->ai_mode)) {
        changed = true;
    }
    *decision = static_cast<unsigned char>(mode);
    return changed;
}

static void HandleMonsterPatrolFailure(W8MonsterInfo* monster_info)
{
    W8MonsterGroup* monster_group = FindMonsterGroup(monster_info->monster_group_id);
    if (monster_group->encounter_registered) {
        if (g_dev_mode) {
            DebugMessage(
                0, fmt::format(
                       "Monster {:d} and associated monsters killed because it couldn't patrol",
                       monster_info->location_id));
        }
        MarkMonsterGroupForRemoval(monster_info->monster_group_id);
    } else {
        if (g_dev_mode) {
            DebugMessage(0, fmt::format("{} {:d} can't path!", GetMonsterName(monster_info, 0, 0),
                                        monster_info->location_id));
        }
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
    Monster* monster = monster_info->p3D;
    W8MonsterGroup* monster_group;
    const W8MonsterRecord* record;
    W8MonsterInfo* member;
    W8SpellEffectEntry* effect;
    SpellVisual* visual;
    glm::vec3 position;
    glm::vec3 patrol_point;
    unsigned char best_range;
    unsigned int attack;
    unsigned int index;

    switch (decision & W8_MONSTER_AI_MODE_MASK) {
    case W8_RT_AI_CHARGE_PARTY:
        best_range = 0;
        if (monster_info == 0) {
            Fail("pMonsterInfo");
        }
        record = GetMonsterDataForInfo(monster_info);
        for (attack = 0; attack < 3; ++attack) {
            if (RateMonsterAttack(monster_info, record, attack, 0, false) == 3 &&
                best_range < record->attacks[attack].range_category) {
                best_range = record->attacks[attack].range_category;
            }
        }
        if (monster->ApproachStartupNavigator(
                CalcRangeDistance(static_cast<W8RangeCategory>(best_range)) * 0.9f) == 1) {
            if (IsSightRangeOverridden()) {
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
        monster->LinkToStartupNavigator();
        break;
    case W8_RT_AI_APPROACH_PARTY:
        if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE) {
            Fail("pMonsterInfo->ubDisposition != DISP_HOSTILE");
        }
        monster->ApproachStartupNavigator(3000.0);
        break;
    case W8_RT_AI_INVESTIGATE_NOISE:
        position = monster_info->heard_noise_position;
        monster->nav.ConfigureMovementToPosition(&position);
        break;
    case W8_RT_AI_PATROL_AREA:
        if ((decision & W8_MONSTER_AI_REAPPLY_MODE) != 0) {
            monster->nav.SetHeightRange(monster->patrol_distance, monster->patrol_variation);
            monster_info->ai_mode &= ~W8_MONSTER_AI_REAPPLY_MODE;
            break;
        }
        if (monster->formation == glm::vec3(0)) {
            monster->formation = monster->nav.GetPosition();
        }
        position = monster->formation;
        if (monster->nav.StartPatrol(&position, monster->patrol_distance,
                                     monster->patrol_variation)) {
            break;
        }
        HandleMonsterPatrolFailure(monster_info);
        decision = W8_RT_AI_IDLE;
        break;
    case W8_RT_AI_MOVE_TO_PATROL_POINT:
        if (!monster->GetPatrolPoint(&patrol_point)) {
            decision = W8_RT_AI_IDLE;
            break;
        }
        if (monster->nav.ConfigureMovementToPosition(&patrol_point) != 0) {
            break;
        }
        if (!monster->IsWithinWorldRange() &&
            monster_info->party_threat.sight_state == W8_SIGHT_SEEN) {
            monster_group = FindMonsterGroup(monster_info->monster_group_id);
            position = patrol_point;
            if (MoveMonsterGroupToPosition(monster_group, &position, monster->nav.GetYaw(), false,
                                           true, false, false)) {
                break;
            }
        }
        HandleMonsterPatrolFailure(monster_info);
        decision = W8_RT_AI_IDLE;
        break;
    case W8_RT_AI_FACE_NOISE:
        monster->nav.flags &= ~0x20000000;
        monster->nav.ClearMovement();
        position = monster_info->heard_noise_position;
        monster_info->heard_noise_radius = 0;
        monster_info->pathing_cooldown = 0x1e;
        monster_group = FindMonsterGroup(monster_info->monster_group_id);
        for (index = 0; index < monster_group->MemberIds().size(); ++index) {
            member = FindMonster(monster_group->MemberLocationId(index));
            member->p3D->nav.AimAtPosition(&position);
        }
        break;
    case W8_RT_AI_FOLLOW_LURE:
        effect = FindMonsterControlSpellEffect();
        if (effect == 0 || (visual = effect->spell_visuals[0]) == 0) {
            break;
        }
        if (monster->nav.SetMovementTargetToNavigator(&(visual)->nav, 2500.0) == 0) {
            position = visual->nav.GetPosition();
            monster->nav.AimAtPosition(&position);
        }
        break;
    case W8_RT_AI_FACE_DIRECTION:
        monster->nav.flags &= ~0x20000000;
        monster->nav.ClearMovement();
        position = monster->nav.GetPosition();
        position += monster->move_direction;
        monster->nav.AimAtPosition(&position);
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
            UpdateMonsterAI(monster_info);
        }
    }
}

/* The cycle a monster must have to cast at all. */

/* Reported once, so a monster missing its spell cycle does not flood the log. */
static bool g_spell_cycle_error_reported;

/* Decide what one monster does this round. A monster taken out of the fight
   by its worst condition, or told to give up, stands down and ends its turn.
   Otherwise it rolls to hold back, and if not, a monster that is not yet
   engaged either holds or gives up by its record. An engaged monster with a
   usable ranged attack and the party out of reach closes in (or backs off
   when hurt); otherwise it rolls to flee and to cast, weighs its attacks, and
   only then settles for closing in, backing off or holding. Whatever it
   settles on is checked, and a group that has given up holds instead. */
void UpdateMonsterAI(W8MonsterInfo* monster_info)
{
    const W8MonsterRecord* record;
    W8RangeCategory range_category;
    unsigned int chance;
    unsigned char rating;
    unsigned int spell;
    W8CombatSlot chosen;
    float hp_ratio;
    bool backs_off;
    glm::vec3 position;

    if (monster_info->highest_condition >= W8_CONDITION_ASLEEP) {
        monster_info->action_kind = W8_MONSTER_ACTION_NONE;
        monster_info->pCombat->phase = 0;
        monster_info->pCombat->active = true;
        return;
    }
    record = GetMonsterDataForInfo(monster_info);
    if (monster_info->monster_species == 0x224) {
        monster_info->action_kind = W8_MONSTER_ACTION_WAIT;
        return;
    }
    chance = MonsterAdvanceChance(monster_info, record);
    if (CurrentGame().rng.below(100) < chance) {
        monster_info->action_kind = W8_MONSTER_ACTION_ADVANCE;
        goto validate;
    }
    if (monster_info->ubDisposition == W8_DISPOSITION_NEUTRAL) {
        if (record->camouflage1 != 0) {
            monster_info->action_kind = W8_MONSTER_ACTION_NONE;
            monster_info->pCombat->phase = 0;
            monster_info->pCombat->active = true;
        } else {
            monster_info->action_kind = W8_MONSTER_ACTION_RETURN_TO_START;
        }
        goto validate;
    }
    if (record->prefer_ranged_actions == 0 &&
        (range_category = GetBestMonsterAttackRange(record, true)) != W8_RANGE_NONE &&
        (monster_info->uiCondition[W8_CONDITION_BLIND] == 0 || record->kind == 0xc) &&
        MonsterChooseTarget(monster_info, &chosen, W8VisibilityCondition::FirstLineOfSightFlag) >
            CalcRangeDistance(range_category)) {
        record = GetMonsterDataForInfo(monster_info);
        hp_ratio = monster_info->hp_current / static_cast<float>(monster_info->uiHPMax);
        backs_off = hp_ratio <= 0.95f && record->prefer_ranged_actions == 0;
        monster_info->action_kind =
            backs_off ? W8_MONSTER_ACTION_BACK_OFF : W8_MONSTER_ACTION_APPROACH;
    } else {
        rating = RateMonsterBestAttack(monster_info, record, false);
        chance = record->flee_chance;
        if (chance != 0) {
            if (rating != 0) {
                chance = 100;
            }
            if (CanMonsterFlee(monster_info, record, false) &&
                CurrentGame().rng.below(100) < chance) {
                monster_info->action_kind = W8_MONSTER_ACTION_SPECIAL_ATTACK;
                if (g_special_attack_table[record->special_attack_kind][0] ==
                    W8_SPECIAL_ATTACK_EFFECT_SUMMON) {
                    position = monster_info->p3D->nav.GetPosition();
                    ResetCombatSlot(&monster_info->Target);
                    monster_info->Target.iType = W8_TARGET_KIND_PLACE;
                    monster_info->Target.point = position;
                } else if (!AimMonsterAtSpellTarget(monster_info, W8_SPELL_SPECIAL_ATTACK_CONE)) {
                    Fail("fSuccess");
                }
                goto validate;
            }
        }
        chance = record->spell_chance;
        if (chance != 0) {
            if (rating != 0) {
                chance = 100;
            }
            if (record->spell_chance != 0) {
                if (!monster_info->p3D->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                    if (!g_spell_cycle_error_reported) {
                        DebugMessage(0, fmt::format("ERROR: {} is missing a SPELL animation cycle",
                                                    record->name0));
                        g_spell_cycle_error_reported = true;
                    }
                } else {
                    for (spell = 0; spell < 10; ++spell) {
                        if (IsSpellUsableByMonster(monster_info, record->spells[spell], true)) {
                            if (CurrentGame().rng.below(100) < chance) {
                                monster_info->action_kind = W8_MONSTER_ACTION_SPELL;
                                monster_info->action_detail =
                                    ChooseMonsterSpell(monster_info, record);
                                if (!AimMonsterAtSpellTarget(monster_info,
                                                             monster_info->action_detail)) {
                                    Fail("fSuccess");
                                }
                                goto validate;
                            }
                            break;
                        }
                    }
                }
            }
        }
        if (rating != 0) {
            if (rating == W8_MONSTER_ATTACK_OUT_OF_REACH) {
                if (monster_info->uiCondition[W8_CONDITION_BLIND] != 0 && record->kind != 0xc) {
                    monster_info->action_kind = W8_MONSTER_ACTION_RETURN_TO_START;
                } else {
                    record = GetMonsterDataForInfo(monster_info);
                    hp_ratio = monster_info->hp_current / static_cast<float>(monster_info->uiHPMax);
                    backs_off = hp_ratio <= 0.95f && record->prefer_ranged_actions == 0;
                    monster_info->action_kind =
                        backs_off ? W8_MONSTER_ACTION_BACK_OFF : W8_MONSTER_ACTION_APPROACH;
                }
            } else {
                monster_info->action_kind = W8_MONSTER_ACTION_RETURN_TO_START;
            }
        } else if (!ChooseRandomMonsterAction(monster_info, false, false, true)) {
            monster_info->action_kind = W8_MONSTER_ACTION_WAIT;
        }
    }
validate:
    if (!IsMonsterActionUsable(monster_info) &&
        GetMonsterGroupEngagementState(monster_info->monster_group_id)) {
        monster_info->action_kind = W8_MONSTER_ACTION_RETURN_TO_START;
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

    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
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
        if (!member->fActive || member->fMotionless || member->p3D->IsDying() ||
            member->hp_current == 0 || member->highest_condition >= W8_CONDITION_BLIND) {
            continue;
        }
        if (RateMonsterBestAttack(member, record, false) == 0) {
            return true;
        }
        if (record->spell_chance != 0) {
            if (!member->p3D->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                if (!g_spell_cycle_error_reported) {
                    DebugMessage(0, fmt::format("ERROR: {} is missing a SPELL animation cycle",
                                                record->name0));
                    g_spell_cycle_error_reported = true;
                }
            } else {
                for (spell = 0; spell < 10; ++spell) {
                    if (MonsterCanAimSpell(record->spells[spell]) &&
                        IsSpellUsableByMonster(member, record->spells[spell], true)) {
                        return true;
                    }
                }
            }
        }
        if (CanMonsterFlee(member, record, true)) {
            return true;
        }
        if (GetBestMonsterAttackRange(record, false) <= W8_RANGE_SHORT) {
            distance = member->p3D->GetDistanceToPlayer();
            if (GetMonsterCombatMoveRange(member) * kGroupEngagementProbeScale > distance) {
                if (!waypoint_checked) {
                    leader = FindMonster(monster_group->leader_location_id);
                    if (leader != 0 && leader->fActive) {
                        destination = PartyNavigator()->GetPosition();
                        source = leader->p3D->nav.GetPosition();
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

/* The percentage chance the monster chooses the advance action this round. A
   badly hurt NPC-disposition monster always presses on, a monster already
   committed keeps advancing until an enemy is inside short range, and a
   record without an explicit advance chance advances unless its behavior byte
   says otherwise. */
unsigned int MonsterAdvanceChance(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    unsigned int hp_percent;
    unsigned int result = 0;
    W8CombatSlot chosen;
    float distance;

    hp_percent = monster_info->hp_current * 100 / monster_info->uiHPMax;
    if (!monster_info->pCombat) {
        Fail("pMonsterInfo->pCombat");
    }
    if ((record->flags & W8_MONSTER_FLAG_NPC) != 0 &&
        monster_info->ubDisposition != W8_DISPOSITION_HOSTILE && hp_percent <= 33) {
        return 100;
    }
    if (monster_info->pCombat->advancing) {
        distance =
            MonsterChooseTarget(monster_info, &chosen, W8VisibilityCondition::FirstLineOfSightFlag);
        if (CalcRangeDistance(W8_RANGE_SHORT) < distance) {
            result = 100;
        } else {
            monster_info->pCombat->advancing = false;
        }
    } else if (record->advance_chance != 0) {
        result = record->advance_chance;
    } else if (record->combat_morale == 0) {
        result = 100;
    }
    return result;
}

/* Refill the monster's pending-action queue with everything it may do this
   round. A fresh pick first rolls the cooperative sweep, queueing a kind-8
   entry for every character and other monster the monster has an attack on,
   and returns early when any of them stuck; a behavior-2 record may then add
   the kind-1 wait action. The rest enumerates attack candidates: each usable
   attack contributes one kind-0 entry per set attack-mode bit for every
   eligible character and monster target. `target_locked` keeps only the
   stored target; `attack_locked` keeps only the committed attack and always
   sweeps every target. A character's evasion skill roll keeps attacks off it
   but flags the slot, and each flagged slot may practice that skill at the
   end. */
void BuildMonsterActionQueue(W8MonsterInfo* monster_info, bool target_locked, bool attack_locked)
{
    W8MonsterInfo* locked_monster = nullptr;
    W8MonsterInfo* other;
    const W8MonsterRecord* record;
    unsigned int index;
    unsigned int attack;
    unsigned int mode_bit;
    unsigned int char_lo;
    unsigned int char_hi;
    std::size_t monster_lo;
    std::size_t monster_hi;
    unsigned int attack_lo;
    unsigned int attack_hi;
    bool scan_chars = false;
    bool scan_monsters = false;
    unsigned char avoided[8] = {0};
    unsigned char resisted[8] = {0};
    bool friendly_targets;
    W8Disposition disposition_needed;

    record = GetMonsterDataForInfo(monster_info);
    auto& actions = monster_info->pCombat->actions;
    actions.clear();
    if (!target_locked && !attack_locked) {
        if (CanMonsterProtect(monster_info) &&
            CurrentGame().rng.below(100) <
                monster_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) {
            monster_info->action_kind = W8_MONSTER_ACTION_PROTECT;
            for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
                 ++index) {
                other = GetMonsterListEntry(index);
                if (other != monster_info) {
                    ResetCombatSlot(&monster_info->Target);
                    monster_info->Target.iType = W8_TARGET_KIND_MONSTER;
                    monster_info->Target.iMonsterID = other->location_id;
                    if (CanMonsterProtectCombatant(monster_info, &monster_info->Target)) {
                        QueueMonsterAction(monster_info, W8_MONSTER_ACTION_PROTECT, 0, 0,
                                           W8_TARGET_KIND_MONSTER, other->location_id);
                    }
                }
            }
            for (index = 0; index < 8; ++index) {
                ResetCombatSlot(&monster_info->Target);
                monster_info->Target.iType = W8_TARGET_KIND_CHARACTER;
                monster_info->Target.iChar = index;
                if (CanMonsterProtectCombatant(monster_info, &monster_info->Target)) {
                    QueueMonsterAction(monster_info, W8_MONSTER_ACTION_PROTECT, 0, 0,
                                       W8_TARGET_KIND_CHARACTER, index);
                }
            }
            if (!actions.empty()) {
                return;
            }
        }
        if (record->combat_morale == 2 && record->combat_behavior != 3 &&
            CurrentGame().rng.below(100) <
                monster_info->attributes[W8_MONSTER_ATTRIBUTE_INTELLIGENCE]) {
            QueueMonsterAction(monster_info, W8_MONSTER_ACTION_WAIT, -1, 0, W8_TARGET_KIND_NONE, 0);
        }
    }
    monster_info->action_kind = W8_MONSTER_ACTION_ATTACK;
    if (!attack_locked) {
        attack_lo = 0;
        attack_hi = W8_MAX_MONSTER_ATTACKS;
    } else {
        attack_lo = monster_info->pCombat->attack_index;
        attack_hi = attack_lo + 1;
    }
    if (!target_locked || attack_locked || monster_info->pCombat->berserk ||
        monster_info->attributes[W8_MONSTER_ATTRIBUTE_DEXTERITY] >= 0x4b) {
        char_lo = 0;
        char_hi = 8;
        scan_chars = true;
        scan_monsters = true;
        monster_lo = 0;
        monster_hi = CurrentLevelPopulation().monster_roster.live().size();
    } else {
        if (!IsTargetStillPresent(&monster_info->Target)) {
            return;
        }
        if (monster_info->Target.iType == W8_TARGET_KIND_CHARACTER) {
            if (monster_info->Target.iChar == -1) {
                Fail("pTarget->iChar != BAD_INDEX");
            }
            char_lo = monster_info->Target.iChar;
            char_hi = char_lo + 1;
            scan_chars = true;
        } else if (monster_info->Target.iType == W8_TARGET_KIND_MONSTER) {
            if (monster_info->Target.iMonsterID == -1) {
                Fail("pTarget->iMonsterID != BAD_INDEX");
            }
            scan_monsters = true;
            locked_monster = FindMonster(monster_info->Target.iMonsterID);
            monster_lo = 0;
            monster_hi = CurrentLevelPopulation().monster_roster.live().size();
            goto targets_chosen;
        } else {
            return;
        }
    }
    for (index = char_lo; index < char_hi; ++index) {
        if (CurrentGame().rng.below(100) <
            CurrentGame().party.characters[index].skills[W8_SKILL_STEALTH].level * 75 / 100) {
            avoided[index] = 1;
        }
    }
targets_chosen:
    friendly_targets = monster_info->pCombat && monster_info->pCombat->berserk;
    disposition_needed = friendly_targets ? W8_DISPOSITION_FRIENDLY : W8_DISPOSITION_HOSTILE;
    for (attack = attack_lo; attack < attack_hi; ++attack) {
        if (RateMonsterAttack(monster_info, record, attack, 1, friendly_targets) != 0) {
            continue;
        }
        if (scan_chars && monster_info->player_visibility.los_flags[RangeCategoryUsesSightCondition(
                              monster_info, static_cast<W8RangeCategory>(
                                                record->attacks[attack].range_category))] != 0) {
            for (index = char_lo; index < char_hi; ++index) {
                if (CanPartySlotParticipate(index) &&
                    MonsterVsCharDisposition(index, monster_info) == disposition_needed &&
                    MonsterAttackReachesCharacter(monster_info, record, attack, index)) {
                    if (avoided[index] == 0) {
                        for (mode_bit = 0; mode_bit < 9; ++mode_bit) {
                            if ((record->attacks[attack].attack_modes & (1 << mode_bit)) != 0) {
                                QueueMonsterAction(monster_info, W8_MONSTER_ACTION_ATTACK, mode_bit,
                                                   attack, W8_TARGET_KIND_CHARACTER, index);
                            }
                        }
                    } else {
                        resisted[index] = 1;
                    }
                }
            }
        }
        if (scan_monsters) {
            for (auto monster_index = monster_lo; monster_index < monster_hi; ++monster_index) {
                other = GetMonsterListEntry(monster_index);
                if (locked_monster && other != locked_monster) continue;
                if (other != monster_info && other->fActive && other->hp_current != 0 &&
                    GetMonsterDataForInfo(other)->untargetable == 0 && other->pCombat &&
                    MonsterHostility(monster_info, other) == disposition_needed &&
                    MonsterAttackReachesMonster(monster_info, record, attack, other)) {
                    for (mode_bit = 0; mode_bit < 9; ++mode_bit) {
                        if ((record->attacks[attack].attack_modes & (1 << mode_bit)) != 0) {
                            QueueMonsterAction(monster_info, W8_MONSTER_ACTION_ATTACK, mode_bit,
                                               attack, W8_TARGET_KIND_MONSTER, other->location_id);
                        }
                    }
                }
            }
        }
    }
    if (scan_chars) {
        for (index = char_lo; index < char_hi; ++index) {
            if (resisted[index] != 0 && CurrentGame().rng.below(2) == 0) {
                PracticeCharacterSkill(CurrentGame(), &CurrentGame().party.characters[index],
                                       W8_SKILL_STEALTH, 1, false);
            }
        }
    }
}

/* Add one decided action to a monster's queue. The third field only carries a
   value for the plain attack, and which of the two target fields the target
   goes in depends on what kind of target it is. Each entry gets a random tie
   break so two equal decisions do not always resolve the same way. */
static void QueueMonsterAction(W8MonsterInfo* monster_info, W8MonsterActionKind action_kind,
                               int action_detail, int attack_index, W8TargetKind target_kind,
                               int target_value)
{
    auto& entry = monster_info->pCombat->actions.emplace_back();
    entry.action_kind = action_kind;
    entry.action_detail = action_detail;
    if (action_kind == W8_MONSTER_ACTION_ATTACK) {
        entry.attack_index = attack_index;
    }
    ResetCombatSlot(&entry.target);
    entry.target.iType = target_kind;
    if (target_kind == W8_TARGET_KIND_CHARACTER) {
        entry.target.iChar = target_value;
    } else if (target_kind == W8_TARGET_KIND_MONSTER) {
        entry.target.iMonsterID = target_value;
    }
    entry.tie_break = static_cast<unsigned char>(CurrentGame().rng.below(100)) + 1;
}

/* Build the monster's list of possible actions and take one of them at
   random into its action fields and target. Committing a plain attack also
   commits its attack index and, when asked, restages the round's attack
   counts; a wait action keeps only its detail word. */
bool ChooseRandomMonsterAction(W8MonsterInfo* monster_info, bool target_locked, bool attack_locked,
                               bool set_attack_rate)
{
    const W8MonsterRecord* record;

    record = GetMonsterDataForInfo(monster_info);
    BuildMonsterActionQueue(monster_info, target_locked, attack_locked);
    const auto& actions = monster_info->pCombat->actions;
    if (actions.empty()) {
        return false;
    }
    const auto& entry =
        actions[CurrentGame().rng.below(wiz8::checked_integer<std::uint32_t>(actions.size()))];
    monster_info->action_kind = entry.action_kind;
    switch (entry.action_kind) {
    case W8_MONSTER_ACTION_ATTACK:
        monster_info->pCombat->attack_index = entry.attack_index;
        monster_info->action_detail = entry.action_detail;
        if (set_attack_rate) {
            monster_info->pCombat->attacks_per_round0 = record->attacks_per_round;
            monster_info->pCombat->attacks_per_round = record->attacks_per_round;
        }
        break;
    case W8_MONSTER_ACTION_WAIT:
        monster_info->action_detail = entry.action_detail;
        break;
    default:
        break;
    }
    monster_info->Target = entry.target;
    return true;
}

/* Whether the monster may cast the spell now: the record allows monsters to
   cast it, nothing is blocking the cast, the special cases for spell 0x3c
   and fire spells under camera sway pass, and `needs_target` also demands a
   target to aim at. */
bool IsSpellUsableByMonster(W8MonsterInfo* monster_info, int spell_id, bool needs_target)
{
    if (spell_id == W8_SPELL_NONE) {
        return false;
    }
    if (Data().spells[spell_id].monster_castable == 0) {
        return false;
    }
    if (IsSpellBlockedForMonster(monster_info, spell_id)) {
        return false;
    }
    if (DispatchWorldCursorNodeCommand(monster_info, 4, 0) != 0) {
        return false;
    }
    if (spell_id == W8_SPELL_SUMMON_ELEMENTAL && monster_info->elemental_summon != -1) {
        return false;
    }
    if (Data().spells[spell_id].realm == W8_SPELL_REALM_FIRE && g_camera_sway_active) {
        return false;
    }
    if (needs_target) {
        std::vector<W8CombatSlot> targets;
        CollectMonsterSpellTargets(monster_info, spell_id, targets);
        if (targets.empty()) {
            return false;
        }
    }
    return true;
}

/* Pick where the spell lands: collect every slot the spell may be cast at
   and take one at random into the monster's stored target. */
bool AimMonsterAtSpellTarget(W8MonsterInfo* monster_info, int spell_id)
{
    std::vector<W8CombatSlot> targets;

    CollectMonsterSpellTargets(monster_info, spell_id, targets);
    if (targets.empty()) {
        return false;
    }
    monster_info->Target = targets[CurrentGame().rng.below(static_cast<int>(targets.size()))];
    return true;
}

/* Whether `combat_slot` accepts `spell_id` from this caster. Target type
   eight needs no checking at all; a character or monster slot is checked
   against its own record, and anything else is resolved into its markers
   and accepted when at least half of them take the spell. The per-spell
   switch then vetoes targets the spell would not help or cannot affect. */
bool MonsterSpellTargetOK(W8MonsterInfo* monster_info, int spell_id, W8CombatSlot* combat_slot)
{
    W8MonsterInfo* target = 0;
    W8Character* character = 0;
    const W8MonsterRecord* record = 0;
    int hp = 0;
    int hp_max = 0;
    int stat = 0;
    int stat_max = 0;
    unsigned int* condition_turns = 0;
    W8Enchantment* enchantments = 0;
    /* 0x00532EE3 and 0x00532F38 test the marker count signed and 0x00532F89
       compares the index signed. */
    int index;
    unsigned int duration;

    if (GetSpellTargetType(spell_id, false) == W8_TARGET_TYPE_POINT) {
        return true;
    }
    if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
        character = &CurrentGame().party.characters[combat_slot->iChar];
        hp_max = character->uiHPMax;
        hp = character->hp_current;
        stat = character->stamina;
        stat_max = character->uiStaminaMax;
        condition_turns = character->uiCondition;
        enchantments = character->enchantments;
    } else {
        if (combat_slot->iType != W8_TARGET_KIND_MONSTER) {
            std::vector<int> monster_markers;
            std::vector<int> party_markers;
            W8TargetSource source;
            W8CombatSlot probe;
            unsigned int valid = 0;
            int total = 0;
            unsigned int power_level;

            SetTargetSourceToMonster(monster_info, &source);
            power_level = ChooseMonsterSpellPowerLevel(
                monster_info, GetMonsterDataForInfo(monster_info), spell_id);
            PopulateSpellTargetMarkers(spell_id, power_level, &source, combat_slot, monster_markers,
                                       party_markers, W8AreaSightRule::RequireLineOfSight);
            for (index = 0; index < static_cast<int>(party_markers.size()); ++index) {
                ++total;
                ResetCombatSlot(&probe);
                probe.iType = W8_TARGET_KIND_CHARACTER;
                probe.iChar = party_markers[index];
                if (MonsterSpellTargetOK(monster_info, spell_id, &probe)) {
                    ++valid;
                }
            }
            for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
                ++total;
                ResetCombatSlot(&probe);
                probe.iType = W8_TARGET_KIND_MONSTER;
                probe.iMonsterID = monster_markers[index];
                if (MonsterSpellTargetOK(monster_info, spell_id, &probe)) {
                    ++valid;
                }
            }
            return valid != 0 && ((total + 1U) >> 1) <= valid;
        }
        target = FindMonster(combat_slot->iMonsterID);
        record = GetMonsterDataForInfo(target);
        stat_max = target->stamina_max;
        stat = target->stamina;
        hp = target->hp_current;
        hp_max = target->uiHPMax;
        condition_turns = target->uiCondition;
        enchantments = target->enchantments;
        if (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE &&
            target->ubDisposition == W8_DISPOSITION_FRIENDLY && monster_info->pCombat &&
            monster_info->pCombat->reconsider_action) {
            return false;
        }
    }
    switch (spell_id) {
    case W8_SPELL_ACID_SPLASH:
    case W8_SPELL_FROST:
    case W8_SPELL_ENERGY_BLAST:
    case W8_SPELL_MAKE_WOUNDS:
    case W8_SPELL_MIND_STAB:
    case W8_SPELL_MAGIC_MISSILES:
    case W8_SPELL_SHRILL_SOUND:
    case W8_SPELL_FIREBALL:
    case W8_SPELL_NOXIOUS_FUMES:
    case W8_SPELL_PSIONIC_FIRE:
    case W8_SPELL_WHIPPING_ROCKS:
    case W8_SPELL_CRUSH:
    case W8_SPELL_EGO_WHIP:
    case W8_SPELL_FIRE_BOMB:
    case W8_SPELL_ICEBALL:
    case W8_SPELL_SUMMON_ELEMENTAL:
    case W8_SPELL_WHIRLWIND:
    case W8_SPELL_DEHYDRATE:
    case W8_SPELL_INSTANT_DEATH:
    case W8_SPELL_PSIONIC_BLAST:
    case W8_SPELL_BLIZZARD:
    case W8_SPELL_BOILING_BLOOD:
    case W8_SPELL_LIGHTNING:
    case W8_SPELL_PRISMIC_RAY:
    case W8_SPELL_QUICKSAND:
    case W8_SPELL_ASPHYXIATION:
    case W8_SPELL_CEREBRAL_HEMORRHAGE:
    case W8_SPELL_CONCUSSION:
    case W8_SPELL_DEATH_WISH:
    case W8_SPELL_EARTHQUAKE:
    case W8_SPELL_FALLING_STARS:
    case W8_SPELL_MIND_FLAY:
    case W8_SPELL_NUCLEAR_BLAST:
    case W8_SPELL_PRISMIC_CHAOS:
    case W8_SPELL_TSUNAMI:
    case W8_SPELL_SPECIAL_ATTACK_CONE:
        break;
    case W8_SPELL_BLESS:
    case W8_SPELL_ELEMENT_SHIELD:
    case W8_SPELL_SOUL_SHIELD:
        if (!CurrentGame().combat.active()) {
            return true;
        }
        for (index = 0; index < 9; ++index) {
            if (spell_id == g_combat_effect_slot_spells_and_cast_success[index]) {
                if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
                    duration = CurrentGame().combat.state()->effect_slots0[index].duration;
                } else {
                    if (!target->pCombat) {
                        continue;
                    }
                    duration = target->pCombat->combat_effects_2[index].duration;
                }
                if (duration != 0) {
                    return false;
                }
            }
        }
        return true;
    case W8_SPELL_HEAL_WOUNDS:
    case W8_SPELL_HEAL_ALL:
        if (hp == hp_max) {
            return false;
        }
        break;
    case W8_SPELL_ITCHING_SKIN:
        if (condition_turns[3] != 0) {
            return false;
        }
        break;
    case W8_SPELL_PARALYZE:
    case W8_SPELL_FREEZE_FLESH:
    case W8_SPELL_FREEZE_ALL:
        if (condition_turns[0x10] != 0) {
            return false;
        }
        break;
    case W8_SPELL_SLEEP:
        if (condition_turns[W8_CONDITION_ASLEEP] != 0) {
            return false;
        }
        break;
    case W8_SPELL_STAMINA:
    case W8_SPELL_REST_ALL:
        if (stat == stat_max) {
            return false;
        }
        break;
    case W8_SPELL_TERROR:
        if (condition_turns[6] != 0) {
            return false;
        }
        break;
    case W8_SPELL_BLINDING_FLASH:
        if (condition_turns[0xc] != 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_LESSER_COND:
        if (condition_turns[3] == 0 && condition_turns[4] == 0 && condition_turns[6] == 0 &&
            condition_turns[W8_CONDITION_ASLEEP] == 0 && condition_turns[0xc] == 0) {
            return false;
        }
        break;
    case W8_SPELL_ENCHANTED_BLADE:
    case W8_SPELL_MISSILE_SHIELD:
    case W8_SPELL_ARMORPLATE:
    case W8_SPELL_MAGIC_SCREEN:
        for (index = 0; index < 12; ++index) {
            if (spell_id == g_being_effect_slot_spells[index]) {
                if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
                    duration = CurrentGame().status.effect_slots[index].duration;
                } else {
                    duration = target->effect_slots[index].duration;
                }
                if (duration != 0) {
                    return false;
                }
            }
        }
        if (spell_id == W8_SPELL_ENCHANTED_BLADE && combat_slot->iType == W8_TARGET_KIND_MONSTER &&
            0x3b < record->spell_chance) {
            return false;
        }
        break;
    case W8_SPELL_GUARDIAN_ANGEL:
        if (enchantments[W8_ENCHANTMENT_GUARDIAN_ANGEL].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_RAZOR_CLOAK:
        if (enchantments[W8_ENCHANTMENT_RAZOR_CLOAK].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_SLOW:
        if (condition_turns[W8_CONDITION_SLOWED] != 0) {
            return false;
        }
        break;
    case W8_SPELL_WEB:
        if (condition_turns[0xe] != 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_PARALYSIS:
        if (condition_turns[0x10] == 0) {
            return false;
        }
        break;
    case W8_SPELL_CURE_POISON:
        if (condition_turns[W8_CONDITION_POISONED] == 0) {
            return false;
        }
        break;
    case W8_SPELL_SILENCE:
        if (condition_turns[W8_CONDITION_SILENCED] != 0) {
            return false;
        }
        if (combat_slot->iType != W8_TARGET_KIND_CHARACTER) {
            if (combat_slot->iType == W8_TARGET_KIND_MONSTER) {
                if (3 < record->kind && (record->kind < 6 || record->kind == 0xd)) {
                    return false;
                }
                if (record->spell_chance != 0) {
                    if (!target->p3D->IsCycleSupported(W8_MONSTER_CYCLE_SPELL)) {
                        if (!g_spell_cycle_error_reported) {
                            DebugMessage(0,
                                         fmt::format("ERROR: {} is missing a SPELL animation cycle",
                                                     record->name0));
                            g_spell_cycle_error_reported = true;
                            return false;
                        }
                    } else {
                        for (index = 0; index < 10; ++index) {
                            if (IsSpellUsableByMonster(target, record->spells[index], false)) {
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }
        if (character->skills[W8_SKILL_MUSIC].level != 0) {
            return true;
        }
        for (index = 0; index < 0x72; ++index) {
            if (character->spell_learned[index] == 1) {
                break;
            }
        }
        if (index > 0x71) {
            return false;
        }
        if (Data().spells[spell_id].alchemy_spell != 0 &&
            character->skills[W8_SKILL_SPELLBOOK_ALCHEMY].level != 0) {
            return false;
        }
        break;
    case W8_SPELL_ACID_BOMB:
    case W8_SPELL_ARMORMELT:
    case W8_SPELL_TOXIC_CLOUD:
    case W8_SPELL_DRAINING_CLOUD:
    case W8_SPELL_FIRESTORM:
    case W8_SPELL_DEATH_CLOUD:
        if (!CurrentGame().combat.active()) {
            return true;
        }
        for (index = 0; index < 9; ++index) {
            if (spell_id == g_combat_effect_slot_spells[index]) {
                if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
                    duration = CurrentGame().combat.state()->effect_slots[index].duration;
                } else {
                    if (!target->pCombat) {
                        continue;
                    }
                    duration = target->pCombat->combat_effects[index].duration;
                }
                if (duration != 0) {
                    return false;
                }
            }
        }
        return true;
    case W8_SPELL_EYE_FOR_AN_EYE:
        if (enchantments[W8_ENCHANTMENT_EYE_FOR_AN_EYE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_HASTE:
        if (enchantments[W8_ENCHANTMENT_HASTE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_SUPERMAN:
        if (enchantments[W8_ENCHANTMENT_SUPERMAN].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_BODY_OF_STONE:
        if (enchantments[W8_ENCHANTMENT_BODY_OF_STONE].turns != 0) {
            return false;
        }
        break;
    case W8_SPELL_HEX:
        if (condition_turns[9] != 0) {
            return false;
        }
        break;
    case W8_SPELL_PURIFY_AIR:
        if (!CurrentGame().combat.active()) {
            return false;
        }
        for (index = 0; index < 9; ++index) {
            if (g_combat_effect_slot_spells[index] != 0x31) {
                if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
                    duration = CurrentGame().combat.state()->effect_slots[index].duration;
                } else {
                    if (!target->pCombat) {
                        continue;
                    }
                    duration = target->pCombat->combat_effects[index].duration;
                }
                if (duration != 0) {
                    return true;
                }
            }
        }
        return false;
    case W8_SPELL_SANE_MIND:
        if (condition_turns[0xb] == 0 && condition_turns[W8_CONDITION_TURNCOAT] == 0) {
            return false;
        }
        break;
    case W8_SPELL_BANISH:
        if (combat_slot->iType == W8_TARGET_KIND_CHARACTER) {
            return false;
        }
        if (record->kind != 0x14 && record->kind != 0x15 && record->kind != 0x1c &&
            target->summoned == W8_MONSTER_SUMMON_NONE) {
            return false;
        }
        break;
    case W8_SPELL_PANDEMONIUM:
        if (condition_turns[6] == 0) {
            return true;
        }
        [[fallthrough]];
    case W8_SPELL_INSANITY:
        if (condition_turns[0xb] != 0) {
            return false;
        }
        break;
    case W8_SPELL_TURNCOAT:
        if (condition_turns[W8_CONDITION_TURNCOAT] != 0) {
            return false;
        }
        break;
    case W8_SPELL_CHARM:
    case W8_SPELL_LIGHT:
    case W8_SPELL_DETECT_SECRETS:
    case W8_SPELL_DIVINE_TRAP:
    case W8_SPELL_DRACON_BREATH:
    case W8_SPELL_HOLY_WATER:
    case W8_SPELL_IDENTIFY_ITEM:
    case W8_SPELL_SONIC_BOOM:
    case W8_SPELL_CHAMELEON:
    case W8_SPELL_HYPNOTIC_LURE:
    case W8_SPELL_KNOCK_KNOCK:
    case W8_SPELL_MINDREAD:
    case W8_SPELL_SHADOW_HOUND:
    case W8_SPELL_CURE_DISEASE:
    case W8_SPELL_REMOVE_CURSE:
    case W8_SPELL_RING_OF_FIRE:
    case W8_SPELL_X_RAY:
    case W8_SPELL_RETURN_TO_PORTAL:
    case W8_SPELL_SET_PORTAL:
    case W8_SPELL_LIFESTEAL:
    case W8_SPELL_MIGHT_TO_MAGIC:
    case W8_SPELL_RESURRECTION:
    case W8_SPELL_RESTORATION:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
    case 0x6a:
    case 0x6b:
    case 0x6c:
    case 0x6d:
    case 0x6e:
    case 0x6f:
    case 0x70:
    case 0x71:
    case W8_SPELL_RESTORE_HEALTH:
    case W8_SPELL_RESTORE_MAGIC:
    case W8_SPELL_SMELLING_SALTS:
    case W8_SPELL_ROUT:
    case W8_SPELL_BOILING_BLOOD_EXPLOSION:
    default:
        DebugMessage(0, fmt::format("WARNING: MonsterSpellTargetOK - unlisted spell {:d}({})",
                                    spell_id, Data().spells[spell_id].display_name));
        break;
    }
    return true;
}

/* Whether the spell's area effect would catch a disposition-neutral monster;
   a true return vetoes the cast, since the AI does not turn neutrals hostile
   by accident. */
bool SpellAreaHitsNeutralMonster(W8MonsterInfo* monster_info, int spell_id,
                                 W8CombatSlot* combat_slot)
{
    std::vector<int> monster_markers;
    std::vector<int> party_markers;
    W8TargetSource source;
    /* 0x005331B5 tests the marker count signed. */
    int index;
    W8MonsterInfo* target;
    unsigned int power_level;

    if (!MonsterCanAimSpell(spell_id)) {
        return false;
    }
    SetTargetSourceToMonster(monster_info, &source);
    power_level =
        ChooseMonsterSpellPowerLevel(monster_info, GetMonsterDataForInfo(monster_info), spell_id);
    PopulateSpellTargetMarkers(spell_id, power_level, &source, combat_slot, monster_markers,
                               party_markers, W8AreaSightRule::RequireLineOfSight);
    for (index = 0; index < static_cast<int>(monster_markers.size()); ++index) {
        target = FindMonster(monster_markers[index]);
        if (target->ubDisposition == W8_DISPOSITION_NEUTRAL) {
            return true;
        }
    }
    return false;
}

/* Which of the record's ten spells the monster casts: each usable slot adds
   its weight from the fixed table, then a roll under the total walks them
   down. The assertion names the original local, uiCastableSpellCnt. */
int ChooseMonsterSpell(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    int weights[10] = {0};
    int spell_ids[10];
    unsigned int count = 0;
    int total = 0;
    int slot;
    int roll;
    unsigned int index;

    for (slot = 0; slot < 10; ++slot) {
        int spell_id = record->spells[slot];
        if (IsSpellUsableByMonster(monster_info, spell_id, true)) {
            weights[count] = g_spell_cast_weights[slot];
            spell_ids[count] = spell_id;
            total += g_spell_cast_weights[slot];
            ++count;
        }
    }
    if (count == 0) {
        Fail("uiCastableSpellCnt > 0");
    }
    roll = CurrentGame().rng.below(total);
    index = 0;
    while (index < count && roll >= weights[index]) {
        roll -= weights[index];
        ++index;
    }
    return spell_ids[index];
}

/* Adds each active monster in combat with the given disposition that can reach the
   caster and accept the spell. exclude_caster skips the caster itself. */
static void addMonsterSpellTargets(W8MonsterInfo* monster_info, int spell_id,
                                   const W8MonsterRecord* record, W8Disposition disposition,
                                   bool exclude_caster, std::vector<W8CombatSlot>& targets)
{
    W8CombatSlot slot;
    W8MonsterInfo* member;

    for (unsigned int index = 0; index < CurrentLevelPopulation().monster_roster.live().size();
         ++index) {
        member = GetMonsterListEntry(index);
        if ((!exclude_caster || member != monster_info) && member->fActive &&
            member->hp_current != 0 && member->highest_condition < W8_CONDITION_DEAD &&
            member->pCombat && MonsterHostility(monster_info, member) == disposition &&
            MonsterAttackReachesMonster(monster_info, record, 0, member)) {
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_MONSTER;
            slot.iMonsterID = member->location_id;
            if (MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
                targets.push_back(slot);
            }
        }
    }
}

/* Fill `targets` with the combat slots `spell_id` may be cast at by this
   monster. The monster's action fields carry the action being probed while
   the reachability helpers run and are restored afterwards; spell 0x77 asks
   for a place to flee to, everything else is a cast. */
void CollectMonsterSpellTargets(W8MonsterInfo* monster_info, int spell_id,
                                std::vector<W8CombatSlot>& targets)
{
    const W8MonsterRecord* record = GetMonsterDataForInfo(monster_info);
    int saved_detail = monster_info->action_detail;
    W8MonsterActionKind saved_kind = monster_info->action_kind;
    W8VisibilityCondition sight_kind;
    W8SpellTargetType target_type;
    W8CombatSlot slot;
    W8MonsterGroup* monster_group;
    W8MonsterInfo* member;
    unsigned int index;

    if (spell_id == W8_SPELL_SPECIAL_ATTACK_CONE) {
        sight_kind = W8VisibilityCondition::ThirdLineOfSightFlag;
        monster_info->action_kind = W8_MONSTER_ACTION_SPECIAL_ATTACK;
    } else {
        sight_kind = W8VisibilityCondition::SpellOriginLineOfSight;
        monster_info->action_kind = W8_MONSTER_ACTION_SPELL;
        monster_info->action_detail = spell_id;
    }
    target_type = GetSpellTargetType(spell_id, false);
    switch (target_type) {
    case W8_TARGET_TYPE_CASTER:
        ResetCombatSlot(&slot);
        slot.iMonsterID = monster_info->location_id;
        slot.iType = W8_TARGET_KIND_MONSTER;
        if (!MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_ALLY:
        addMonsterSpellTargets(monster_info, spell_id, record, W8_DISPOSITION_FRIENDLY, false,
                               targets);
        if (IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, sight_kind)) {
            for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
                if (CanPartySlotParticipate(index) &&
                    MonsterVsCharDisposition(index, monster_info) == W8_DISPOSITION_FRIENDLY &&
                    MonsterAttackReachesCharacter(monster_info, record, 0, index)) {
                    ResetCombatSlot(&slot);
                    slot.iType = W8_TARGET_KIND_CHARACTER;
                    slot.iChar = index;
                    if (MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
                        targets.push_back(slot);
                    }
                }
            }
        }
        break;
    case W8_TARGET_TYPE_ENEMY:
        addMonsterSpellTargets(monster_info, spell_id, record, W8_DISPOSITION_HOSTILE, true,
                               targets);
        if (IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, sight_kind)) {
            for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
                if (CanPartySlotParticipate(index) &&
                    MonsterVsCharDisposition(index, monster_info) == W8_DISPOSITION_HOSTILE &&
                    MonsterAttackReachesCharacter(monster_info, record, 0, index)) {
                    ResetCombatSlot(&slot);
                    slot.iType = W8_TARGET_KIND_CHARACTER;
                    slot.iChar = index;
                    if (MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
                        targets.push_back(slot);
                    }
                }
            }
        }
        break;
    case W8_TARGET_TYPE_ENEMY_GROUP:
        for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
            monster_group = CurrentLevelPopulation().monster_groups[index].get();
            if (monster_group->group_id != monster_info->monster_group_id &&
                monster_group->members_active && monster_group->fInCombat &&
                !MonsterGroupAllMembersDying(monster_group) &&
                MonsterGroupHalfSpellTargetsValid(monster_info, spell_id, monster_group)) {
                ResetCombatSlot(&slot);
                slot.iType = W8_TARGET_KIND_GROUP;
                slot.iGroupID = monster_group->group_id;
                if (MonsterActionReachesTarget(monster_info, record, 0, &slot)) {
                    targets.push_back(slot);
                }
            }
        }
        if (!PartyHalfSpellTargetsValid(monster_info, spell_id)) {
            break;
        }
        ResetCombatSlot(&slot);
        slot.iType = W8_TARGET_KIND_PARTY;
        if (!MonsterActionReachesTarget(monster_info, record, 0, &slot)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_CONE:
    case W8_TARGET_TYPE_RADIUS:
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            member = GetMonsterListEntry(index);
            if (member != monster_info && member->fActive && member->hp_current != 0 &&
                member->highest_condition < W8_CONDITION_DEAD && member->pCombat &&
                MonsterHostility(monster_info, member) == W8_DISPOSITION_HOSTILE &&
                MonsterAttackReachesMonster(monster_info, record, 0, member)) {
                ResetCombatSlot(&slot);
                slot.iType = W8_TARGET_KIND_MONSTER;
                slot.iMonsterID = member->location_id;
                ResolveTargetPoint(&slot, target_type == W8_TARGET_TYPE_RADIUS);
                slot.iType = W8_TARGET_KIND_PLACE;
                if (MonsterSpellTargetOK(monster_info, spell_id, &slot) &&
                    !SpellAreaHitsNeutralMonster(monster_info, spell_id, &slot)) {
                    targets.push_back(slot);
                }
            }
        }
        if (IsVisibleUnderConditions(monster_info, &monster_info->player_visibility, sight_kind)) {
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_PARTY;
            ResolveTargetPoint(&slot, target_type == W8_TARGET_TYPE_RADIUS);
            slot.iType = W8_TARGET_KIND_PLACE;
            if (MonsterSpellTargetOK(monster_info, spell_id, &slot) &&
                !SpellAreaHitsNeutralMonster(monster_info, spell_id, &slot)) {
                for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
                    if (CanPartySlotParticipate(index) &&
                        MonsterVsCharDisposition(index, monster_info) == W8_DISPOSITION_HOSTILE &&
                        MonsterAttackReachesCharacter(monster_info, record, 0, index)) {
                        targets.push_back(slot);
                    }
                }
            }
        }
        break;
    case W8_TARGET_TYPE_ALL_ENEMIES:
        ResetCombatSlot(&slot);
        slot.iType = W8_TARGET_KIND_ALL_ENEMIES;
        if (!MonsterSpellTargetOK(monster_info, spell_id, &slot) ||
            SpellAreaHitsNeutralMonster(monster_info, spell_id, &slot)) {
            break;
        }
        targets.push_back(slot);
        break;
    case W8_TARGET_TYPE_POINT:
        ResetCombatSlot(&slot);
        slot.point = monster_info->p3D->nav.GetPosition();
        targets.push_back(slot);
        break;
    default:
        ResetCombatSlot(&slot);
        if (!MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
            break;
        }
        targets.push_back(slot);
        break;
    }
    monster_info->action_kind = saved_kind;
    monster_info->action_detail = saved_detail;
}

/* Whether a monster can aim the spell it wants to cast. The two area target
   types aim at the world; anything else either needs no aim at all or has to
   pass the slot check. */
bool CanMonsterAimSpell(W8MonsterInfo* monster_info, int spell_id)
{
    W8SpellTargetType target_type = GetSpellTargetType(spell_id, false);

    if (target_type > W8_TARGET_TYPE_ENEMY_GROUP && target_type < W8_TARGET_TYPE_ALL_ENEMIES) {
        return AimMonsterAtSpellTarget(monster_info, spell_id);
    }
    if (Data().spells[spell_id].needs_aim != 0) {
        return ResolveTargetPoint(&monster_info->Target, false);
    }
    return true;
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

    RefreshOutwardSightForAllMonsters();
    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        group = CurrentLevelPopulation().monster_groups[group_index].get();
        if (!group->members_active || !group->fInCombat || MonsterGroupAllMembersDying(group)) {
            continue;
        }
        if (group->ubDisposition == W8_DISPOSITION_NEUTRAL) {
            if (!MonsterGroupHasReinforcement(group)) {
                MonsterGroupLeaveCombat(group);
            }
            continue;
        }
        if (group == 0) {
            Fail("pMonsterGroup");
        }
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
            if (GetMonsterGroupEngagementState(group->group_id) && leader != 0 && leader->fActive) {
                nearest = GetGroupNearestDistance(group);
                reach = CalcRangeDistance(GetMonsterBestRangeCategory(leader, true, &sight)) +
                        GetMonsterCombatMoveRange(leader) * 1e+03f;
                minimum = GetMonsterEngagementRange() + kMonsterEngagementRangeFloor;
                if (reach <= minimum) {
                    reach = minimum;
                }
                if (reach < nearest && !MonsterGroupCanEngage(group)) {
                    MonsterGroupLeaveCombat(group);
                    break;
                }
            }
            if (group == 0) {
                Fail("pMonsterGroup");
            }
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
            MonsterGroupLeaveCombat(group);
        }
    }
    if (engaged != 0) {
        return;
    }
    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        group = CurrentLevelPopulation().monster_groups[group_index].get();
        if (group->members_active && group->fInCombat && !MonsterGroupAllMembersDying(group)) {
            MonsterGroupLeaveCombat(group);
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
            if (CanPartySlotParticipate(index) &&
                MonsterVsCharDisposition(index, monster_info) == W8_DISPOSITION_HOSTILE) {
                return false;
            }
        }
    }
    if (!party_only) {
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            other = GetMonsterListEntry(index);
            if (other != monster_info && other->fActive && other->hp_current != 0 &&
                other->highest_condition < W8_CONDITION_DEAD && other->pCombat &&
                MonsterHostility(monster_info, other) == W8_DISPOSITION_HOSTILE &&
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

    if (monster_group == 0) {
        Fail("pMonsterGroup");
    }
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
        (!within_reach || monster_info->p3D->GetDistanceToPlayer() <= reach)) {
        for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (CanPartySlotParticipate(index)) {
                disposition = MonsterVsCharDisposition(index, monster_info);
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
                disposition = MonsterHostility(monster_info, other);
                if (hostility == W8_VISIBLE_TARGET_ANY ||
                    static_cast<W8VisibleTargetFilter>(disposition) == hostility ||
                    (hostility == W8_VISIBLE_TARGET_NON_NEUTRAL &&
                     disposition != W8_DISPOSITION_NEUTRAL)) {
                    record = FindMonToMonVisibility(monster_info, other);
                    if (record != 0 && record->sight_state == W8_SIGHT_SEEN &&
                        record->los_flags[2]) {
                        if (!within_reach ||
                            monster_info->p3D->GetDistanceToMonster(other->p3D) <= reach) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

/* Whether the monster can flee at all: it has a flee chance, a special-attack
   row, the special-attack cycle the flee uses, enough stamina left, and - for
   non-summoning attacks - somewhere to run. Summoning rows flee at the party
   instead and may not be offered as a special action. */
bool CanMonsterFlee(W8MonsterInfo* monster_info, const W8MonsterRecord* record,
                    bool exclude_special)
{
    std::vector<W8CombatSlot> targets;

    if (record->flee_chance == 0) {
        return false;
    }
    if (record->special_attack_kind == 0) {
        return false;
    }
    if (!monster_info->p3D->IsCycleSupported(0x12)) {
        if (!g_special_attack_cycle_error_reported) {
            DebugMessage(0, fmt::format("ERROR: {} is missing a SPECIAL ATTACK animation cycle",
                                        record->name0));
            g_special_attack_cycle_error_reported = true;
        }
        return false;
    }
    if (CurrentGame().combat.active() && monster_info->pCombat->special_cooldown != 0) {
        return false;
    }
    if (static_cast<unsigned int>(monster_info->stamina) <
        static_cast<unsigned int>(monster_info->stamina_max) / 10) {
        return false;
    }
    if (monster_info->uiCondition[W8_CONDITION_SILENCED] != 0 &&
        MonsterSpecialAttackHonorsCastingBlock(record->special_attack_kind)) {
        return false;
    }
    if (g_special_attack_table[record->special_attack_kind][0] == W8_SPECIAL_ATTACK_EFFECT_SUMMON) {
        if (CalcRangeDistance(W8_RANGE_LONG) < monster_info->p3D->GetDistanceToPlayer()) {
            return false;
        }
        if (monster_info->ubDisposition == W8_DISPOSITION_FRIENDLY) {
            return false;
        }
        if (exclude_special) {
            return false;
        }
    } else {
        CollectMonsterSpellTargets(monster_info, W8_SPELL_SPECIAL_ATTACK_CONE, targets);
        if (targets.empty()) {
            return false;
        }
    }
    return true;
}

/* Aim a monster that wants to get away. A summoning special-attack row aims
   at the monster's own position. */
bool AimFleeingMonster(W8MonsterInfo* monster_info, const W8MonsterRecord* record)
{
    glm::vec3 position;

    if (g_special_attack_table[record->special_attack_kind][0] == W8_SPECIAL_ATTACK_EFFECT_SUMMON) {
        position = monster_info->p3D->nav.GetPosition();
        ResetCombatSlot(&monster_info->Target);
        monster_info->Target.iType = W8_TARGET_KIND_PLACE;
        monster_info->Target.point = position;
        return true;
    }
    return AimMonsterAtSpellTarget(monster_info, W8_SPELL_SPECIAL_ATTACK_CONE);
}

/* Whether the action a monster has settled on can actually be carried out. An
   attack needs a character to swing at; a spell needs to be castable and needs
   a target of a kind it accepts; fleeing is refused outright for the summoning
   special-attack rows that have nowhere to flee to. */
bool IsMonsterActionUsable(W8MonsterInfo* monster_info)
{
    int spell_id;

    switch (monster_info->action_kind) {
    case W8_MONSTER_ACTION_ATTACK:
        return monster_info->Target.iType == W8_TARGET_KIND_CHARACTER;
    case W8_MONSTER_ACTION_SPELL:
        spell_id = monster_info->action_detail;
        if (!MonsterCanAimSpell(spell_id)) {
            return false;
        }
        switch (monster_info->Target.iType) {
        case W8_TARGET_KIND_CHARACTER:
        case W8_TARGET_KIND_PARTY:
            return true;
        case W8_TARGET_KIND_ALL_ENEMIES:
        case W8_TARGET_KIND_PLACE:
            break;
        default:
            return false;
        }
        break;
    case W8_MONSTER_ACTION_SPECIAL_ATTACK:
        if (g_special_attack_table[GetMonsterDataForInfo(monster_info)->special_attack_kind][0] ==
            W8_SPECIAL_ATTACK_EFFECT_SUMMON) {
            return false;
        }
        spell_id = W8_SPELL_SPECIAL_ATTACK_CONE;
        break;
    default:
        return false;
    }
    return MonsterSpellHasPartyTarget(monster_info, spell_id, &monster_info->Target);
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

    if (group == 0) {
        Fail("pMonsterGroup");
    }

    for (index = 0; index < group->MemberIds().size(); ++index) {
        location_id = group->MemberLocationId(index);
        monster_info = FindMonster(location_id);
        distance = monster_info->p3D->GetDistanceToPlayer();
        if (distance < furthest) {
            furthest = distance;
        }
    }
    return furthest;
}

/* Whether the point the monster-control effect is anchored to is still within
   reach. spell_visuals.data sits at W8SpellEffectEntry + 0x10c; the first visual is
   a SpellVisual whose Navigator secondary base is the ordinary GrCycle
   conversion at +0x18. With no effect running, or nothing anchored, there is
   nothing to be in range of; failing the test falls back on where the party
   is standing. */
short IsMonsterControlPointInRange(W8MonsterInfo* monster_info)
{
    W8SpellEffectEntry* effect = FindMonsterControlSpellEffect();
    SpellVisual* visual;
    Navigator* anchor_navigator;
    short in_range;
    glm::vec3 party;

    if (effect == 0) {
        return 0;
    }
    visual = effect->spell_visuals[0];
    if (visual == 0) {
        return 0;
    }
    anchor_navigator = &(visual)->nav;
    in_range = monster_info->p3D->nav.SetMovementTargetToNavigator(anchor_navigator, 2500.0);
    if (in_range == 0) {
        party = anchor_navigator->GetPosition();
        monster_info->p3D->nav.AimAtPosition(&party);
    }
    return in_range;
}

/* Whether at least half of the group's live in-combat members the caster is
   hostile to accept the spell; the probe is the same MonsterSpellTargetOK
   the cast itself would face. */
bool MonsterGroupHalfSpellTargetsValid(W8MonsterInfo* monster_info, int spell_id,
                                       W8MonsterGroup* target_group)
{
    unsigned int valid = 0;
    int eligible = 0;
    unsigned int index;
    W8MonsterInfo* member;
    W8CombatSlot slot;

    if (target_group == 0) {
        Fail("pTargetMonsterGroup");
    }
    for (index = 0; index < target_group->MemberIds().size(); ++index) {
        int location_id = target_group->MemberLocationId(index);
        member = FindMonster(location_id);
        if (member->fActive && member->hp_current != 0 &&
            member->highest_condition < W8_CONDITION_DEAD && member->pCombat &&
            MonsterHostility(monster_info, member) == W8_DISPOSITION_HOSTILE) {
            ++eligible;
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_MONSTER;
            slot.iMonsterID = location_id;
            if (MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
                ++valid;
            }
        }
    }
    return valid != 0 && ((eligible + 1U) >> 1) <= valid;
}

/* Whether at least half of the party members the caster is hostile to accept
   the spell. */
bool PartyHalfSpellTargetsValid(W8MonsterInfo* monster_info, int spell_id)
{
    unsigned int valid = 0;
    int eligible = 0;
    unsigned int index;
    W8CombatSlot slot;

    for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
        if (CanPartySlotParticipate(index) &&
            MonsterVsCharDisposition(index, monster_info) == W8_DISPOSITION_HOSTILE) {
            ++eligible;
            ResetCombatSlot(&slot);
            slot.iType = W8_TARGET_KIND_CHARACTER;
            slot.iChar = index;
            if (MonsterSpellTargetOK(monster_info, spell_id, &slot)) {
                ++valid;
            }
        }
    }
    return valid != 0 && ((eligible + 1U) >> 1) <= valid;
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
        member_distance = member->p3D->GetDistanceToPlayer();
        if (member_distance > GetMonsterEngagementRange()) {
            continue;
        }
        for (other_index = 0; other_index < CurrentLevelPopulation().monster_roster.live().size();
             ++other_index) {
            other = GetMonsterListEntry(other_index);
            if (other != member && other->party_threat.los_flags[1] && other->fActive &&
                other->pCombat && other->hp_current != 0 &&
                other->ubDisposition == W8_DISPOSITION_HOSTILE) {
                other_distance = other->p3D->GetDistanceToPlayer();
                monster_distance = member->p3D->GetDistanceToMonster(other->p3D);
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
    if (group == 0) {
        Fail("pMonsterGroup");
    }
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

    if (CurrentGame().combat.state()->combat_update_count <= 2) {
        return;
    }
    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        group = CurrentLevelPopulation().monster_groups[group_index].get();
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

/* Whether the spell's markers catch at least one party member when cast at
   `slot`. */
bool MonsterSpellHasPartyTarget(W8MonsterInfo* monster_info, int spell_id, W8CombatSlot* slot)
{
    std::vector<int> monster_markers;
    std::vector<int> party_markers;
    W8TargetSource source;
    unsigned int power_level;

    SetTargetSourceToMonster(monster_info, &source);
    power_level =
        ChooseMonsterSpellPowerLevel(monster_info, GetMonsterDataForInfo(monster_info), spell_id);
    PopulateSpellTargetMarkers(spell_id, power_level, &source, slot, monster_markers, party_markers,
                               W8AreaSightRule::RequireLineOfSight);
    return !party_markers.empty();
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

    RefreshAllSight();
    for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
        group = CurrentLevelPopulation().monster_groups[index].get();
        if (group->fInCombat && group->ubDisposition == W8_DISPOSITION_HOSTILE) {
            AlertSameFactionGroups(group);
        }
    }
    for (index = 0; index < CurrentLevelPopulation().monster_groups.size(); ++index) {
        group = CurrentLevelPopulation().monster_groups[index].get();
        if (!group->fInCombat) {
            if (ShouldMonsterGroupEnterCombat(group)) {
                MonsterGroupEnterCombat(group);
                if (group->ubDisposition == W8_DISPOSITION_NEUTRAL) {
                    record = MonsterGroupGetRecord(group);
                    if ((record->flags & W8_MONSTER_FLAG_NPC) == 0 &&
                        record->faction_id == W8_FACTION_UNALIGNED &&
                        record->hostility_radius != 0 && record->hostility_radius != -1) {
                        SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
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

    if (MonsterGroupAllMembersDying(monster_group) || !monster_group->members_active) {
        return false;
    }
    if (monster_group->leader_location_id == -0x32323233 ||
        (leader = FindMonster(monster_group->leader_location_id)) == 0 || leader->p3D == 0) {
        DebugMessage(1, fmt::format("ERROR: Group {:d} is without an active leader",
                                    monster_group->group_id));
        return false;
    }
    if (!CurrentGame().combat.active()) {
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
                party = PartyNavigator()->GetPosition();
                if (glm::length((leader->p3D->nav.movement.position - party)) <= reach &&
                    leader->p3D->nav.FindNavigatorPathDistance(750.0f, &path_distance) &&
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
