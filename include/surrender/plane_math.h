#pragma once

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"

namespace srPlaneMath {
inline glm::vec4 normalized(const glm::vec4& plane)
{
    const float length = glm::length(glm::vec3(plane));
    return length > 0 ? plane / length : plane;
}
} // namespace srPlaneMath
