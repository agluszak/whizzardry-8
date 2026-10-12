#ifndef WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H
#define WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H

#include <cstdint>
#include <optional>

#include <deque>
#include <vector>
#include "glm/ext/vector_float3.hpp"
#include "surrender/srBoundingBox.h"

#include "wiz8/engine_code/OctPreTree.h"
#include "compat/compiler.h"

struct W8GDSurface;
template <class T> class srVector3T;

struct W8OctBuildLink {
    void* surface; /* region polygons and GD surfaces share the build links */
    W8OctBuildLink* next;
};

class OctPreTree;

struct W8OctBuildNode {
    W8OctBuildNode();
    ~W8OctBuildNode();

    bool RearrangeNodePolys(std::int16_t current_depth, std::int16_t target_depth);
    std::vector<void*> linkedSurfaces(std::int16_t current_depth, std::int16_t target_depth, std::int16_t mode) const;
    std::vector<void*> storedSurfaces(std::int16_t mode) const;
    std::uint32_t ConvertToOctPreTree(std::uint16_t depth, OctPreTree* tree);

    union {
        W8OctBuildNode* children[8];
        W8OctBuildLink* links[8];
        void** surface_arrays[8]; /* elements follow the insert mode */
        std::uint16_t* region_arrays[8];
    };
    std::uint16_t region;
    std::uint16_t leaf_kind;
    std::uint16_t provisional_region;
    std::uint16_t positional;
};

/* A zero-storage node variant with independently evidenced behavior: its
   constructor at 0x004AF760 increments the live-node counter after invoking
   the ordinary node constructor.  Its original spelling and source owner are
   still unresolved. */
struct W8CountedOctBuildNode : W8OctBuildNode {
    W8CountedOctBuildNode();
    ~W8CountedOctBuildNode();
};

/* Original owner: Engine Code\OctBuildTree.cpp. The original class spelling
   is unknown. Non-polymorphic like runtime W8Octree: neither the
   constructor at 0x00446390 nor the destructor at 0x004466D0 stores a vptr;
   the vtables emitted near this TU belong to vector/template material, not to
   this type. */
struct W8OctBuildTree {
    W8OctBuildTree(float leaf_size, std::optional<srBoundingBox> bounds,
                   std::uint16_t item_limit, std::int16_t extent_mode);
    ~W8OctBuildTree();

    std::uint8_t InsertSurface(W8GDSurface* surface, std::uint32_t mode);
    std::uint8_t InsertSurfaceRecursive(W8OctSpatialState* working, W8GDSurface* surface,
                                         const glm::vec3& planeNormal, std::uint32_t mode);
    std::vector<W8GDSurface*> queryAlongSegment(const glm::vec3& origin,
                                               const glm::vec3& delta, float extent,
                                               std::uint16_t kind);
    /* Append `payload` to the node's `kind` link list, growing the tail and
       raising the leaf counter plus the tree's deepest-list watermark. */
    void AppendLink(W8OctBuildNode* node, void* payload, std::int16_t kind);
    W8OctSpatialState spatial;
    /* Deque appends preserve the addresses stored in node link chains. */
    std::deque<W8OctBuildLink> links;
    std::uint32_t leaf_polygon_count;
    std::uint32_t gd_surface_count;
    std::uint32_t leaf_count;
    std::uint16_t max_leaf_regions;
    std::uint16_t unknown_ae;
    std::uint32_t region_assignments;
    bool use_owned_nodes;
    std::uint8_t unknown_b5[3];
    std::uint32_t deepest_link_list;

private:
    struct SurfaceCollector;
    void collectRecursive(const W8OctSpatialState& state, const srBoundingBox& bounds,
                          std::int16_t kind, SurfaceCollector& collector) const;
    void collectLeaf(W8OctBuildNode* node, std::int16_t depth, std::int16_t kind,
                     SurfaceCollector& collector) const;
};

extern const float g_octree_cell_extent_scale;

#endif
