#pragma once

#include "glm/ext/vector_float3.hpp"

struct BoundingSphere {
    glm::vec3 center{0};
    float radius = 0;
};
