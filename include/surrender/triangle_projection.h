#pragma once

#include <span>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/matrix.hpp"

namespace srTriangleProjection {
template<class T, glm::qualifier Q>
T orientedArea(const glm::vec<2, T, Q>& first, const glm::vec<2, T, Q>& second,
               const glm::vec<2, T, Q>& point)
{
    return glm::determinant(glm::mat<2, 2, T, Q>(second - first, point - first));
}

inline bool containsClosed(std::span<const glm::vec2, 3> vertices, const glm::vec2& point)
{
    for (unsigned opposite = 0; opposite < vertices.size(); ++opposite) {
        const auto& first = vertices[(opposite + 1) % vertices.size()];
        const auto& second = vertices[(opposite + 2) % vertices.size()];
        if (!(orientedArea(first, second, point) *
                  orientedArea(first, second, vertices[opposite]) >=
              0))
            return false;
    }
    return true;
}

inline glm::vec2 coordinates(const glm::vec3& point, unsigned axis)
{
    return {point[(axis + 1) % 3], point[(axis + 2) % 3]};
}

// Visibility uses strict vertical crossings and includes the ray intersection.
// Collision includes the lower vertical endpoint and excludes the intersection.
enum class Boundary { visibility, collision };
inline bool contains(std::span<const glm::vec3, 3> vertices, unsigned axis, const glm::vec3& point,
                     Boundary boundary)
{
    const auto query = coordinates(point, axis);
    bool inside = false;
    for (unsigned edge = 0; edge < vertices.size(); ++edge) {
        const auto first = coordinates(vertices[edge], axis);
        const auto second = coordinates(vertices[(edge + 1) % vertices.size()], axis);
        const bool first_crosses = boundary == Boundary::visibility
                                       ? first.y < query.y && query.y < second.y
                                       : first.y <= query.y && query.y < second.y;
        const bool second_crosses = boundary == Boundary::visibility
                                        ? second.y < query.y && query.y < first.y
                                        : second.y <= query.y && query.y < first.y;
        if (!first_crosses && !second_crosses) continue;
        const auto area = orientedArea(first, second, query);
        const bool upward = second.y > first.y;
        const bool crosses = boundary == Boundary::visibility ? (upward ? area >= 0 : area <= 0)
                                                              : (upward ? area > 0 : area < 0);
        if (crosses) inside = !inside;
    }
    return inside;
}
} // namespace srTriangleProjection
