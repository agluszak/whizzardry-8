#ifndef WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H
#define WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/octree_spatial.h"

struct W8GDSurface;

struct W8OctBuildNode {

    std::array<std::unique_ptr<W8OctBuildNode>, 8> children;
    std::array<std::vector<void*>, 2> surfaces;
};

/* Original owner: Engine Code\OctBuildTree.cpp. The original class spelling
   is unknown. Non-polymorphic like runtime W8Octree: neither the
   constructor at 0x00446390 nor the destructor at 0x004466D0 stores a vptr;
   the vtables emitted near this TU belong to vector/template material, not to
   this type. */
struct W8OctBuildTree {
    W8OctBuildTree(float leaf_size, std::optional<srBoundingBox> bounds, unsigned short item_limit,
                   short extent_mode);
    ~W8OctBuildTree();

    unsigned char InsertSurface(W8GDSurface* surface, w8_ulong mode);
    unsigned char InsertSurfaceRecursive(W8OctSpatialState* working, W8GDSurface* surface,
                                         const glm::vec3& planeNormal, w8_ulong mode);
    std::vector<W8GDSurface*> queryAlongSegment(const glm::vec3& origin, const glm::vec3& delta,
                                                float extent, unsigned short kind);
    // Append a polygon or physical surface in insertion order.
    void AppendLink(W8OctBuildNode* node, void* payload, short kind);
    W8OctSpatialState spatial;
    std::unique_ptr<W8OctBuildNode> root;

private:
    struct SurfaceCollector;
    void collectRecursive(const W8OctSpatialState& state, const srBoundingBox& bounds, short kind,
                          SurfaceCollector& collector) const;
    void collectLeaf(W8OctBuildNode* node, short depth, short kind,
                     SurfaceCollector& collector) const;
};

extern const float g_octree_cell_extent_scale;

#endif
