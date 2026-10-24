#pragma once
#include <glm/glm.hpp>

enum W8FootstepSurface : unsigned char;
enum W8FootstepMaterial : unsigned char;
struct LevelEnvironment;
enum {
    W8_LEVEL_FLAG_PROP_CONTACT = 0x001,
    W8_LEVEL_FLAG_PROP_NORMAL_MISMATCH = 0x002,
    W8_LEVEL_FLAG_NEGLIGIBLE_MOTION = 0x004,
    W8_LEVEL_FLAG_NO_SOUND_ENVIRONMENT = 0x008,
    W8_LEVEL_FLAG_WALKABLE_CONTACT = 0x010,
    W8_LEVEL_FLAG_MOVEMENT_ACTIVE = 0x020,
    W8_LEVEL_FLAG_MOVEMENT_STOPPED = 0x040,
    W8_LEVEL_FLAG_MOVEMENT_RESET = 0x080,
    W8_LEVEL_MOVEMENT_STATE_MASK = W8_LEVEL_FLAG_MOVEMENT_ACTIVE | W8_LEVEL_FLAG_MOVEMENT_STOPPED |
        W8_LEVEL_FLAG_MOVEMENT_RESET,
    W8_LEVEL_FLAG_FAST_MOVEMENT = 0x100,
    W8_LEVEL_FLAG_MOVED_THIS_UPDATE = 0x200
};

struct PartyMotion {
    bool movement_override = false;
    unsigned int flags; /* 0x00 */
    /* 0x04/0x08: prop ids filled by the motion collision path; ToggleBoundProps
       flips setting-6e props referenced here. */
    int primary_contact_prop_id;   /* 0x04 */
    int secondary_contact_prop_id; /* 0x08 */
    W8FootstepSurface sound_environment;
    W8FootstepMaterial sound_environment_alt;
    float footstep_accumulator; /* 0x10 */
    float camera_scale;         /* 0x14 */
    /* 0x18/0x1c: residual segment length and facing metric from the nearest
       contact during updatePartyMovement. */
    float residual_contact_length; /* 0x18 */
    float contact_facing;          /* 0x1c */
    float speed;                   /* 0x20 */
    /* 0x24/0x28: pending elapsed times ConsumeLevelElapsedTime hands
       to the movement/fatigue pass, then clears. */
    float real_elapsed;
    float frame_elapsed;
    float movement_limit;
    float movement_progress;
    glm::vec3 camera_position{0};            /* 0x34 */
    glm::vec3 motion_input{0};               /* 0x40 */
    glm::vec3 camera_motion_velocity{0};     /* 0x4c */
    glm::vec3 contact_velocity{0};           /* 0x58 */
    glm::vec3 motion_velocity{0};            /* 0x64 */
    glm::vec3 integrated_motion{0};          /* 0x70 */
    glm::vec3 camera_motion_displacement{0}; /* 0x7c */
    glm::vec3 contact_motion{0};             /* 0x88 */
    glm::vec3 contact_displacement{0};       /* 0x94 */
    glm::vec3 motion_displacement{0};        /* 0xa0 */
    /* Contact surface normal and scale used by camera collision. */
    glm::vec3 contact_normal{0}; /* 0xac */
    float contact_normal_scale;       /* 0xb8 */
    bool flag5;                       /* 0xec */
    bool flag6;                       /* 0xed */
    float vertical_motion; /* 0xf0 */

    PartyMotion(); /* 0x0041FD10 */
    /* 0x0041FE20: when the camera sits outside the game-data AABB, push
       motion_displacement toward the box, clear environ vector, and optionally start
       party movement; returns non-zero when a clamp fired. */
    unsigned char ClampCameraToBounds(LevelEnvironment& environ, const glm::vec3* minimum,
                                      const glm::vec3* maximum);
    /* 0x0041FF00: toggle setting-6e props referenced by
     * primary_contact_prop_id/secondary_contact_prop_id. */
    unsigned char ToggleBoundProps();
    /* 0x00420470: integrate camera_motion_velocity into motion_velocity/integrated_motion. */
    unsigned int resolveMotionFlags(unsigned int input, bool engaged);
    bool IntegrateCameraForward(const LevelEnvironment& environ);
    /* 0x00420810: rotate motion_input by the saved yaw matrix and refresh
       motion_displacement; returns the updated fast-move latch. */
    unsigned char ApplySavedMotionMatrix(const LevelEnvironment& environ, unsigned char prior_fast,
                                         bool fast_move, const glm::mat3& saved,
                                         float vertical_limit);
    /* 0x0041FF90: advance movement progress / footstep state for one tick. */
    void UpdateMotionProgress(LevelEnvironment& environ, unsigned char fast_move,
                              unsigned char moved, bool camera_sway);
    /* 0x00420A60: accumulate footstep distance and optionally play a step. */
    bool UpdateFootstepFromMotion();
};
