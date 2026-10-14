#pragma once

#include <glm/gtx/euler_angles.hpp>
#include <glm/glm.hpp>
#include <cmath>

#include "wiz8/engine_code/World.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"

template<class T> class srVector3T;

/* Orientation limits shared by the inline setters and GDCamera.cpp. */
extern const float g_camera_angle_period0;
extern const float g_camera_angle_lower;
extern const float g_camera_pitch_upper;
extern const float g_camera_pitch_lower;

class srNode;
class srCamera;
class W8IntervalGate;
struct W8LevelDataRecord;

/* Reconstructed owner with unproven TU identity. The cluster 0x476140-
   0x478EB0 sits in the gap between stMeshModel.cpp and AmbientSound.cpp and
   is NOT Engine Code\Camera.cpp - that TU's only anchor is
   UpdateCameraPathState at 0x0048F2F0. GameData.cpp's original
   `gpGDCamera` assertion identifies the owner allocated at 0x0065A0F8; its
   constructor allocation proves the complete 0xC0-byte extent. Positional
   members remain named by offset until consumers establish their original
   roles. */
/* Camera controller flags; keep combinations in the original unsigned long. */
enum W8CameraOrientationFlag {
    W8_CAMERA_MANUAL_INPUT = 0x01UL,
    W8_CAMERA_PITCH_LOWER_LIMIT = 0x04UL,
    W8_CAMERA_PITCH_UPPER_LIMIT = 0x08UL,
    W8_CAMERA_BRAKING_PITCH = 0x10UL,
    W8_CAMERA_LEVELING = 0x20UL,
    W8_CAMERA_YAW_MOVING = 0x40UL,
    W8_CAMERA_ORIENTATION_SNAPPED = 0x80UL
};

class GDCamera {
public:
    GDCamera(); /* 0x00476140 */

    srCamera* CreateOrAttachCamera(srNode* parent, srCamera* camera); /* 0x00476440 */
    void ApplyRotationMatrix(glm::mat3* rotation, W8LevelDataRecord* context);                       /* 0x00476610 */
    void SnapToTarget(const srVector3T<float>* target);                         /* 0x00476950 */
    void SetOrientationImmediate(float pitch, float angle);                     /* 0x00476C30 */
    unsigned char LookAt(const srVector3T<float>* target, bool preserve_pitch); /* 0x00476F90 */
    unsigned char ComputeTrackingOrientation(const srVector3T<float>* target, float* angle,
                                             float* pitch); /* 0x00477180 */
    unsigned char BeginOrientationTransition(float target_pitch, float target_angle, bool force); /* 0x00477440 */
    void Update(float elapsed);                           /* 0x004776A0 */
    void ApplyYawInput(float input);                      /* 0x00477B90 */
    void ApplyPitchInput(float input);                    /* 0x00477EB0 */
    void BrakePitchAtLimit();                             /* 0x00478290 */
    // FUNCTION: WIZ8 0x004784C0
    void SetPitch(float pitch)
    {
        m_pitch = glm::clamp(pitch, g_camera_pitch_lower, g_camera_pitch_upper);
        m_pitch_rotation = glm::mat3(glm::eulerAngleX(m_pitch));
        MarkRendererReady();
    }
    // FUNCTION: WIZ8 0x00478720
    void SetYaw(float angle)
    {
        m_yaw = glm::mod(angle, g_camera_angle_period0);
        m_yaw_rotation = glm::mat3(glm::eulerAngleY(m_yaw));
        MarkRendererReady();
    }
    void SetOrientation(float angle, float pitch);                   /* 0x004788E0 */
    glm::mat3 rotationMatrix();               /* 0x00478BD0 */
    void BeginLeveling();                                            /* 0x00478CC0 */
    void GetForwardPoint(float distance, srVector3T<float>* output); /* 0x00478CE0 */
    void SetManualControlActive(bool enabled);                       /* 0x00478E00 */

    w8_ulong m_orientation_flags;
    float m_yaw;                        /* 0x004 */
    float m_pitch;                      /* 0x008 */
    glm::mat3 m_pitch_rotation;    /* 0x00c */
    glm::mat3 m_yaw_rotation;      /* 0x030 */
    glm::mat3 m_rotation;       /* 0x054 */
    glm::vec3 m_direction;      /* 0x078 */
    float m_frame_elapsed;              /* 0x084 */
    bool m_transition_active;           /* 0x088 */
    bool m_forced_transition;           /* 0x089 */
    unsigned char m_padding_08a[2];
    glm::vec3 m_position;                 /* 0x08c */
    float m_target_yaw;                   /* 0x098 */
    float m_target_pitch;                 /* 0x09c */
    float m_start_yaw;                    /* 0x0a0 */
    float m_start_pitch;                  /* 0x0a4 */
    float m_yaw_velocity;                 /* 0x0a8 */
    float m_pitch_velocity;               /* 0x0ac */
    float m_yaw_distance;                 /* 0x0b0 */
    float m_pitch_distance;               /* 0x0b4 */
    float m_transition_duration;          /* 0x0b8 */
    W8IntervalGate* m_manual_input_timer; /* 0x0bc */

private:
    bool CanTrackTarget() const;
};

extern GDCamera* g_gd_camera;
extern srCamera* g_game_camera;

void GetCameraForwardPointCopy(float distance, srVector3T<float>* output);
void GetCameraForwardPoint(float distance, srVector3T<float>* output);
/* 0x00421170: accumulate `distance` along +Z, rotated by yaw and pitch, into
   `position`. */
void OffsetPositionByYawPitch(float distance, srVector3T<float>* position, float yaw, float pitch);
/* Elevation/heading of the direction from `position` to the camera, in
   radians; the homing missile tick faces its representation with them. */
/* ElevationToTargetCPP and HeadingToTargetCPP are declared in
   wiz8/engine_code/PolyPick.h. */
extern float g_camera_level_forward_scale;
extern float g_camera_max_yaw_velocity;
extern const float g_negative_one;
extern const double g_camera_pi;
extern const float g_camera_transition_epsilon;
extern float g_camera_default_forward_scale;
extern float g_camera_forward_scale;

bool IsCameraTransitionActive(void);
