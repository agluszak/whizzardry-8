#include <vector>
#include <cstring>
#include <span>
#include <unordered_map>

#include "wiz8/fonts.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/integer_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/character_skills.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/fact_state.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/float_constants.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/monster_generators.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/character_event_queue.h"
#include "random.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/sr_api.h"
#include "wiz8/startup_world.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "conditions.h"
#include "gameplay_modifiers.h"
#include "layouts/levels.h"
#include "local_code/Factions.h"
#include "sight_state.h"

struct W8World;

#define SIGHT_CPP "C:\\Projects\\Wizardry 8\\Local Code\\Sight.cpp"

// GLOBAL: WIZ8 0x005ec254
const float g_sight_default = 12.0f;

/* Put the sight subsystem back to its starting state. */
// FUNCTION: WIZ8 0x005048e0
void ResetSight(void)
{
    SetViewDistance(12.0f);
    SetNavigatorLinkMode(0);
    g_game_time_accumulator->ResetDurationScale();
    ResetMonsterGeneratorTimers();
}

/* Whether the view distance has been moved off its default. */
// FUNCTION: WIZ8 0x00504910
bool IsSightRangeOverridden(void)
{
    return GetViewDistance() != g_sight_default;
}

/* Re-run both directions of the sight update for one monster. */
// FUNCTION: WIZ8 0x00505760
void RefreshMonsterSight(W8MonsterInfo* monster_info)
{
    UpdateMonsterSight(monster_info, true, false);
    UpdateMonsterSight(monster_info, false, false);
}

/* Re-run the outward direction over every live monster, and tell the combat
   display about it if a fight is on. The count is re-read every step because
   the update can remove a monster. */
// FUNCTION: WIZ8 0x00505780
void RefreshOutwardSightForAllMonsters(void)
{
    unsigned int index;

    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        UpdateMonsterSight(MonsterGetScriptPartByLocationIndex(index), true, false);
    }
    if (gXStatus.fCombatMode) {
        RefreshFlaggedMainGameState();
        RefreshAllPartyTargets();
    }
}

/* The inward direction on its own, with nothing to tell the display. */
// FUNCTION: WIZ8 0x005057d0
void RefreshInwardSightForAllMonsters(void)
{
    unsigned int index;

    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        UpdateMonsterSight(MonsterGetScriptPartByLocationIndex(index), false, false);
    }
}

/* Both directions over every monster, in two separate passes rather than one
   pass doing both - the outward pass and its display notification have to
   finish before the inward pass starts. */
// FUNCTION: WIZ8 0x00505810
void RefreshAllSight(void)
{
    unsigned int index;

    RefreshOutwardSightForAllMonsters();
    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        UpdateMonsterSight(MonsterGetScriptPartByLocationIndex(index), false, false);
    }
}

/* Whether one monster can see another in combat once line of sight is clear. */
// FUNCTION: WIZ8 0x005058a0
bool CanMonsterSeeMonster(W8MonsterInfo* source, W8MonsterInfo* target, W8VisibilityRecord* record)
{
    W8MonsterRecord* target_record;
    W8Monster* source_monster;
    srVector3T<float> observer_position;
    W8Monster* target_monster;
    srVector3T<float> target_position;
    float observer_yaw;
    int penalty_modifier;
    float distance;
    W8MonsterRecord* source_record;
    unsigned char ranged_bonus;
    float threshold;

    if (source->hp_current == 0 || source->highest_condition >= W8_CONDITION_ASLEEP) {
        return false;
    }
    target_record = GetMonsterDataForInfo(target);
    source_monster = source->p3D;
    observer_position = source_monster->movement.position;
    observer_position.y += source_monster->movement.height_offset;
    target_monster = target->p3D;
    target_position = target_monster->movement.position;
    target_position.y += target_monster->movement.height_offset;
    observer_yaw = source_monster->GetYaw();
    if (!target->fInCombat) {
        penalty_modifier = target_record->camouflage0;
    } else {
        penalty_modifier = 0;
    }
    distance = source_monster->GetDistanceToMonster(target_monster);
    source_record = GetMonsterDataForInfo(source);
    if (source_record->kind == 4) {
        ranged_bonus = source_record->effective_level * 5;
        if (ranged_bonus > 0x7d) {
            ranged_bonus = 0x7d;
        }
    } else {
        ranged_bonus = 0;
    }
    source_record = GetMonsterDataForInfo(source);
    threshold = ComputeSightThreshold(
        observer_position, target_position, observer_yaw, source->attributes[W8_MONSTER_ATTRIBUTE_SENSES], ranged_bonus,
        static_cast<unsigned char>(source->uiCondition[W8_CONDITION_BLIND] != 0),
        static_cast<unsigned char>(source_record->kind == 12), static_cast<int>(target_record->effective_level),
        penalty_modifier, static_cast<int>(record->sight_state), 0, distance);
    if (threshold < distance) {
        return false;
    }
    return true;
}

/* Maximum distance at which the observer can perceive the target. */
// FUNCTION: WIZ8 0x00505a40
float ComputeSightThreshold(srVector3T<float> observer_position, srVector3T<float> target_position, float observer_yaw,
                            unsigned int perception_attribute, unsigned char ranged_bonus, bool blinded,
                            bool extended_sight_active, int penalty_source, int penalty_modifier,
                            int skip_field_of_view, unsigned char sight_override, float distance)
{
    W8World* world;
    double far_clip;
    float viewing_distance;
    float heading;
    float angle_delta;
    int sight_percent;
    int penalty;

    world = GetWorld();
    far_clip = WorldGetFarClip(world);
    viewing_distance = static_cast<float>(far_clip);
    if (viewing_distance <= g_float_zero) {
        srAssertFail("flViewingDistance > 0", SIGHT_CPP, 0x30c, 0);
    }
    heading = GetHeadingAngle(&observer_position, &target_position);
    angle_delta = ShortestAngleDistance(observer_yaw, heading);
    if (blinded && !extended_sight_active) {
        return g_float_zero;
    }
    if (angle_delta < static_cast<float>(g_monster_facing_tolerance) || sight_override != 0 ||
        skip_field_of_view != 0 || distance == g_float_zero) {
        sight_percent = 100;
    } else {
        float sight_factor = static_cast<float>(static_cast<int>(perception_attribute) - penalty_source * 2);
        float sight_ratio = (viewing_distance - distance) / distance;
        sight_percent = static_cast<int>(sight_factor * sight_ratio);
    }
    if (penalty_modifier != 0) {
        if (skip_field_of_view == 0) {
            penalty = penalty_modifier * -10;
        } else {
            penalty = penalty_modifier * -5;
        }
        sight_percent += penalty;
    }
    if (ranged_bonus != 0) {
        sight_percent = (static_cast<int>(ranged_bonus) + 100) * sight_percent / 100;
    }
    ClampInteger(&sight_percent, 2, 100);
    return sight_percent * viewing_distance * g_movement_speed_step;
}

/* Drop every visibility record anyone held about one departing monster. */
// FUNCTION: WIZ8 0x00505ba0
void ReleaseMonToMonVisibilityInfoAbout(int location_id)
{
    for (auto* monster : gXStatus.monsters) {
        if (monster->fActive && monster->monster_visibility) {
            monster->monster_visibility->erase(location_id);
        }
    }
}

/* Empty and destroy one monster's whole visibility list, then drop what
   everybody else held about it. */
// FUNCTION: WIZ8 0x00505c80
void ReleaseMonToMonVisibilityList(W8MonsterInfo* monster_info)
{
    delete monster_info->monster_visibility;
    monster_info->monster_visibility = nullptr;
    ReleaseMonToMonVisibilityInfoAbout(monster_info->location_id);
}

/* Whether a monster group still has anybody in it worth seeing: alive, not
   dying, not too far gone, and marked as a threat. */
// FUNCTION: WIZ8 0x00505ea0
bool MonsterGroupHasVisibleThreat(W8MonsterGroup* group)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < group->MemberIds().size(); ++index) {
        monster_info = FindMonster(group->MemberLocationId(index));
        if (monster_info->fActive && !monster_info->p3D->IsDying() && monster_info->hp_current != 0 &&
            monster_info->highest_condition < W8_CONDITION_BLIND &&
            monster_info->party_threat.sight_state == W8_SIGHT_SEEN) {
            return true;
        }
    }
    return false;
}

/* Whether projectile visibility uses the separate launch origin. */
// FUNCTION: WIZ8 0x00505e60
bool MonsterHasProjectileOrigin(const W8MonsterInfo* monster)
{
    return monster->has_projectile_origin;
}

/* Spell visibility occupies entry two or three, depending on launch origin. */
// FUNCTION: WIZ8 0x00505e80
char GetMonsterSpellSightIndex(const W8MonsterInfo* monster)
{
    return (monster->has_spell_origin) + 2;
}

/* Read one flag out of a visibility record. Two of the seven kinds are pairs
   that the monster's missile and spell conditions choose between; the rest are
   fixed, and anything past the named kinds falls back to the last flag. */
// FUNCTION: WIZ8 0x00505dd0
bool IsVisibleUnderConditions(const W8MonsterInfo* monster, const W8VisibilityRecord* row, int kind)
{
    if (row == 0) {
        return false;
    }
    switch (kind) {
    case 0:
        return row->sight_state != W8_SIGHT_UNSEEN;
    case 1:
        return row->can_see;
    case 2:
        return row->los_flags[0];
    case 3:
        return row->los_flags[2];
    case 4:
        return row->los_flags[monster->has_projectile_origin];
    case 5:
        return row->los_flags[2 + (monster->has_spell_origin)];
    default:
        return row->line_of_sight;
    }
}

/* Bring every monster's sight up to date with the clock, in whole two-minute
   steps - anything short of one step is left for next time. */
// FUNCTION: WIZ8 0x00504930
unsigned int AgeAllMonsterSight(void)
{
    unsigned int elapsed;
    unsigned int steps;
    unsigned int index;
    W8MonsterInfo* monster_info;

    elapsed = g_status.world_clock * 1000 - g_status.level_progress[g_status.current_level].sight_clock * 1000;
    steps = elapsed / 120000;
    if (steps == 0) {
        return 0;
    }
    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        monster_info = MonsterGetScriptPartByLocationIndex(index);
        if (monster_info->highest_condition < W8_CONDITION_DEAD) {
            AgeMonsterSight(monster_info, steps, 1);
        }
    }
    return gXStatus.monsters.size();
}

/* The map owns records and keeps their addresses stable while other entries
   are inserted or removed. A missing list or target has no visibility record. */
// FUNCTION: WIZ8 0x00505d20
W8VisibilityRecord* FindMonToMonVisibility(W8MonsterInfo* source, W8MonsterInfo* target)
{
    if (!source->monster_visibility) {
        return nullptr;
    }
    auto& visibility = *source->monster_visibility;
    auto found = visibility.find(target->location_id);
    return found == visibility.end() ? nullptr : &found->second;
}

/* Bring every monster's sight up to date from a cleared starting state: the
   two per-monster sight blocks are emptied first, then the outward pass, the
   combat display notification it feeds, and finally the inward pass. */
// FUNCTION: WIZ8 0x005060c0
void ResetAndRefreshAllSight(void)
{
    unsigned int index;

    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        W8MonsterInfo* monster_info = MonsterGetScriptPartByLocationIndex(index);

        memset(&monster_info->party_threat, 0, sizeof(monster_info->party_threat));
        memset(&monster_info->player_visibility, 0, sizeof(monster_info->player_visibility));
    }
    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        UpdateMonsterSight(MonsterGetScriptPartByLocationIndex(index), true, false);
    }
    if (gXStatus.fCombatMode) {
        RefreshFlaggedMainGameState();
        RefreshAllPartyTargets();
    }
    for (index = 0; index < gXStatus.monsters.size(); ++index) {
        UpdateMonsterSight(MonsterGetScriptPartByLocationIndex(index), false, false);
    }
}

// GLOBAL: WIZ8 0x00689b6c
static int g_sight_marker_tick;
// GLOBAL: WIZ8 0x00689b70
static int g_sight_fade_in_tick;
// GLOBAL: WIZ8 0x00689b74
static int g_sight_fade_out_tick;

// GLOBAL: WIZ8 0x005ed7f8
const float g_sight_threat_scale = 0.6667f;

/* Recompute one monster's sight state. Direction zero is the monster-to-
   monster pass, which revisits every other monster's per-monster record;
   nonzero is the player pass, which refreshes the party-facing record, stamps
   the notice and fade state, and ends in the player-to-monster flag pass. */
// FUNCTION: WIZ8 0x005049c0
void UpdateMonsterSight(W8MonsterInfo* monster_info, bool direction, bool use_bounds)
{
    W8MonsterRecord* record;
    W8Monster* monster;
    srVector3T<float> own_position;
    float viewing_distance;
    srVector3T<float> camera_position;
    srVector3T<float> other_position;
    srVector3T<float> trace_position;

    if (g_current_screen_state.id != W8_SCREEN_MAIN_GAME || !monster_info->fActive || monster_info->hp_current == 0) {
        return;
    }
    viewing_distance = static_cast<float>(WorldGetFarClip(GetWorld()));
    if (viewing_distance <= g_float_zero) {
        srAssertFail("flViewingDistance > 0", SIGHT_CPP, 0x5e, 0);
    }
    record = GetMonsterDataForInfo(monster_info);
    monster = monster_info->p3D;
    own_position = monster->movement.position;
    own_position.y += monster->movement.height_offset;

    if (!direction) {
        unsigned int index;

        if (!gXStatus.fCombatMode) {
            return;
        }
        for (index = 0; index < gXStatus.monsters.size(); ++index) {
            W8MonsterInfo* other = MonsterGetScriptPartByLocationIndex(index);

            if (other != monster_info && other->fInCombat && other->fActive && other->hp_current != 0) {
                auto* entry = &(*monster_info->monster_visibility)[other->location_id];
                entry->about_location_id = other->location_id;
                {
                    W8Monster* other_monster = other->p3D;
                    float distance;

                    other_position = other_monster->movement.position;
                    other_position.y += other_monster->movement.height_offset;
                    entry->can_see = false;
                    entry->line_of_sight = false;
                    bool can_see = false;

                    distance = monster->GetDistanceToMonster(other->p3D);
                    if (viewing_distance >= distance) {
                        bool line_of_sight = monster->HasLineOfSightToMonster(other_monster);

                        entry->line_of_sight = line_of_sight;
                        if (line_of_sight) {
                            can_see = CanMonsterSeeMonster(monster_info, other, entry);
                            entry->can_see = can_see;
                        }
                    }
                    if (can_see) {
                        entry->sight_state = W8_SIGHT_SEEN;
                        entry->last_seen_clock = g_status.world_clock;
                        entry->subject_position = own_position;
                        entry->target_position = other_position;
                    } else if (entry->last_seen_clock == 0) {
                        entry->sight_state = W8_SIGHT_UNSEEN;
                    } else {
                        entry->sight_state = static_cast<unsigned char>(
                            (0xf0U < static_cast<unsigned int>(g_status.world_clock - entry->last_seen_clock))
                                ? W8_SIGHT_UNSEEN
                                : W8_SIGHT_RECENT);
                    }
                    if (!entry->line_of_sight) {
                        memset(entry->los_flags, 0, 4);
                    } else {
                        monster->GetMonsterSightFlags(other->p3D, entry->los_flags, entry->los_flags + 2);
                        if (monster_info->has_projectile_origin) {
                            bool found = monster->GetProjectilePosition(&trace_position);
                            bool clear;

                            if (!found) {
                                srAssertFail("fFoundMissileVertex", SIGHT_CPP, 0x1f5, 0);
                                clear = false;
                            } else {
                                short line =
                                    g_octree->TraceLineOfSight(&trace_position, &other_position, true,
                                                               monster_info->location_id, other->location_id, true, 1);

                                clear = line == 0;
                            }
                            entry->los_flags[1] = clear;
                        }
                        if (monster_info->has_spell_origin) {
                            bool found = monster->GetSpellPosition(&trace_position);

                            if (!found) {
                                srAssertFail("fFoundSpellVertex", SIGHT_CPP, 0x1fd, 0);
                                entry->los_flags[3] = false;
                            } else {
                                short line =
                                    g_octree->TraceLineOfSight(&trace_position, &other_position, true, -3, -3, true, 0);

                                entry->los_flags[3] = line != 1;
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    camera_position = cameraLocation();
    {
        float distance = monster->GetDistanceToPlayer();
        bool visible_to_player;

        monster_info->within_viewing_distance = distance <= viewing_distance;
        monster_info->player_visibility.can_see = false;
        monster_info->player_visibility.line_of_sight = false;
        if (viewing_distance < distance) {
            if (monster_info->player_visibility.last_seen_clock == 0) {
                monster_info->player_visibility.sight_state = W8_SIGHT_UNSEEN;
            } else {
                monster_info->player_visibility.sight_state = static_cast<unsigned char>(
                    (0xf0U <
                     static_cast<unsigned int>(g_status.world_clock - monster_info->player_visibility.last_seen_clock))
                        ? W8_SIGHT_UNSEEN
                        : W8_SIGHT_RECENT);
            }
        } else {
            monster_info->player_visibility.line_of_sight = monster->CheckLineOfSightToPlayer();
        }
        if (monster_info->player_visibility.line_of_sight) {
            if (monster_info->hp_current == 0 || monster_info->highest_condition > W8_CONDITION_WEBBED) {
                visible_to_player = false;
            } else {
                float yaw = monster->GetYaw();
                unsigned int sight_override;
                unsigned int minimum_level = 9999;
                unsigned char fade_flag;
                float player_distance;

                if (!gXStatus.fCombatMode) {
                    sight_override = g_status.party_modifiers.sight_override;
                } else {
                    sight_override = 0;
                }
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8PartySlotRow* row = &g_status.buffers.XChar[slot];
                    W8Character* character = &g_status.buffers.Char[slot];

                    if (row->fOccupied && character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP && character->uiExpLevel < minimum_level) {
                        minimum_level = character->uiExpLevel;
                    }
                }

                player_distance = monster->GetDistanceToPlayer();
                if (record->kind == 4) {
                    int bonus = record->effective_level * 5;

                    fade_flag = static_cast<unsigned char>(bonus > 0x7d ? 0x7d : bonus);
                } else {
                    fade_flag = 0;
                }
                srVector3T<float> observer_position;
                srVector3T<float> target_position;

                observer_position = own_position;
                target_position = camera_position;
                float threshold = ComputeSightThreshold(
                    observer_position, target_position, yaw, monster_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES],
                    fade_flag, monster_info->uiCondition[W8_CONDITION_BLIND] != 0, record->kind == 0xc,
                    static_cast<int>(minimum_level), static_cast<int>(sight_override),
                    monster_info->player_visibility.sight_state, 0, player_distance);

                visible_to_player = threshold >= player_distance;
            }
            monster_info->player_visibility.can_see = visible_to_player;
            if (visible_to_player) {
                monster_info->player_visibility.sight_state = W8_SIGHT_SEEN;
                monster_info->player_visibility.last_seen_clock = g_status.world_clock;
                monster_info->player_visibility.target_position = own_position;
                monster_info->player_visibility.subject_position = camera_position;
            }
        }
    }

    if (!monster_info->player_visibility.line_of_sight) {
        monster_info->player_visibility.los_flags[0] = false;
        monster_info->player_visibility.los_flags[1] = false;
        monster_info->player_visibility.los_flags[2] = false;
        monster_info->player_visibility.los_flags[3] = false;
    } else {
        monster->GetPlayerSightFlags(monster_info->player_visibility.los_flags,
                                     monster_info->player_visibility.los_flags + 2);
        if (monster_info->has_projectile_origin) {
            bool found = monster->GetProjectilePosition(&trace_position);
            bool clear;

            if (!found) {
                srAssertFail("fFoundMissileVertex", SIGHT_CPP, 0x9b, 0);
                clear = false;
            } else {
                short line = g_octree->TraceLineOfSight(&trace_position, &camera_position, true,
                                                        monster_info->location_id, -1, true, 1);

                clear = line == 0;
            }
            monster_info->player_visibility.los_flags[1] = clear;
        }
        if (monster_info->has_spell_origin) {
            bool found = monster->GetSpellPosition(&trace_position);

            if (!found) {
                srAssertFail("fFoundSpellVertex", SIGHT_CPP, 0xa3, 0);
                monster_info->player_visibility.los_flags[3] = false;
            } else {
                short line = g_octree->TraceLineOfSight(&trace_position, &camera_position, true, -3, -3, true, 0);

                monster_info->player_visibility.los_flags[3] = line != 1;
            }
        }
    }

    {
        bool in_range = monster_info->within_viewing_distance;
        bool seen_by_party = false;

        monster_info->party_threat.party_detected = false;
        monster_info->party_threat.visible_to_player = false;
        monster_info->party_threat.use_bounds = false;
        if (in_range) {
            if (monster_info->party_threat.sight_state != W8_SIGHT_UNSEEN) {
                use_bounds = true;
            }
            monster_info->party_threat.use_bounds = use_bounds;
            seen_by_party = monster->IsVisibleToPlayer(use_bounds);
            monster_info->party_threat.visible_to_player = seen_by_party;
            if (seen_by_party) {
                float yaw;
                float distance;
                unsigned char npc_fade_flag;

                record = GetMonsterDataForInfo(monster_info);
                camera_position = cameraLocation();
                yaw = GetCameraYawRadians();
                distance = monster->GetDistanceToPlayer();
                npc_fade_flag = 0;
                if (!monster_info->fInCombat) {
                    npc_fade_flag = record->camouflage0;
                }
                seen_by_party = false;
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8PartySlotRow* row = &g_status.buffers.XChar[slot];
                    W8Character* character = &g_status.buffers.Char[slot];

                    if (row->fOccupied && character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP) {
                        srVector3T<float> observer_position;
                        srVector3T<float> target_position;

                        observer_position = camera_position;
                        target_position = own_position;
                        float threshold = ComputeSightThreshold(
                            observer_position, target_position, yaw,
                            character->attributes[W8_ATTRIBUTE_SENSES].effective,
                            static_cast<unsigned char>(character->skills[W8_SKILL_SCOUTING].level),
                            character->uiCondition[W8_CONDITION_BLIND] != 0,
                            character->iProfession == W8_PROFESSION_MONK, record->effective_level, npc_fade_flag,
                            static_cast<int>(monster_info->party_threat.sight_state),
                            g_status.party_modifiers.sight_override, distance);

                        if (g_status.search_mode != 0) {
                            threshold *= g_sight_threat_scale;
                        }
                        if (threshold >= distance) {
                            seen_by_party = true;
                            break;
                        }
                    }
                }
                monster_info->party_threat.party_detected = seen_by_party;
                if (seen_by_party) {
                    if (GetViewDistance() == g_sight_default) {
                        if (record == 0 || (SetFactionFlag(static_cast<signed char>(record->faction_id), true),
                                            (record->flags & W8_MONSTER_FLAG_NPC) == 0)) {
                            unsigned int now = static_cast<unsigned int>(g_game_time_accumulator->GetElapsed());

                            if ((((monster_info->party_threat.last_seen_clock == 0) ||
                                  (0x78U < static_cast<unsigned int>(g_status.world_clock -
                                                                     monster_info->party_threat.last_seen_clock))) &&
                                 (ShowMonsterTargetMarker(monster_info) == 0)) &&
                                (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE ||
                                 monster_info->ubDisposition == W8_DISPOSITION_NEUTRAL) &&
                                (g_sight_marker_tick == 0 || now - g_sight_marker_tick > 199)) {
                                g_sight_marker_tick = now;
                                int party_slot = GetRandomCharacter(0, 0, -1, -1);

                                if (party_slot != -1) {
                                    int effect = g_sight_effect2;

                                    if (Random(2) == 0) {
                                        effect = g_sight_effect3;
                                    }
                                    W8CharacterEvent* notice =
                                        QueueCharacterEvent(&g_status.buffers.Char[party_slot], effect, 0,
                                                            g_character_event_no_flags, g_character_event_full_volume);

                                    if (notice != 0) {
                                        notice->DelayDispatch(0x5dc);
                                    }
                                }
                            }
                        } else {
                            W8NpcState* npc = GetNpcStateByKind(record->npc_kind);

                            if (npc == 0) {
                                srAssertFail("pNPC != NULL", SIGHT_CPP, 0xe9, 0);
                            } else if (!npc->party_noticed && npc->record->monster_bound == 0 &&
                                       ShowMonsterTargetMarker(monster_info) != 0) {
                                srVector3T<float> delta;
                                delta = own_position - g_startup_world->GetPosition();
                                if (delta.Length() < 25000.0f &&
                                    (npc->party_noticed = true, !gXStatus.fSurprisePossible) &&
                                    g_status.current_level != 4) {
                                    int party_slot = GetRandomCharacter(0, 0, -1, -1);

                                    if (party_slot != -1) {
                                        int effect = g_sight_effect0;

                                        if (npc->name_style == W8_NPC_VI_DOMINA && GetFact(W8_FACT_PARTY_MET_VI) != 0) {
                                            effect = g_sight_effect1;
                                        }
                                        W8CharacterEvent* notice = QueueCharacterEvent(
                                            &g_status.buffers.Char[party_slot], effect, 0, g_character_event_no_flags,
                                            g_character_event_full_volume);

                                        if (notice != 0) {
                                            notice->DelayDispatch(0x5dc);
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (monster_info->party_threat.sight_state != W8_SIGHT_SEEN && record->camouflage0 != 0) {
                        unsigned int now = static_cast<unsigned int>(g_game_time_accumulator->GetElapsed());

                        monster->BeginFadeIn(5.0f);
                        if (!gXStatus.fSurprisePossible &&
                            (g_sight_fade_in_tick == 0 || now - g_sight_fade_in_tick > 199)) {
                            g_sight_fade_in_tick = now;
                            ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x1dd]);
                        }
                    }
                    monster_info->party_threat.sight_state = W8_SIGHT_SEEN;
                    monster_info->party_threat.last_seen_clock = g_status.world_clock;
                    monster_info->party_threat.camera_position = camera_position;
                    monster_info->party_threat.own_position = own_position;
                }
            }
        }
        if (!seen_by_party) {
            if (monster_info->party_threat.sight_state == W8_SIGHT_SEEN && record->camouflage0 != 0) {
                unsigned int now = static_cast<unsigned int>(g_game_time_accumulator->GetElapsed());

                monster->BeginFadeOut(5.0f);
                if (!gXStatus.fSurprisePossible && (g_sight_fade_out_tick == 0 || now - g_sight_fade_out_tick > 199)) {
                    g_sight_fade_out_tick = now;
                    ShowNotice(W8_FONT_PALETTE_WHITE, g_localized_strings[0x1de]);
                }
            }
            if (monster_info->party_threat.last_seen_clock == 0) {
                monster_info->party_threat.sight_state = W8_SIGHT_UNSEEN;
            } else {
                monster_info->party_threat.sight_state = static_cast<unsigned char>(
                    (0x78U <
                     static_cast<unsigned int>(g_status.world_clock - monster_info->party_threat.last_seen_clock))
                        ? W8_SIGHT_UNSEEN
                        : W8_SIGHT_RECENT);
            }
            if (record != 0 && (record->flags & W8_MONSTER_FLAG_NPC) != 0) {
                W8NpcState* npc = GetNpcStateByKind(record->npc_kind);

                if (npc == 0) {
                    srAssertFail("pNPC != NULL", SIGHT_CPP, 0x15b, 0);
                } else if (npc->event_pending != 0) {
                    HandleMarkedNpcEvent(npc, 0);
                }
            }
        }
    }

    if (!monster_info->party_threat.visible_to_player) {
        monster_info->party_threat.los_flags[0] = false;
        monster_info->party_threat.los_flags[1] = false;
        return;
    }
    monster->GetPlayerToMonsterSightFlags(monster_info->party_threat.los_flags,
                                          monster_info->party_threat.los_flags + 1, 0);
}

/* Whether the source group's monster can see the target group's monster. Out
   of combat the sight check is distance-gated by the world far clip and runs
   CanMonsterSeeMonster plus the 3D line-of-sight test; in combat the source's
   mon-to-mon visibility list is consulted directly. */
// FUNCTION: WIZ8 0x00505F30
bool MonsterGroupCanSeeGroup(W8MonsterGroup* source, W8MonsterGroup* target)
{
    W8MonsterInfo* source_info;
    W8MonsterInfo* target_info;
    W8VisibilityRecord record;
    W8Monster* source_monster;
    W8Monster* target_monster;
    W8World* world;
    float distance;
    double far_clip;

    source_info = FindMonster(source->leader_location_id);
    target_info = FindMonster(target->leader_location_id);
    source_monster = source_info->p3D;
    target_monster = target_info->p3D;
    if (!target_info->fInCombat) {
        distance = source_monster->GetDistanceToMonster(target_monster);
        world = GetWorld();
        far_clip = WorldGetFarClip(world);
        if (distance <= far_clip) {
            memset(&record, 0, sizeof(record));
            record.sight_state = W8_SIGHT_SEEN;
            if (CanMonsterSeeMonster(source_info, target_info, &record) &&
                source_monster->HasLineOfSightToMonster(target_monster)) {
                return true;
            }
        }
    } else {
        const auto* visibility = FindMonToMonVisibility(source_info, target_info);
        return visibility && visibility->sight_state == W8_SIGHT_SEEN;
    }
    return false;
}
