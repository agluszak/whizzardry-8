#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/spatial/bounding_box.h"

struct W8GDSurface;

struct SurfaceIndexNode {
    std::array<std::unique_ptr<SurfaceIndexNode>, 8> children;
    std::vector<const W8GDSurface*> surfaces;
};

// Geometry owns the surfaces; this acceleration tree borrows them until the
// geometry bank is replaced. Traversal state belongs to each recursive call.
class SurfaceIndex {
public:
    SurfaceIndex(float leaf_size, BoundingBox bounds);
    bool InsertSurface(const W8GDSurface* surface, std::span<const glm::vec3> vertices);
    std::vector<const W8GDSurface*> queryAlongSegment(const glm::vec3& origin,
                                                      const glm::vec3& delta, float extent) const;

private:
    struct SurfaceCollector;
    bool insert(SurfaceIndexNode& node, const BoundingBox& bounds, float extent,
                const W8GDSurface& surface, std::span<const glm::vec3, 3> vertices);
    void collect(const SurfaceIndexNode& node, const BoundingBox& node_bounds, float extent,
                 const BoundingBox& query, SurfaceCollector& collector) const;
    static void collectLeaves(const SurfaceIndexNode& node, SurfaceCollector& collector);
    BoundingBox domain{};
    float extent = 0.0f;
    float cell_size = 0.0f;
    std::unique_ptr<SurfaceIndexNode> root;
};
