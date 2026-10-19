#pragma once

#include "glm/ext/vector_float3.hpp"

struct GroundProbe {
    glm::vec3 position;
    bool hit;
    // Index of the surface the probe's last trace selected; zero when none.
    int surface = 0;
    // Prop id the probe's prop test last reported; -1 when the resolve touched only static geometry.
    int prop = -1;
};
