#include "wiz8/world/party_motion.h"

#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "camera_motion.h"
#include "geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtx/projection.hpp"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/Noise.h"
#include "wiz8/math.h"
#include "wiz8/render/frame.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

unsigned char PartyMotion::ClampCameraToBounds(LevelEnvironment& environ, const glm::vec3* minimum,
                                               const glm::vec3* maximum)
{
    bool clamped = false;
    bool below_min_y = false;

    if (camera_position.y < minimum->y) {
        if (CurrentGame().status.world_suspended) {
            motion_displacement.y = maximum->y - minimum->y;
        }
        clamped = true;
        below_min_y = true;
    }
    if (camera_position.x < minimum->x) {
        clamped = true;
        motion_displacement.x = maximum->x - minimum->x;
    }
    if (camera_position.z < minimum->z) {
        clamped = true;
        motion_displacement.z = maximum->z - minimum->z;
    }
    if (maximum->x < camera_position.x) {
        clamped = true;
        motion_displacement.x = minimum->x - maximum->x;
    }
    if (camera_position.z <= maximum->z) {
        if (!clamped) {
            return 0;
        }
    } else {
        clamped = true;
        motion_displacement.z = minimum->z - maximum->z;
    }

    environ.vector = glm::vec3(0);
    if (below_min_y) {
        if (CurrentGame().status.world_suspended) {
            movement_override = false;
            return clamped;
        }
        BeginPartyMovement();
    }
    return clamped;
}

/* Probe active collidable props along the motion segment. On a hit, rewrites
   the caller's position into world space, may nudge `direction`, and latches
   plane / level-data contact fields used by the collision response pass. */

PartyMotion::PartyMotion()
{
    flags = 0;
    camera_scale = 0;
    sound_environment = W8_FOOTSTEP_SURFACE_NONE;
    sound_environment_alt = W8_FOOTSTEP_MATERIAL_NONE;
    residual_contact_length = 0.0f;
    contact_facing = 0.0f;
    speed = 0;
    real_elapsed = 0;
    frame_elapsed = 0;
    movement_limit = 0;
    movement_progress = 0;
    flag5 = false;
    flag6 = false;
    vertical_motion = 0;
    footstep_accumulator = -2000.0f;
    primary_contact_prop_id = -1;
    secondary_contact_prop_id = -1;
    contact_normal_scale = 1.0f;
    movement_override = false;
}

unsigned int PartyMotion::resolveMotionFlags(unsigned int input, bool engaged)
{
    if (!engaged || (flags & (W8_LEVEL_FLAG_MOVEMENT_STOPPED | W8_LEVEL_FLAG_MOVEMENT_RESET))) {
        input &= W8_CAMERA_MOTION_HIGH_BYTE_MASK;
        if (!(flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE)) flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    } else if (flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) {
        if (flags & W8_LEVEL_FLAG_FAST_MOVEMENT)
            input |= W8_CAMERA_MOTION_FAST;
        else
            input &= ~W8_CAMERA_MOTION_FAST;
    } else {
        if (input & W8_CAMERA_MOTION_FAST)
            flags |= W8_LEVEL_FLAG_FAST_MOVEMENT;
        else
            flags &= ~W8_LEVEL_FLAG_FAST_MOVEMENT;
    }
    return input;
}

bool PartyMotion::IntegrateCameraForward(const LevelEnvironment& environ)
{
    float forward_length;
    float limit;
    float delta_length;
    float scale;
    bool cleared_vector;
    glm::vec3 adjustment;
    glm::vec3 combined;

    cleared_vector = false;
    forward_length = glm::length(camera_motion_velocity);
    limit = environ.motion_limit * camera_scale;
    if (limit <= glm::length(motion_input)) {
        adjustment = camera_motion_velocity;
        adjustment = ProjectVectorOntoVector(adjustment, integrated_motion);
        adjustment -= camera_motion_velocity;
        delta_length = glm::length(adjustment);
        limit = environ.motion_factor * limit;
        if (delta_length <= limit) {
            if (delta_length < limit * kCameraSnapEpsilon) {
                adjustment = glm::vec3(0);
            }
        } else {
            adjustment = normalizedOrZero(adjustment) * limit;
        }
        scale = environ.momentum_scale * environ.motion_factor;
        integrated_motion *= scale;
    } else {
        if (forward_length < limit) {
            motion_velocity = glm::vec3(0);
            return false;
        }
        limit = environ.motion_factor * limit;
        adjustment = -camera_motion_velocity;
        if (limit < forward_length) {
            adjustment = normalizedOrZero(adjustment) * limit;
        }
        integrated_motion = glm::vec3(0);
        cleared_vector = true;
    }

    combined = camera_motion_velocity + integrated_motion + adjustment;
    motion_velocity = combined;
    integrated_motion = combined;
    if (cleared_vector) {
        if (glm::dot(motion_input, adjustment) > g_camera_transition_epsilon) {
            motion_velocity = glm::vec3(0);
            return false;
        }
    }
    return true;
}

unsigned char PartyMotion::ApplySavedMotionMatrix(const LevelEnvironment& environ,
                                                  unsigned char prior_fast, bool fast_move,
                                                  const glm::mat3& saved)
{
    float horizontal;
    float clamp_scale;
    if (prior_fast != 0 && !fast_move) {
        motion_input = glm::vec3(0);
        integrated_motion = glm::vec3(0);
    }
    motion_input = saved * motion_input;
    integrated_motion = motion_input * camera_scale;
    if (!IntegrateCameraForward(environ)) {
        return 0;
    }

    horizontal = glm::length(glm::vec2(motion_velocity.x, motion_velocity.z));
    if (fast_move)
        prior_fast = 1;
    else if (horizontal <= g_camera_level_forward_scale)
        prior_fast = 0;
    clamp_scale = prior_fast ? g_camera_default_forward_scale : g_camera_level_forward_scale;
    if (horizontal > clamp_scale) {
        const auto limited =
            glm::vec2(motion_velocity.x, motion_velocity.z) * (clamp_scale / horizontal);
        motion_velocity.x = limited.x;
        motion_velocity.z = limited.y;
    }
    const float vertical_limit =
        g_environment_load_flag ? g_camera_forward_scale : g_camera_level_forward_scale;
    motion_velocity.y = glm::clamp(motion_velocity.y, -vertical_limit, vertical_limit);
    motion_displacement = motion_velocity * camera_scale;
    return prior_fast;
}

bool PartyMotion::UpdateFootstepFromMotion()
{
    const auto movement = camera_motion_displacement -
                          ((flags & W8_LEVEL_FLAG_PROP_CONTACT) ? contact_motion : glm::vec3(0));
    float distance;
    bool large_radius;

    if ((flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0 ||
        (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0) {
        return false;
    }
    distance = glm::length(movement) + footstep_accumulator;
    footstep_accumulator = distance;
    if (2e+03f < distance) {
        if (!CurrentGame().status.search_mode && (flags & W8_LEVEL_FLAG_FAST_MOVEMENT) == 0) {
            large_radius = false;
        } else {
            large_radius = true;
        }
        AlertCombatNoise(large_radius);
        if (sound_environment != W8_FOOTSTEP_SURFACE_UNSET &&
            sound_environment_alt != W8_FOOTSTEP_MATERIAL_UNSET) {
            PlayFootstep(sound_environment, sound_environment_alt, W8_FOOTSTEP_KIND_STEP);
            while (2e+03f < footstep_accumulator) {
                footstep_accumulator -= 2e+03f;
            }
        }
    }
    return true;
}

/* Flip the two props the last motion contact bound: the primary prop toggles
   only while its setting-6f2 latch is set, and the secondary follows when it
   is still set on the far side; a secondary that fails the check reports 0. */
unsigned char PartyMotion::ToggleBoundProps()
{
    Prop* prop;
    bool toggled = false;

    BeginPartyMovement();
    if (primary_contact_prop_id >= 0) {
        prop = GetWorld()->collidable_props[primary_contact_prop_id];
        if (prop->IsAnimationPingPong()) {
            prop->ReverseAnimationDirection();
            toggled = true;
            if (secondary_contact_prop_id >= 0) {
                prop = GetWorld()->collidable_props[secondary_contact_prop_id];
                if (!prop->IsAnimationPingPong()) {
                    return 0;
                }
                prop->ReverseAnimationDirection();
            }
        }
    }
    return toggled;
}

void PartyMotion::UpdateMotionProgress(LevelEnvironment& environ, unsigned char fast_move,
                                       unsigned char moved)
{
    bool allow_override;
    float length;
    float scale;
    glm::vec3 projected;
    glm::vec3 gravity;
    glm::vec3 environ_vector;

    allow_override = true;
    if ((flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0) {
        contact_velocity = glm::vec3(0);
        contact_displacement = glm::vec3(0);
        if (0.0f < motion_velocity.y) {
            length = glm::length(motion_velocity);
            const float limit =
                fast_move ? g_camera_default_forward_scale : g_camera_level_forward_scale;
            if (length > limit) {
                motion_velocity = normalizedOrZero(motion_velocity) * (limit);
                motion_displacement = motion_velocity * camera_scale;
            }
        }
    }
    if (moved == 0) {
        if ((flags & W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT) != 0 ||
            (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0) {
            allow_override = false;
        }
        camera_motion_velocity = glm::vec3(0);
        camera_motion_displacement = glm::vec3(0);
        footstep_accumulator = 1800.0f;
    } else {
        camera_motion_velocity = motion_velocity - contact_velocity;
        camera_motion_displacement = camera_motion_velocity * camera_scale;
        allow_override = UpdateFootstepFromMotion();
        camera_motion_velocity -= environ.vector;
        projected = contact_velocity;
        gravity = {environ.gravity_x, environ.gravity_y, environ.gravity_z};
        projected = ProjectVectorOntoVector(projected, gravity);
        environ_vector = environ.vector;
        if (glm::length(environ_vector) < glm::length(projected)) {
            environ.vector = projected;
        }
        motion_input = glm::vec3(0);
        integrated_motion = glm::vec3(0);
    }

    motion_velocity = motion_displacement * (static_cast<float>(1.0) / camera_scale);
    speed = glm::length(motion_displacement);
    if (speed < g_camera_transition_epsilon) {
        speed = 0;
        motion_displacement = glm::vec3(0);
        motion_velocity = glm::vec3(0);
    }
    if (!(flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) || (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT)) {
        auto& elapsed = (flags & W8_LEVEL_FLAG_FAST_MOVEMENT) ? frame_elapsed : real_elapsed;
        elapsed += speed;
    }
    if ((flags & W8_LEVEL_FLAG_MOVEMENT_ACTIVE) && (flags & W8_LEVEL_FLAG_WALKABLE_CONTACT)) {
        scale = movement_progress;
        movement_progress = speed + scale;
        if (movement_limit < speed + scale) {
            camera_motion_velocity = glm::vec3(0);
            camera_motion_displacement = glm::vec3(0);
            footstep_accumulator = 2000.0f;
            flags |= W8_LEVEL_FLAG_MOVEMENT_STOPPED;
            if (CurrentGame().party_movement_mode) {
                UpdateActivePartyMovement();
            }
        }
    }

    if (!g_camera_sway_active) {
        if (!movement_override) {
            if (allow_override && vertical_motion < 0.1f) {
                movement_override = true;
            }
        } else if (allow_override && 0.35f < vertical_motion) {
            HandleLevelOverride(vertical_motion);
        }
        vertical_motion = -(motion_velocity.y / g_camera_motion_divisor);
        return;
    }
    vertical_motion = 0;
}

LevelEnvironment::LevelEnvironment()
{

    value_00 = 0;
    ground_latch = false;
    airborne = 0;
    scale = 1.0f;
    value_08 = 0;
    gravity_x = 0;
    gravity_y = -g_navigator_gravity;
    gravity_z = 0;
    motion_step = 0.05f;
    motion_factor = 1.0f;
    world_height = g_default_world_height;
    forward_scale = g_camera_level_forward_scale * g_navigator_linked_radius_scale;
    motion_limit = g_default_motion_limit;
    momentum_scale = g_default_momentum_scale;
    value_40 = 1.0f;
}
