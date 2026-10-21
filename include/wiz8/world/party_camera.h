#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/camera_motion.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/retail_opaque_records.h"
#include "wiz8/world/environment_state.h"
#include "wiz8/world/party_motion.h"

/* Orientation limits shared by the inline setters and PartyCamera.cpp. */
extern const float g_camera_angle_period0;
extern const float g_camera_pitch_upper;
extern const float g_camera_pitch_lower;

class Camera3D;
class LevelSpatialIndex;
struct PartyMotion;

// Party orientation, transitions and physical motion owned by one world.
enum W8CameraOrientationFlag {
    W8_CAMERA_MANUAL_INPUT = 0x01UL,
    W8_CAMERA_PITCH_LOWER_LIMIT = 0x04UL,
    W8_CAMERA_PITCH_UPPER_LIMIT = 0x08UL,
    W8_CAMERA_BRAKING_PITCH = 0x10UL,
    W8_CAMERA_LEVELING = 0x20UL,
    W8_CAMERA_YAW_MOVING = 0x40UL,
    W8_CAMERA_ORIENTATION_SNAPPED = 0x80UL
};

enum class CameraSwayMode : signed char { Stop = -1, Reset = 0, Start = 1 };

class PartyCamera {
public:
    PartyMotion motion;
    wiz8::retail::CameraPoseOpaque saved_pose_bytes;
    unsigned char fast_move_state = 0;
    bool footstep_pending = false;
    float footstep_time = 0;
    bool manual_mouse_input = false;
    float base_horizontal_fov = 85.0f;
    float base_vertical_fov = 71.0f;
    float sway_horizontal_phase = 0;
    float sway_vertical_phase = 0;
    void applySway(Camera3D& camera, CameraSwayMode mode);
    ActiveEnvironment environment;
    bool motion_started = false;
    int saved_environment_flag = 1;
    glm::vec3 last_automap_refresh_position{0};
    /* One frame of movement. `turn` applies the frame's turn and look input and returns the
       basis the movement input is taken in; `move` advances the party under that input
       against the collision index and reports the displacement. */
    glm::mat3 turn(unsigned int flags);
    CameraMotion move(LevelSpatialIndex& collision, unsigned int flags,
                      const glm::mat3& movement_basis);
    PartyCamera(); /* 0x00476140 */

    void ApplyRotationMatrix(glm::mat3* rotation, PartyMotion* context); /* 0x00476610 */
    void SnapToTarget(const glm::vec3* target); /* 0x00476950 */
    void SetOrientationImmediate(float pitch, float angle); /* 0x00476C30 */
    unsigned char LookAt(const glm::vec3* target, bool preserve_pitch); /* 0x00476F90 */
    bool ComputeTrackingOrientation(const glm::vec3* target, float* angle,
                                    float* pitch); /* 0x00477180 */
    unsigned char BeginOrientationTransition(float target_pitch, float target_angle,
                                             bool force); /* 0x00477440 */
    void Update(float elapsed); /* 0x004776A0 */
    void ApplyYawInput(float input); /* 0x00477B90 */
    void ApplyPitchInput(float input); /* 0x00477EB0 */
    void BrakePitchAtLimit(); /* 0x00478290 */
    void SetPitch(float pitch)
    {
        m_pitch = glm::clamp(pitch, g_camera_pitch_lower, g_camera_pitch_upper);
        m_pitch_rotation = glm::mat3(glm::eulerAngleX(m_pitch));
        m_rotation = m_yaw_rotation * m_pitch_rotation;
    }
    void SetYaw(float angle)
    {
        m_yaw = glm::mod(angle, g_camera_angle_period0);
        m_yaw_rotation = glm::mat3(glm::eulerAngleY(m_yaw));
        m_rotation = m_yaw_rotation * m_pitch_rotation;
    }
    void SetOrientation(float angle, float pitch); /* 0x004788E0 */
    // The orientation as a rotation: yaw about Y, then pitch about X.
    glm::mat3 rotationMatrix() const { return m_rotation; }
    void BeginLeveling(); /* 0x00478CC0 */
    void GetForwardPoint(float distance, glm::vec3* output); /* 0x00478CE0 */
    void SetManualControlActive(bool enabled); /* 0x00478E00 */

    std::uint32_t m_orientation_flags;
    /* The party's orientation, kept by SetYaw and SetPitch with its rotation. */
    float m_yaw;
    float m_pitch;
    glm::mat3 m_pitch_rotation{1};
    glm::mat3 m_yaw_rotation{1};
    glm::mat3 m_rotation{1};
    glm::vec3 m_direction{0};
    float m_frame_elapsed;
    bool m_transition_active;
    bool m_forced_transition;
    /* The party's eye position: the one position gameplay, saves and the rendered views
       read. Movement writes it here; everything else places the party through
       `placeParty` and `restorePartyPose` (world.h), which keep the party navigator in
       step. */
    glm::vec3 m_position;
    float m_target_yaw; /* 0x098 */
    float m_target_pitch; /* 0x09c */
    float m_start_yaw; /* 0x0a0 */
    float m_start_pitch; /* 0x0a4 */
    float m_yaw_velocity; /* 0x0a8 */
    float m_pitch_velocity; /* 0x0ac */
    float m_yaw_distance; /* 0x0b0 */
    float m_pitch_distance; /* 0x0b4 */
    float m_transition_duration; /* 0x0b8 */
    W8IntervalGate m_manual_input_timer{1.0f, TimerClock::World, true};

private:
    bool CanTrackTarget();
};

// Active world controller, or startup state while no world is published.
PartyCamera& partyCamera();

/* Where the party navigator stands for an eye at `eye`: the default world height below it.
   The eye is what the party sees from and what the views draw from; the navigator is the
   party's collision and pathing proxy at ground level. */
glm::vec3 navigationPosition(const glm::vec3& eye);

void GetCameraForwardPoint(float distance, glm::vec3* output);
/* 0x00421170: accumulate `distance` along +Z, rotated by yaw and pitch, into
   `position`. */
void OffsetPositionByYawPitch(float distance, glm::vec3* position, float yaw, float pitch);
/* Elevation/heading of the direction from `position` to the camera, in
   radians; the homing missile tick faces its representation with them. */
/* ElevationToTargetCPP and HeadingToTargetCPP are declared in
   wiz8/render/picking.h. */
extern float g_camera_level_forward_scale;
extern float g_camera_max_yaw_velocity;
extern const float g_negative_one;
extern const double g_camera_pi;
extern const float g_camera_transition_epsilon;
extern float g_camera_default_forward_scale;
extern float g_camera_forward_scale;
