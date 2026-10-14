#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <vector>
#include "glm/ext/matrix_float4x3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/geometry.h"

namespace surfaceGeometry {
// Decode only at the input boundary; use one affine copy for every frame format.
template<class PositionAt>
void transformPositions(std::span<srVector3> destination, const glm::mat4x3& matrix,
                        PositionAt position_at)
{
    for (std::size_t vertex = 0; vertex < destination.size(); ++vertex) {
        destination[vertex] = matrix * glm::vec4(position_at(vertex), 1);
    }
}

inline void initializePropSurface(W8GDSurface& surface, std::span<const srVector3> vertices)
{
    const auto triangle = indexedTriangle(vertices, surface.vertex_indices);
    surface.plane = trianglePlane(triangle[0], triangle[1], triangle[2]);
    surface.flags =
        dominantPlaneAxis(surface.plane.normal.glmValue()) + W8_GD_SURFACE_PROP_GEOMETRY;
    surface.hit_plane = 0;
    surface.contact_margin = 500;
    if (surface.plane.normal.y >= 0.5f) {
        surface.flags |= W8_GD_SURFACE_WALKABLE;
    } else {
        surface.slope = 0;
    }
}

struct CompilationOrder {
    std::vector<int> indices;
    int walkable_count;
};

// Keep source order within both runs, dropping triangles collapsed by welding.
inline CompilationOrder compilationOrder(std::span<const W8GDSurface> surfaces)
{
    CompilationOrder result;
    result.indices.reserve(surfaces.size());
    for (std::size_t index = 0; index < surfaces.size(); ++index) {
        const auto& vertices = surfaces[index].vertex_indices;
        if (vertices[0] != vertices[1] && vertices[0] != vertices[2] &&
            vertices[1] != vertices[2]) {
            result.indices.push_back(static_cast<int>(index));
        }
    }
    const auto boundary =
        std::stable_partition(result.indices.begin(), result.indices.end(), [&](int index) {
            return (surfaces[index].flags & W8_GD_SURFACE_WALKABLE) != 0;
        });
    result.walkable_count = static_cast<int>(boundary - result.indices.begin());
    return result;
}
} // namespace surfaceGeometry
