#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <span>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "surrender/plane_math.h"
#include "surrender/triangle_projection.h"

namespace srPickingMath {
// Picking uses |1/w| for coverage and keeps the sign to recover plane depth.
inline glm::vec4 projectedPosition(const glm::vec4& position)
{
    const float inverse = 1.0f / position.w;
    return {glm::vec3(position) * std::abs(inverse), inverse < 0 ? -1.0f : 1.0f};
}

inline std::optional<float> triangleDepth(std::span<const glm::vec4, 3> corners,
                                          const glm::vec3& pick)
{
    unsigned positive_edges = 0;
    for (unsigned edge = 0; edge < corners.size(); ++edge) {
        const glm::vec2 first(corners[edge]);
        const glm::vec2 second(corners[(edge + 1) % corners.size()]);
        const auto area = srTriangleProjection::orientedArea(first, second, glm::vec2(pick));
        positive_edges += area > 0;
    }
    if (positive_edges != 0 && positive_edges != 3) return std::nullopt;
    std::array<glm::vec3, 3> points;
    for (unsigned corner = 0; corner < points.size(); ++corner)
        points[corner] = glm::vec3(corners[corner]) * corners[corner].w;
    const auto normal = srPlaneMath::triangleNormal(points[0], points[1], points[2]);
    const float depth =
        -(glm::dot(glm::vec2(normal), glm::vec2(pick)) - glm::dot(normal, points[0])) / normal.z;
    // This also rejects degenerate and non-finite intersections.
    return depth >= -1 && depth < pick.z ? std::optional<float>(depth) : std::nullopt;
}
} // namespace srPickingMath
