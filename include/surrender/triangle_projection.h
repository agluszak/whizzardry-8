#pragma once

#include <span>
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"

namespace srTriangleProjection {
inline glm::vec2 coordinates(const glm::vec3& point, unsigned axis)
{
    return {point[(axis + 1) % 3], point[(axis + 2) % 3]};
}

// Visibility uses strict vertical crossings and includes the ray intersection.
// Collision includes the lower vertical endpoint and excludes the intersection.
enum class Boundary { visibility, collision };
inline bool contains(std::span<const glm::vec3, 3> vertices, unsigned axis,
                      const glm::vec3& point, Boundary boundary)
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
        const float crossing = (query.y - first.y) * (second.x - first.x) /
                                (second.y - first.y) + first.x;
        if (boundary == Boundary::visibility ? query.x <= crossing : query.x < crossing)
            inside = !inside;
    }
    return inside;
}
} // namespace srTriangleProjection
