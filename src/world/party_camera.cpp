#include "wiz8/world/party_camera.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <numbers>
#include <optional>
#include <string_view>

#include <SDL3/SDL_stdinc.h>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "camera_motion.h"
#include "geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/gtx/projection.hpp"
#include "layouts/main_game_screen.h"
#include "message_box.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/fight.h"
#include "wiz8/direction_angles.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/FormationAndFacing.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/story.h"
#include "wiz8/unicode.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_motion.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

extern const double g_camera_view_factor1 = 3.141592653589793;
extern const double g_camera_view_factor2 = 90.0;
extern const float g_negative_one = -1.0f;
extern const float g_camera_transition_epsilon = 0.0010000000474974513f;
extern const float g_camera_angle_period0 = 6.2831854820251465f;
extern const float g_camera_pitch_upper = 0.7853981852531433f;
extern const float g_camera_pitch_lower = -0.7853981852531433f;
extern const float g_camera_transition_duration_factor = 10.0f;
extern const float g_camera_forced_speed = 6.2831854820251465f;
extern const float g_camera_half_period = 3.1415927410125732f;
extern const float g_camera_angle_dead_zone = 0.03141592815518379f;
extern const float g_camera_transition_duration_scale = 0.31830987334251404f;
extern const double g_camera_smoothing_scale = 0.31830989161357204;
/* Ten degrees a second, the turn deceleration and the pitch speed limit.
   Retail stores it as an immediate where it is assigned and compares with
   the constants below. */
#define CAMERA_TURN_RATE 0.1745329350233078f

extern const float g_camera_input_deceleration = 0.1745329350233078f;
extern const float g_camera_negative_input_deceleration = -0.1745329350233078f;
extern const float g_camera_velocity_stop_scale = 5.729577541351318f;
extern const float g_camera_negative_velocity_epsilon = -0.0010000000474974513f;
extern const float g_camera_velocity_factor = 2.0f;
extern const double g_camera_pi = 3.1415926;
extern const float g_camera_horizontal_margin = 0.5563237071037292f;
extern const float g_camera_vertical_margin = 0.2168571501970291f;
/* Retail emits a dynamic initializer (0x00476120): the transition speed is
   the half-period divided by the duration factor, so the plain constant
   initializer would under-produce the .data bytes. */
static float g_camera_transition_speed = g_camera_half_period / g_camera_transition_duration_factor;
float g_camera_max_yaw_velocity = 0.3490658700466156f;
float g_camera_level_forward_scale = 375.0f;
float g_camera_default_forward_scale = 750.0f;
float g_camera_forward_scale = 750.0f;

PartyCamera& partyCamera()
{
    static PartyCamera startup_camera;
    if (auto* world = GetWorld()) return world->party.camera;
    return startup_camera;
}

glm::vec3 navigationPosition(const glm::vec3& eye)
{
    return {eye.x, eye.y - g_default_world_height, eye.z};
}

void GetCameraForwardPoint(float distance, glm::vec3* output)
{
    partyCamera().GetForwardPoint(distance, output);
}

/* Accumulate a forward offset rotated by yaw and pitch. */
void OffsetPositionByYawPitch(float distance, glm::vec3* position, float yaw, float pitch)
{
    *position += glm::rotateY(glm::rotateX(glm::vec3(0, 0, distance), pitch), yaw);
}

PartyCamera::PartyCamera(TimerClockView clocks)
    : clocks(clocks), m_manual_input_timer(clocks, 1.0f, TimerClock::World, true)
{
    m_orientation_flags = 0;
    m_target_yaw = 0.0f;
    m_target_pitch = 0.0f;
    m_position = glm::vec3(0);
    m_position.y = g_default_world_height;
    m_transition_active = false;

    SetPitch(0.0f);
    SetYaw(0.0f);

    m_frame_elapsed = 0.0f;
    m_transition_active = false;
    m_forced_transition = false;
    m_target_yaw = 0.0f;
    m_target_pitch = 0.0f;
    m_start_yaw = 0.0f;
    m_start_pitch = 0.0f;
    m_yaw_velocity = 0.0f;
    m_pitch_velocity = 0.0f;
    m_yaw_distance = 0.0f;
    m_pitch_distance = 0.0f;
    m_transition_duration = 0.0f;
}

/* Creates the game camera when a parent is supplied, otherwise installs or
   updates a caller-provided camera. Both paths finish by applying the game
   camera owner's current rotation. */

void PartyCamera::ApplyRotationMatrix(glm::mat3* rotation, PartyMotion* context)
{
    const auto forward = (*rotation)[2];
    if (context && forward != glm::vec3(0, 0, 1)) {
        context->camera_motion_velocity = forward * g_camera_level_forward_scale;
        context->camera_motion_displacement =
            context->camera_motion_velocity * context->camera_scale;
    }
    const auto angles = directionAngles(forward);
    SetOrientation(angles.yaw, angles.pitch);
    *rotation = m_rotation;
}

bool PartyCamera::CanTrackTarget(bool conversation)
{
    return conversation ||
           (!(m_orientation_flags & W8_CAMERA_MANUAL_INPUT) && m_manual_input_timer.PollFinished());
}

/* Angles that aim from position at target, with yaw wrapped into one turn. Targets
   within a unit of the camera have no usable bearing. */
static std::optional<DirectionAngles> aimAngles(const glm::vec3& target, const glm::vec3& position)
{
    const auto direction = target - position;
    if (glm::length(direction) < 1.0f) return std::nullopt;
    auto angles = directionAngles(direction);
    angles.yaw = glm::mod(angles.yaw, g_camera_angle_period0);
    return angles;
}

void PartyCamera::SnapToTarget(const glm::vec3* target, bool conversation)
{
    if (!CanTrackTarget(conversation)) return;

    const auto aim = aimAngles((*target), m_position);
    if (!aim) return;
    m_target_pitch = aim->pitch;
    m_target_yaw = aim->yaw;
    SetOrientationImmediate(aim->pitch, aim->yaw, conversation);
}

void PartyCamera::SetOrientationImmediate(float pitch, float angle, bool conversation)
{
    if (!CanTrackTarget(conversation)) return;

    m_target_pitch = pitch;
    m_target_yaw = angle;
    m_orientation_flags = W8_CAMERA_ORIENTATION_SNAPPED;
    m_transition_active = false;
    SetYaw(angle);
    SetPitch(pitch);
    m_pitch_velocity = 0.0f;
    m_yaw_velocity = 0.0f;
}

unsigned char PartyCamera::LookAt(const glm::vec3* target, bool preserve_pitch, bool conversation)
{
    if (!CanTrackTarget(conversation)) return 0;

    const auto aim = aimAngles((*target), m_position);
    if (!aim) return 0;
    return BeginOrientationTransition(preserve_pitch ? m_pitch : aim->pitch, aim->yaw, false,
                                      conversation);
}

bool PartyCamera::ComputeTrackingOrientation(const glm::vec3* target, float* angle, float* pitch)
{
    float lower_margin = -g_camera_vertical_margin;
    if (bottom_panel_view) {
        lower_margin = -g_camera_vertical_margin * 0.5f;
    }

    const auto desired = directionAngles((*target) - m_position);
    float angle_delta = std::remainder(desired.yaw - m_yaw, kCameraAnglePeriod);
    float pitch_delta = std::remainder(desired.pitch - m_pitch, kCameraAnglePeriod);

    if ((target->x != m_position.x || target->z != m_position.z) &&
        (static_cast<float>(fabs(angle_delta)) > g_camera_horizontal_margin ||
         pitch_delta >= g_camera_vertical_margin || pitch_delta <= lower_margin)) {
        if (static_cast<float>(fabs(angle_delta)) > g_camera_horizontal_margin) {
            float correction = g_camera_horizontal_margin * 0.5f;
            angle_delta += angle_delta >= 0.0f ? -correction : correction;
        }
        if (pitch_delta < g_camera_vertical_margin) {
            float correction = g_camera_vertical_margin * 0.5f;
            pitch_delta += pitch_delta >= 0.0f ? -correction : correction;
        }
        if (pitch_delta > lower_margin) {
            float correction = -lower_margin * 0.5f;
            pitch_delta += pitch_delta >= 0.0f ? -correction : correction;
        }

        *angle = NormalizeAngle(angle_delta + m_yaw);
        *pitch = pitch_delta + m_pitch;
        if (*pitch > g_camera_pitch_upper || *pitch < g_camera_pitch_lower) {
            *pitch = m_pitch;
        }
        return false;
    }

    *angle = m_yaw;
    *pitch = m_pitch;
    return true;
}

unsigned char PartyCamera::BeginOrientationTransition(float target_pitch, float target_angle,
                                                      bool force, bool conversation)
{
    if (!force && !CanTrackTarget(conversation)) return 0;

    m_target_pitch = target_pitch;
    m_target_yaw = target_angle;
    m_forced_transition = force;
    m_transition_active = false;

    const float speed = force ? g_camera_forced_speed : g_camera_transition_speed;
    const float yaw_delta = std::remainder(target_angle - m_yaw, kCameraAnglePeriod);
    const float pitch_delta = target_pitch - m_pitch;
    m_yaw_distance = std::abs(yaw_delta);
    m_pitch_distance = std::abs(pitch_delta);
    if (target_pitch != 0.0f || m_yaw_distance >= kCameraSnapEpsilon)
        m_orientation_flags = 0;
    else
        m_orientation_flags &= W8_CAMERA_LEVELING;

    if (m_yaw_distance + m_pitch_distance > g_camera_transition_epsilon) {
        m_start_pitch = m_pitch;
        m_start_yaw = m_yaw;
        m_transition_active = true;
        const float distance = std::max(m_yaw_distance, m_pitch_distance);
        m_transition_duration =
            distance * g_camera_transition_duration_scale * g_camera_transition_duration_factor;
        m_pitch_velocity = pitch_delta / distance * speed;
        m_yaw_velocity =
            m_yaw_distance <= g_camera_angle_dead_zone ? 0.0f : yaw_delta / distance * speed;
        if (m_yaw_velocity != 0) m_orientation_flags |= W8_CAMERA_YAW_MOVING;
    } else {
        m_forced_transition = false;
    }
    return m_transition_active;
}

void PartyCamera::Update(float elapsed)
{
    m_frame_elapsed = elapsed;
    if (!m_transition_active && !m_forced_transition) {
        BrakePitchAtLimit();
        return;
    }

    const float angle_traveled =
        std::min(std::abs(std::remainder(m_start_yaw - m_yaw, kCameraAnglePeriod)), m_yaw_distance);
    const float pitch_traveled = std::min(std::abs(m_start_pitch - m_pitch), m_pitch_distance);
    const float progress = m_yaw_distance <= m_pitch_distance ? pitch_traveled / m_pitch_distance
                                                              : angle_traveled / m_yaw_distance;
    const float phase =
        m_forced_transition
            ? 1.0f - progress
            : std::acos(glm::clamp(1.0f - 2 * progress, -1.0f, 1.0f)) * g_camera_smoothing_scale;

    float next_time = phase * m_transition_duration + elapsed;
    if (next_time <= m_transition_duration) {
        float step;
        if (!m_forced_transition) {
            float next_weight = static_cast<float>(sin((next_time / m_transition_duration) *
                                                       static_cast<double>(g_camera_half_period)));
            float current_weight =
                static_cast<float>(sin(phase * static_cast<double>(g_camera_half_period)));
            step = static_cast<float>(fabs(((next_weight + current_weight) * elapsed) * 0.5));
        } else {
            step = elapsed;
        }
        m_yaw += step * m_yaw_velocity;
        m_pitch += step * m_pitch_velocity;
    } else {
        m_pitch = m_target_pitch;
        m_yaw = m_target_yaw;
        if ((m_orientation_flags & W8_CAMERA_LEVELING) == 0) {
            m_yaw_velocity = 0.0f;
            m_orientation_flags &= ~W8_CAMERA_YAW_MOVING;
        }
        m_pitch_velocity = 0.0f;
        m_transition_active = false;
        m_forced_transition = false;
        m_orientation_flags &= ~W8_CAMERA_LEVELING;
    }

    SetYaw(m_yaw);
    SetPitch(m_pitch);
}

void PartyCamera::ApplyYawInput(float input)
{
    if (input != 0.0f) {
        if (!CurrentGame().activity.conversation && CurrentGame().status.world_cursor_gate == 0) {
            m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
            m_manual_input_timer.Arm();
            m_transition_active = false;
        } else {
            m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
        }
    }

    if (!m_transition_active || (m_orientation_flags & W8_CAMERA_LEVELING) != 0) {
        bool decelerating_negative = false;
        bool decelerating_positive = false;
        if (input == 0.0f) {
            if (m_yaw_velocity < 0.0f) {
                input = CAMERA_TURN_RATE;
                decelerating_negative = true;
            } else if (m_yaw_velocity > 0.0f) {
                input = -CAMERA_TURN_RATE;
                decelerating_positive = true;
            } else {
                m_yaw_velocity = 0.0f;
                m_orientation_flags &= ~W8_CAMERA_YAW_MOVING;
                return;
            }
        }

        m_yaw_velocity += input * m_frame_elapsed;
        if ((decelerating_negative && m_yaw_velocity > 0.0f) ||
            (decelerating_positive && m_yaw_velocity < 0.0f)) {
            m_yaw_velocity = 0.0f;
            m_orientation_flags &= ~W8_CAMERA_YAW_MOVING;
            return;
        }
        m_yaw_velocity =
            glm::clamp(m_yaw_velocity, -g_camera_max_yaw_velocity, g_camera_max_yaw_velocity);
        m_yaw += m_frame_elapsed * m_yaw_velocity;
        if ((m_orientation_flags & W8_CAMERA_LEVELING) != 0) {
            m_target_yaw = m_yaw;
        }
        SetYaw(m_yaw);
        m_orientation_flags |= W8_CAMERA_YAW_MOVING;
    }
}

void PartyCamera::ApplyPitchInput(float input)
{
    if (input != 0.0f && !CurrentGame().activity.conversation &&
        CurrentGame().status.world_cursor_gate == 0) {
        m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
        m_manual_input_timer.Arm();
        m_transition_active = false;
    } else {
        m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
    }
    if (m_transition_active) {
        return;
    }
    if (input > 0.0f && (m_orientation_flags & W8_CAMERA_PITCH_UPPER_LIMIT) != 0) {
        return;
    }
    if (input < 0.0f && (m_orientation_flags & W8_CAMERA_PITCH_LOWER_LIMIT) != 0) {
        return;
    }

    bool decelerating_negative = false;
    bool decelerating_positive = false;
    if (input == 0.0f) {
        if (m_pitch_velocity < g_camera_negative_velocity_epsilon) {
            input = CAMERA_TURN_RATE;
            decelerating_negative = true;
        } else if (m_pitch_velocity > g_camera_transition_epsilon) {
            input = -CAMERA_TURN_RATE;
            decelerating_positive = true;
        } else {
            m_pitch_velocity = 0.0f;
            return;
        }
    } else if (input <= 0.0f) {
        m_orientation_flags &=
            ~(W8_CAMERA_LEVELING | W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_UPPER_LIMIT);
    } else {
        m_orientation_flags &=
            ~(W8_CAMERA_LEVELING | W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_LOWER_LIMIT);
    }

    m_pitch_velocity += input * m_frame_elapsed;
    if ((decelerating_negative && m_pitch_velocity > 0.0f) ||
        (decelerating_positive && m_pitch_velocity < 0.0f)) {
        m_pitch_velocity = 0.0f;
        return;
    }
    if (m_pitch_velocity > g_camera_input_deceleration) {
        m_pitch_velocity = CAMERA_TURN_RATE;
    } else if (m_pitch_velocity < g_camera_negative_input_deceleration) {
        m_pitch_velocity = -CAMERA_TURN_RATE;
    }

    float stopping_distance = m_pitch_velocity * g_camera_velocity_stop_scale *
                              g_camera_velocity_factor * m_pitch_velocity * 0.5f;
    if (m_pitch_velocity < 0.0f) {
        stopping_distance = -stopping_distance;
    }
    m_pitch += m_frame_elapsed * m_pitch_velocity;
    if (m_pitch >= 0.0f) {
        if (m_pitch + stopping_distance > g_camera_pitch_upper) {
            m_orientation_flags |= (W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_UPPER_LIMIT);
        }
    } else if (m_pitch + stopping_distance < g_camera_pitch_lower) {
        m_orientation_flags |= (W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_LOWER_LIMIT);
    }
    if (m_pitch > g_camera_pitch_upper) {
        m_pitch = g_camera_pitch_upper;
        m_pitch_velocity = 0.0f;
    }
    if (m_pitch < g_camera_pitch_lower) {
        m_pitch = g_camera_pitch_lower;
        m_pitch_velocity = 0.0f;
    }
    SetPitch(m_pitch);
}

void PartyCamera::BrakePitchAtLimit()
{
    if ((m_orientation_flags & W8_CAMERA_BRAKING_PITCH) == 0) {
        return;
    }

    float limit = g_camera_pitch_upper;
    if (m_pitch < 0.0f) {
        limit = g_camera_pitch_lower;
    }
    float braking_time = ((limit - m_pitch) / m_pitch_velocity) * 2.0f;
    if (braking_time < 0.0f) {
        braking_time = -braking_time;
    }
    if (m_frame_elapsed <= braking_time) {
        float next_velocity = (1.0f - m_frame_elapsed / braking_time) * m_pitch_velocity;
        m_pitch += (next_velocity + m_pitch_velocity) * m_frame_elapsed * 0.5f;
        m_pitch_velocity = next_velocity;
    } else {
        if (m_pitch < 0.0f) {
            m_pitch = g_camera_pitch_lower;
        } else {
            m_pitch = g_camera_pitch_upper;
        }
        m_orientation_flags &=
            ~(W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_UPPER_LIMIT | W8_CAMERA_PITCH_LOWER_LIMIT);
    }
    SetPitch(m_pitch);
}

void PartyCamera::SetOrientation(float angle, float pitch)
{
    SetYaw(angle);
    SetPitch(pitch);
}

void PartyCamera::BeginLeveling()
{
    m_orientation_flags |= W8_CAMERA_LEVELING;
    BeginOrientationTransition(0.0f, m_yaw, false, CurrentGame().activity.conversation);
}

void PartyCamera::GetForwardPoint(float distance, glm::vec3* output)
{
    m_direction = m_rotation[2];
    *output = m_position + m_direction * distance;
}

void PartyCamera::SetManualControlActive(bool enabled)
{
    if (enabled && !CurrentGame().activity.conversation &&
        CurrentGame().status.world_cursor_gate == 0) {
        m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
        m_manual_input_timer.Arm();
        m_transition_active = false;
        return;
    }
    m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
}

glm::mat3 PartyCamera::turn(unsigned int flags)
{
    float pitch_input;
    float yaw_input;

    // Held time leaves the orientation alone, and movement takes the whole rotation.
    if (clocks.held() || clocks.motionSlowed()) {
        return m_rotation;
    }

    if ((this->m_orientation_flags & W8_CAMERA_ORIENTATION_SNAPPED) != 0) {
        this->m_orientation_flags &= ~W8_CAMERA_ORIENTATION_SNAPPED;
    }

    {
        motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        flags = motion.resolveMotionFlags(flags, AnyCharacterEngaged());
    }

    pitch_input = 0.0f;
    yaw_input = 0.0f;
    if ((flags & W8_CAMERA_MOTION_LOOK_DOWN) != 0) {
        pitch_input = 0.1745329350233078f;
    } else if ((flags & W8_CAMERA_MOTION_LOOK_UP) != 0) {
        pitch_input = -0.1745329350233078f;
    }
    if ((flags & W8_CAMERA_MOTION_TURN_LEFT) != 0) {
        yaw_input = -0.1745329350233078f;
    } else if ((flags & W8_CAMERA_MOTION_TURN_RIGHT) != 0) {
        yaw_input = 0.1745329350233078f;
    }
    if ((flags & W8_CAMERA_MOTION_SLOW_TURN) != 0) {
        g_camera_max_yaw_velocity = 0.116355285f;
        yaw_input *= 0.1f;
    } else {
        g_camera_max_yaw_velocity = 0.3490658700466156f;
    }

    if ((pitch_input != 0.0f || yaw_input != 0.0f) &&
        (motion.flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) == 0) {
        g_navigator_position_changed = true;
    }
    if (manual_mouse_input) {
        BeginManualCameraControl();
    }
    this->ApplyPitchInput(pitch_input);
    this->ApplyYawInput(yaw_input);
    this->Update(clocks.motionDelta());
    return m_yaw_rotation;
}

float g_camera_motion_clamp = 1125.0f;
float g_camera_motion_divisor = 3000.0f;
const double g_motion_delta_epsilon = 0.10000000149011612;
const float g_footstep_fall_threshold = -250.0f;

/* Advance the party under the frame's motion flags. Zero elapsed (`camera_scale`)
   returns before the motion helpers. */
CameraMotion PartyCamera::move(LevelSpatialIndex& collision, unsigned int flags,
                               const glm::mat3& movement_basis)
{
    auto* environ = &environment.value;
    CameraMotion result;
    auto& delta = result.displacement;
    unsigned int level_flags;
    PartyMotion* level;
    float forward_scale;
    bool fast_move;
    signed char moved;

    if (!motion_started) {
        motion_started = true;
        fast_move_state = 0;
        clocks.resume();
    }

    if (clocks.held()) return result;

    level = &motion;
    level_flags = level->flags;
    if ((((level_flags & W8_LEVEL_FLAG_MOVEMENT_STOPPED) != 0 &&
          (level_flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) != 0) &&
         ((level_flags & W8_LEVEL_FLAG_PROP_CONTACT) == 0 && !g_animated_prop_present)) ||
        clocks.motionSlowed()) {
        return result;
    }

    flags = level->resolveMotionFlags(flags, AnyCharacterEngaged());

    level = &motion;
    forward_scale = g_camera_level_forward_scale;
    level->flags &= ~(W8_LEVEL_FLAG_PROP_CONTACT | W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH |
                      W8_LEVEL_FLAG_WALKABLE_CONTACT);
    level->camera_scale = clocks.motionDelta();
    level->camera_position = m_position;
    level->motion_velocity = glm::vec3(0);
    level->motion_displacement = glm::vec3(0);
    level->motion_input = glm::vec3(0);
    level->sound_environment = W8_FOOTSTEP_SURFACE_UNSET;
    level->sound_environment_alt = W8_FOOTSTEP_MATERIAL_UNSET;

    if (motion.camera_scale == 0.0f) {
        return result;
    }

    if (flags == 0) {
        motion.motion_input = glm::vec3(0);
        level->integrated_motion = glm::vec3(0);
    }

    motion.motion_input =
        cameraMotionInput(flags, motion.motion_input, forward_scale, g_camera_motion_clamp,
                          g_environment_load_flag == 0, g_environment_load_flag == 0);
    fast_move = (flags & W8_CAMERA_MOTION_FAST) != 0;
    fast_move_state =
        motion.ApplySavedMotionMatrix(*environ, fast_move_state, fast_move, movement_basis);

    if (g_environment_load_flag == 0) {
        delta = motion.motion_displacement;
        moved = static_cast<float>(g_motion_delta_epsilon) < glm::length(delta);
        motion.UpdateMotionProgress(*environ, fast_move_state, moved);
        if (moved == 0) {
            goto after_move;
        }
    } else {
        moved = collision.AdvanceEnvironmentMotion(motion, environment);
        motion.UpdateMotionProgress(*environ, fast_move_state, moved);
        delta = motion.motion_displacement;
        if (glm::length(delta) <= static_cast<float>(g_motion_delta_epsilon)) {
            if (moved == 0) {
                goto after_move;
            }
        } else {
            moved = 1;
        }
    }

    m_position += delta;

after_move:
    if (environ->airborne == 0) {
        footstep_pending = true;
    } else if (footstep_pending) {
        if (motion.motion_velocity.y <= g_footstep_fall_threshold) {
            PlayFootstep(motion.sound_environment, motion.sound_environment_alt,
                         W8_FOOTSTEP_KIND_JUMP);
            motion.footstep_accumulator = 0;
        }
        footstep_pending = false;
    }
    if (collision.owner_world) updateMovementAudio(*collision.owner_world);
    if (moved == 0) {
        motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
    } else {
        motion.flags |= W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
    }
    result.moved = moved != 0;
    return result;
}

struct W8NpcState;

W8CameraPath::W8CameraPath() = default;
W8CameraPath::~W8CameraPath() = default;

/* Retail Engine Code\Camera.cpp. The TU's single anchored function is
   0x0048F2F0 below; the only other evidence is the path string and it owns no
   globals beyond the saved-environment flag. Nothing in GameData.cpp or
   world_selection.cpp falls in this hull. The PartyCamera cluster
   0x476140-0x478EB0 is NOT Camera.cpp: it sits in the gap between
   Mesh.cpp and AmbientSound.cpp, ~0x7A40 below this anchor, and remains
   a reconstructed owner in PartyCamera.cpp. */

/* Find the named camera path in the world's list and toggle it. Retail
   callers push the flag as a plain int and the body forwards it raw to
   UpdateCameraPathState. */
void UpdateCameraPathStateByName(World* world, std::string_view name, int active)
{
    for (const std::unique_ptr<W8CameraPath>& path : world->plsCameras) {
        if (CompareTextIgnoreAsciiCase(path->name0, name) == 0) {
            UpdateCameraPathState(world, path.get(), active);
            return;
        }
    }
}

void UpdateCameraPathState(World* world, W8CameraPath* path, int active)
{
    W8NpcState* npc;
    W8MonsterGroup* group;
    W8MonsterInfo* monster_info;
    glm::vec3 target;
    glm::vec3 position;
    glm::mat3 rotation;
    float angle;
    float pitch;

    if (!path->active && active != 0) {
        g_camera_path_active = true;
        path->active = true;
        PathAISetValue((path->path ? &*path->path : nullptr), 0.0f);
        PathAIResetTick((path->path ? &*path->path : nullptr), world->clocks.worldMilliseconds());
        path->path->distance_travelled = 0.0f;
        path->path->upright = 1;
        world->party.camera.saved_environment_flag = SetEnvironmentLoadFlag(0);
        return;
    }
    if (!path->active || active != 0) {
        return;
    }
    g_camera_path_active = false;
    target = glm::vec3(0.0f, 0.0f, 1.0f);
    rotation = world->party.camera.rotationMatrix();
    ApplyCameraRotation(&rotation);
    path->active = false;
    SetEnvironmentLoadFlag(world->party.camera.saved_environment_flag);
    if (GetLoadedLevelID() == 1) {
        if (SDL_strcasecmp(path->name0, "Camera01") == 0) {
            CurrentGame().npc_dialogue.Queue(W8NpcPortraitString{0x721});
            CurrentGame().npc_dialogue.Queue(W8NpcStoryAction::path2_trigger);
            return;
        }
        if (SDL_strcasecmp(path->name0, "Camera02") == 0) {
            npc = CurrentGame().story.npcOfKind(0x33);
        } else {
            if (SDL_strcasecmp(path->name0, "Camera03") != 0) {
                return;
            }
            group = FindFirstMonsterByID(0x18c);
            if (group != 0) {
                monster_info = FindMonster(group->leader_location_id);
                CurrentGame().pointCameraAtMonster(*monster_info, true, true);
                position = monster_info->entity->nav.GetPosition();
                target = position;
                if (g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_ALL_TARGETS) {
                    if (g_settings.camera_rotation_style == W8_CAMERA_ROTATION_SNAP) {
                        if (partyCamera().ComputeTrackingOrientation(&target, &angle, &pitch) ==
                            0) {
                            CameraSnapToTarget(&target);
                        }
                    } else if (g_settings.camera_rotation_style == W8_CAMERA_ROTATION_SMOOTH &&
                               partyCamera().ComputeTrackingOrientation(&target, &angle, &pitch) ==
                                   0) {
                        partyCamera().BeginOrientationTransition(
                            pitch, angle, false, CurrentGame().activity.conversation);
                    }
                }
                monster_info->entity->ForwardReferencePosition(0);
            }
            npc = CurrentGame().story.npcOfKind(0x34);
        }
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 0, true, 0);
        }
        return;
    }
    if (GetLoadedLevelID() == 4 && SDL_strcasecmp(path->name0, "CameraPath2") != 0) {
        if (SDL_strcasecmp(path->name0, "CameraPath3") == 0) {
            ClearMainGameTargetState();
            group = FindFirstMonsterByID(0x1b4);
            if (group != 0) {
                monster_info = FindMonster(group->leader_location_id);
                monster_info->entity->ForwardReferencePosition(0);
                return;
            }
        } else if (SDL_strcasecmp(path->name0, "CameraPath4") == 0) {
            ResetLevelDataVectors();
            return;
        }
    }
}

/* Turn the camera to face a monster: when rotation tracking is off the
   monster's own combat target still drives it if that target is the selected
   character. Otherwise the force flag decides whether to orient at all, and
   animate chooses the eased transition over the snap. The aim point is the
   head height, or the lower offset when the two nearly coincide or the
   monster is far away. */

/* Turn the camera to face a world position: without the force flag the camera
   only moves when tracking mode is enabled, snapping or easing per the
   configured rotation style; with it the orientation is always updated -
   snapped unless animate asks for the transition. */

void PartyCamera::applySway(Camera3D& camera, CameraSwayMode mode)
{
    const float elapsed = clocks.motionSuspended() ? 0.0f : clocks.motionDelta();

    if (mode != CameraSwayMode::Reset) {
        if (mode == CameraSwayMode::Start) {
            base_horizontal_fov =
                static_cast<float>(camera.getHorizontalFOV() * (180.0 / std::numbers::pi));
            base_vertical_fov =
                static_cast<float>(camera.getVerticalFOV() * (180.0 / std::numbers::pi));
            if (!g_camera_sway_active) {
                sway_horizontal_phase = 0.0f;
                sway_vertical_phase = 0.0f;
                BeginCameraSway();
            }
        } else {
            double radians = std::numbers::pi * 0.0055555557f;
            camera.setViewPlane(radians * base_horizontal_fov, radians * base_vertical_fov);
            EndCameraSway();
            return;
        }
    }
    if (!g_camera_sway_active) {
        return;
    }

    sway_horizontal_phase += elapsed * 12.0f;
    sway_vertical_phase += elapsed * 11.0f;
    if (sway_horizontal_phase > 360.0f) {
        sway_horizontal_phase -= 360.0f;
    }
    if (sway_vertical_phase > 360.0f) {
        sway_vertical_phase -= 360.0f;
    }

    double radians = std::numbers::pi * 0.0055555557f;
    camera.setViewPlane((sin(radians * sway_horizontal_phase) * 0.75 + base_horizontal_fov) *
                            radians,
                        (sin(radians * sway_vertical_phase) * 0.63f + base_vertical_fov) * radians);
}

void PartyCamera::updateMovementAudio(World& world)
{
    auto& loop = world.audio.footstep_loop;
    if ((m_orientation_flags & W8_CAMERA_YAW_MOVING) == 0 ||
        (motion.flags & W8_LEVEL_FLAG_WALKABLE_CONTACT) == 0 ||
        motion.motion_velocity != glm::vec3(0)) {
        if (loop) {
            wiz8::sound::stop(loop, 500);
            loop = nullptr;
        }
        return;
    }
    const auto now = clocks.motionElapsed();
    if (g_facing_tolerance0 < now - footstep_time) {
        footstep_time = now;
        if (!loop || !wiz8::sound::is_playing(loop))
            loop = PlayFootstep(motion.sound_environment, motion.sound_environment_alt,
                                W8_FOOTSTEP_KIND_SCUFF);
    }
}

void PartyCamera::pointAt(const glm::vec3& position, bool force, bool animate, bool conversation)
{
    if (!force && g_settings.camera_rotation_mode != W8_CAMERA_ROTATION_ALL_TARGETS) return;
    if (force && !animate) {
        SnapToTarget(&position, conversation);
        return;
    }
    float yaw, pitch;
    if (force || g_settings.camera_rotation_style == W8_CAMERA_ROTATION_SMOOTH) {
        if (!ComputeTrackingOrientation(&position, &yaw, &pitch))
            BeginOrientationTransition(pitch, yaw, false, conversation);
    } else if (g_settings.camera_rotation_style == W8_CAMERA_ROTATION_SNAP &&
               !ComputeTrackingOrientation(&position, &pitch, &yaw)) {
        SnapToTarget(&position, conversation);
    }
}
