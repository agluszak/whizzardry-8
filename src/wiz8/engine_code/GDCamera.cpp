#include <cmath>
#include <cstdlib>
#include <glm/gtx/rotate_vector.hpp>
#include <algorithm>
#include <optional>
#include <string>

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "wiz8/direction_angles.h"
#include "wiz8/xstatus.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/layouts/game_status.h"
#include "surrender/srCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/float_constants.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/utility.h"
#include "glm/ext/vector_double3.hpp"
#include "layouts/main_game_screen.h"
#include "surrender/srMath.h"

// GLOBAL: WIZ8 0x005ec300
extern const double g_camera_view_factor0 = 0.005555555555555556;
// GLOBAL: WIZ8 0x005ec538
extern const double g_camera_view_factor1 = 3.141592653589793;
// GLOBAL: WIZ8 0x005ec568
extern const double g_camera_view_factor2 = 90.0;
// GLOBAL: WIZ8 0x005ebc38
extern const float g_negative_one = -1.0f;
// GLOBAL: WIZ8 0x005ebc2c
const float g_camera_snap_epsilon = 0.009999999776482582f;
// GLOBAL: WIZ8 0x005ebc84
extern const float g_camera_transition_epsilon = 0.0010000000474974513f;
// GLOBAL: WIZ8 0x005ec54c
extern const float g_camera_angle_period0 = 6.2831854820251465f;
// GLOBAL: WIZ8 0x005ec014
extern const float g_camera_angle_period = 6.283185005187988f;
// GLOBAL: WIZ8 0x005ec548
extern const float g_camera_angle_lower = 0.0f;
// GLOBAL: WIZ8 0x005ec550
extern const float g_camera_pitch_upper = 0.7853981852531433f;
// GLOBAL: WIZ8 0x005ec554
extern const float g_camera_pitch_lower = -0.7853981852531433f;
// GLOBAL: WIZ8 0x005ec558
extern const float g_camera_transition_duration_factor = 10.0f;
// GLOBAL: WIZ8 0x005ec560
extern const float g_camera_forced_speed = 6.2831854820251465f;
// GLOBAL: WIZ8 0x005ec564
extern const float g_camera_half_period = 3.1415927410125732f;
// GLOBAL: WIZ8 0x005ec578
extern const float g_camera_angle_dead_zone = 0.03141592815518379f;
// GLOBAL: WIZ8 0x005ec57c
extern const float g_camera_transition_duration_scale = 0.31830987334251404f;
// GLOBAL: WIZ8 0x005ec580
extern const double g_camera_smoothing_scale = 0.31830989161357204;
/* Ten degrees a second, the turn deceleration and the pitch speed limit.
   Retail stores it as an immediate where it is assigned and compares with
   the constants below. */
#define CAMERA_TURN_RATE 0.1745329350233078f

// GLOBAL: WIZ8 0x005ec590
extern const float g_camera_input_deceleration = 0.1745329350233078f;
// GLOBAL: WIZ8 0x005ec58c
extern const float g_camera_negative_input_deceleration = -0.1745329350233078f;
// GLOBAL: WIZ8 0x005ec588
extern const float g_camera_velocity_stop_scale = 5.729577541351318f;
// GLOBAL: WIZ8 0x005ec594
extern const float g_camera_negative_velocity_epsilon = -0.0010000000474974513f;
// GLOBAL: WIZ8 0x005ec55c
extern const float g_camera_velocity_factor = 2.0f;
// GLOBAL: WIZ8 0x005ec2a0
extern const double g_camera_pi = 3.1415926;
// GLOBAL: WIZ8 0x005ec574
extern const float g_camera_horizontal_margin = 0.5563237071037292f;
// GLOBAL: WIZ8 0x005ec570
extern const float g_camera_vertical_margin = 0.2168571501970291f;
// GLOBAL: WIZ8 0x005ec3fc
const float g_camera_half_pi = 1.570796012878418f;
/* Retail emits a dynamic initializer (0x00476120): the transition speed is
   the half-period divided by the duration factor, so the plain constant
   initializer would under-produce the .data bytes. */
// GLOBAL: WIZ8 0x0065a0f4
static float g_camera_transition_speed = g_camera_half_period / g_camera_transition_duration_factor;
// GLOBAL: WIZ8 0x00609ea4
float g_camera_max_yaw_velocity = 0.3490658700466156f;
// GLOBAL: WIZ8 0x00603aac
float g_camera_level_forward_scale = 375.0f;
// GLOBAL: WIZ8 0x00603ab0
float g_camera_default_forward_scale = 750.0f;
// GLOBAL: WIZ8 0x00603ab4
float g_camera_forward_scale = 750.0f;

// GLOBAL: WIZ8 0x0065A0FC
srCamera* g_game_camera;
// GLOBAL: WIZ8 0x0065a0f8
GDCamera* g_gd_camera;

/* Two thin GDCamera wrappers over GetForwardPoint, placed here because their
   GameData.cpp ownership was never evidence-backed.  The copy-through variant
   remains a distinct recovered identity even though both expose the same operation. */
// FUNCTION: WIZ8 0x00421100
void GetCameraForwardPointCopy(float distance, srVector3T<float>* output)
{
    GetCameraForwardPoint(distance, output);
}

// FUNCTION: WIZ8 0x00421150
void GetCameraForwardPoint(float distance, srVector3T<float>* output)
{
    g_gd_camera->GetForwardPoint(distance, output);
}

/* Accumulate a forward offset rotated by yaw and pitch. */
// FUNCTION: WIZ8 0x00421170
void OffsetPositionByYawPitch(float distance, srVector3T<float>* position, float yaw, float pitch)
{
    *position += glm::rotateY(glm::rotateX(glm::vec3(0, 0, distance), pitch), yaw);
}

// FUNCTION: WIZ8 0x00476140
GDCamera::GDCamera()
{
    m_orientation_flags = 0;
    m_target_yaw = 0.0f;
    m_target_pitch = 0.0f;
    m_position = glm::vec3(0);
    m_position.y = g_default_world_height;
    m_transition_active = false;

    SetPitch(0.0f);
    SetYaw(g_float_zero);

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
    m_manual_input_timer = new W8IntervalGate(1.0f, false, true);

    m_rotation = m_yaw_rotation * m_pitch_rotation;
}

/* Creates the game camera when a parent is supplied, otherwise installs or
   updates a caller-provided camera. Both paths finish by applying the game
   camera owner's current rotation. */
// FUNCTION: WIZ8 0x00476440
srCamera* GDCamera::CreateOrAttachCamera(srNode* parent, srCamera* camera)
{
    if (parent != 0) {
        glm::dvec3 position;

        g_game_camera = new srCamera(parent);
        g_game_camera->setName("Sirtech Camera");
        position = glm::dvec3(m_position);
        g_game_camera->setLocation(position);
        g_game_camera->setClipRange(250.0, 75000.0);
        g_game_camera->setRotation(0.0, 0.0, 0.0);
        g_game_camera->setEnvironmentRange(0.0f, 1.0f);
        double view = g_camera_view_factor2 * g_camera_view_factor0 * g_camera_view_factor1;
        g_game_camera->setViewPlane(view, view);
    } else {
        glm::dvec3 position;

        if (camera == 0) {
            if (g_game_camera == 0) {
                return 0;
            }
        } else {
            g_game_camera = camera;
        }
        position = glm::dvec3(m_position);
        g_game_camera->setLocation(position);
        g_game_camera->setRotation(0.0, 0.0, 0.0);
    }

    m_transition_active = false;
    m_forced_transition = false;
    g_game_camera->setRotation(m_rotation);
    return g_game_camera;
}

// FUNCTION: WIZ8 0x00476610
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

bool GDCamera::CanTrackTarget() const
{
    return gXStatus.fNpcDialogueMode || (!(m_orientation_flags & W8_CAMERA_MANUAL_INPUT) &&
                                         m_manual_input_timer->PollFinished());
}

/* Angles that aim from position at target, with yaw wrapped into one turn. Targets
   within a unit of the camera have no usable bearing. */
static std::optional<DirectionAngles> aimAngles(const glm::vec3& target, const glm::vec3& position)
{
    const auto direction = target - position;
    if (glm::length(direction) < 1.0f)
        return std::nullopt;
    auto angles = directionAngles(direction);
    angles.yaw = glm::mod(angles.yaw, g_camera_angle_period0);
    return angles;
}

// FUNCTION: WIZ8 0x00476950
void GDCamera::SnapToTarget(const srVector3T<float>* target)
{
    if (!CanTrackTarget())
        return;

    const auto aim = aimAngles(target->glmValue(), m_position);
    if (!aim)
        return;
    m_target_pitch = aim->pitch;
    m_target_yaw = aim->yaw;
    SetOrientationImmediate(aim->pitch, aim->yaw);
}

// FUNCTION: WIZ8 0x00476C30
void GDCamera::SetOrientationImmediate(float pitch, float angle)
{
    if (!CanTrackTarget())
        return;

    m_target_pitch = pitch;
    m_target_yaw = angle;
    m_orientation_flags = W8_CAMERA_ORIENTATION_SNAPPED;
    m_transition_active = false;
    SetYaw(angle);
    SetPitch(pitch);
    m_pitch_velocity = 0.0f;
    m_yaw_velocity = 0.0f;
}

// FUNCTION: WIZ8 0x00476F90
unsigned char GDCamera::LookAt(const srVector3T<float>* target, bool preserve_pitch)
{
    if (!CanTrackTarget())
        return 0;

    const auto aim = aimAngles(target->glmValue(), m_position);
    if (!aim)
        return 0;
    return BeginOrientationTransition(preserve_pitch ? m_pitch : aim->pitch, aim->yaw, false);
}

// FUNCTION: WIZ8 0x00477180
unsigned char GDCamera::ComputeTrackingOrientation(const srVector3T<float>* target, float* angle,
                                                   float* pitch)
{
    float lower_margin = -g_camera_vertical_margin;
    if (g_level_block->camera_mode == 1 || g_level_block->camera_mode == 2) {
        lower_margin = -g_camera_vertical_margin * g_float_half;
    }

    const auto desired = directionAngles(target->glmValue() - m_position);
    float angle_delta = std::remainder(desired.yaw - m_yaw, g_camera_angle_period);
    float pitch_delta = std::remainder(desired.pitch - m_pitch, g_camera_angle_period);

    if ((target->x != m_position.x || target->z != m_position.z) &&
        (static_cast<float>(fabs(angle_delta)) > g_camera_horizontal_margin ||
         pitch_delta >= g_camera_vertical_margin || pitch_delta <= lower_margin)) {
        if (static_cast<float>(fabs(angle_delta)) > g_camera_horizontal_margin) {
            float correction = g_camera_horizontal_margin * g_float_half;
            angle_delta += angle_delta >= 0.0f ? -correction : correction;
        }
        if (pitch_delta < g_camera_vertical_margin) {
            float correction = g_camera_vertical_margin * g_float_half;
            pitch_delta += pitch_delta >= 0.0f ? -correction : correction;
        }
        if (pitch_delta > lower_margin) {
            float correction = -lower_margin * g_float_half;
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

// FUNCTION: WIZ8 0x00477440
unsigned char GDCamera::BeginOrientationTransition(float target_pitch, float target_angle,
                                                   bool force)
{
    if (!force && !CanTrackTarget())
        return 0;

    m_target_pitch = target_pitch;
    m_target_yaw = target_angle;
    m_forced_transition = force;
    m_transition_active = false;

    const float speed = force ? g_camera_forced_speed : g_camera_transition_speed;
    const float yaw_delta = std::remainder(target_angle - m_yaw, g_camera_angle_period);
    const float pitch_delta = target_pitch - m_pitch;
    m_yaw_distance = std::abs(yaw_delta);
    m_pitch_distance = std::abs(pitch_delta);
    if (target_pitch != g_float_zero || m_yaw_distance >= g_camera_snap_epsilon)
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
        if (m_yaw_velocity != 0)
            m_orientation_flags |= W8_CAMERA_YAW_MOVING;
    } else {
        m_forced_transition = false;
    }
    return m_transition_active;
}

// FUNCTION: WIZ8 0x004776A0
void GDCamera::Update(float elapsed)
{
    m_frame_elapsed = elapsed;
    if (!m_transition_active && !m_forced_transition) {
        BrakePitchAtLimit();
        return;
    }

    const float angle_traveled = std::min(
        std::abs(std::remainder(m_start_yaw - m_yaw, g_camera_angle_period)), m_yaw_distance);
    const float pitch_traveled = std::min(std::abs(m_start_pitch - m_pitch), m_pitch_distance);
    const float progress = m_yaw_distance <= m_pitch_distance ? pitch_traveled / m_pitch_distance
                                                              : angle_traveled / m_yaw_distance;
    const float phase = m_forced_transition
                            ? g_float_one - progress
                            : std::acos(glm::clamp(g_float_one - 2 * progress, -1.0f, 1.0f)) *
                                  g_camera_smoothing_scale;

    float next_time = phase * m_transition_duration + elapsed;
    if (next_time <= m_transition_duration) {
        float step;
        if (!m_forced_transition) {
            float next_weight = static_cast<float>(sin((next_time / m_transition_duration) *
                                                       static_cast<double>(g_camera_half_period)));
            float current_weight =
                static_cast<float>(sin(phase * static_cast<double>(g_camera_half_period)));
            step = static_cast<float>(
                fabs(((next_weight + current_weight) * elapsed) * g_double_half));
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

// FUNCTION: WIZ8 0x00477B90
void GDCamera::ApplyYawInput(float input)
{
    if (input != g_float_zero) {
        if (!gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
            m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
            m_manual_input_timer->Arm();
            m_transition_active = false;
        } else {
            m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
        }
    }

    if (!m_transition_active || (m_orientation_flags & W8_CAMERA_LEVELING) != 0) {
        bool decelerating_negative = false;
        bool decelerating_positive = false;
        if (input == g_float_zero) {
            if (m_yaw_velocity < g_float_zero) {
                input = CAMERA_TURN_RATE;
                decelerating_negative = true;
            } else if (m_yaw_velocity > g_float_zero) {
                input = -CAMERA_TURN_RATE;
                decelerating_positive = true;
            } else {
                m_yaw_velocity = 0.0f;
                m_orientation_flags &= ~W8_CAMERA_YAW_MOVING;
                return;
            }
        }

        m_yaw_velocity += input * m_frame_elapsed;
        if ((decelerating_negative && m_yaw_velocity > g_float_zero) ||
            (decelerating_positive && m_yaw_velocity < g_float_zero)) {
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

// FUNCTION: WIZ8 0x00477EB0
void GDCamera::ApplyPitchInput(float input)
{
    if (input != g_float_zero && !gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
        m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
        m_manual_input_timer->Arm();
        m_transition_active = false;
    } else {
        m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
    }
    if (m_transition_active) {
        return;
    }
    if (input > g_float_zero && (m_orientation_flags & W8_CAMERA_PITCH_UPPER_LIMIT) != 0) {
        return;
    }
    if (input < g_float_zero && (m_orientation_flags & W8_CAMERA_PITCH_LOWER_LIMIT) != 0) {
        return;
    }

    bool decelerating_negative = false;
    bool decelerating_positive = false;
    if (input == g_float_zero) {
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
    } else if (input <= g_float_zero) {
        m_orientation_flags &=
            ~(W8_CAMERA_LEVELING | W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_UPPER_LIMIT);
    } else {
        m_orientation_flags &=
            ~(W8_CAMERA_LEVELING | W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_LOWER_LIMIT);
    }

    m_pitch_velocity += input * m_frame_elapsed;
    if ((decelerating_negative && m_pitch_velocity > g_float_zero) ||
        (decelerating_positive && m_pitch_velocity < g_float_zero)) {
        m_pitch_velocity = 0.0f;
        return;
    }
    if (m_pitch_velocity > g_camera_input_deceleration) {
        m_pitch_velocity = CAMERA_TURN_RATE;
    } else if (m_pitch_velocity < g_camera_negative_input_deceleration) {
        m_pitch_velocity = -CAMERA_TURN_RATE;
    }

    float stopping_distance = m_pitch_velocity * g_camera_velocity_stop_scale *
                              g_camera_velocity_factor * m_pitch_velocity * g_float_half;
    if (m_pitch_velocity < g_float_zero) {
        stopping_distance = -stopping_distance;
    }
    m_pitch += m_frame_elapsed * m_pitch_velocity;
    if (m_pitch >= g_float_zero) {
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

// FUNCTION: WIZ8 0x00478290
void GDCamera::BrakePitchAtLimit()
{
    if ((m_orientation_flags & W8_CAMERA_BRAKING_PITCH) == 0) {
        return;
    }

    float limit = g_camera_pitch_upper;
    if (m_pitch < g_float_zero) {
        limit = g_camera_pitch_lower;
    }
    float braking_time = ((limit - m_pitch) / m_pitch_velocity) * 2.0f;
    if (braking_time < g_float_zero) {
        braking_time = -braking_time;
    }
    if (m_frame_elapsed <= braking_time) {
        float next_velocity = (g_float_one - m_frame_elapsed / braking_time) * m_pitch_velocity;
        m_pitch += (next_velocity + m_pitch_velocity) * m_frame_elapsed * g_float_half;
        m_pitch_velocity = next_velocity;
    } else {
        if (m_pitch < g_float_zero) {
            m_pitch = g_camera_pitch_lower;
        } else {
            m_pitch = g_camera_pitch_upper;
        }
        m_orientation_flags &=
            ~(W8_CAMERA_BRAKING_PITCH | W8_CAMERA_PITCH_UPPER_LIMIT | W8_CAMERA_PITCH_LOWER_LIMIT);
    }
    SetPitch(m_pitch);
}

// FUNCTION: WIZ8 0x004788E0
void GDCamera::SetOrientation(float angle, float pitch)
{
    SetYaw(angle);
    SetPitch(pitch);
    m_rotation = m_yaw_rotation * m_pitch_rotation;
}

// FUNCTION: WIZ8 0x00478BD0
glm::mat3 GDCamera::rotationMatrix()
{
    m_rotation = m_yaw_rotation * m_pitch_rotation;
    return m_rotation;
}

// FUNCTION: WIZ8 0x00478CC0
void GDCamera::BeginLeveling()
{
    m_orientation_flags |= W8_CAMERA_LEVELING;
    BeginOrientationTransition(0.0f, m_yaw, false);
}

// FUNCTION: WIZ8 0x00478CE0
void GDCamera::GetForwardPoint(float distance, srVector3T<float>* output)
{
    m_rotation = m_yaw_rotation * m_pitch_rotation;
    m_direction = m_rotation[2];
    *output = m_position + m_direction * distance;
}

// FUNCTION: WIZ8 0x00478E00
void GDCamera::SetManualControlActive(bool enabled)
{
    if (enabled && !gXStatus.fNpcDialogueMode && g_status.world_cursor_gate == 0) {
        m_orientation_flags |= W8_CAMERA_MANUAL_INPUT;
        m_manual_input_timer->Arm();
        m_transition_active = false;
        return;
    }
    m_orientation_flags &= ~W8_CAMERA_MANUAL_INPUT;
}
