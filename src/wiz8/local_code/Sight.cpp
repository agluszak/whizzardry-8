#include "wiz8/local_code/Sight.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <span>
#include <unordered_map>
#include <vector>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "local_code/Factions.h"
#include "sight_state.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/render/picking.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/gameplay_modifiers.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

struct World;

const float g_sight_default = 12.0f;

/* Put the sight subsystem back to its starting state. */
void ResetSight(void)
{
    SetViewDistance(12.0f);
    SetNavigatorLinkMode(W8_NAVIGATOR_LINK_MODE_DEFAULT);
    GameTimeAccumulator().ResetTimeScale();
    ResetMonsterGeneratorTimers();
}

/* Whether the view distance has been moved off its default. */
bool IsSightRangeOverridden(void)
{
    return GetViewDistance() != g_sight_default;
}

/* Re-run both directions of the sight update for one monster. */
void RefreshMonsterSight(W8MonsterInfo* monster_info)
{
    UpdateMonsterSight(monster_info, true, false);
    UpdateMonsterSight(monster_info, false, false);
}

/* Re-run the outward direction over every live monster, and tell the combat
   display about it if a fight is on. The count is re-read every step because
   the update can remove a monster. */
void RefreshOutwardSightForAllMonsters(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        UpdateMonsterSight(GetMonsterListEntry(index), true, false);
    }
    if (CurrentGame().combat.has_value()) {
        RefreshFlaggedMainGameState();
        RefreshAllPartyTargets();
    }
}

/* The inward direction on its own, with nothing to tell the display. */
void RefreshInwardSightForAllMonsters(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        UpdateMonsterSight(GetMonsterListEntry(index), false, false);
    }
}

/* Both directions over every monster, in two separate passes rather than one
   pass doing both - the outward pass and its display notification have to
   finish before the inward pass starts. */
void RefreshAllSight(void)
{
    unsigned int index;

    RefreshOutwardSightForAllMonsters();
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        UpdateMonsterSight(GetMonsterListEntry(index), false, false);
    }
}

/* Whether one monster can see another in combat once line of sight is clear. */
bool CanMonsterSeeMonster(W8MonsterInfo* source, W8MonsterInfo* target, W8VisibilityRecord* record)
{
    const W8MonsterRecord* target_record;
    glm::vec3 observer_position;
    glm::vec3 target_position;
    float observer_yaw;
    int penalty_modifier;
    float distance;
    const W8MonsterRecord* source_record;
    unsigned char ranged_bonus;
    float threshold;

    if (source->hp_current == 0 || source->highest_condition >= W8_CONDITION_ASLEEP) {
        return false;
    }
    target_record = GetMonsterDataForInfo(target);
    Monster& source_monster = *source->entity;
    observer_position = source_monster.nav.movement.position;
    observer_position.y += source_monster.nav.movement.height_offset;
    Monster& target_monster = *target->entity;
    target_position = target_monster.nav.movement.position;
    target_position.y += target_monster.nav.movement.height_offset;
    observer_yaw = source_monster.nav.GetYaw();
    if (!target->pCombat) {
        penalty_modifier = target_record->camouflage0;
    } else {
        penalty_modifier = 0;
    }
    distance = source_monster.GetDistanceToMonster(target_monster);
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
        observer_position, target_position, observer_yaw,
        source->attributes[W8_MONSTER_ATTRIBUTE_SENSES], ranged_bonus,
        static_cast<unsigned char>(source->uiCondition[W8_CONDITION_BLIND] != 0),
        static_cast<unsigned char>(source_record->kind == 12),
        static_cast<int>(target_record->effective_level), penalty_modifier,
        static_cast<int>(record->sight_state), 0, distance);
    if (threshold < distance) {
        return false;
    }
    return true;
}

/* Maximum distance at which the observer can perceive the target. */
float ComputeSightThreshold(glm::vec3 observer_position, glm::vec3 target_position,
                            float observer_yaw, unsigned int perception_attribute,
                            unsigned char ranged_bonus, bool blinded, bool extended_sight_active,
                            int penalty_source, int penalty_modifier, int skip_field_of_view,
                            unsigned char sight_override, float distance)
{
    World* world;
    double far_clip;
    float viewing_distance;
    float heading;
    float angle_delta;
    int sight_percent;
    int penalty;

    world = GetWorld();
    far_clip = WorldGetFarClip(world);
    viewing_distance = static_cast<float>(far_clip);
    assert(viewing_distance > 0.0f);
    heading = GetHeadingAngle(&observer_position, &target_position);
    angle_delta = ShortestAngleDistance(observer_yaw, heading);
    if (blinded && !extended_sight_active) {
        return 0.0f;
    }
    if (angle_delta < static_cast<float>(g_monster_facing_tolerance) || sight_override != 0 ||
        skip_field_of_view != 0 || distance == 0.0f) {
        sight_percent = 100;
    } else {
        float sight_factor =
            static_cast<float>(static_cast<int>(perception_attribute) - penalty_source * 2);
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
    sight_percent = std::clamp(sight_percent, 2, 100);
    return sight_percent * viewing_distance * kMovementSpeedStep;
}

/* Drop every visibility record anyone held about one departing monster. */
void ReleaseMonToMonVisibilityInfoAbout(int location_id)
{
    for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
        auto* monster = monster_owner.get();
        if (monster->fActive && monster->monster_visibility) {
            monster->monster_visibility->erase(location_id);
        }
    }
}

/* Empty and destroy one monster's whole visibility list, then drop what
   everybody else held about it. */
void ReleaseMonToMonVisibilityList(W8MonsterInfo* monster_info)
{
    monster_info->monster_visibility.reset();
    ReleaseMonToMonVisibilityInfoAbout(monster_info->location_id);
}

/* Whether a monster group still has anybody in it worth seeing: alive, not
   dying, not too far gone, and marked as a threat. */
bool MonsterGroupHasVisibleThreat(W8MonsterGroup* group)
{
    unsigned int index;
    W8MonsterInfo* monster_info;

    for (index = 0; index < group->MemberIds().size(); ++index) {
        monster_info = FindMonster(group->MemberLocationId(index));
        if (monster_info->fActive && !monster_info->entity->IsDying() &&
            monster_info->hp_current != 0 && monster_info->highest_condition < W8_CONDITION_BLIND &&
            monster_info->party_threat.sight_state == W8_SIGHT_SEEN) {
            return true;
        }
    }
    return false;
}

/* Spell visibility occupies entry two or three, depending on launch origin. */
signed char GetMonsterSpellSightIndex(const W8MonsterInfo* monster)
{
    return (monster->has_spell_origin) + 2;
}

/* Read one flag out of a visibility record. Two of the seven kinds are pairs
   that the monster's missile and spell conditions choose between; the rest are
   fixed, and anything past the named kinds falls back to the last flag. */
// FUNCTION: WIZ8 0x00505dd0
bool IsVisibleUnderConditions(const W8MonsterInfo* monster, const W8VisibilityRecord* row,
                              W8VisibilityCondition condition)
{
    if (row == 0) {
        return false;
    }
    switch (condition) {
    case W8VisibilityCondition::SightState:
        return row->sight_state != W8_SIGHT_UNSEEN;
    case W8VisibilityCondition::CanSee:
        return row->can_see;
    case W8VisibilityCondition::FirstLineOfSightFlag:
        return row->los_flags[0];
    case W8VisibilityCondition::ThirdLineOfSightFlag:
        return row->los_flags[2];
    case W8VisibilityCondition::ProjectileOriginLineOfSight:
        return row->los_flags[monster->has_projectile_origin];
    case W8VisibilityCondition::SpellOriginLineOfSight:
        return row->los_flags[2 + (monster->has_spell_origin)];
    case W8VisibilityCondition::GeneralLineOfSight:
        return row->line_of_sight;
    }
    return false;
}

/* Bring every monster's sight up to date with the clock, in whole two-minute
   steps - anything short of one step is left for next time. */
void AgeAllMonsterSight(void)
{
    unsigned int elapsed;
    unsigned int steps;
    unsigned int index;
    W8MonsterInfo* monster_info;

    // Retail scales both clocks to milliseconds modulo 2^32 before subtracting.
    elapsed = static_cast<unsigned int>(CurrentGame().status.world_clock) * 1000u -
              static_cast<unsigned int>(
                  CurrentGame().status.level_progress[GetLoadedLevelID()].sight_clock) *
                  1000u;
    steps = elapsed / 120000;
    if (steps == 0) {
        return;
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        monster_info = GetMonsterListEntry(index);
        if (monster_info->highest_condition < W8_CONDITION_DEAD) {
            AgeMonsterSight(monster_info, steps, 1);
        }
    }
}

/* The map owns records and keeps their addresses stable while other entries
   are inserted or removed. A missing list or target has no visibility record. */
W8VisibilityRecord* FindMonToMonVisibility(W8MonsterInfo* source, W8MonsterInfo* target)
{
    if (!source->monster_visibility) return nullptr;
    auto& visibility = *source->monster_visibility;
    auto found = visibility.find(target->location_id);
    return found == visibility.end() ? nullptr : &found->second;
}

/* Bring every monster's sight up to date from a cleared starting state: the
   two per-monster sight blocks are emptied first, then the outward pass, the
   combat display notification it feeds, and finally the inward pass. */
void ResetAndRefreshAllSight(void)
{
    unsigned int index;

    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* monster_info = GetMonsterListEntry(index);

        monster_info->party_threat = {};
        monster_info->player_visibility = {};
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        UpdateMonsterSight(GetMonsterListEntry(index), true, false);
    }
    if (CurrentGame().combat.has_value()) {
        RefreshFlaggedMainGameState();
        RefreshAllPartyTargets();
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        UpdateMonsterSight(GetMonsterListEntry(index), false, false);
    }
}

static int g_sight_marker_tick;
static int g_sight_fade_in_tick;
static int g_sight_fade_out_tick;

const float g_sight_threat_scale = 0.6667f;

/* Recompute one monster's sight state. Direction zero is the monster-to-
   monster pass, which revisits every other monster's per-monster record;
   nonzero is the player pass, which refreshes the party-facing record, stamps
   the notice and fade state, and ends in the player-to-monster flag pass. */
void UpdateMonsterSight(W8MonsterInfo* monster_info, bool direction, bool use_bounds)
{
    const W8MonsterRecord* record;
    glm::vec3 own_position;
    float viewing_distance;
    glm::vec3 camera_position;
    glm::vec3 other_position;
    glm::vec3 trace_position;

    if (!CurrentGame().activity.in_world() || CurrentGame().advancement.hold.covered ||
        !monster_info->fActive || monster_info->hp_current == 0) {
        return;
    }
    record = GetMonsterDataForInfo(monster_info);
    Monster& monster = *monster_info->entity;
    viewing_distance = static_cast<float>(WorldGetFarClip(&monster.owner_world));
    assert(viewing_distance > 0.0f);
    own_position = monster.nav.movement.position;
    own_position.y += monster.nav.movement.height_offset;

    if (!direction) {
        unsigned int index;

        if (!CurrentGame().combat.has_value()) {
            return;
        }
        for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
            W8MonsterInfo* other = GetMonsterListEntry(index);

            if (other != monster_info && other->pCombat && other->fActive &&
                other->hp_current != 0) {
                auto* entry = &(*monster_info->monster_visibility)[other->location_id];
                entry->about_location_id = other->location_id;
                {
                    Monster& other_monster = *other->entity;
                    float distance;

                    other_position = other_monster.nav.movement.position;
                    other_position.y += other_monster.nav.movement.height_offset;
                    entry->can_see = false;
                    entry->line_of_sight = false;
                    bool can_see = false;

                    distance = monster.GetDistanceToMonster(other_monster);
                    if (viewing_distance >= distance) {
                        bool line_of_sight = monster.HasLineOfSightToMonster(other_monster);

                        entry->line_of_sight = line_of_sight;
                        if (line_of_sight) {
                            can_see = CanMonsterSeeMonster(monster_info, other, entry);
                            entry->can_see = can_see;
                        }
                    }
                    if (can_see) {
                        entry->sight_state = W8_SIGHT_SEEN;
                        entry->last_seen_clock = CurrentGame().status.world_clock;
                        entry->subject_position = own_position;
                        entry->target_position = other_position;
                    } else if (entry->last_seen_clock == 0) {
                        entry->sight_state = W8_SIGHT_UNSEEN;
                    } else {
                        entry->sight_state = static_cast<W8SightState>(
                            (0xf0U < static_cast<unsigned int>(CurrentGame().status.world_clock -
                                                               entry->last_seen_clock))
                                ? W8_SIGHT_UNSEEN
                                : W8_SIGHT_RECENT);
                    }
                    if (!entry->line_of_sight) {
                        std::ranges::fill(entry->los_flags, false);
                    } else {
                        monster.GetMonsterSightFlags(other_monster, entry->los_flags,
                                                     entry->los_flags + 2);
                        if (monster_info->has_projectile_origin) {
                            bool found = monster.GetProjectilePosition(&trace_position);
                            bool clear;

                            // A model without the origin vertex cannot see past itself.
                            if (!found) {
                                clear = false;
                            } else {
                                const auto hit = monster.owner_world.compiledIndex()->traceSight(
                                    trace_position, other_position,
                                    {.locations = SightLocations{monster_info->location_id,
                                                                 other->location_id, true}});
                                // The spell-origin trace ends where this one stopped.
                                if (hit) other_position = hit->point;
                                clear = !hit;
                            }
                            entry->los_flags[1] = clear;
                        }
                        if (monster_info->has_spell_origin) {
                            bool found = monster.GetSpellPosition(&trace_position);

                            if (!found) {
                                entry->los_flags[3] = false;
                            } else {
                                entry->los_flags[3] =
                                    !monster.owner_world.compiledIndex()->traceSight(
                                        trace_position, other_position);
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
        float distance = monster.GetDistanceToPlayer();
        bool visible_to_player;

        monster_info->within_viewing_distance = distance <= viewing_distance;
        monster_info->player_visibility.can_see = false;
        monster_info->player_visibility.line_of_sight = false;
        if (viewing_distance < distance) {
            if (monster_info->player_visibility.last_seen_clock == 0) {
                monster_info->player_visibility.sight_state = W8_SIGHT_UNSEEN;
            } else {
                monster_info->player_visibility.sight_state = static_cast<W8SightState>(
                    (0xf0U <
                     static_cast<unsigned int>(CurrentGame().status.world_clock -
                                               monster_info->player_visibility.last_seen_clock))
                        ? W8_SIGHT_UNSEEN
                        : W8_SIGHT_RECENT);
            }
        } else {
            monster_info->player_visibility.line_of_sight = monster.CheckLineOfSightToPlayer();
        }
        if (monster_info->player_visibility.line_of_sight) {
            if (monster_info->hp_current == 0 ||
                monster_info->highest_condition > W8_CONDITION_WEBBED) {
                visible_to_player = false;
            } else {
                float yaw = monster.nav.GetYaw();
                unsigned int sight_override;
                unsigned int minimum_level = 9999;
                unsigned char fade_flag;
                float player_distance;

                if (!CurrentGame().combat.has_value()) {
                    sight_override = CurrentGame().party.modifiers.sight_override;
                } else {
                    sight_override = 0;
                }
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8Character* character = &CurrentGame().party.characters[slot];

                    if (CurrentGame().party.members[slot].has_value() &&
                        character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP &&
                        character->uiExpLevel < minimum_level) {
                        minimum_level = character->uiExpLevel;
                    }
                }

                player_distance = monster.GetDistanceToPlayer();
                if (record->kind == 4) {
                    int bonus = record->effective_level * 5;

                    fade_flag = static_cast<unsigned char>(bonus > 0x7d ? 0x7d : bonus);
                } else {
                    fade_flag = 0;
                }
                glm::vec3 observer_position;
                glm::vec3 target_position;

                observer_position = own_position;
                target_position = camera_position;
                float threshold = ComputeSightThreshold(
                    observer_position, target_position, yaw,
                    monster_info->attributes[W8_MONSTER_ATTRIBUTE_SENSES], fade_flag,
                    monster_info->uiCondition[W8_CONDITION_BLIND] != 0, record->kind == 0xc,
                    static_cast<int>(minimum_level), static_cast<int>(sight_override),
                    monster_info->player_visibility.sight_state, 0, player_distance);

                visible_to_player = threshold >= player_distance;
            }
            monster_info->player_visibility.can_see = visible_to_player;
            if (visible_to_player) {
                monster_info->player_visibility.sight_state = W8_SIGHT_SEEN;
                monster_info->player_visibility.last_seen_clock = CurrentGame().status.world_clock;
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
        monster.GetPlayerSightFlags(monster_info->player_visibility.los_flags,
                                    monster_info->player_visibility.los_flags + 2);
        if (monster_info->has_projectile_origin) {
            bool found = monster.GetProjectilePosition(&trace_position);
            bool clear;

            if (!found) {
                clear = false;
            } else {
                const auto hit = monster.owner_world.compiledIndex()->traceSight(
                    trace_position, camera_position,
                    {.locations = SightLocations{monster_info->location_id, -1, true}});
                // The spell-origin trace ends where this one stopped.
                if (hit) camera_position = hit->point;
                clear = !hit;
            }
            monster_info->player_visibility.los_flags[1] = clear;
        }
        if (monster_info->has_spell_origin) {
            bool found = monster.GetSpellPosition(&trace_position);

            if (!found) {
                monster_info->player_visibility.los_flags[3] = false;
            } else {
                monster_info->player_visibility.los_flags[3] =
                    !monster.owner_world.compiledIndex()->traceSight(trace_position,
                                                                     camera_position);
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
            seen_by_party = monster.IsVisibleToPlayer(use_bounds);
            monster_info->party_threat.visible_to_player = seen_by_party;
            if (seen_by_party) {
                float yaw;
                float distance;
                unsigned char npc_fade_flag;

                record = GetMonsterDataForInfo(monster_info);
                camera_position = cameraLocation();
                yaw = GetCameraYawRadians();
                distance = monster.GetDistanceToPlayer();
                npc_fade_flag = 0;
                if (!monster_info->pCombat) {
                    npc_fade_flag = record->camouflage0;
                }
                seen_by_party = false;
                for (int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
                    W8Character* character = &CurrentGame().party.characters[slot];

                    if (CurrentGame().party.members[slot].has_value() &&
                        character->hp_current != 0 &&
                        character->highest_condition < W8_CONDITION_ASLEEP) {
                        glm::vec3 observer_position;
                        glm::vec3 target_position;

                        observer_position = camera_position;
                        target_position = own_position;
                        float threshold = ComputeSightThreshold(
                            observer_position, target_position, yaw,
                            character->attributes[W8_ATTRIBUTE_SENSES].effective,
                            static_cast<unsigned char>(character->skills[W8_SKILL_SCOUTING].level),
                            character->uiCondition[W8_CONDITION_BLIND] != 0,
                            character->iProfession == W8_PROFESSION_MONK, record->effective_level,
                            npc_fade_flag, static_cast<int>(monster_info->party_threat.sight_state),
                            CurrentGame().party.modifiers.sight_override, distance);

                        if (CurrentGame().status.search_mode) {
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
                        if (record == 0 ||
                            (SetFactionFlag(static_cast<signed char>(record->faction_id), true),
                             (record->flags & W8_MONSTER_FLAG_NPC) == 0)) {
                            unsigned int now =
                                static_cast<unsigned int>(GameTimeAccumulator().GetElapsed());

                            if ((((monster_info->party_threat.last_seen_clock == 0) ||
                                  (0x78U < static_cast<unsigned int>(
                                               CurrentGame().status.world_clock -
                                               monster_info->party_threat.last_seen_clock))) &&
                                 (ShowMonsterTargetMarker(monster_info) == 0)) &&
                                (monster_info->ubDisposition == W8_DISPOSITION_HOSTILE ||
                                 monster_info->ubDisposition == W8_DISPOSITION_NEUTRAL) &&
                                (g_sight_marker_tick == 0 || now - g_sight_marker_tick > 199)) {
                                g_sight_marker_tick = now;
                                int party_slot = GetRandomCharacter(0, 0, -1, -1);

                                if (party_slot != -1) {
                                    int effect = 0x43;

                                    if (CurrentGame().rng.below(2) == 0) {
                                        effect = 0x44;
                                    }
                                    W8CharacterEvent* notice = QueueCharacterEvent(
                                        &CurrentGame().party.characters[party_slot], effect, 0,
                                        kCharacterEventFullVolume);

                                    if (notice != 0) {
                                        notice->DelayDispatch(std::chrono::milliseconds{1500});
                                    }
                                }
                            }
                        } else {
                            W8NpcState* npc = GetNpcStateByKind(record->npc_kind);

                            if (npc != 0 && !npc->party_noticed &&
                                npc->record->monster_bound == 0 &&
                                ShowMonsterTargetMarker(monster_info) != 0) {
                                glm::vec3 delta;
                                delta = own_position - monster.owner_world.party.position();
                                if (glm::length(delta) < 25000.0f &&
                                    (npc->party_noticed = true, !CurrentGame().surprise_possible) &&
                                    GetLoadedLevelID() != 4) {
                                    int party_slot = GetRandomCharacter(0, 0, -1, -1);

                                    if (party_slot != -1) {
                                        int effect = 0x26;

                                        if (npc->name_style == W8_NPC_VI_DOMINA &&
                                            GetFact(W8_FACT_PARTY_MET_VI) != 0) {
                                            effect = 0x39;
                                        }
                                        W8CharacterEvent* notice = QueueCharacterEvent(
                                            &CurrentGame().party.characters[party_slot], effect, 0,
                                            kCharacterEventFullVolume);

                                        if (notice != 0) {
                                            notice->DelayDispatch(std::chrono::milliseconds{1500});
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (monster_info->party_threat.sight_state != W8_SIGHT_SEEN &&
                        record->camouflage0 != 0) {
                        unsigned int now =
                            static_cast<unsigned int>(GameTimeAccumulator().GetElapsed());

                        monster.BeginFadeIn(5.0f);
                        if (!CurrentGame().surprise_possible &&
                            (g_sight_fade_in_tick == 0 || now - g_sight_fade_in_tick > 199)) {
                            g_sight_fade_in_tick = now;
                            ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x1dd]);
                        }
                    }
                    monster_info->party_threat.sight_state = W8_SIGHT_SEEN;
                    monster_info->party_threat.last_seen_clock = CurrentGame().status.world_clock;
                    monster_info->party_threat.camera_position = camera_position;
                    monster_info->party_threat.own_position = own_position;
                }
            }
        }
        if (!seen_by_party) {
            if (monster_info->party_threat.sight_state == W8_SIGHT_SEEN &&
                record->camouflage0 != 0) {
                unsigned int now = static_cast<unsigned int>(GameTimeAccumulator().GetElapsed());

                monster.BeginFadeOut(5.0f);
                if (!CurrentGame().surprise_possible &&
                    (g_sight_fade_out_tick == 0 || now - g_sight_fade_out_tick > 199)) {
                    g_sight_fade_out_tick = now;
                    ShowNotice(W8_FONT_PALETTE_WHITE, Data().strings[0x1de]);
                }
            }
            if (monster_info->party_threat.last_seen_clock == 0) {
                monster_info->party_threat.sight_state = W8_SIGHT_UNSEEN;
            } else {
                monster_info->party_threat.sight_state = static_cast<W8SightState>(
                    (0x78U < static_cast<unsigned int>(CurrentGame().status.world_clock -
                                                       monster_info->party_threat.last_seen_clock))
                        ? W8_SIGHT_UNSEEN
                        : W8_SIGHT_RECENT);
            }
            if (record != 0 && (record->flags & W8_MONSTER_FLAG_NPC) != 0) {
                W8NpcState* npc = GetNpcStateByKind(record->npc_kind);

                if (npc != 0 && npc->event_pending != 0) {
                    HandleMarkedNpcEvent(npc, W8MarkedNpcEventMode::arm_restore);
                }
            }
        }
    }

    if (!monster_info->party_threat.visible_to_player) {
        monster_info->party_threat.los_flags[0] = false;
        monster_info->party_threat.los_flags[1] = false;
        return;
    }
    monster.GetPlayerToMonsterSightFlags(monster_info->party_threat.los_flags,
                                         monster_info->party_threat.los_flags + 1, 0);
}

/* Whether the source group's monster can see the target group's monster. Out
   of combat the sight check is distance-gated by the world far clip and runs
   CanMonsterSeeMonster plus the 3D line-of-sight test; in combat the source's
   mon-to-mon visibility list is consulted directly. */
bool MonsterGroupCanSeeGroup(W8MonsterGroup* source, W8MonsterGroup* target)
{
    W8MonsterInfo* source_info;
    W8MonsterInfo* target_info;
    W8VisibilityRecord record;
    World* world;
    float distance;
    double far_clip;

    source_info = FindMonster(source->leader_location_id);
    target_info = FindMonster(target->leader_location_id);
    Monster& source_monster = *source_info->entity;
    Monster& target_monster = *target_info->entity;
    if (!target_info->pCombat) {
        distance = source_monster.GetDistanceToMonster(target_monster);
        world = GetWorld();
        far_clip = WorldGetFarClip(world);
        if (distance <= far_clip) {
            record = {};
            record.sight_state = W8_SIGHT_SEEN;
            if (CanMonsterSeeMonster(source_info, target_info, &record) &&
                source_monster.HasLineOfSightToMonster(target_monster)) {
                return true;
            }
        }
    } else {
        const auto* visibility = FindMonToMonVisibility(source_info, target_info);
        return visibility && visibility->sight_state == W8_SIGHT_SEEN;
    }
    return false;
}
