#pragma once

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include <ranges>
#include <algorithm>
#include "surrender/srMath.h"

namespace srPlaneMath {
inline glm::vec4 normalized(const glm::vec4& plane)
{
    const float length = glm::length(glm::vec3(plane));
    return length > 0 ? plane / length : plane;
}
inline glm::vec3 triangleNormal(const glm::vec3& first, const glm::vec3& second, const glm::vec3& third)
{
    return glm::cross(second - first, third - first);
}

inline glm::vec4 triangle(const glm::vec3& first, const glm::vec3& second, const glm::vec3& third)
{
    const auto normal = normalizedOrZero(triangleNormal(first, second, third));
    return {normal, -glm::dot(normal, first)};
}

// Works with native coefficients or a lazy view over packed plane records.
template<std::ranges::input_range Planes>
inline bool containsSphere(const Planes& planes, const glm::vec3& center, float radius)
{
    return std::ranges::none_of(
        planes, [&](const glm::vec4& plane) { return glm::dot(center, glm::vec3(plane)) + plane.w < -radius; });
}
} // namespace srPlaneMath
