#include "wiz8/engine_code/GDCamera.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "glm/ext/vector_float3.hpp"
#include "layouts/main_game_screen.h"
#include "wiz8/direction_angles.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

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

GDCamera& PartyCamera()
{
    static GDCamera camera;
    return camera;
}

void GetCameraForwardPoint(float distance, glm::vec3* output)
{
    PartyCamera().GetForwardPoint(distance, output);
}

/* Accumulate a forward offset rotated by yaw and pitch. */
void OffsetPositionByYawPitch(float distance, glm::vec3* position, float yaw, float pitch)
{
    *position += glm::rotateY(glm::rotateX(glm::vec3(0, 0, distance), pitch), yaw);
}

GDCamera::GDCamera()
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

    m_rotation = m_yaw_rotation * m_pitch_rotation;
}

/* Creates the game camera when a parent is supplied, otherwise installs or
   updates a caller-provided camera. Both paths finish by applying the game
   camera owner's current rotation. */

void GDCamera::ApplyRotationMatrix(glm::mat3* rotation, W8LevelDataRecord* context)
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

bool GDCamera::CanTrackTarget()
{
    return gXStatus.fNpcDialogueMode ||
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

void GDCamera::SnapToTarget(const glm::vec3* target)
{
    if (!CanTrackTarget()) return;

    const auto aim = aimAngles((*target), m_position);
    if (!aim) return;
    m_target_pitch = aim->pitch;
    m_target_yaw = aim->yaw;
    SetOrientationImmediate(aim->pitch, aim->yaw);
}

void GDCamera::SetOrientationImmediate(float pitch, float angle)
{
    if (!CanTrackTarget()) return;

    m_target_pitch = pitch;
    m_target_yaw = angle;
    m_orientation_flags = W8_CAMERA_ORIENTATION_SNAPPED;
    m_transition_active = false;
    SetYaw(angle);
    SetPitch(pitch);
    m_pitch_velocity = 0.0f;
    m_yaw_velocity = 0.0f;
}

unsigned char GDCamera::LookAt(const glm::vec3* target, bool preserve_pitch)
{
    if (!CanTrackTarget()) return 0;

    const auto aim = aimAngles((*target), m_position);
    if (!aim) return 0;
    return BeginOrientationTransition(preserve_pitch ? m_pitch : aim->pitch, aim->yaw, false);
}

unsigned char GDCamera::ComputeTrackingOrientation(const glm::vec3* target, float* angle,
                                                   float* pitch)
{
    float lower_margin = -g_camera_vertical_margin;
    if (g_level_block->camera_mode == W8_VIEWPORT_MODE_FULL_WITH_BOTTOM_PANEL ||
        g_level_block->camera_mode == W8_VIEWPORT_MODE_CENTERED_WITH_BOTTOM_PANEL) {
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
        return 0;
    }

    *angle = m_yaw;
    *pitch = m_pitch;
    return 1;
}

unsigned char GDCamera::BeginOrientationTransition(float target_pitch, float target_angle,
                                                   bool force)
{
    if (!force && !CanTrackTarget()) return 0;

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

void GDCamera::Update(float elapsed)
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

void GDCamera::ApplyYawInput(float input)
{
    if (input != 0.0f) {
        if (!gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
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

void GDCamera::ApplyPitchInput(float input)
{
    if (input != 0.0f && !gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
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

void GDCamera::BrakePitchAtLimit()
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

void GDCamera::SetOrientation(float angle, float pitch)
{
    SetYaw(angle);
    SetPitch(pitch);
    m_rotation = m_yaw_rotation * m_pitch_rotation;
}

glm::mat3 GDCamera::rotationMatrix()
{
    m_rotation = m_yaw_rotation * m_pitch_rotation;
    return m_rotation;
}

void GDCamera::BeginLeveling()
{
    m_orientation_flags |= W8_CAMERA_LEVELING;
    BeginOrientationTransition(0.0f, m_yaw, false);
}

void GDCamera::GetForwardPoint(float distance, glm::vec3* output)
{
    m_rotation = m_yaw_rotation * m_pitch_rotation;
    m_direction = m_rotation[2];
    *output = m_position + m_direction * distance;
}

void GDCamera::SetManualControlActive(bool enabled)
{
    if (enabled && !gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
        m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
        m_manual_input_timer.Arm();
        m_transition_active = false;
        return;
    }
    m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
}
