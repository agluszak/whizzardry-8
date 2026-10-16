#include "wiz8/engine_code/OctBuildTree.h"

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

#include "OctPreTree.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "glm/common.hpp"
#include "glm/ext/vector_bool3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/swept_bounds.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/oct_build_traversal.h"
#include "wiz8/float_constants.h"

#define OCT_BUILD_TREE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\OctBuildTree.cpp"

// GLOBAL: WIZ8 0x005ec188
const float g_octree_cell_extent_scale = 1.000100016593933f;

// FUNCTION: WIZ8 0x00446330
W8OctBuildNode::W8OctBuildNode() : children{}, region(0), leaf_kind(0), provisional_region(0) {}

// FUNCTION: WIZ8 0x00446350
W8OctBuildNode::~W8OctBuildNode()
{
    if (leaf_kind != 0) {
        return;
    }
    for (W8OctBuildNode* child : children) {
        delete child;
    }
}

/* Build the cubic spatial domain used to insert processed GameData surfaces.
   The optional level bounds are expanded locally by half a leaf on every axis. */
// FUNCTION: WIZ8 0x00446390
W8OctBuildTree::W8OctBuildTree(float leaf_size, std::optional<srBoundingBox> bounds,
                               unsigned short item_limit, short extent_mode)
    : spatial(0)
{
    spatial.Reset();
    leaf_polygon_count = 0;
    gd_surface_count = 0;
    leaf_count = 0;
    max_leaf_regions = 0;
    unknown_ae = 0;
    region_assignments = 0;
    use_owned_nodes = false;
    unknown_b5[0] = 0;
    unknown_b5[1] = 0;
    unknown_b5[2] = 0;
    deepest_link_list = 0;

    if (leaf_size < g_float_one_thousand) {
        ReportBuildStatus(7, "Leaf Size too small--try a larger leaf size!\n");
    }
    spatial.flags = 0;
    spatial.m_item_limit = item_limit;
    spatial.m_level_kind = 2;
    spatial.m_extent = 0.0f;
    spatial.m_root = 0;
    spatial.m_triangle_vertices = 0;
    spatial.m_depth = 0;

    if (bounds) {
        const float half_leaf = leaf_size * g_float_half;
        bounds->minimum -= half_leaf;
        bounds->maximum += half_leaf;
        spatial.m_bounds = *bounds;
        spatial.m_clipped_bounds = *bounds;
        spatial.m_working_bounds = *bounds;
        const auto dimensions = bounds->maximum - bounds->minimum;
        spatial.m_extent = std::max({0.0f, dimensions.x, dimensions.y, dimensions.z});

        spatial.m_depth = 0;
        if (extent_mode == 0) {
            spatial.m_node_extent = spatial.m_extent;
            while (leaf_size + leaf_size <= spatial.m_node_extent && spatial.m_depth < 6) {
                spatial.m_node_extent *= g_float_half;
                ++spatial.m_depth;
            }
        } else if (extent_mode == 1) {
            spatial.m_node_extent = leaf_size;
            spatial.m_cell_size = leaf_size;
            while (spatial.m_cell_size < spatial.m_extent) {
                if (spatial.m_depth > 6) {
                    break;
                }
                spatial.m_cell_size += spatial.m_cell_size;
                ++spatial.m_depth;
            }
            if (spatial.m_depth > 6) {
                ReportBuildStatus(7, "Leaf Size too small--try a larger leaf size!\n");
            }
            spatial.m_extent = spatial.m_cell_size;
        } else {
            spatial.m_node_extent = spatial.m_extent;
            while (leaf_size + leaf_size <= spatial.m_node_extent && spatial.m_depth < 6) {
                spatial.m_node_extent *= g_float_half;
                ++spatial.m_depth;
            }
            if (extent_mode == 2) {
                float doubled = spatial.m_node_extent + spatial.m_node_extent;
                if (doubled - leaf_size < leaf_size - spatial.m_node_extent) {
                    --spatial.m_depth;
                    spatial.m_node_extent = doubled;
                }
            }
        }

        spatial.m_cell_size = spatial.m_node_extent * g_octree_cell_extent_scale;
        spatial.m_bounds.maximum = bounds->minimum + spatial.m_extent;
        spatial.m_polygon_count = 1;
        spatial.m_item_count = 0;
        spatial.m_root = 0;
        spatial.m_triangle_vertices = 0;
    }
}

/* The tree owns its nodes and stable link storage. Queries own their results. */
// FUNCTION: WIZ8 0x004466d0
W8OctBuildTree::~W8OctBuildTree()
{
    delete spatial.m_root;
    spatial.m_triangle_vertices = nullptr;
}

/* Recursive node teardown: internal nodes delete the eight children, leaf
   nodes (leaf_kind != 0) clear the ten link/array slots. */

/* Reject triangles outside the build domain, lazily create the root node, and
   then hand the complete typed working record to the recursive inserter. */
// FUNCTION: WIZ8 0x00446820
unsigned char W8OctBuildTree::InsertSurface(W8GDSurface* surface, w8_ulong mode)
{
    W8OctSpatialState working(&spatial);
    // Only physical surfaces have a vertex table in this builder.
    if (static_cast<short>(mode) != 3) return 0;
    const auto triangle = surfaceTriangle(surface->vertex_indices);
    if (!triangle) return 0;
    const auto normal = surface->plane.normal;
    if (!triangleIntersectsBox(spatial.bounds(), *triangle, normal)) return 0;

    if (spatial.m_root == 0) {
        if (!use_owned_nodes) {
            spatial.m_root = new W8OctBuildNode;
        } else {
            spatial.m_root = new W8CountedOctBuildNode;
        }
    }
    working.m_root = spatial.m_root;
    working.m_triangle_vertices = triangle->data();
    working.m_depth = 0;
    working.m_level_kind = 1;
    if (InsertSurfaceRecursive(&working, surface, normal, mode) == 0) {
        return 0;
    }
    ++spatial.m_item_count;
    return 1;
}

/* Descend through every overlapping octant.  Branch nodes own child nodes;
   leaf nodes reuse the same eight slots as per-mode linked-list heads. */
// FUNCTION: WIZ8 0x004469f0
unsigned char W8OctBuildTree::InsertSurfaceRecursive(W8OctSpatialState* working,
                                                     W8GDSurface* surface,
                                                     const glm::vec3& planeNormal, w8_ulong mode)
{
    W8OctSpatialState child(working);
    bool inserted = false;

    if (spatial.m_depth < working->m_depth) {
        spatial.m_depth = working->m_depth;
    }

    if (working->m_extent <= working->m_cell_size) {
        W8OctBuildNode* node = working->m_root;
        AppendLink(node, surface, static_cast<short>(mode));
        inserted = true;
    } else {
        float half_extent = working->m_extent * g_float_half;
        child.m_cell_size = working->m_cell_size;
        child.m_depth = working->m_depth + 1;

        inserted = InsertBuildSurfaceChildren(
            working, child, half_extent,
            [&](W8OctSpatialState& candidate) {
                return triangleIntersectsBox(
                    candidate.bounds(),
                    std::span<const glm::vec3, 3>(working->m_triangle_vertices, 3), planeNormal);
            },
            [&]() -> W8OctBuildNode* {
                if (use_owned_nodes) return new W8CountedOctBuildNode;
                return new W8OctBuildNode;
            },
            [&](W8OctSpatialState& candidate) {
                candidate.m_triangle_vertices = working->m_triangle_vertices;
                return InsertSurfaceRecursive(&candidate, surface, planeNormal, mode);
            });
    }
    return inserted;
}

/* Append `payload` to the node's `kind` link list: bump the leaf counter and
   the tree watermark, then either extend the tail or seed the head. */
// FUNCTION: WIZ8 0x00446d00
void W8OctBuildTree::AppendLink(W8OctBuildNode* node, void* payload, short kind)
{
    ++node->leaf_kind;
    if (deepest_link_list < node->leaf_kind) {
        deepest_link_list = node->leaf_kind;
    }
    W8OctBuildLink* link = &links.emplace_back(W8OctBuildLink{payload, nullptr});
    W8OctBuildLink* head = node->links[kind];
    if (head == nullptr) {
        node->links[kind] = link;
        return;
    }
    while (head->next != nullptr) {
        head = head->next;
    }
    head->next = link;
}

/* A query owns both ordered results and its membership index. Visit flags
   retain the kind-3/list-3 filter, and are cleared even if allocation throws. */
struct W8OctBuildTree::SurfaceCollector {
    std::vector<W8GDSurface*> results;
    std::unordered_set<W8GDSurface*> seen;

    ~SurfaceCollector() { clearVisited(); }

    void clearVisited() const
    {
        for (auto* surface : results) surface->flags &= ~W8_GD_SURFACE_QUERY_VISITED;
    }

    void append(W8GDSurface& surface, bool mark)
    {
        if (mark) {
            if ((surface.flags & W8_GD_SURFACE_QUERY_VISITED) != 0) return;
            // A marked reference after an unmarked list remains a separate
            // result; the original collector uses the flag for this path.
            seen.insert(&surface);
        } else if (!seen.insert(&surface).second) {
            return;
        }
        results.push_back(&surface);
        if (mark) surface.flags |= W8_GD_SURFACE_QUERY_VISITED;
    }
};

/* Preserve the corner test's mixed bounds: query maxima are exclusive for
   tree corners, while the fallback checks query corners inclusively. At a
   leaf, an inside first corner retains the whole-leaf classification. */
static int classifyBuildBox(const srBoundingBox& box, const srBoundingBox& bounds, bool leaf)
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

// FUNCTION: WIZ8 0x00446d80
std::vector<W8GDSurface*> W8OctBuildTree::queryAlongSegment(const glm::vec3& origin,
                                                            const glm::vec3& delta, float extent,
                                                            unsigned short kind)
{
    SurfaceCollector collector;
    if (spatial.m_root) {
        W8OctSpatialState state(&spatial);
        state.m_depth = 0;
        state.m_level_kind = 1;
        collectRecursive(state, srSegmentMath::sweptBounds(origin, delta, extent),
                         static_cast<short>(kind), collector);
    }
    collector.clearVisited();
    return std::move(collector.results);
}

// FUNCTION: WIZ8 0x00446f20
void W8OctBuildTree::collectRecursive(const W8OctSpatialState& state, const srBoundingBox& bounds,
                                      short kind, SurfaceCollector& collector) const
{
    const bool leaf = state.m_depth == spatial.m_depth;
    const int verdict = classifyBuildBox(state.bounds(), bounds, leaf);
    if (verdict == 2 || (verdict == 1 && leaf)) {
        collectLeaf(state.m_root, state.m_depth, kind, collector);
    } else if (verdict == 1) {
        W8OctSpatialState child(&state);
        visitBuildChildren(state, child, [&](W8OctSpatialState& candidate, unsigned) {
            collectRecursive(candidate, bounds, kind, collector);
        });
    }
}

// FUNCTION: WIZ8 0x00447110
void W8OctBuildTree::collectLeaf(W8OctBuildNode* node, short depth, short kind,
                                 SurfaceCollector& collector) const
{
    if (depth != spatial.m_depth) {
        for (auto* child : node->children)
            if (child) collectLeaf(child, depth + 1, kind, collector);
        return;
    }
    const auto collect_links = [&](int list, bool mark) {
        for (auto* link = node->links[list]; link; link = link->next)
            collector.append(*static_cast<W8GDSurface*>(link->surface), mark);
    };
    if (kind == 9) {
        collect_links(3, true);
        collect_links(4, false);
    } else if (kind == 10) {
        collect_links(7, false);
        // No producer builds an eleventh list. The old second head aliased
        // provisional region metadata; it cannot be a native link pointer.
    } else if (kind >= 0 && kind < 8) {
        collect_links(kind, kind == 3);
    }
}
