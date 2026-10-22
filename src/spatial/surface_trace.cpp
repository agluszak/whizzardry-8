#include <cstddef>
#include <optional>
#include <span>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "wiz8/geometry.h"
#include "wiz8/render/triangle_projection.h"

std::optional<SurfaceRayHit> traceSurfaces(std::span<const W8GDSurface> surfaces,
                                           std::span<const glm::vec3> vertices,
                                           const PlaneSegment& segment, SurfaceTraceFilter filter,
                                           std::span<const unsigned int> surface_ids)
{
    std::optional<SurfaceRayHit> result;
    auto nearest = segment;
    const auto test = [&](std::size_t slot) {
        const auto& surface = surfaces[slot];
        const auto flags = surface.flags;
        if ((filter.walkable_only && !(flags & W8_GD_SURFACE_WALKABLE)) ||
            (flags & W8_GD_SURFACE_CROSSING_MASK) ||
            (filter.skip_filtered && (flags & W8_GD_SURFACE_SKIP_FILTERED_TRACE)))
            return;
        if (surface.chance != 0) {
            if (filter.mode == -1 ||
                (filter.mode >= 2 && surface.chance >= static_cast<unsigned>(filter.mode)))
                return;
            if (filter.mode == 1 && (surface.chance >= 100 || !filter.percent_roll ||
                                     surface.chance >= filter.percent_roll()))
                return;
        }
        const auto contact = planeContact(glm::vec4(surface.plane.normal, surface.plane.w), nearest,
                                          PlaneContactMode::SurfaceCollision);
        if (!contact || contact->distance >= nearest.limit) return;
        if (!triangleProjection::contains(indexedTriangle(vertices, surface.vertex_indices),
                                          flags & W8_GD_SURFACE_AXIS_MASK, contact->point,
                                          triangleProjection::Boundary::visibility))
            return;
        nearest.limit = contact->distance;
        result = SurfaceRayHit{contact->point, contact->distance, static_cast<int>(surface.index)};
    };
    if (surface_ids.empty()) {
        for (std::size_t slot = 0; slot < surfaces.size(); ++slot) test(slot);
    } else {
        for (const auto slot : surface_ids) test(slot);
    }
    return result;
}

/* Resolve a complete triangle from an explicit processed vertex table. */
std::optional<RuntimeTriangle> surfaceTriangle(std::span<const glm::vec3> vertices,
                                               std::span<const int, 3> vertexIndices)
{
    for (const int index : vertexIndices)
        if (index < 0 || static_cast<std::size_t>(index) >= vertices.size()) return std::nullopt;
    return indexedTriangle(vertices, vertexIndices);
}
