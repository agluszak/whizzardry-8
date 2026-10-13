#ifndef WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H
#define WIZ8_ENGINE_CODE_OCT_BUILD_TREE_H

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
    W8OctBuildNode(const W8OctBuildNode&) = default;
    W8OctBuildNode& operator=(const W8OctBuildNode&) = default;

    unsigned char RearrangeNodePolys(short current_depth, short target_depth);
    std::vector<void*> linkedSurfaces(short current_depth, short target_depth, short mode) const;
    std::vector<void*> storedSurfaces(short mode) const;
    w8_ulong ConvertToOctPreTree(unsigned short depth, OctPreTree* tree);

    union {
        W8OctBuildNode* children[8];
        W8OctBuildLink* links[8];
        void** surface_arrays[8]; /* elements follow the insert mode */
        unsigned short* region_arrays[8];
    };
    unsigned short region;
    unsigned short leaf_kind;
    unsigned short provisional_region;
    unsigned short positional;
};

/* A zero-storage node variant with independently evidenced behavior: its
   constructor at 0x004AF760 increments the live-node counter after invoking
   the ordinary node constructor.  Its original spelling and source owner are
   still unresolved. */
struct W8CountedOctBuildNode : W8OctBuildNode {
    W8CountedOctBuildNode();
    ~W8CountedOctBuildNode();
    W8CountedOctBuildNode(const W8CountedOctBuildNode&) = default;
    W8CountedOctBuildNode& operator=(const W8CountedOctBuildNode&) = default;
};

/* Original owner: Engine Code\OctBuildTree.cpp. The original class spelling
   is unknown. Non-polymorphic like runtime W8Octree: neither the
   constructor at 0x00446390 nor the destructor at 0x004466D0 stores a vptr;
   the vtables emitted near this TU belong to vector/template material, not to
   this type. */
struct W8OctBuildTree {
    W8OctBuildTree(float leaf_size, std::optional<srBoundingBox> bounds,
                   unsigned short item_limit, short extent_mode);
    ~W8OctBuildTree();
    W8OctBuildTree(const W8OctBuildTree&) = default;
    W8OctBuildTree& operator=(const W8OctBuildTree&) = default;

    unsigned char InsertSurface(W8GDSurface* surface, w8_ulong mode);
    unsigned char InsertSurfaceRecursive(W8OctSpatialState* working, W8GDSurface* surface,
                                         const glm::vec3& planeNormal, w8_ulong mode);
    std::vector<W8GDSurface*> queryAlongSegment(const glm::vec3& origin,
                                               const glm::vec3& delta, float extent,
                                               unsigned short kind);
    /* Append `payload` to the node's `kind` link list, growing the tail and
       raising the leaf counter plus the tree's deepest-list watermark. */
    void AppendLink(W8OctBuildNode* node, void* payload, short kind);
    W8OctSpatialState spatial;
    /* Deque appends preserve the addresses stored in node link chains. */
    std::deque<W8OctBuildLink> links;
    w8_ulong leaf_polygon_count;
    w8_ulong gd_surface_count;
    w8_ulong leaf_count;
    unsigned short max_leaf_regions;
    unsigned short unknown_ae;
    w8_ulong region_assignments;
    bool use_owned_nodes;
    unsigned char unknown_b5[3];
    w8_ulong deepest_link_list;

private:
    struct SurfaceCollector;
    void collectRecursive(const W8OctSpatialState& state, const srBoundingBox& bounds,
                          short kind, SurfaceCollector& collector) const;
    void collectLeaf(W8OctBuildNode* node, short depth, short kind,
                     SurfaceCollector& collector) const;
};

extern const float g_octree_cell_extent_scale;

#endif
