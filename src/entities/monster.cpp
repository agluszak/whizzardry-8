#include "wiz8/entities/monster.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "application.h"
#include "asset_paths.h"
#include "audio_resources.h"
#include "glm/ext/matrix_double3x3.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "monster_actions.h"
#include "monster_cycles.h"
#include "texture_animation.h"
#include "wiz8/audio/SoundEvent.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/animation.h"
#include "wiz8/entities/animation_mesh.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/cycle_resources.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/ground_shadow.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/mesh_loader.h"
#include "wiz8/render/particle_modes.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/texture.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/surface_storage.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/simulation_sink.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

struct W8NpcState;

float g_monster_light_scale = 1.0f;

unsigned char g_monster_shadow_updates_enabled;
/* Layout-compatible with glm::vec3 but POD so VC6 emits static .data
   instead of a dynamic initializer into .bss. */
struct W8AttachmentOffset {
    float x;
    float y;
    float z;
};

static W8AttachmentOffset g_monster_attachment_offsets[8][8] = {
    {},
    {{-75.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}},
    {{-75.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {75.0f, 0.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f}, {-37.5f, 0.0f, 0.0f}, {37.5f, 0.0f, 0.0f}, {112.5f, 0.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {0.0f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-37.5f, 75.0f, 0.0f},
     {37.5f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-75.0f, 75.0f, 0.0f},
     {0.0f, 75.0f, 0.0f},
     {75.0f, 75.0f, 0.0f}},
    {{-112.5f, 0.0f, 0.0f},
     {-37.5f, 0.0f, 0.0f},
     {37.5f, 0.0f, 0.0f},
     {112.5f, 0.0f, 0.0f},
     {-112.5f, 75.0f, 0.0f},
     {-37.5f, 75.0f, 0.0f},
     {37.5f, 75.0f, 0.0f},
     {112.5f, 75.0f, 0.0f}}};
static float g_monster_attachment_scales[8] = {0.3f,  0.2f,  0.15f, 0.15f,
                                               0.15f, 0.15f, 0.15f, 0.15f};

const float g_monster_rotation_offset = 3.141592502593994f;
const double g_monster_death_rotation_pi = 3.141592653589793;
const float g_monster_attachment_distance_scale = 0.00039999998989515007f;
const float g_monster_attachment_vertical_scale = 30.0f;
const double g_monster_attachment_group_spacing = 15.0;
const float g_monster_linked_vertical_scale = 255.0f;
const float g_monster_poster_vertical_rate = 0.20000000298023224f;
const double g_monster_poster_max_distance = 1000.0;
const double g_monster_facing_tolerance = 0.78539815;
const double g_monster_group_nearest_range = 12500.0;
W8CycleNameRow g_cycle_names[W8_MONSTER_CYCLE_COUNT] = {
    {"BIRTH", 5},
    {"IDLE", 4},
    {"SPICE", 5},
    {"TRANSITION", 10},
    {"WALK", 4},
    {"DRAW", 4},
    {"ATTACK_CLOSE", 12},
    {"ATTACK_RANGED", 13},
    {"unused", 6},
    {"ATTACK_SWING", 12},
    {"ATTACK_THRUST", 13},
    {"ATTACK_BASH", 11},
    {"ATTACK_MELEE", 12},
    {"ATTACK_THROW", 12},
    {"ATTACK_PUNCH", 12},
    {"ATTACK_KICK", 11},
    {"ATTACK_LASH", 11},
    {"ATTACK_SHOOT", 12},
    {"ATTACK_SPECIAL", 14},
    {"DODGE", 5},
    {"GET_HIT", 7},
    {"DIE", 3},
    {"TURN", 4},
    {"TALK_SPICE", 10},
    {"TALK", 4},
    {"SPELL", 5},
    {"SPECIAL", 7},
};

/* Highlight triangle bitmaps indexed by the slot's marching-order position:
   the marker SetMonsterPartySlotMarker hangs on a monster. */
const char* g_party_target_marker_bitmaps[8] = {"TriRed.tga",  "TriGreen.tga",  "TriPurple.tga",
                                                "TriBlue.tga", "TriOrange.tga", "TriYellow.tga",
                                                "TriPink.tga", "TriBrown.tga"};

unsigned short ChooseDifferentMonsterDirection(unsigned short previous_direction)
{
    unsigned short direction;

    for (;;) {
        direction = static_cast<unsigned short>(CurrentGame().rng.below(6));
        if (direction > 3) {
            direction = 0;
        }
        switch (previous_direction) {
        case 0:
            if (direction == 0) {
                continue;
            }
            break;
        case 1:
            if (direction == 1) {
                continue;
            }
            break;
        case 2:
            if (direction == 2) {
                continue;
            }
            break;
        case 3:
            if (direction == 3) {
                continue;
            }
            break;
        default:
            continue;
        }
        return direction;
    }
}

/* Give an empty cycle its own copies of another cycle's animations and lights;
   the light copies stay out of the scene until SetCycle selects them. */
void Monster::CopyCycle(signed char cycle, signed char other_cycle)
{
    for (const auto& source : cycles[other_cycle]) cycles[cycle].emplace_back(source);
}

Monster::Monster(World* owner)
    : owner_world(owner), nav(owner), visual(owner), runtime_flags(0), location_id(-1),
      missile_frame(0), spell_frame(0), talking(0), animate_mouth(false), mouth_frame_clock(0),
      mouth_frame(0), talk_start(0), inactive(false), pending_finalize(1), disabled(0),
      nearest_to_party(false), script(0), script_wait(MONSCR_NONE), trigger(0),
      spell_effect_armed(0), sector_mesh(0), fade_state(W8_MONSTER_FADE_IDLE),
      hostility_preserved(false), sound(nullptr)
{
    nav.monster_owner = this;
    formation = glm::vec3(0);

    nav.trace_mask = 2;
    direction_x = 0;
    direction_y = 0;
    direction_z = 0;
    target_scale = 1.0f;
    current_scale = 1.0f;
    sunlit_state = W8_MONSTER_SUNLIGHT_UNKNOWN;
    orders_finished = false;
    defining_orders = false;
    deaf = false;
    face_party = false;
    stay_home = false;
    patrol_index = 0;
    look_frequency = 0;
    look_duration = 0;
    removal_state = W8_MONSTER_REMOVAL_NONE;
    cycle_callback = 0;
    move_dirty = true;
    order_mode = W8_MONSTER_ORDER_NONE;
}

Monster::Monster(const MonsterDefinition& definition, World* owner)
    : owner_world(owner), nav(owner), visual(definition.events, owner),
      asset_name(definition.asset_name), cycles(definition.cycles),
      damage_stage_count(definition.damage_stage_count), name(definition.asset_name),
      scale(definition.scale), minimum_scale(definition.minimum_scale),
      maximum_scale(definition.maximum_scale), death_scale(definition.death_scale),
      special_movement(definition.special_movement), runtime_flags(0), location_id(-1),
      missile_frame(definition.missile_frame), spell_frame(definition.spell_frame), talking(0),
      animate_mouth(false), mouth_frame_clock(0), mouth_frame(0), talk_start(0), inactive(false),
      pending_finalize(1), disabled(0), nearest_to_party(false), script(0),
      script_wait(MONSCR_NONE), trigger(0), spell_effect_armed(0), sector_mesh(0),
      fade_state(W8_MONSTER_FADE_IDLE), hostility_preserved(definition.hostility_preserved),
      sound(nullptr)
{
    nav.monster_owner = this;
    nav.trace_mask = 2;
    nav.movement.collision_radius = definition.collision_radius;
    nav.movement.alternate_radius = definition.alternate_radius;
    nav.configureStartupDepth(definition.height_offset, definition.secondary_height_offset);
    nav.SetBounds(definition.bounds);
    nav.SetMovementScale(definition.movement_scale);
    nav.SetTurnRate(definition.turn_rate);
    nav.SetNavigationMode(definition.navigation_mode);
    if (hostility_preserved) {
        nav.active = false;
        nav.movement.boundary_enabled = false;
    }
    if (owner && owner->compiledIndex())
        owner->compiledIndex()->registry.maximum_navigation_radius =
            std::max({owner->compiledIndex()->registry.maximum_navigation_radius,
                      nav.movement.collision_radius, nav.movement.alternate_radius, nav.radius});

    visual.skin = definition.skin;
    visual.damage_stage = definition.damage_stage;
    if (definition.lod_range) std::tie(visual.lod_near, visual.lod_far) = *definition.lod_range;
    if (definition.opacity) visual.opacity = *definition.opacity;
    visual.emissive_override = definition.emissive_override;
    if (definition.ground_shadow)
        visual.CreateGroundShadow(definition.ground_shadow->first,
                                  definition.ground_shadow->second);
    // Stand on the first authored animation until the spawn selects a cycle.
    if (const auto first =
            std::ranges::find_if(cycles, [](const auto& cycle) { return !cycle.empty(); });
        first != cycles.end()) {
        Animation& animation = *first->front().animation;
        visual.active_animation = &animation;
        visual.current_cycle = static_cast<signed char>(first - cycles.begin());
        visual.animation_behaviour = animation.behaviour;
        visual.frame_method = animation.frame_method;
        visual.animation_playing = animation.animation_playing;
    }
    visual.rep_active = true;
    visual.frame_direction = W8_ANIMATION_FORWARD;
    visual.m_bLOD = 2;
    if (definition.light) {
        monster_light = std::make_unique<Light>(*definition.light);
        monster_light->setScene(owner ? owner->scene.get() : nullptr);
    }

    formation = glm::vec3(0);
    target_scale = 1.0f;
    current_scale = 1.0f;
    sunlit_state = W8_MONSTER_SUNLIGHT_UNKNOWN;
    orders_finished = false;
    defining_orders = false;
    deaf = false;
    face_party = false;
    stay_home = false;
    patrol_index = 0;
    direction_x = 0;
    direction_y = 0;
    direction_z = 0;
    look_frequency = 0;
    look_duration = 0;
    removal_state = W8_MONSTER_REMOVAL_NONE;
    cycle_callback = 0;
    move_dirty = true;
    order_mode = W8_MONSTER_ORDER_NONE;
}

Monster::~Monster()
{
    visual.SetLights(0);
    ClearMonsterSpellIcons(this);
    script.reset();
    runtime_flags &= ~W8_MONSTER_SCRIPT_WAIT;
    script_line = 0;
    script_wait = MONSCR_NONE;
    sound.reset();
}

/* Navigator is Monster's second base at +0x18. VC6 places this override in
   that secondary table and emits the adjusted entry form at 0x004CA840. */
void Monster::SetPosition(const glm::vec3* position)
{
    visual.SetLocation(position);
    nav.SetPositionInternal(position);
    nav.position_dirty = true;
}

/* Advance the non-rendering half of one live Monster. This is the main
   Monster.cpp update slot: it maintains distance-driven model scale, the
   sunlight/ground-shadow transition, Navigator state, pending animation
   cycles, attached objects, and the optional scene node. Rendering remains in
   UpdateRepresentation. */
void Monster::Update()
{
    glm::vec3 party_position;
    glm::vec3 monster_position;
    W8MonsterInfo* monster_info = 0;
    float distance;
    int cycle;

    party_position = cameraLocation(*owner_world);
    {
        glm::vec3 position = nav.GetPosition();
        monster_position = position;
    }
    distance = glm::length((monster_position - party_position));

    if (sunlit_state == W8_MONSTER_SUNLIGHT_UNKNOWN || g_monster_light_scale < 1.0f) {
        current_scale = 0.75f;
        visual.light_scale = current_scale;
        target_scale = 0.75f;
        light_scale_timer.SetDuration(0.025f);
        light_scale_timer.Restart();
        sunlit_state = W8_MONSTER_SUNLIGHT_LIT;
        move_dirty = true;
    }

    if (g_monster_shadow_updates_enabled != 0 && (move_dirty || nav.UpdateTrackedPosition())) {
        move_dirty = false;
        if (distance < owner_world->render_range) {
            Light* sun = FindWorldLight("SUN");
            if (sun != 0) {
                glm::vec3 mapped_position;
                glm::dvec3 sun_location = sun->transform.getLocation();
                glm::vec3 sun_position;

                GetMappedPosition(&mapped_position);
                sun_position = glm::vec3(sun_location);
                if (owner_world->compiledIndex()->hasLineOfSight(mapped_position, sun_position)) {
                    if (sunlit_state == W8_MONSTER_SUNLIGHT_SHADOWED) {
                        target_scale = 0.75f;
                        light_scale_timer.SetDuration(0.025f);
                        light_scale_timer.Restart();
                        sunlit_state = W8_MONSTER_SUNLIGHT_LIT;
                    }
                } else if (sunlit_state == W8_MONSTER_SUNLIGHT_LIT) {
                    target_scale = 0.0f;
                    light_scale_timer.SetDuration(0.025f);
                    light_scale_timer.Restart();
                    sunlit_state = W8_MONSTER_SUNLIGHT_SHADOWED;
                }
            }
        }
    }

    if (current_scale != target_scale && light_scale_timer.GetProgress() >= 1.0f) {
        if (target_scale <= current_scale) {
            current_scale -= g_facing_tolerance0;
            if (current_scale < target_scale) {
                current_scale = target_scale;
            }
        } else {
            current_scale += g_facing_tolerance0;
            if (current_scale > target_scale) {
                current_scale = target_scale;
            }
        }
        visual.light_scale = current_scale;
        light_scale_timer.Restart();
    }

    if (fade_state != W8_MONSTER_FADE_IDLE) {
        float progress = fade_timer.GetProgress();
        if (progress > 1.0f) {
            progress = 1.0f;
        }
        if (fade_state == W8_MONSTER_FADE_DELAYED_REMOVAL) {
            if (progress == 1.0f) {
                fade_state = W8_MONSTER_FADE_IDLE;
                fade_timer.SetDuration(3.0f);
                fade_timer.Restart();
                if (fade_state < W8_MONSTER_FADE_IN) {
                    visual.instance_scale = 1.0f;
                    visual.apply_instance_scale = true;
                    fade_state = W8_MONSTER_FADE_OUT;
                } else {
                    fade_timer.SetProgress(1.0f - visual.instance_scale);
                    fade_state = W8_MONSTER_FADE_OUT;
                }
            }
        } else {
            if (fade_state < W8_MONSTER_FADE_IN) {
                visual.instance_scale = 1.0f - progress;
            } else {
                visual.instance_scale = progress;
            }
            visual.apply_instance_scale = true;
            if (progress == 1.0f) {
                if (fade_state < W8_MONSTER_FADE_IDLE) {
                    runtime_flags |= W8_MONSTER_FADED_OUT;
                }
                fade_state = W8_MONSTER_FADE_IDLE;
            }
        }
    }

    cycle = Query(W8_MONSTER_QUERY_CYCLE);
    visual.SetGroundShadowVisible(
        cycle != W8_MONSTER_CYCLE_BIRTH && cycle != W8_MONSTER_CYCLE_DIE &&
        fade_state == W8_MONSTER_FADE_IDLE && (runtime_flags & W8_MONSTER_FADED_OUT) == 0);

    if (W8MonsterInfo* found = owner_world->population->findMonster(location_id)) {
        monster_info = found;
    }

    if ((cycle == W8_MONSTER_CYCLE_IDLE || cycle == W8_MONSTER_CYCLE_SPICE) &&
        visual.pending_cycle == -1 && !nav.movement_stopped && !nav.halted) {
        nav.flags |= 0x100000;
    }

    const auto player = partyCollisionSphere(*owner_world);
    if (monster_info == 0) {
        nav.UpdateNavigation(0, false, player);
    } else {
        nav.UpdateNavigation(monster_info->highest_condition >= W8_CONDITION_WEBBED,
                             monster_info->uiCondition[W8_CONDITION_SLOWED] != 0, player);
    }

    if (cycle != W8_MONSTER_CYCLE_DIE && script != 0 && !CurrentGame().combat.has_value()) {
        ProcessScript();
        cycle = Query(W8_MONSTER_QUERY_CYCLE);
    }

    if ((runtime_flags & W8_MONSTER_SCRIPT_WAIT) == 0 && visual.pending_cycle == -1) {
        switch (cycle) {
        case W8_MONSTER_CYCLE_TALK:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (talk_duration < GameClockMilliseconds() - talk_start &&
                    IsCycleSupported(0x17)) {
                    visual.pending_cycle = 0x17;
                } else {
                    visual.pending_cycle = 0x18;
                }
                visual.frame_direction = W8_ANIMATION_FORWARD;
                visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                visual.animation_playing = 1;
                visual.timer = GameClockMilliseconds();
                visual.pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_TALK_SPICE:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (talking) {
                    /* 0x004C276F..0x004C27D7: the talking path never stores
                           pending_subcycle, so TALK keeps the current frame
                           (clamped by ApplyPendingCycle) instead of frame 0. */
                    talk_start = GameClockMilliseconds();
                    talk_duration = CurrentGame().rng.below(2000) + 2000;
                    visual.pending_cycle = 0x18;
                    visual.frame_direction = W8_ANIMATION_FORWARD;
                    visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                    visual.animation_playing = 1;
                    visual.timer = GameClockMilliseconds();
                } else {
                    visual.pending_cycle = 1;
                    visual.animation_playing = 1;
                    visual.timer = GameClockMilliseconds();
                    visual.pending_subcycle = 0;
                }
            }
            break;
        case W8_MONSTER_CYCLE_SPELL:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                visual.pending_cycle = 1;
                visual.frame_direction = W8_ANIMATION_FORWARD;
                visual.animation_playing = 1;
                visual.timer = GameClockMilliseconds();
                visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                visual.pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_WALK:
            if (nav.movement_stopped || nav.halted) {
                bool transition = Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) != 0 || visual.wrapped;
                if (!transition && !special_movement) {
                    transition =
                        Query(W8_MONSTER_QUERY_FRAME) < Query(W8_MONSTER_QUERY_FRAME_COUNT) / 2;
                }
                if (transition) {
                    while (!sounds.empty()) {
                        wiz8::sound::stop(sounds[0]);
                        sounds.erase(sounds.begin());
                    }
                    if (!IsCycleSupported(3)) {
                        visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                        visual.pending_cycle = 1;
                    } else {
                        visual.pending_cycle = 3;
                        visual.frame_direction = W8_ANIMATION_REVERSE;
                        visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                        visual.pending_subcycle =
                            static_cast<unsigned short>(Query(W8_MONSTER_QUERY_FRAME_COUNT) - 1);
                        runtime_flags |= W8_MONSTER_KEEP_FRAME_DIRECTION;
                    }
                    visual.animation_playing = 1;
                    visual.timer = GameClockMilliseconds();
                }
            }
            break;
        case W8_MONSTER_CYCLE_IDLE:
        case W8_MONSTER_CYCLE_SPICE:
            if (!nav.movement_stopped && !nav.halted &&
                (Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) != 0 || visual.wrapped)) {
                nav.flags &= ~0x100000;
                if (!IsCycleSupported(3)) {
                    visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                    visual.pending_cycle = 4;
                } else {
                    visual.pending_cycle = 3;
                    visual.frame_direction = W8_ANIMATION_FORWARD;
                    visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
                    visual.pending_subcycle = 0;
                }
                visual.animation_playing = 1;
                visual.timer = GameClockMilliseconds();
            }
            break;
        case W8_MONSTER_CYCLE_TRANSITION:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                if (visual.frame_direction == W8_ANIMATION_REVERSE) {
                    visual.frame_direction = W8_ANIMATION_FORWARD;
                    visual.pending_cycle = 1;
                } else {
                    visual.pending_cycle = 4;
                }
                visual.animation_playing = 1;
                visual.timer = GameClockMilliseconds();
                visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                visual.pending_subcycle = 0;
            }
            break;
        case W8_MONSTER_CYCLE_BIRTH:
            if (Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) != 0) {
                visual.pending_cycle = 1;
                visual.animation_playing = 1;
                visual.timer = GameClockMilliseconds();
                visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                visual.pending_subcycle = 0;
            }
            break;
        }
    }

    if (!visual.rep_active) {
        return;
    }

    UpdateAttachedObjects();
    cycle = Query(W8_MONSTER_QUERY_CYCLE);
    if (((GetInputState().modifiers & CTRL_DOWN) != 0) && CurrentGame().combat.has_value() &&
        (CurrentGame().combat->round_active || CurrentGame().party_movement_mode) &&
        (cycle == W8_MONSTER_CYCLE_IDLE || cycle == W8_MONSTER_CYCLE_SPICE) &&
        (visual.pending_cycle == -1 || visual.pending_cycle == 1 || visual.pending_cycle == 2) &&
        !CurrentGame().combat->resolvingTurnOf(MonsterActor{location_id})) {
        visual.timer = GameClockMilliseconds();
    }

    if (monster_info != 0 && monster_info->uiCondition[W8_CONDITION_SLOWED] != 0) {
        TickAnimation(Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_WALK
                          ? nav.movement.movement_speed * 0.5f
                          : 0.5f);
    } else {
        TickAnimation(Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_WALK
                          ? nav.movement.movement_speed
                          : 1.0f);
    }
    InitializeAnimatedTexture();

    if (sound != 0) {
        sound->position = nav.GetPosition();
        sound->Update(cameraLocation(*owner_world), owner_world->party_camera.m_yaw);
    }
}

bool Monster::GetProjectilePosition(glm::vec3* position)
{
    signed char cycle;

    if (position == 0) {
        return false;
    }
    assert(!(IsCycleSupported(0x11) && IsCycleSupported(0x0d)));
    if (IsCycleSupported(0x0d)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_THROW;
    } else if (IsCycleSupported(0x11)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_SHOOT;
    } else if (IsCycleSupported(7)) {
        cycle = W8_MONSTER_CYCLE_ATTACK_RANGED;
    } else {
        return false;
    }

    return GetCycleMappedPosition(cycle, 5, position);
}

/* Cycle 25 mapping six is the spell launch vertex; the result reports whether
   a launch position was found. */
bool Monster::GetSpellPosition(glm::vec3* position)
{
    if (position == 0) {
        return false;
    }
    return GetCycleMappedPosition(0x19, 6, position);
}

bool Monster::GetCycleMappedPosition(signed char cycle, int mapped_index, glm::vec3* position)
{
    MeshInstance* current_model = visual.GetCurrentModelInstance();
    Animation* animation;
    int subcycle;
    int dispatch_value;

    if (cycle == W8_MONSTER_CYCLE_NONE) {
        cycle = visual.current_cycle;
        subcycle = visual.current_subcycle;
    } else {
        subcycle = 0;
    }
    auto& cycle_animations = cycles[cycle];
    if (static_cast<int>(cycle_animations.size()) <= subcycle) {
        ShutdownWithErrorBox(fmt::format("Monster {}: Missing CYCLE_{}, sub-cycle {:d}", name,
                                         g_cycle_names[cycle].name, subcycle));
    }
    animation = cycle_animations[subcycle].animation.get();

    /* 0x004C7A3A..0x004C7A50: the missile vertex (5) is sampled on
       missile_frame (0x1f4) and the spell vertex (6) on spell_frame (0x1f8). */
    if (mapped_index == 5) {
        dispatch_value = missile_frame;
    } else if (mapped_index == 6) {
        dispatch_value = spell_frame;
    } else {
        dispatch_value = 0;
    }
    if (animation == 0) {
        return false;
    }
    if (animation->path_lists != 0) {
        const auto center = GetAnimationCenter();
        if (!center) return false;
        *position = *center;
        return true;
    }

    const auto frame = AnimObjDispatch(animation, 2, dispatch_value);
    if (frame) {
        Mesh* mesh = frame.mesh.get();
        if (const auto mapped = findMappedVertex(mesh, mapped_index)) {
            mesh = mapped->mesh;
            const int vertex = mapped->index;
            const auto vertex_position = mesh->framePosition(dispatch_value, vertex);
            if (vertex_position && current_model != 0) {
                auto rotation = current_model->transform.orientation<glm::mat3>();
                glm::vec3 local = *vertex_position * scale;
                glm::vec3 rotated = rotation * local;
                glm::vec3 owner_position = nav.GetPosition();
                (*position) = glm::vec3(rotated.x + owner_position.x,
                                        rotated.y + owner_position.y + nav.movement.vertical_base,
                                        rotated.z + owner_position.z);
                return true;
            }
        }
    }
    return false;
}

bool Monster::IsPendingFinalize() const
{
    return pending_finalize;
}

bool Monster::IsWithinWorldRange()
{
    if (sector_mesh != 0) {
        return (!sector_mesh->visible) == 0;
    } else {
        double far_clip = WorldGetFarClip(owner_world);
        glm::vec3 position = nav.GetPosition();
        glm::vec3 reference = owner_world->party_navigator.GetPosition();
        glm::vec3 delta = reference - position;

        return glm::dot(delta, delta) <=
               static_cast<float>(far_clip) * static_cast<float>(far_clip);
    }
}

/* Exercise the inexpensive elevated-origin sight query from this Monster to
   the player, and answer with the trace's own result. */
bool Monster::CheckLineOfSightToPlayer()
{
    glm::vec3 monster_position;
    glm::vec3 player_position;

    monster_position = nav.movement.position;
    monster_position.y += nav.movement.height_offset;
    player_position = cameraLocation(*owner_world);
    return owner_world->compiledIndex()->hasLineOfSight(monster_position, player_position);
}

/* The sight code keeps a sight trace's three outcomes as two flags: a clear
   line sets both, a location sphere in the way sets only the secondary flag,
   and an obstacle sets neither. */
static void applySightFlags(const std::optional<SightHit>& hit, bool& primary, bool& secondary)
{
    secondary = !hit || hit->blocker == SightBlocker::Location;
    primary = !hit;
}

void Monster::GetPlayerSightFlags(bool* primary, bool* secondary)
{
    glm::vec3 monster_position = nav.movement.position;
    monster_position.y += nav.movement.height_offset;
    const auto hit =
        owner_world->compiledIndex()->traceSight(monster_position, cameraLocation(*owner_world),
                                                 {.locations = SightLocations{location_id, -1}});
    applySightFlags(hit, *primary, *secondary);
}

/* The inexpensive visibility path tests the Monster's elevated origin. The
   detailed path tests the translated animation bounds at their centre and
   corners. */
bool Monster::IsVisibleToPlayer(bool use_bounds)
{
    glm::vec3 player_position = cameraLocation(*owner_world);
    if (use_bounds) {
        const auto bounds = this->GetWorldAnimationBounds();
        return bounds && hasLineOfSightToBounds(player_position, *bounds);
    }

    glm::vec3 monster_position = nav.movement.position;
    monster_position.y += nav.movement.height_offset;
    return owner_world->compiledIndex()->hasLineOfSight(player_position, monster_position);
}

void Monster::GetPlayerToMonsterSightFlags(bool* primary, bool* secondary, const glm::vec3* source)
{
    glm::vec3 monster_position = nav.movement.position;
    monster_position.y += nav.movement.height_offset;
    const glm::vec3 player_position = source == 0 ? cameraLocation(*owner_world) : *source;
    const auto hit = owner_world->compiledIndex()->traceSight(
        player_position, monster_position, {.locations = SightLocations{-1, location_id}});
    applySightFlags(hit, *primary, *secondary);
}

bool Monster::HasLineOfSightToMonster(Monster* monster)
{
    glm::vec3 from;
    glm::vec3 to;

    from = nav.movement.position;
    from.y += nav.movement.height_offset;
    to = monster->nav.movement.position;
    to.y += monster->nav.movement.height_offset;
    return owner_world->compiledIndex()->hasLineOfSight(from, to);
}

void Monster::GetMonsterSightFlags(Monster* monster, bool* primary, bool* secondary)
{
    glm::vec3 from = nav.movement.position;
    from.y += nav.movement.height_offset;
    glm::vec3 to = monster->nav.movement.position;
    to.y += monster->nav.movement.height_offset;
    const auto hit = owner_world->compiledIndex()->traceSight(
        from, to, {.locations = SightLocations{location_id, monster->location_id}});
    applySightFlags(hit, *primary, *secondary);
}

bool Monster::HasLineOfSightFromPoint(glm::vec3 point)
{
    glm::vec3 monster_position;

    monster_position = nav.movement.position;
    monster_position.y += nav.movement.height_offset;
    return !owner_world->compiledIndex()->traceSight(point, monster_position);
}

bool Monster::IsFacingMonster(Monster* monster)
{
    float bearing;
    float facing;
    glm::vec3 from;
    glm::vec3 to;

    assert(monster != 0);
    to = monster->nav.GetPosition();
    from = nav.GetPosition();
    bearing = NormalizeAngle(GetHeadingAngle(&from, &to));
    facing = NormalizeAngle(nav.GetYaw());
    return fabs(bearing - facing) <= g_monster_facing_tolerance;
}

bool Monster::IsFacingPlayer()
{
    float bearing;
    float facing;
    glm::vec3 from;
    glm::vec3 to;

    to = owner_world->party_navigator.GetPosition();
    from = nav.GetPosition();
    bearing = NormalizeAngle(GetHeadingAngle(&from, &to));
    facing = NormalizeAngle(nav.GetYaw());
    return fabs(bearing - facing) <= g_monster_facing_tolerance;
}

/* Attach or remove the party slot's target-marker triangle on a monster's
   representation: on set it hangs the slot's colored Tri*.tga object on
   attachment index `party_slot`, on clear it detaches and deletes it. The
   rep's object count tracks the live markers. */
void SetMonsterPartySlotMarker(int party_slot, int location_id, signed char on)
{
    Monster* rep;
    W8MonsterInfo* info;
    WorldItemEntity* item;
    std::string path;

    info = FindMonster(location_id);
    rep = info->entity.find();
    if (on == 0) {
        item = rep->objects[party_slot].get();
        if (item != 0) {
            item->DetachMesh();
            rep->objects[party_slot].reset();
            --rep->icon_count;
        }
    } else {
        if (rep->objects[party_slot] == 0) {
            path = fmt::format(
                "Data\\Monsters\\Bitmaps\\{}",
                g_party_target_marker_bitmaps[CurrentGame().party.orderIndexOf(party_slot)]);
            rep->objects[party_slot] =
                CreateMonsterIconItem(*info->entity->owner_world, path.c_str());
            ++rep->icon_count;
        }
    }
    info->entity->UpdateAttachedObjects();
}

/* Start making the representation visible.  Reversing an active fade-out
   preserves its current scale by seeding the opposite timer at that progress;
   an idle monster starts from zero instead. */
void Monster::BeginFadeIn(float duration)
{
    if (fade_state <= W8_MONSTER_FADE_IDLE) {
        fade_timer.SetDuration(duration);
        fade_timer.Restart();
        if (fade_state < W8_MONSTER_FADE_IDLE) {
            Monster* rep = this;
            fade_timer.SetProgress(rep->visual.instance_scale);
        } else {
            Monster* rep = this;
            rep->visual.instance_scale = 0.0f;
            rep->visual.apply_instance_scale = true;
        }
        fade_state = W8_MONSTER_FADE_IN;
        runtime_flags &= ~W8_MONSTER_FADED_OUT;
    }
}

void Monster::BeginDelayedRemoval()
{
    runtime_flags |= W8_MONSTER_REMOVE_AFTER_FADE;
    fade_state = W8_MONSTER_FADE_DELAYED_REMOVAL;
    fade_timer.SetDuration(5.0f);
    fade_timer.Restart();
}

/* Begin the three-second disappearance transition, remove the live monster
   from the manager without destroying it, and retain the requested terminal
   state for the transition's completion. */
void Monster::BeginFadeOutAndRemove(W8MonsterRemovalState state)
{
    runtime_flags |= W8_MONSTER_REMOVE_AFTER_FADE;
    if (fade_state >= W8_MONSTER_FADE_IDLE) {
        fade_timer.SetDuration(3.0f);
        fade_timer.Restart();
        Monster* rep = this;
        if (fade_state > W8_MONSTER_FADE_IDLE) {
            fade_timer.SetProgress(1.0f - rep->visual.instance_scale);
        } else {
            rep->visual.instance_scale = 1.0f;
            rep->visual.apply_instance_scale = true;
        }
        fade_state = W8_MONSTER_FADE_OUT;
    }
    RemoveMonster(owner_world->population->findMonster(location_id), W8MonsterRemoval::Deactivate);
    removal_state = state;
}

void Monster::BeginFadeOut(float duration)
{
    if (fade_state < W8_MONSTER_FADE_IDLE) {
        return;
    }
    fade_timer.SetDuration(duration);
    fade_timer.Restart();
    if (fade_state > W8_MONSTER_FADE_IDLE) {
        Monster* rep = this;
        fade_timer.SetProgress(1.0f - rep->visual.instance_scale);
        fade_state = W8_MONSTER_FADE_OUT;
        return;
    }
    Monster* rep = this;
    rep->visual.instance_scale = 1.0f;
    rep->visual.apply_instance_scale = true;
    fade_state = W8_MONSTER_FADE_OUT;
}

void Monster::StartTalking(bool animate_mouth)
{
    talking = true;
    this->animate_mouth = animate_mouth;
    mouth_frame_clock = GameClockMilliseconds();
    mouth_open = false;
    talk_start = GameClockMilliseconds();
    talk_duration = CurrentGame().rng.below(2000) + 2000;
    visual.pending_cycle = 0x18;
    visual.pending_behaviour = W8_ANIMATION_PLAY_ONCE;
}

void Monster::StopTalking()
{
    MeshInstance* model;
    Texture* mouth;

    talking = false;
    model = visual.GetCurrentModelInstance();
    if (model != 0) {
        mouth = static_cast<MeshInstance*>(model)->FindMouthTexture();
        if (mouth != 0) {
            mouth->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
            mouth->SetFrame(0);
        }
    }
    if (visual.current_cycle != 0x15) {
        visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
        visual.pending_cycle = 1;
    }
}

void Monster::SetCycleCallback(int cycle, CycleCallback callback)
{
    cycle_callback = callback;
    callback_cycle = cycle;
}

bool Monster::GetPatrolPoint(glm::vec3* point)
{
    if (!orders_finished || patrol_index < 0) {
        return false;
    }
    if (vector.empty()) {
        if (formation == glm::vec3(0)) {
            glm::vec3 position = nav.GetPosition();
            formation = position;
        }
        vector.push_back(formation);
    }

    if (order_mode == W8_MONSTER_ORDER_GUARD) {
        *point = vector.front();
    } else if (order_mode > W8_MONSTER_ORDER_PATROL &&
               order_mode < W8_MONSTER_ORDER_FACE_DIRECTION) {
        *point = vector[patrol_index];
    } else {
        return false;
    }

    return true;
}

std::optional<BoundingBox> Monster::GetWorldAnimationBounds()
{
    auto bounds = GetAnimationBounds();
    if (bounds) {
        const auto position = nav.GetPosition();
        bounds->minimum += position;
        bounds->maximum += position;
    }
    return bounds;
}

void Monster::TrackSound(wiz8::sound::Playback playback)
{
    if (sounds.size() > 6) {
        for (const auto& sound : sounds) wiz8::sound::stop(sound);
        sounds.clear();
    }
    sounds.push_back(std::move(playback));
    std::erase_if(sounds, [](const auto& sound) { return !wiz8::sound::is_playing(sound); });
}

/* Mark the closest live member of each loaded group inside the selection
   range. Members that do not improve the current candidate are unmarked. */
void UpdateNearestMonsterGroupMembers()
{
    glm::vec3 player_position;
    unsigned int group_index;

    player_position = cameraLocation();
    for (group_index = 0; group_index < CurrentLevelPopulation().monster_groups.size();
         ++group_index) {
        W8MonsterGroup* group = CurrentLevelPopulation().monster_groups[group_index].get();

        if (group != 0) {
            double nearest_distance = 1e11;
            W8MonsterInfo* nearest = 0;
            unsigned int member_index;

            for (member_index = 0; member_index < group->MemberIds().size(); ++member_index) {
                W8MonsterInfo* member = FindMonster(group->MemberLocationId(member_index));

                if (member != 0 && member->entity) {
                    glm::vec3 position = member->entity->nav.GetPosition();
                    float distance = glm::length((position - player_position));

                    if (distance < g_monster_group_nearest_range && distance < nearest_distance) {
                        nearest_distance = distance;
                        nearest = member;
                    } else {
                        member->entity->nearest_to_party = false;
                    }
                }
            }
            if (nearest != 0) {
                nearest->entity->nearest_to_party = true;
            }
        }
    }
}

float Monster::GetDistanceToPlayer()
{
    glm::vec3 position = nav.GetPosition();
    return GetPointDistanceToPlayer(position);
}

float Monster::GetPointDistanceToPlayer(glm::vec3 point)
{
    glm::vec3 player_position;
    float distance;

    player_position = cameraLocation(*owner_world);
    player_position.y -= g_default_world_height;
    distance = glm::length((point - player_position)) - nav.movement.alternate_radius -
               owner_world->party_navigator.movement.alternate_radius;
    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

float Monster::GetDistanceToMonster(Monster* monster)
{
    glm::vec3 position = nav.GetPosition();
    glm::vec3 other_position = monster->nav.GetPosition();
    float distance = glm::length((position - other_position)) - nav.movement.alternate_radius -
                     monster->nav.movement.alternate_radius;

    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

float Monster::GetPointDistanceToMonster(Monster* monster, glm::vec3 point)
{
    glm::vec3 position = monster->nav.GetPosition();
    float distance = glm::length((point - position)) - nav.movement.alternate_radius -
                     monster->nav.movement.alternate_radius;

    if (distance < 0.0f) {
        distance = 0.0f;
    }
    return distance;
}

/* Decide whether a requested cycle can replace the current one. Monster adds
   combat, motionless, and death rules to CyclePlayer's primary-table operation;
   Navigator's distinct slot 3 remains inherited in the secondary table. */
bool Monster::CanEnterCycle(signed char cycle)
{
    W8MonsterInfo* monster_info = owner_world->population->findMonster(location_id);

    if (CurrentGame().combat.has_value() && partyCamera().m_transition_active) {
        return false;
    }
    if (visual.animation_playing == 0) {
        if (cycle != W8_MONSTER_CYCLE_GET_HIT && cycle != W8_MONSTER_CYCLE_DIE &&
            cycle != W8_MONSTER_CYCLE_BIRTH && monster_info->fMotionless) {
            return false;
        }
    } else {
        if (!IsCycleInterruptable(static_cast<signed char>(Query(W8_MONSTER_QUERY_CYCLE))) &&
            Query(W8_MONSTER_QUERY_CYCLE_COMPLETE) == 0) {
            return false;
        }
        if (cycle == W8_MONSTER_CYCLE_DIE && monster_info->monster_species == 0x199 &&
            Query(W8_MONSTER_QUERY_AT_PLAYBACK_END) == 0) {
            return false;
        }
    }
    return true;
}

/* Report whether a cycle may be interrupted. The original diagnostic names
   this operation `Monster::CycleInterruptable`; its spelling is retained here
   because it is the only source-level name available. */
bool Monster::IsCycleInterruptable(signed char cycle)
{
    if (visual.animation_playing == 0) {
        return true;
    }

    switch (cycle) {
    case W8_MONSTER_CYCLE_NONE:
    case W8_MONSTER_CYCLE_IDLE:
    case W8_MONSTER_CYCLE_SPICE:
    case W8_MONSTER_CYCLE_TRANSITION:
    case W8_MONSTER_CYCLE_WALK:
    case W8_MONSTER_CYCLE_TURN:
        return true;
    }
    return false;
}

/* Apply the two script-specific side effects selected before an ungrouped
   monster is removed: state two returns it to Balbrak's home marker, while
   state three clears the ScregActive trigger variable. */
void Monster::ApplyRemovalStateEffects()
{
    glm::vec3 position;

    switch (removal_state) {
    case W8_MONSTER_REMOVAL_NONE:
        break;
    case W8_MONSTER_REMOVAL_RETURN_BALBRAK_HOME:
        if (FindEntityByName("NP_Balbrakhome", &position, 0, 0)) {
            SetPosition(&position);
        }
        break;
    case W8_MONSTER_REMOVAL_CLEAR_SCREG_ACTIVE:
        SetTriggerVariableByName("ScregActive", 0);
        break;
    }
}

unsigned char Monster::GetNumSubsPerCycle(signed char bCycle)
{
    assert(bCycle < W8_MONSTER_CYCLE_COUNT);
    if (bCycle == -1) {
        bCycle = visual.current_cycle;
    }
    return static_cast<unsigned char>(cycles[bCycle].size());
}

/* The selected subcycle's AniMesh for one animation cycle. */
AnimationMesh* Monster::GetEmitterAniMesh(signed char cycle)
{
    Animation* animation = cycles[cycle][visual.current_subcycle].animation.get();

    if (animation == 0) {
        return 0;
    }
    return AnimObjEntry(animation, visual.m_bLOD, 0);
}

/* Synchronize the live world representation with the Navigator state, update
   transient mouth/scale effects, and attach or hide the cycle's light graph.
   This is Monster's primary slot four; the world is the ordinary stack
   argument also consumed by the inherited GrCycle implementation. */
void Monster::UpdateRepresentation()
{
    glm::vec3 position;
    glm::mat3 rotation;
    MeshInstance* model;
    Texture* mouth;
    std::vector<std::unique_ptr<Light>>* lights;
    int index;
    int count;

    position = nav.movement.position;
    position.y += nav.movement.vertical_offset;
    visual.SetLocation(&position);

    rotation = glm::mat3(1);
    {
        float angle = NormalizeAngle(nav.GetYaw() + g_monster_rotation_offset);
        if (angle != 0.0f) {
            rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
        }
    }
    {
        float angle = nav.GetPitch();
        if (angle != 0.0f) {
            {
                if (static_cast<double>(angle) != 0.0) {
                    rotation = rotation * glm::mat3(glm::mat3_cast(glm::angleAxis(
                                              static_cast<double>(angle), glm::dvec3(1, 0, 0))));
                }
            }
        }
    }
    if (nav.movement.roll != 0.0f) {
        rotation *= glm::mat3_cast(
            glm::angleAxis(float(static_cast<double>(nav.movement.roll)), glm::vec3(0, 0, 1)));
    }
    visual.SetRotation(&rotation);

    if (visual.model_scale) visual.model_scale = CycleModelScale();

    if (talking && animate_mouth) {
        if (mouth_open) {
            model = visual.GetCurrentModelInstance();
            if (model != 0 &&
                (mouth = static_cast<MeshInstance*>(model)->FindMouthTexture()) != 0) {
                mouth->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                mouth->SetFrame(0);
            }
        } else if (GameClockMilliseconds() - mouth_frame_clock > 120) {
            unsigned short frame;
            mouth_frame_clock = GameClockMilliseconds();
            do {
                frame = static_cast<unsigned short>(CurrentGame().rng.below(6));
                if (frame > 3) {
                    frame = 0;
                }
            } while (frame == static_cast<unsigned short>(mouth_frame));
            mouth_frame = frame;
            model = visual.GetCurrentModelInstance();
            if (model != 0 &&
                (mouth = static_cast<MeshInstance*>(model)->FindMouthTexture()) != 0) {
                mouth->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                mouth->SetFrame(frame);
            }
        }
    }

    if (nav.position_dirty || nav.movement.position_adjusted) {
        owner_world->compiledIndex()->UpdateMonsterLocation(
            static_cast<unsigned short>(location_id), position);
    }

    /* Only a monster whose current world sector mesh is enabled (or that has
       none yet) is re-attached; this is the octree sector cache at 0x308, not
       the navigator's scene node. */
    if ((sector_mesh == 0 || (!sector_mesh->visible) == 0) && IsRenderable(false)) {
        visual.UpdateRepresentation(nav);
        model = visual.GetCurrentModelInstance();
        if (model != 0) {
            static_cast<MeshInstance*>(model)->frame_interpolation =
                g_settings.smooth_monster_animations != 0 ? visual.frame_fraction : 0.0f;
            SetModelInstanceExclusionMask(model, 4);
        }
        if (monster_light != 0) {
            monster_light->Update(&position,
                                  std::chrono::duration<float>(CurrentGame().visual_time).count());
        }
        if (!visual.enabled) {
            visual.enabled = true;
            SetShakeEventVisibility(visual.current_cycle);
            lights = &cycles[visual.current_cycle][visual.current_subcycle].lights.lights;
            if (lights != 0 && (count = static_cast<int>(lights->size())) != 0) {
                for (index = 0; index < count; ++index) {
                    ((*lights)[index])->visible = true;
                }
            }
            if (monster_light != 0) {
                monster_light->SetVisible(true);
            }
        }
    } else if (visual.enabled) {
        visual.enabled = false;
        SetShakeEventVisibility(visual.current_cycle);
        lights = &cycles[visual.current_cycle][visual.current_subcycle].lights.lights;
        if (lights != 0 && (count = static_cast<int>(lights->size())) != 0) {
            for (index = 0; index < count; ++index) {
                ((*lights)[index])->visible = false;
            }
        }
        if (monster_light != 0) {
            monster_light->SetVisible(false);
        }
    }
}

/* Enable only the shake particles belonging to the requested cycle and the
   currently selected subcycle. A ranged particle is left to the frame-driven
   update path; this method only toggles particles without a distinct range. */
void Monster::SetShakeEventVisibility(signed char cycle)
{
    int index;
    int count;

    if ((count = static_cast<int>(visual.particles.size())) == 0) {
        return;
    }

    for (index = 0; index < count; ++index) {
        ParticleAttachment* event = &visual.particles[index];

        if (event->cycle == cycle && event->subcycle == visual.current_subcycle && visual.enabled) {
            Animation* animation =
                cycles[visual.current_cycle][visual.current_subcycle].animation.get();

            if (animation == 0 || animation->start_frame == 0 ||
                animation->end_frame < animation->start_frame) {
                ParticleSystem* particle = event->m_pstParticles.get();
                if (particle->start_frame != -1 && particle->end_frame != -1 &&
                    particle->start_frame != particle->end_frame) {
                    continue;
                }
                particle->SetActive(true);
            }
        } else {
            event->m_pstParticles->SetActive(false);
        }
    }
}

/* The Monster vtable's slot-three method selects the active subcycle's
   AnimObj (falling back to entry zero) and submits the Monster's current
   animation index.  The assertion's `pao` spelling establishes the pointee's
   AnimObj identity without supplying a name for this Monster method. */
unsigned int Monster::ApplyEmitterSetting(signed char cycle)
{
    Animation* animation = cycles[cycle][visual.current_subcycle].animation.get();
    assert(animation != 0);
    return AnimObjValue(animation, visual.m_bLOD);
}

signed char Monster::GetNumSubCycles()
{
    Monster* representation = this;
    Animation* animation =
        representation
            ->cycles[representation->visual.current_cycle][representation->visual.current_subcycle]
            .animation.get();
    return static_cast<signed char>(AnimObjValue(animation, representation->visual.m_bLOD));
}

/* Monster stores its animation object immediately after the shared
   0x1d8-byte GrCycle base. */
bool Monster::IsCycleSupported(signed char cycle)
{
    assert(cycle < W8_MONSTER_CYCLE_COUNT);
    return static_cast<int>(cycles[cycle].size()) != 0;
}

/* Select a concrete animation subcycle and rebuild the renderer-facing light
   and model state for it.  The one stack argument and RET 4 establish the
   primary-vtable slot as an ordinary signed-byte cycle setter. */
void Monster::SetCycle(signed char cycle)
{
    std::vector<std::unique_ptr<Light>>* lights;
    Animation* animation;
    signed char subcycle;
    int count;
    int index;

    assert(cycle >= W8_MONSTER_CYCLE_BIRTH && cycle < W8_MONSTER_CYCLE_COUNT);

    auto& cycle_animations = cycles[cycle];
    count = static_cast<int>(cycle_animations.size());
    if (count == 0) {
        if (static_cast<int>(cycles[1].size()) < 1) {
            throw std::runtime_error(fmt::format("monster {} has no idle cycle", name));
        }

        CopyCycle(cycle, 1);
        count = static_cast<int>(cycle_animations.size());
        if (count == 0) {
            return;
        }
    }

    if (count == 1) {
        subcycle = 0;
    } else if (visual.forced_subcycle == -1 || count <= visual.forced_subcycle) {
        if ((runtime_flags & W8_MONSTER_KEEP_SUBCYCLE) == 0) {
            subcycle = static_cast<signed char>(GameClockMilliseconds() % count);
        } else {
            runtime_flags &= ~W8_MONSTER_KEEP_SUBCYCLE;
            subcycle = visual.current_subcycle;
        }
    } else {
        runtime_flags &= ~W8_MONSTER_KEEP_SUBCYCLE;
        subcycle = visual.forced_subcycle;
        visual.forced_subcycle = -1;
    }

    if (cycle_callback != 0 && callback_cycle == visual.current_cycle) {
        cycle_callback(this);
        cycle_callback = 0;
    }

    if (visual.current_cycle != 0) {
        lights = &cycles[visual.current_cycle][visual.current_subcycle].lights.lights;
        if (lights != 0) {
            count = static_cast<int>(lights->size());
            for (index = 0; index < count; ++index) {
                Light* light = (*lights)[index].get();

                light->setScene(0);
            }
        }
    }

    visual.current_cycle = cycle;
    visual.current_subcycle = subcycle;
    animation = cycle_animations[subcycle].animation.get();
    visual.active_animation = animation;
    assert(animation != 0);
    visual.frame_method = animation->frame_method;

    {
        const glm::vec3 listener = cameraLocation(*owner_world);
        visual.SelectLOD(&listener);
    }

    GetAnimationRadius(&visual.animation_radius);
    if ((runtime_flags & W8_MONSTER_KEEP_FRAME_DIRECTION) != 0) {
        runtime_flags &= ~W8_MONSTER_KEEP_FRAME_DIRECTION;
    } else {
        visual.frame_direction = W8_ANIMATION_FORWARD;
    }

    lights = &cycles[cycle][subcycle].lights.lights;
    visual.SetLights(lights);
    if (lights != 0) {
        count = static_cast<int>(lights->size());
        for (index = 0; index < count; ++index) {
            Light* light = (*lights)[index].get();

            light->setScene((owner_world->scene.get()));
            light->Reset();
        }
    }

    runtime_flags &= ~W8_MONSTER_TEXTURE_CHECKED;
    visual.first_frame = 0;
    visual.last_frame = GetNumSubCycles() - 1;
    SetShakeEventVisibility(cycle);

    if (lights != 0) {
        count = static_cast<int>(lights->size());
        for (index = 0; index < count; ++index) {
            if (visual.enabled) {
                ((*lights)[index])->visible = true;
            } else {
                ((*lights)[index])->visible = false;
            }
        }
    }
    if (monster_light != 0) {
        monster_light->SetVisible(visual.enabled);
    }

    visual.light_scale = current_scale;

    if (cycle == W8_MONSTER_CYCLE_DIE) {
        glm::vec4 empty;
        empty = glm::vec4(0);
        visual.highlight_colour = empty;
        // Gib behavior belongs to the source asset, not a generated renderer label.
        const auto* death_mesh = GetEmitterAniMesh(visual.current_cycle);
        if (death_mesh && wiz8::path_to_utf8(death_mesh->source_file.filename()).contains("gib")) {
            nav.SetAngles(static_cast<float>(g_monster_death_rotation_pi * 0.0055555557f *
                                             CurrentGame().rng.below(0x168)));
        }
        if (monster_light != 0) {
            monster_light->StartFadeOut(
                std::chrono::duration<float>(CurrentGame().visual_time).count());
        }
    }
}

signed char Monster::GetTotalAnimationCount()
{
    signed char total = 0;
    int cycle;

    for (cycle = W8_MONSTER_CYCLE_BIRTH; cycle < W8_MONSTER_CYCLE_COUNT; ++cycle) {
        total += static_cast<signed char>(cycles[cycle].size());
    }
    return total;
}

float Monster::GetCurrentAnimationScale()
{
    Monster* representation = this;

    return representation
        ->cycles[representation->visual.current_cycle][representation->visual.current_subcycle]
        .playback_scale;
}

std::optional<BoundingBox> Monster::GetAnimationBounds()
{
    auto bounds = visual.GetAnimationBounds();
    if (bounds) {
        bounds->minimum *= scale;
        bounds->maximum *= scale;
    }
    return bounds;
}

unsigned char Monster::GetAnimationRadius(float* arg_radius)
{
    unsigned char result = visual.GetAnimationRadius(arg_radius);

    *arg_radius *= scale;
    return result;
}

static const float g_monster_bounds_vertical_factor = 0.66f;

std::optional<glm::vec3> Monster::GetAnimationCenter()
{
    return GetAnimationBounds().transform([this](const auto& bounds) {
        auto center = nav.GetPosition();
        center.y += (bounds.maximum.y - bounds.minimum.y) * g_monster_bounds_vertical_factor;
        return center;
    });
}

/* Keep equipped items, spell icons, and temporary poster model instances in
   the camera-facing attachment layout selected by the representation. */
void Monster::UpdateAttachedObjects(bool hovered)
{
    Monster* representation = this;
    int attachment_layout = representation->icon_count;
    const auto linked_count = representation->spell_icons.size();
    int poster_count = static_cast<int>(representation->linked_runtime_objects.size());
    glm::mat3 camera_rotation;
    glm::vec3 base_position;
    glm::vec3 party_position;
    float distance_scale;
    unsigned int elapsed;
    int index;

    if (attachment_layout == 0 && linked_count == 0 && poster_count == 0) {
        return;
    }

    elapsed = GameClockMilliseconds() - representation->visual.timer;
    camera_rotation = cameraRotation(*owner_world);
    base_position = nav.movement.position;
    base_position.y += nav.movement.vertical_base + nav.movement.vertical_amplitude;

    party_position = cameraLocation(*owner_world);
    party_position -= base_position;
    distance_scale = glm::length(party_position) * g_monster_attachment_distance_scale;
    if (!hovered && distance_scale > 1.0f) {
        distance_scale = 1.0f;
    }

    if (attachment_layout != 0) {
        /* 0x004C40C4 indexes the layout row with a count of the markers
           already placed (0x28(%esp), bumped at 0x004C4243 only for a present
           slot), so the live markers pack into the first row entries
           whatever party slots they belong to. */
        int placed = 0;

        for (index = 0; index < 8; ++index) {
            WorldItemEntity* item = representation->objects[index].get();

            if (item != 0) {
                const W8AttachmentOffset& raw =
                    g_monster_attachment_offsets[attachment_layout - 1][placed];
                glm::vec3 source(raw.x, raw.y, raw.z);
                glm::vec3 offset;
                glm::vec3 location;
                MeshInstance* mesh;
                float mesh_scale;
                glm::dvec3 widened_scale;

                offset = source * distance_scale;
                glm::vec3 rotated = camera_rotation * offset;
                location = base_position + rotated;
                location.y += representation->standing_height +
                              distance_scale * g_monster_attachment_vertical_scale;

                item->SetLocation(&location);
                mesh = item->GetMesh();
                mesh_scale = distance_scale * g_monster_attachment_scales[attachment_layout - 1];
                widened_scale = glm::dvec3(mesh_scale);
                mesh->transform.setScale(widened_scale);
                if ((runtime_flags & W8_MONSTER_FADED_OUT) == 0) {
                    mesh->visible = true;
                } else {
                    mesh->visible = false;
                }
                item->ApplyRepTransform();
                ++placed;
            }
        }
    }

    index = 0;
    {
        int group = 0;

        while (index < static_cast<int>(linked_count)) {
            int chunk_count = static_cast<int>(linked_count) - index;
            int chunk_index;
            float group_height;

            if (chunk_count > 4) {
                chunk_count = 4;
            }
            group_height = static_cast<float>(group * g_monster_attachment_group_spacing);

            for (chunk_index = 0; chunk_index < chunk_count; ++chunk_index, ++index) {
                const auto* entry = &representation->spell_icons[index];
                WorldItemEntity* item = entry->item.get();
                const W8AttachmentOffset& raw =
                    g_monster_attachment_offsets[chunk_count - 1][chunk_index];
                glm::vec3 source(raw.x, raw.y, raw.z);
                glm::vec3 offset;
                glm::vec3 location;
                MeshInstance* mesh;
                float mesh_scale;
                glm::dvec3 widened;

                offset = source * distance_scale;
                offset.y += group_height * distance_scale;
                glm::vec3 rotated = camera_rotation * offset;
                location = base_position + rotated;
                location.y += representation->standing_height +
                              distance_scale * g_monster_linked_vertical_scale;

                item->SetLocation(&location);
                mesh = item->GetMesh();
                mesh_scale = distance_scale * g_monster_attachment_scales[chunk_count - 1];
                widened = glm::dvec3(mesh_scale);
                mesh->transform.setScale(widened);
                widened = glm::dvec3(location);
                mesh->transform.setLocation(widened);
                if ((runtime_flags & W8_MONSTER_FADED_OUT) == 0) {
                    mesh->visible = true;
                } else {
                    mesh->visible = false;
                }
            }
            ++group;
        }
    }

    if (poster_count != 0) {
        glm::vec3 mapped_position;
        float vertical_offset = elapsed * g_monster_poster_vertical_rate;
        int poster_index = 0;

        GetMappedPosition(&mapped_position);
        while (poster_index < poster_count) {
            MeshInstance* poster = representation->linked_runtime_objects[poster_index].get();
            glm::dvec3 location = poster->transform.getLocation();
            glm::vec3 poster_position(static_cast<float>(location.x),
                                      static_cast<float>(location.y) + vertical_offset,
                                      static_cast<float>(location.z));

            if (glm::length((poster_position - mapped_position)) <=
                static_cast<float>(g_monster_poster_max_distance)) {
                location = glm::dvec3(poster_position);
                poster->transform.setLocation(location);
                ++poster_index;
            } else {
                representation->linked_runtime_objects.erase(
                    representation->linked_runtime_objects.begin() + poster_index);
                --poster_count;
            }
        }
    }
}

/* Query the current animation state. The selector is an internal ten-entry
   interface used by MonsterManager and the animation driver; selector eight is
   intentionally unsupported and returns -1 with out-of-range selectors. */
int Monster::Query(W8MonsterQueryKind query)
{
    int result = -1;
    unsigned int animation_value;

    switch (query) {
    case W8_MONSTER_QUERY_FRAME_COUNT:
        result = ApplyEmitterSetting(visual.current_cycle);
        break;
    case W8_MONSTER_QUERY_ANIMATION_COUNT:
        result = GetTotalAnimationCount();
        break;
    case W8_MONSTER_QUERY_AT_PLAYBACK_END:
        if (visual.frame_direction != W8_ANIMATION_FORWARD &&
            visual.frame_direction != W8_ANIMATION_FORWARD_COMPLETE) {
            result = visual.subcycle == 0;
            break;
        }
        animation_value = ApplyEmitterSetting(visual.current_cycle);
        result = visual.subcycle == animation_value - 1;
        break;
    case W8_MONSTER_QUERY_AT_PLAYBACK_START:
        if (visual.frame_direction == W8_ANIMATION_FORWARD ||
            visual.frame_direction == W8_ANIMATION_FORWARD_COMPLETE) {
            result = visual.subcycle == 0;
            break;
        }
        animation_value = ApplyEmitterSetting(visual.current_cycle);
        result = visual.subcycle == animation_value - 1;
        break;
    case W8_MONSTER_QUERY_FRAME:
        result = visual.subcycle;
        break;
    case W8_MONSTER_QUERY_HAS_FRAMES:
        result = ApplyEmitterSetting(visual.current_cycle) != static_cast<unsigned int>(-1);
        break;
    case W8_MONSTER_QUERY_CYCLE:
        result = visual.current_cycle;
        break;
    case W8_MONSTER_QUERY_SUBCYCLE:
        result = visual.current_subcycle;
        break;
    case W8_MONSTER_QUERY_CYCLE_COMPLETE:
        result = 0;
        if (visual.animation_behaviour == W8_ANIMATION_NEVER_STOP) {
            if (visual.animation_playing == 0) {
                result = 1;
            }
            break;
        }

        animation_value = ApplyEmitterSetting(visual.current_cycle);
        if ((visual.frame_direction == W8_ANIMATION_FORWARD &&
             visual.subcycle == animation_value - 1) ||
            (visual.frame_direction == W8_ANIMATION_REVERSE && visual.subcycle == 0) ||
            visual.frame_direction == W8_ANIMATION_REVERSE_COMPLETE ||
            visual.frame_direction == W8_ANIMATION_FORWARD_COMPLETE) {
            result = 1;
        }
        break;
    default:
        break;
    }
    return result;
}

void Monster::dispatchAnimationFrame(unsigned char previous_frame)
{
    if ((visual.current_cycle == 7 || visual.current_cycle == 13 || visual.current_cycle == 17) &&
        ((missile_frame > 0 && previous_frame < missile_frame &&
          missile_frame <= visual.subcycle) ||
         (missile_frame == 0 && visual.subcycle == 1))) {
        if (owner_world && owner_world->simulation) owner_world->simulation->missileReleased(*this);
    }
    HandleAnimationFrame(previous_frame);
    if (!visual.particles.empty()) {
        UpdateShakeEvents(previous_frame);
    }
}

static int g_spell_effect_frame = 1;
static int g_spell_index;

/* Cycle 25 launches either the queued spell visual or, by reporting it to the
   simulation, the monster's pending spell action when its animation crosses the
   configured frame. */
void Monster::HandleAnimationFrame(unsigned char previous_frame)
{
    if (visual.current_cycle == 25 &&
        ((spell_frame > 0 && previous_frame < spell_frame && spell_frame <= visual.subcycle) ||
         (spell_frame == 0 && visual.subcycle == 1))) {
        if (spell_effect_armed) {
            spell_effect_armed = false;
            CreateAttachedSpellEffect(Data().spells[g_spell_index].resource_name,
                                      g_spell_effect_frame, this, 0, 0);
            return;
        }

        if (owner_world && owner_world->simulation) owner_world->simulation->spellReleased(*this);
    }
}

void W8MonsterShakeCallback::RestoreAnimation()
{
    Monster* representation;

    assert(m_pMonster != 0);
    assert(m_pParticles != 0);

    m_pParticles->SetActive(false);
    representation = m_pMonster;
    assert(saved_behaviour >= 1 && saved_behaviour <= 3);
    representation->visual.pending_behaviour = saved_behaviour;
    representation->visual.SetFrameMethod(saved_frame_method);
    representation->visual.frame_direction = W8_ANIMATION_FORWARD;
    representation->visual.first_frame = 0;
    representation->visual.last_frame = m_pMonster->GetNumSubCycles() - 1;
}

/* Drive the particles attached to the active cycle/subcycle. A particle with
   no distinct frame range fires when the animation crosses its own start
   frame; a ranged particle is switched on and off at its explicit bounds. */
void Monster::UpdateShakeEvents(unsigned char previous_frame)
{
    Animation* animation;
    ParticleAttachment* event;
    ParticleSystem* particle;
    int count;
    int index;
    bool animation_has_range;

    count = static_cast<int>(visual.particles.size());
    animation = cycles[visual.current_cycle][visual.current_subcycle].animation.get();
    animation_has_range = animation != 0 && animation->start_frame != 0 &&
                          animation->end_frame >= animation->start_frame;

    for (index = 0; index < count; ++index) {
        event = &visual.particles[index];
        if (event->cycle != visual.current_cycle || event->subcycle != visual.current_subcycle) {
            continue;
        }

        particle = event->m_pstParticles.get();
        if (animation_has_range &&
            (particle->start_frame == -1 || particle->end_frame == -1 ||
             particle->start_frame == particle->end_frame) &&
            previous_frame < animation->start_frame && animation->start_frame <= visual.subcycle) {
            if (visual.enabled) {
                particle->SetActive(true);
                particle->emission_count = 0;
                if (index == 0) {
                    auto callback = std::make_unique<W8MonsterShakeCallback>();
                    callback->m_pMonster = this;
                    callback->m_pParticles = particle;
                    callback->saved_behaviour = visual.animation_behaviour;
                    callback->saved_frame_method = visual.frame_method;
                    particle->callback = std::move(callback);

                    visual.pending_behaviour = W8_ANIMATION_NEVER_STOP;
                    if (animation->start_frame == animation->end_frame) {
                        visual.SetFrameMethod(W8_ANIMATION_UNKNOWN_FRAME_METHOD);
                        visual.frame_direction = W8_ANIMATION_FORWARD;
                    } else {
                        visual.SetFrameMethod(W8_ANIMATION_PING_PONG);
                        visual.first_frame = animation->start_frame;
                        if (animation->end_frame < GetNumSubCycles()) {
                            visual.last_frame = animation->end_frame;
                        } else {
                            visual.last_frame = GetNumSubCycles() - 1;
                        }
                    }
                }
            }
            continue;
        }

        if (particle->start_frame != -1 && particle->end_frame != -1 &&
            particle->start_frame != particle->end_frame) {
            if (static_cast<unsigned int>(previous_frame) ==
                static_cast<unsigned int>(particle->start_frame)) {
                if (visual.enabled) {
                    particle->SetActive(true);
                    particle->emission_count = 0;
                }
            } else if (static_cast<unsigned int>(visual.subcycle) ==
                       static_cast<unsigned int>(particle->end_frame)) {
                particle->SetActive(false);
            }
        }
    }
}

Animation* Monster::GetCurrentAnimation()
{
    Monster* representation = this;

    return representation
        ->cycles[representation->visual.current_cycle][representation->visual.current_subcycle]
        .animation.get();
}

/* Store one value in the two cycle records used as its compact mirrors, then
   propagate it to every attached object's +0x28 field.  The body consumes two
   cdecl arguments; callers that reserve another stack slot clean it themselves. */
void Monster::SetLocationId(int value)
{
    location_id = value;
    nav.movement.location_id = static_cast<unsigned short>(value);
    for (auto& event : visual.sound_events) {
        event.location_id = value;
    }
}

bool Monster::HasPendingCycle()
{
    return visual.pending_cycle != -1;
}

/* Compare the cycle's selected frame against the renderer's typed current-model
   slot.  Prop.cpp independently compares that slot with MeshInstance values. */

glm::vec3 Monster::GetRepresentationLocation() const
{
    return visual.GetLocation();
}

glm::vec3 Monster::GetRepresentationLocalLocation() const
{
    return visual.GetLocalLocation();
}

/* Reset pitch, update the Navigator's facing, and rebuild the representation's
   complete yaw/pitch/roll matrix. */
void Monster::SetFacing(float angle)
{
    glm::mat3 rotation;

    nav.SetAngles(NormalizeAngle(angle));
    nav.SetPitch(0.0f);

    rotation = glm::mat3(1);

    angle = NormalizeAngle(nav.GetYaw() + g_monster_rotation_offset);
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));

    angle = nav.GetPitch();
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(1, 0, 0)));

    angle = nav.movement.roll;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 0, 1)));

    visual.SetRotation(&rotation);
}

void Monster::GetScaleRange(float* minimum, float* maximum)
{
    Monster* runtime = this;

    *minimum = runtime->minimum_scale;
    *maximum = runtime->maximum_scale;
}

/* Returns the previous animation state and timestamps every update through the
   recovered shared SurRender timer. */
unsigned char Monster::SetAnimating(bool animating)
{
    Monster* runtime = this;
    unsigned char previous = runtime->visual.animation_playing;

    runtime->visual.animation_playing = animating;
    runtime->visual.timer = GameClockMilliseconds();
    return previous;
}

unsigned char Monster::IsAnimating()
{
    return visual.animation_playing;
}

/* A blocking script cycle keeps its pending-cycle slot; otherwise the request
   is stored as the representation's pending cycle. */
void Monster::SetPendingCycle(int cycle)
{
    if ((runtime_flags & W8_MONSTER_SCRIPT_WAIT) == 0) {
        visual.pending_cycle = static_cast<signed char>(cycle);
    }
}

void Monster::SetRuntimeBehaviour(W8AnimationBehaviour behaviour)
{
    assert(behaviour >= 1 && behaviour <= 3);
    visual.pending_behaviour = behaviour;
}

void Monster::SetActive(bool state)
{
    nav.active = state;
}

bool Monster::IsDying()
{
    bool dying =
        Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_DIE || visual.pending_cycle == 0x15;

    return dying;
}

/* Resolve mapped vertex zero on the current model and transform it into world
   space. Models without that mapping use the Navigator position plus the
   Monster's vertical offset. */
void Monster::GetMappedPosition(glm::vec3* position)
{
    MeshInstance* instance = visual.GetCurrentModelInstance();

    if (instance != 0) {
        Mesh* mesh = static_cast<Mesh*>(instance->getModel());
        for (auto* mesh_part : meshParts(mesh)) {
            int index = mesh_part->FindMappedIndex(0);

            if (index >= 0) {
                std::optional<glm::vec3> vertex;
                if ((mesh_part->flags & W8_MESH_HAS_FRAME_STORAGE) == 0) {
                    if (const auto* vertices = mesh_part->getVertexLoc()) vertex = vertices[index];
                } else {
                    vertex = mesh_part->framePosition(0, index);
                }
                if (vertex) {
                    const auto matrix = instance->transform.matrix<glm::mat4>();
                    *position = glm::vec3(matrix * glm::vec4(*vertex, 1));
                    return;
                }
            }
        }
    }

    *position = nav.movement.position;
    position->y += nav.movement.height_offset;
}

/* Six thin bodies over the live animation object. Each is a null check and a
   forward, or a single member read; nothing here says what the members and
   slots are for, so each is named for what it reaches. */

void Monster::SetAdjustedPosition(const glm::vec3* position)
{
    glm::vec3 current;
    glm::vec3 adjusted;
    glm::vec3* adjusted_position;
    glm::vec3 result;

    current = *position;
    adjusted_position =
        nav.AdjustPosition(&adjusted, &current, &current, *owner_world->compiledIndex());
    result = *adjusted_position;
    nav.SetPositionInternal(&result);
}

unsigned short Monster::ApproachStartupNavigator(double separation)
{
    unsigned short result =
        nav.SetMovementTargetToNavigator(&owner_world->party_navigator, separation);
    if (result != 0) {
        nav.movement.boundary_enabled = false;
    }
    return result;
}

unsigned char Monster::LinkToStartupNavigator()
{
    Navigator* target = &owner_world->party_navigator;

    return nav.LinkToNavigator(target, WorldGetFarClip(owner_world) * 2.0);
}

unsigned short Monster::ConfigureMovementToPlayer(float separation, float maximum_distance,
                                                  glm::vec3 position, int trace_mode,
                                                  unsigned char* probe_result)
{
    Navigator* target = &owner_world->party_navigator;

    return nav.ConfigureMovementToNavigator(target, separation, maximum_distance, position,
                                            trace_mode, nav.GetYaw(), probe_result);
}

unsigned short Monster::ConfigureMovementToMonster(Monster* target, float separation,
                                                   float maximum_distance, glm::vec3 position,
                                                   int trace_mode, unsigned char* probe_result)
{
    if (target == 0) {
        return 0;
    }
    return nav.ConfigureMovementToNavigator(&(target)->nav, separation, maximum_distance, position,
                                            trace_mode, nav.GetYaw(), probe_result);
}

/* Hands the shared reference position to one of Navigator's two position
   sinks. The monster's script part is found the long way round - the location
   id lives in the cycle array at 0x1e4, and the lookup pair turns it into the
   W8MonsterInfo whose control state gates the whole body - which is what puts
   this in Monster.cpp rather than in the engine: the assertion path the lookup
   carries names this file and line 5299.
   Control state one is the only value that suppresses the update; every other
   value falls through. The position is fetched before the flag is read, so it
   is read even on the path that turns out not to need one sink over the other,
   and the flag decides only which sink receives it. */
void Monster::ForwardReferencePosition(signed char alternate)
{
    W8MonsterInfo* monster_info;
    glm::vec3 position;

    monster_info = owner_world->population->findMonster(location_id);
    if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
        position = cameraLocation(*owner_world);
        if (alternate != 0) {
            nav.SetFacingToward(&position);
        } else {
            nav.AimAtPosition(&position);
        }
    }
}

/* Aim one live Monster at another unless the source is controlled directly.
   The alternate path uses Navigator's second position sink, matching the
   corresponding player-position helper above. */
void Monster::AimAtMonster(Monster* target, bool alternate)
{
    W8MonsterInfo* monster_info;
    glm::vec3 target_position;
    glm::vec3 position;

    if (target != 0) {
        monster_info = owner_world->population->findMonster(location_id);
        if (monster_info->control_state != W8_MONSTER_CONTROL_LURED) {
            target_position = target->nav.GetPosition();
            position = target_position;
            if (alternate) {
                nav.SetFacingToward(&position);
            } else {
                nav.AimAtPosition(&position);
            }
        }
    }
}

/* Floating damage feedback: render the amount into a scratch ARGB1555
   surface, wrap it in a camera-facing poster ahead of the monster, and fire a
   ten-particle blood burst at the same spot. The poster scales with camera
   distance so the number stays legible. */
void Monster::SpawnDamageNumber(unsigned int amount)
{
    if ((runtime_flags & W8_MONSTER_FADED_OUT) != 0) {
        return;
    }

    auto surface = std::shared_ptr<SDL_Surface>(
        CreateCpuStorage(0x100, 0x100, SDL_PIXELFORMAT_ARGB1555).release(), SDL_DestroySurface);
    SDL_FillSurfaceRect(surface.get(), nullptr, 0);
    unsigned char* data = static_cast<unsigned char*>(surface->pixels);
    if (data != 0) {
        std::string text;
        glm::vec3 position;
        glm::vec3 camera_position;
        glm::vec3 facing;
        glm::dvec3 location;
        glm::vec4 colour;
        glm::mat3 rotation;
        glm::dmat3 world;
        ParticleSystem* particle;
        DrawShader shader;
        float distance;
        float scale;
        float pitch;
        float yaw;

        {

            text = fmt::format("{:d}", amount);
            SDLSurfaceOwner text_surface{SDL_CreateSurfaceFrom(surface->w, surface->h,
                                                               SDL_PIXELFORMAT_RGB565, data,
                                                               surface->pitch),
                                         SDL_DestroySurface};
            if (text_surface)
                DrawText(*text_surface, g_monster_damage_font,
                         {0x80 - g_monster_damage_font.textWidth(text) / 2,
                          0x80 - g_monster_damage_font.height() / 2},
                         text);
        }

        GetMappedPosition(&position);
        facing = nav.GetPosition();
        pitch = ElevationToTargetCPP(&facing);
        facing = nav.GetPosition();
        yaw = HeadingToTargetCPP(&facing);
        OffsetPositionByYawPitch(500.0f, &position, yaw, pitch);
        camera_position = cameraLocation(*owner_world);
        distance = glm::length((position - camera_position));
        if (distance > 5e+04f) {
            distance = 5e+04f;
        }
        scale = (distance * 0.0002f + 1.0f) * 375.0f;
        {
            auto poster = VideoMakePoster(surface, scale, scale, true);
            poster->alignment_flags |= 1u;
            location = glm::dvec3(position);
            poster->transform.setLocation(location);
            poster->setScene(owner_world->scene.get());
            linked_runtime_objects.push_back(std::move(poster));

            auto owned_particle = std::make_unique<ParticleSystem>(owner_world->scene.get(), 0xa);
            particle = owned_particle.get();
            auto material = std::make_shared<Material>();
            colour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            material->setEmissive(colour);
            material->setDiffuse(colour);
            particle->SetMaterial(std::move(material));
            particle->SetTexture(LoadTextureFromPath(
                wiz8::asset_path("data/monsters/bitmaps/BloodParticle.tga"), nullptr, true));
            shader.value = 0x100c4b3;
            particle->SetRenderFlags(shader);
            particle->particle_size = 20.0;
            particle->expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
            particle->bounds_origin = {0, 0, 0};
            particle->cone_yaw = 1.5707963f;
            particle->cone_pitch = 1.5707963f;
            particle->emission_interval = 1;
            particle->has_acceleration = true;
            particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
            particle->lifetime_ms = 10000;
            particle->direction_mode = W8_PARTICLE_DIRECTION_CONE;
            particle->speed_min = 1000.0f;
            particle->speed_max = 3000.0f;
            particle->emission_limit = 8;
            particle->release_when_done = true;
            particle->bounds_mode = W8_PARTICLE_BOUNDS_SPHERE;
            particle->bounds_radius = 1000.0f;
            location = glm::dvec3(position);
            particle->transform.setLocation(location);
            rotation = visual.GetCurrentModelInstance()->transform.getRotation<glm::mat3>();
            rotation *= glm::mat3_cast(glm::angleAxis(float(g_camera_pi), glm::vec3(0, 1, 0)));

            world = glm::dmat3(rotation);
            particle->transform.rotate(world);
            RetireParticle(*owner_world, std::move(owned_particle));
        }
    }
}

/* Resolve the database-controlled render gate after the Monster's transient
   runtime overrides. The alternate argument selects the secondary live-info
   flag used by the world-update path. */
bool Monster::IsRenderable(bool alternate)
{
    bool disabled = this->disabled;
    int location_id = this->location_id;
    W8MonsterInfo* monster_info;
    const W8MonsterRecord* record;

    if (disabled) {
        return false;
    }
    if (inactive) {
        return true;
    }
    if (location_id == -1) {
        return true;
    }
    if ((runtime_flags & W8_MONSTER_REMOVE_AFTER_FADE) != 0) {
        return true;
    }
    if (fade_state != W8_MONSTER_FADE_IDLE) {
        return true;
    }

    monster_info = owner_world->population->findMonster(location_id);
    record = GetMonsterDataForInfo(monster_info);
    if (record->camouflage0 > 0) {
        return monster_info->party_threat.party_detected;
    }
    if (alternate) {
        return monster_info->party_threat.visible_to_player;
    }
    return monster_info->within_viewing_distance;
}

/* Discover animated material state once, cache it in the monster flags, and restart
   the selected model's animated texture on frame zero when present. */
void Monster::InitializeAnimatedTexture()
{
    MeshInstance* instance = 0;

    if (visual.subcycle == 0) {
        if ((runtime_flags & W8_MONSTER_TEXTURE_CHECKED) == 0) {
            Mesh* model;

            instance = visual.SelectCycleFrameLod(0, visual.m_bLOD);
            model = static_cast<Mesh*>(instance->getModel());
            if (!MeshHasAnimatedTexture(model)) {
                runtime_flags &= ~W8_MONSTER_ANIMATED_TEXTURE;
            } else {
                runtime_flags |= W8_MONSTER_ANIMATED_TEXTURE;
            }
            runtime_flags |= W8_MONSTER_TEXTURE_CHECKED;
        }
        if ((runtime_flags & W8_MONSTER_ANIMATED_TEXTURE) != 0) {
            if (instance == 0) {
                instance = visual.SelectCycleFrameLod(0, visual.m_bLOD);
            }
            SetModelAnimatedTextureFrame(instance, 0);
        }
    }
}

/* Advance a cycle's representation through its own virtual layout. */

void Monster::RefreshStandingHeight()
{
    Monster* representation = this;
    signed char previous_cycle = representation->visual.current_cycle;
    SetCycle(1);
    visual.SetSubCycle(0);
    const glm::vec3 camera_location = cameraLocation(*owner_world);
    visual.SelectLOD(&camera_location);
    if (const auto bounds = GetAnimationBounds())
        representation->standing_height = bounds->maximum.y;
    if (previous_cycle != -1) {
        SetCycle(previous_cycle);
    }
}

/* The model's scale for the current cycle: the monster's own, the death
   cycle's share of it while dying, mirrored on X for a left-handed monster. */
glm::vec3 Monster::CycleModelScale() const
{
    const float cycle_scale =
        visual.current_cycle == W8_MONSTER_CYCLE_DIE ? death_scale * scale : scale;
    return {visual.mirror_x ? -cycle_scale : cycle_scale, cycle_scale, cycle_scale};
}

void Monster::ApplyRepresentationScale()
{
    float old_scale = nav.movement.scale;
    nav.SetScale(scale);
    visual.model_scale = CycleModelScale();
    if (!visual.particles.empty()) {
        for (int index = 0; index < static_cast<int>(visual.particles.size()); ++index) {
            ParticleSystem* particle = visual.particles[index].m_pstParticles.get();
            particle->SetParticleScale(scale);
            if (visual.mirror_x) {
                visual.particles[index].position.x *= -1.0f;
                particle->transform.setScale(glm::dvec3(-1.0, 1.0, 1.0));
            }
        }
    }
    if (visual.m_ground_shadow != 0) {
        float ratio = scale / old_scale;
        visual.m_ground_shadow->width *= ratio;
        visual.m_ground_shadow->depth *= ratio;
    }
    if (monster_light != 0) {
        monster_light->SetRange(nav.movement.collision_radius * 3.0f);
        std::get<MonsterLightAnim>(monster_light->animator).vertical_offset =
            nav.movement.height_offset;
    }
}

/* Build a floating icon item from a bitmap path: clamp-wrapped texture, a
   500-unit-wide poster quad raised so its bottom edge rests on the origin, with alignment
   enabled, wrapped in a WorldItemEntity whose mesh attachment and bounds are refreshed
   for the world. */
std::unique_ptr<WorldItemEntity> CreateMonsterIconItem(World& world, std::string_view path)
{
    auto texture = std::make_shared<Texture>(wiz8::asset_path(path), false);
    texture->setWrapS(Texture::WRAP_CLAMP);
    texture->setWrapT(Texture::WRAP_CLAMP);
    texture->loadSurface();
    auto poster = MakePosterQuad(texture, 500.0f, 0.0f, true);
    auto* instance = poster.get();
    auto* mesh = static_cast<Mesh*>(instance->getModel());
    // The height follows the icon's aspect ratio; quad vertex 0 is its bottom-left corner.
    if (!mesh->vertex_locations.empty())
        mesh->relocateVertices(glm::vec3(0, -mesh->vertex_locations[0].y, 0));
    instance->setAlignment(1);
    auto item = std::make_unique<WorldItemEntity>();
    item->flags |= W8_ITEM_ENTITY_MESH_LOADED;
    item->m_psrMesh = std::move(poster);
    item->RefreshBounds();
    item->AttachMesh(&world);
    instance->light_scale = glm::vec3(0);
    return item;
}

void ApplyMonsterRepresentationScale(Monster* target)
{
    target->ApplyRepresentationScale();
}

void DestroyWorldMonster(Monster* monster)
{
    if (!monster) return;
    monster->visual.DetachRepresentation();
    auto& world = *monster->owner_world;
    std::erase(world.active_monsters, monster);
    std::erase_if(world.monsters, [monster](const auto& owner) { return owner.get() == monster; });
}

void RefreshMonsterStandingHeight(Monster* monster)
{
    if (monster != 0) {
        monster->RefreshStandingHeight();
    }
}

void Monster::TickAnimation(float time_scale)
{
    ApplyPendingCycle();
    if (!visual.animation_playing) return;
    visual.TickAnimation(
        cycles[visual.current_cycle][visual.current_subcycle].playback_scale * time_scale,
        nav.GetPosition(), false, [this]([[maybe_unused]] unsigned char previous_frame) {
            dispatchAnimationFrame(previous_frame);
            return ApplyPendingCycle();
        });
}

bool Monster::ApplyPendingCycle()
{
    if (visual.pending_cycle == -1 || CanEnterCycle(visual.pending_cycle) == 0) return false;
    SetCycle(visual.pending_cycle);
    visual.finishCycleSelection(nav.GetPosition());
    return true;
}

void Monster::SetCyclePosition(glm::vec3* position)
{
    visual.SetLocation(position);
    nav.SetPositionInternal(position);
}

bool Monster::IsPlayingDeath()
{
    return Query(W8_MONSTER_QUERY_CYCLE) == W8_MONSTER_CYCLE_DIE;
}

bool Monster::HoldsCombatTurn(bool resolving_own_action, bool missile_released, bool can_turn)
{
    if (HasPendingCycle()) return true;
    const auto cycle = static_cast<W8MonsterCycle>(Query(W8_MONSTER_QUERY_CYCLE));
    if (IsAnimating() != 0 && cycle != W8_MONSTER_CYCLE_IDLE && cycle != W8_MONSTER_CYCLE_SPICE &&
        cycle != W8_MONSTER_CYCLE_WALK && cycle != W8_MONSTER_CYCLE_DIE) {
        if (!resolving_own_action) return true;
        switch (NormalizeMonsterCycle(cycle)) {
        case W8_MONSTER_CYCLE_ATTACK_CLOSE:
            if (Query(W8_MONSTER_QUERY_FRAME) < Query(W8_MONSTER_QUERY_FRAME_COUNT) / 2)
                return true;
            break;
        case W8_MONSTER_CYCLE_ATTACK_RANGED:
            return !missile_released;
        default:
            return true;
        }
    }
    return can_turn &&
           std::fabs(nav.movement.target_yaw - nav.movement.yaw) >= g_camera_transition_epsilon;
}
