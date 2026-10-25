#pragma once
#include <glm/glm.hpp>

#include "wiz8/retail_opaque_records.h"
inline constexpr float kDefaultGravity = 187.5f;

struct LevelEnvironment {
    explicit LevelEnvironment(float gravity = kDefaultGravity);

    int value_00;
    bool ground_latch;
    unsigned char airborne;
    wiz8::retail::EnvironOpaque serialized;
    int value_08;
    /* Per-frame scale copied from the level camera_scale; 0x00421850 multiplies
       vector by it when advancing the camera under environment load. */
    float scale;
    float gravity_x;
    float gravity_y;
    float gravity_z;
    float motion_step;
    float motion_factor;
    glm::vec3 vector{0};
    float world_height;
    float forward_scale;
    float motion_limit;
    float momentum_scale;
    float value_40;

    bool RescaleToReference(const LevelEnvironment* reference, float gravity);
    /* 0x00421800: store `motion` / scale as the per-frame vector. */
    void SetScaledMotion(const glm::vec3& motion);
    /* 0x00421850: add vector * scale into `position`. */
    glm::vec3 withScaledMotion(const glm::vec3& position) const;
};

struct ActiveEnvironment {
    LevelEnvironment value;
    int index = 0;
};

// A boundary selects authored settings while carrying physical state forward.
inline void selectEnvironment(ActiveEnvironment& active, const LevelEnvironment& definition,
                              int index)
{
    const auto previous = active.value;
    active.value = definition;
    active.value.ground_latch = previous.ground_latch;
    active.value.airborne = previous.airborne;
    active.value.value_08 = previous.value_08;
    active.value.scale = previous.scale;
    active.value.motion_factor = previous.motion_factor;
    active.value.vector = previous.vector;
    active.index = index;
}
