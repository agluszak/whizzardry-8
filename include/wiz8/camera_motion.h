#pragma once

/* Combined input masks consumed by W8GameData camera motion and 3dapi. */
enum {
    W8_CAMERA_MOTION_STRAFE_LEFT = 0x0001u,
    W8_CAMERA_MOTION_STRAFE_RIGHT = 0x0002u,
    W8_CAMERA_MOTION_FORWARD = 0x0004u,
    W8_CAMERA_MOTION_BACKWARD = 0x0008u,
    W8_CAMERA_MOTION_UP = 0x0010u,
    W8_CAMERA_MOTION_DOWN = 0x0020u,
    W8_CAMERA_MOTION_SKIP = 0x0040u,
    W8_CAMERA_MOTION_FAST = 0x0080u,
    W8_CAMERA_MOTION_TURN_LEFT = 0x0100u,
    W8_CAMERA_MOTION_TURN_RIGHT = 0x0200u,
    W8_CAMERA_MOTION_LOOK_UP = 0x0400u,
    W8_CAMERA_MOTION_LOOK_DOWN = 0x0800u,
    W8_CAMERA_MOTION_KEEP_ROTATION = 0x1000u,
    W8_CAMERA_MOTION_SLOW_TURN = 0x2000u,
    /* Retail suppresses the whole low byte; retain that exact extent. */
    W8_CAMERA_MOTION_HIGH_BYTE_MASK = 0xff00u
};

#include <array>
#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"

struct CameraRotation {
    glm::mat3 rotation;
    glm::mat3 movement_basis;
};

struct CameraMotion {
    glm::vec3 displacement{0};
    bool moved = false;
};

// Apply each held key in input order, clamping after each contribution. Opposed
// keys near a limit are not equivalent to summing the contributions first.
inline glm::vec3 cameraMotionInput(unsigned flags, glm::vec3 input, float step,
                                   float limit, bool allow_up, bool allow_down)
{
    struct Contribution { unsigned flag; unsigned axis; float step; bool allowed; };
    const std::array contributions{
        Contribution{W8_CAMERA_MOTION_FORWARD, 2, step, true},
        Contribution{W8_CAMERA_MOTION_BACKWARD, 2, -step, true},
        Contribution{W8_CAMERA_MOTION_STRAFE_LEFT, 0, -step, true},
        Contribution{W8_CAMERA_MOTION_STRAFE_RIGHT, 0, step, true},
        Contribution{W8_CAMERA_MOTION_UP, 1, step, allow_up},
        Contribution{W8_CAMERA_MOTION_DOWN, 1, -step, allow_down}};
    for (const auto& contribution : contributions)
        if ((flags & contribution.flag) && contribution.allowed)
            input[contribution.axis] = glm::clamp(
                input[contribution.axis] + contribution.step, -limit, limit);
    if (glm::length(input) > limit) input = glm::normalize(input) * limit;
    return input;
}
