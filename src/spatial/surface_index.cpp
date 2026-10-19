#include "wiz8/spatial/surface_index.h"

#include <algorithm>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/vector_relational.hpp>

#include "geometry.h"
#include "glm/ext/vector_bool3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/swept_bounds.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/octree_geometry.h"

SurfaceIndex::SurfaceIndex(float leaf_size, BoundingBox bounds)
{
    if (leaf_size < 1000.0f) throw std::invalid_argument("Leaf size must be at least 1000");
    bounds.minimum -= leaf_size * 0.5f;
    bounds.maximum += leaf_size * 0.5f;
    const auto dimensions = bounds.maximum - bounds.minimum;
    extent = std::max({0.0f, dimensions.x, dimensions.y, dimensions.z});
    domain = {bounds.minimum, bounds.minimum + extent};
    cell_size = extent;
    for (unsigned depth = 0; leaf_size * 2 <= cell_size && depth < 6; ++depth) cell_size *= 0.5f;
    cell_size *= 1.000100016593933f;
}

static BoundingBox childBounds(const BoundingBox& parent, unsigned octant, float half_extent)
{
    const glm::vec3 offset(octant >> 2, (octant >> 1) & 1, octant & 1);
    const auto minimum = parent.minimum + offset * half_extent;
    return {minimum, minimum + half_extent};
}

bool SurfaceIndex::InsertSurface(const W8GDSurface* surface, std::span<const glm::vec3> vertices)
{
    const auto triangle = surfaceTriangle(vertices, surface->vertex_indices);
    if (!triangle || !triangleIntersectsBox(domain, *triangle, surface->plane.normal)) return false;
    if (!root) root = std::make_unique<SurfaceIndexNode>();
    return insert(*root, domain, extent, *surface, *triangle);
}

bool SurfaceIndex::insert(SurfaceIndexNode& node, const BoundingBox& bounds, float extent,
                          const W8GDSurface& surface, std::span<const glm::vec3, 3> vertices)
{
    if (extent <= cell_size) {
        node.surfaces.push_back(&surface);
        return true;
    }
    const float half_extent = extent * 0.5f;
    bool inserted = false;
    for (unsigned octant = 0; octant < node.children.size(); ++octant) {
        const auto child_bounds = childBounds(bounds, octant, half_extent);
        if (!triangleIntersectsBox(child_bounds, vertices, surface.plane.normal)) continue;
        auto& child = node.children[octant];
        if (!child) child = std::make_unique<SurfaceIndexNode>();
        inserted = insert(*child, child_bounds, half_extent, surface, vertices) || inserted;
    }
    return inserted;
}

// Membership belongs to the query; geometry never carries transient visit bits.
struct SurfaceIndex::SurfaceCollector {
    std::vector<const W8GDSurface*> results;
    std::unordered_set<const W8GDSurface*> seen;
    void append(const W8GDSurface& surface)
    {
        if (seen.insert(&surface).second) results.push_back(&surface);
    }
};

/* Preserve the corner test's mixed bounds: query maxima are exclusive for
   tree corners, while the fallback checks query corners inclusively. At a
   leaf, an inside first corner retains the whole-leaf classification. */
static int classifyBuildBox(const BoundingBox& box, const BoundingBox& bounds, bool leaf)
{
    bool all_inside = true;
    bool inside = false;
    for (unsigned corner = 0; corner < 8; ++corner) {
        const auto point =
            glm::mix(box.minimum, box.maximum, glm::bvec3(corner & 4, corner & 2, corner & 1));
        if (glm::any(glm::lessThan(point, bounds.minimum)) ||
            glm::any(glm::greaterThanEqual(point, bounds.maximum))) {
            all_inside = false;
            if (inside) break;
        } else {
            inside = true;
            if (!all_inside || leaf) break;
        }
    }
    if (all_inside) return 2;
    if (!inside) {
        for (unsigned corner = 0; corner < 8; ++corner) {
            const auto point = glm::mix(bounds.minimum, bounds.maximum,
                                        glm::bvec3(corner & 4, corner & 2, corner & 1));
            if (box.contains(point)) return 1;
        }
    }
    return inside ? 1 : 0;
}

std::vector<const W8GDSurface*>
SurfaceIndex::queryAlongSegment(const glm::vec3& origin, const glm::vec3& delta, float extent) const
{
    SurfaceCollector collector;
    if (root)
        collect(*root, domain, this->extent, segmentMath::sweptBounds(origin, delta, extent),
                collector);
    return std::move(collector.results);
}

void SurfaceIndex::collect(const SurfaceIndexNode& node, const BoundingBox& node_bounds,
                           float extent, const BoundingBox& query,
                           SurfaceCollector& collector) const
{
    const bool leaf = extent <= cell_size;
    const int verdict = classifyBuildBox(node_bounds, query, leaf);
    if (verdict == 2 || (verdict == 1 && leaf)) {
        collectLeaves(node, collector);
    } else if (verdict == 1) {
        const float half_extent = extent * 0.5f;
        for (unsigned octant = 0; octant < node.children.size(); ++octant)
            if (const auto& child = node.children[octant])
                collect(*child, childBounds(node_bounds, octant, half_extent), half_extent, query,
                        collector);
    }
}

void SurfaceIndex::collectLeaves(const SurfaceIndexNode& node, SurfaceCollector& collector)
{
    for (const auto* surface : node.surfaces) collector.append(*surface);
    for (const auto& child : node.children)
        if (child) collectLeaves(*child, collector);
}
