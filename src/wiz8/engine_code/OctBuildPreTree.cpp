#include "wiz8/engine_code/OctBuildPreTree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <wiz8/filesystem.h>

#include "OctBuildTree.h"
#include "OctPreTree.h"
#include "ReadMesh.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "index_algorithms.h"
#include "surrender/srMath.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/geometry_repack.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/mesh_links.h"
#include "wiz8/engine_code/oct_build_traversal.h"
#include "wiz8/float_constants.h"

// GLOBAL: WIZ8 0x0065be60
int g_build_node_instances;

// GLOBAL: WIZ8 0x005ed034
const float g_region_boundary_tolerance = -0.009999999776482582f;
// GLOBAL: WIZ8 0x005ed038
const float g_region_radius_limit = 4000.0f;
// GLOBAL: WIZ8 0x005ec52c
const float g_float_three = 3.0f;

#define OCT_BUILD_PRE_TREE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\OctBuildPreTree.cpp"

/* This zero-storage node form is constructed by OctBuildTree when its
   pre-tree ownership mode is active.  Its immediately following conversion
   methods and the first assertion-backed boundary at 0x004B19F0 establish
   Engine Code\OctBuildPreTree.cpp as the owning cluster. */
// FUNCTION: WIZ8 0x004af760
W8CountedOctBuildNode::W8CountedOctBuildNode()
{
    ++g_build_node_instances;
}

// FUNCTION: WIZ8 0x004af780
W8CountedOctBuildNode::~W8CountedOctBuildNode()
{
    free(children[1]);
    if (g_build_node_instances != 0) {
        --g_build_node_instances;
    }
}

/* Replace the two polygon-link chains used by pre-tree leaves with compact,
   null-terminated surface arrays. */
// FUNCTION: WIZ8 0x004af7b0
unsigned char W8OctBuildNode::RearrangeNodePolys(short current_depth, short target_depth)
{
    if (current_depth == target_depth) {
        for (short mode = 2; mode < 4; ++mode) {
            if (leaf_kind != 0 && links[mode] != 0) {
                const auto collected = linkedSurfaces(current_depth, target_depth, mode);
                void** surfaces =
                    static_cast<void**>(malloc((collected.size() + 1) * sizeof(void*)));
                if (surfaces == 0) {
                    ReportBuildStatus(7, "RearrangeNodePolys: Could not allocate poly list.\n");
                    return 0;
                }
                w8_ulong index;
                for (index = 0; index < collected.size(); ++index) {
                    surfaces[index] = collected[index];
                }
                surfaces[index] = 0;
                surface_arrays[mode] = surfaces;
            }
        }
    } else {
        for (int child = 0; child != 8; ++child) {
            if (children[child] != 0) {
                children[child]->RearrangeNodePolys(current_depth + 1, target_depth);
            }
        }
    }
    return 1;
}

namespace {
// The first appearance of each surface determines its emitted order.
template<class Stop, class Append>
std::vector<void*> collectBuildSurfaces(const W8OctBuildNode& root, Stop stop, Append append)
{
    std::vector<void*> result;
    std::unordered_set<void*> seen;
    const auto emit = [&](void* surface) {
        if (seen.insert(surface).second) result.push_back(surface);
    };
    const auto visit = [&](auto&& self, const W8OctBuildNode& node, unsigned depth) -> void {
        if (stop(node, depth)) {
            append(node, emit);
            return;
        }
        for (const auto* child : node.children)
            if (child) self(self, *child, depth + 1);
    };
    visit(visit, root, 0);
    return result;
}

struct PackedSurfaceRun {
    w8_ulong offset;
    w8_ulong end;
};

template<class SurfaceIndex>
PackedSurfaceRun packLeafSurfaces(W8OctBuildNode& node, short mode, w8_ulong* stream,
                                  w8_ulong cursor, SurfaceIndex index)
{
    const auto surfaces = node.storedSurfaces(mode);
    const w8_ulong offset = surfaces.empty() ? 0 : cursor;
    if (!surfaces.empty()) {
        stream[cursor++] = static_cast<w8_ulong>(surfaces.size());
        for (auto* surface : surfaces) stream[cursor++] = index(surface);
    }
    free(node.surface_arrays[mode]);
    node.surface_arrays[mode] = nullptr;
    return {offset, cursor};
}
}

// FUNCTION: WIZ8 0x004af8f0
std::vector<void*> W8OctBuildNode::linkedSurfaces(short current_depth, short target_depth,
                                                  short mode) const
{
    if (mode < 2 || mode > 3 || current_depth > target_depth) return {};
    return collectBuildSurfaces(
        *this,
        [remaining = target_depth - current_depth](const W8OctBuildNode&, unsigned depth) {
            return depth == static_cast<unsigned>(remaining);
        },
        [mode](const W8OctBuildNode& node, auto emit) {
            for (auto* link = node.links[mode]; link; link = link->next) emit(link->surface);
        });
}

// FUNCTION: WIZ8 0x004af9b0
std::vector<void*> W8OctBuildNode::storedSurfaces(short mode) const
{
    if (mode < 2 || mode > 3) return {};
    return collectBuildSurfaces(
        *this,
        [](const W8OctBuildNode& node, unsigned) {
            return node.leaf_kind != 0 && node.provisional_region == 0;
        },
        [mode](const W8OctBuildNode& node, auto emit) {
            if (auto** surfaces = node.surface_arrays[mode])
                for (; *surfaces; ++surfaces) emit(*surfaces);
        });
}

/* Consume the counted build nodes into the compact arrays owned by the
   runtime pre-tree.  Leaf polygon pointers are deduplicated before their
   persistent surface indices are appended; branch children are converted and
   destroyed as soon as their compact indices have been recorded. */
// FUNCTION: WIZ8 0x004afa30
w8_ulong W8OctBuildNode::ConvertToOctPreTree(unsigned short depth, OctPreTree* tree)
{
    w8_ulong node_index;

    if (depth == tree->m_spatial.m_depth) {
        node_index = tree->m_leaf_count++;

        const auto polygons = packLeafSurfaces(
            *this, 2, tree->m_polygon_index_stream.data(), tree->polygon_cursor,
            [](void* surface) { return static_cast<W8OctRegionPolygon*>(surface)->ordinal; });
        tree->m_leaves[node_index].polygon_offset = polygons.offset;
        tree->polygon_cursor = polygons.end;
        const auto terrain = packLeafSurfaces(
            *this, 3, tree->m_gd_surface_index_stream.data(), tree->m_gd_surface_stream_len,
            [](void* surface) { return static_cast<W8GDSurface*>(surface)->index; });
        tree->m_leaves[node_index].gd_polygon_offset = terrain.offset;
        tree->m_gd_surface_stream_len = terrain.end;

        unsigned short* regions = region_arrays[1];
        if (regions != 0 && *regions != 0) {
            tree->m_leaves[node_index].region_offset = tree->m_region_list_len;
            while (*regions != 0) {
                tree->m_region_index_stream[tree->m_region_list_len++] = *regions++;
            }
            tree->m_region_index_stream[tree->m_region_list_len++] = 0;
        }
        if (region_arrays[1] != 0) {
            free(region_arrays[1]);
            region_arrays[1] = 0;
        }
    } else {
        node_index = tree->m_branch_count++;
        for (w8_ulong child = 0; child < 8; ++child) {
            if (children[child] != 0) {
                tree->m_branches[node_index].children[child] =
                    children[child]->ConvertToOctPreTree(depth + 1, tree);
                delete static_cast<W8CountedOctBuildNode*>(children[child]);
                children[child] = 0;
            }
        }
        tree->m_branches[node_index].region = region;
        tree->m_branches[node_index].provisional_region = provisional_region;
    }
    return node_index;
}

/* Extend the surface build tree with the storage consumed by the destructive
   pre-tree conversion pass. */
// FUNCTION: WIZ8 0x004afda0
OctBuildPreTree::OctBuildPreTree(float leaf_size, std::optional<srBoundingBox> bounds,
                                 unsigned short item_limit, w8_ulong path_capacity,
                                 short extent_mode)
    : W8OctBuildTree(leaf_size, bounds, item_limit, extent_mode)
{
    mesh_linking = true;
    use_owned_nodes = true;
    game_data = 0;
    unknown_138 = 0;
    unknown_13c = 0;
    deepest_link_list = 0;
    memset(level_counts, 0, sizeof(level_counts));
    this->path_capacity = path_capacity;
    selected_depth = 0;
    region_remap = 0;

    particle_count = 0;
}

/* Deduplicate and repack the shared geometry: weld-chain every flagged vertex
   to its canonical copy, drop unused vertices and degenerate polygons, rebuild
   both arrays, then re-insert every surviving polygon and continue into the
   region assignment pass. Normalizes each vertex normal on the way. */
// FUNCTION: WIZ8 0x004afea0
unsigned char OctBuildPreTree::SortGeometry(W8OctPreTreeGeometry* geometry)
{
    game_data = geometry;
    if (!geometry->compact()) {
        ReportBuildStatus(7, "SortGeometry: Could not allocate replacement geometry tables");
        return 0;
    }
    for (w8_ulong polygon = 1; polygon < geometry->m_polygon_count; ++polygon) {
        if (InsertSurface(&geometry->m_polygons[polygon], 2) == 0) {
            char message[1024];
            sprintf(message, "SortGeometry: Polygon %d cannot be inserted into tree",
                    static_cast<int>(polygon));
            ReportBuildStatus(7, message);
            return 0;
        }
    }
    return AssignPolygonRegions(geometry);
}

// Prepare every record and index before replacing either owned table.
bool W8OctPreTreeGeometry::compact()
{
    std::vector<W8OctPreTreeVertex> prepared(std::max(w8_ulong{1}, vertex_count));
    std::ranges::copy(std::span(m_vertices, vertex_count), prepared.begin());
    for (auto& vertex : std::span(prepared).subspan(1)) vertex.m_normal_count = 0;
    for (const auto& polygon : std::span(m_polygons, m_polygon_count) | std::views::drop(1))
        if (!polygon.degenerate)
            for (const auto* vertex : polygon.vertices)
                ++prepared[vertex - m_vertices].m_normal_count;

    w8_ulong next = 1;
    for (auto& vertex : std::span(prepared).subspan(1)) {
        vertex.m_normal = normalizedOrZero(vertex.m_normal);
        if (!(vertex.flags & W8OctPreTreeVertex::EXCLUDED)) {
            if (!vertex.m_normal_count)
                vertex.flags |= W8OctPreTreeVertex::EXCLUDED;
            else
                vertex.m_vertex_index = next++;
        } else {
            // Weld redirects are resolved in original source order.
            vertex.m_vertex_index = prepared[vertex.m_vertex_index].m_vertex_index;
        }
    }
    const auto vertex_records =
        geometryRepack::compactTable<W8OctPreTreeVertex>(prepared, [](const auto& vertex) {
            return !(vertex.flags & W8OctPreTreeVertex::EXCLUDED);
        });
    auto polygon_records = geometryRepack::compactTable<W8OctRegionPolygon>(
        {m_polygons, m_polygon_count}, [](const auto& polygon) { return !polygon.degenerate; });
    std::vector<geometryRepack::CornerIndices> corners(polygon_records.size());
    for (w8_ulong face = 1; face < polygon_records.size(); ++face) {
        auto& polygon = polygon_records[face];
        polygon.ordinal = face;
        for (unsigned corner = 0; corner < 3; ++corner) {
            const auto index = prepared[polygon.vertices[corner] - m_vertices].m_vertex_index;
            corners[face][corner] = index;
            polygon.face.vertices[corner] = index;
        }
    }
    auto vertices = geometryRepack::copyTable<W8OctPreTreeVertex>(vertex_records);
    auto polygons = geometryRepack::copyTable<W8OctRegionPolygon>(polygon_records);
    if (!vertices || !polygons) return false;
    geometryRepack::repointCorners({polygons.get(), polygon_records.size()}, corners,
                                   vertices.get());
    std::free(m_vertices);
    std::free(m_polygons);
    m_vertices = vertices.release();
    m_polygons = polygons.release();
    vertex_count = static_cast<w8_ulong>(vertex_records.size());
    m_polygon_count = static_cast<w8_ulong>(polygon_records.size());
    return true;
}

/* Seed a temporary root when the build tree is still empty, account for the
   inserted polygon kind, and route the region polygon through the recursive
   inserter. Unlike UpdateRegionForGeometry the root here is the plain node;
   the mode counter mirrors that function's 2/3 split. */
// FUNCTION: WIZ8 0x004b02f0
unsigned char OctBuildPreTree::InsertSurface(W8OctRegionPolygon* polygon, w8_ulong mode)
{
    W8OctSpatialState working(&spatial);
    if (spatial.m_root == 0) {
        working.m_root = new W8OctBuildNode;
        spatial.m_root = working.m_root;
    }
    if (static_cast<short>(mode) == 2) {
        ++spatial.m_polygon_count;
    } else if (static_cast<short>(mode) == 3) {
        ++spatial.m_item_count;
    }
    working.m_depth = 0;
    working.m_level_kind = 1;
    return InsertSurfaceRecursive(&working, polygon, mode);
}

/* Descend the build octree to the leaf holding the region polygon: subdivide
   while the working depth sits above the tree's bottom level, creating child
   nodes on demand and counting them per level. At the leaf, a first-time node
   reports progress and collects its overlapping region ids; every leaf bumps
   its kind counter and appends the polygon to the mode link list. Depth 16 is
   the hard floor and returns failure. */
// FUNCTION: WIZ8 0x004b03e0
unsigned char OctBuildPreTree::InsertSurfaceRecursive(W8OctSpatialState* working,
                                                      W8OctRegionPolygon* polygon, w8_ulong mode)
{
    W8OctSpatialState child(working);
    bool inserted = false;

    if (working->m_depth < 0x10) {
        if (working->m_depth < spatial.m_depth) {
            const RuntimeTriangle vertices{polygon->vertices[0]->position,
                                           polygon->vertices[1]->position,
                                           polygon->vertices[2]->position};
            inserted = InsertBuildSurfaceChildren(
                working, child, child.m_extent,
                [&](W8OctSpatialState& candidate) {
                    return triangleIntersectsBox(candidate.bounds(), vertices,
                                                 polygon->plane.normal.glmValue());
                },
                [&]() {
                    ++level_counts[working->m_depth];
                    return new W8OctBuildNode;
                },
                [&](W8OctSpatialState& candidate) {
                    return InsertSurfaceRecursive(&candidate, polygon, mode);
                });
        } else {
            W8OctBuildNode* node = working->m_root;
            if (node->leaf_kind == 0) {
                ReportBuildStatus(2, {});
                ++leaf_count;
                W8BoundingBox leaf_bounds;
                leaf_bounds.minimum = working->m_bounds.minimum;
                leaf_bounds.maximum = working->m_bounds.maximum;
                FindLeafRegions(node, &leaf_bounds);
            }
            ++node->leaf_kind;
            if (static_cast<short>(mode) == 2) {
                ++leaf_polygon_count;
            } else if (static_cast<short>(mode) == 3) {
                ++gd_surface_count;
            }
            AppendLink(node, polygon, static_cast<short>(mode));
            inserted = true;
        }
    }
    return inserted;
}

/* Region geometry is a point (particle membership) or a box (prop overlap).
   The query type selects its map and intersection rule throughout recursion. */
// FUNCTION: WIZ8 0x004b06e0
bool OctBuildPreTree::updateRegionForGeometry(const RegionGeometry& geometry, short value)
{
    W8OctSpatialState working(&spatial);
    if (!spatial.m_root) {
        working.m_root = new W8CountedOctBuildNode;
        spatial.m_root = working.m_root;
    }
    working.m_depth = 0;
    working.m_level_kind = 1;
    return updateRegionMap(working, geometry, value);
}

// FUNCTION: WIZ8 0x004b07e0
bool OctBuildPreTree::updateRegionMap(const W8OctSpatialState& state,
                                      const RegionGeometry& geometry, short value)
{
    if (state.m_depth >= 16) return false;
    bool changed = false;
    const auto* point = std::get_if<glm::vec3>(&geometry);
    auto& map = point ? particle_regions.regions : prop_regions.regions;
    const auto updateRegion = [&](unsigned short region) {
        if (region == 0) return;
        RemoveIndexValue(map, region, value);
        InsertIndexValue(map, region, value);
        changed = true;
    };
    if (state.m_depth < spatial.m_depth) {
        W8OctSpatialState child(&state);
        visitBuildChildren(state, child, [&](W8OctSpatialState& candidate, unsigned) {
            const auto box = candidate.bounds();
            const bool intersects = point ? box.containsHalfOpen(*point)
                                          : box.overlapsStrict(std::get<srBoundingBox>(geometry));
            if (!intersects) return;
            updateRegion(candidate.m_root->region);
            if (updateRegionMap(candidate, geometry, value)) changed = true;
        });
    } else {
        updateRegion(state.m_root->region);
    }
    return changed;
}

#pragma pack(push, 1)
/* One 0x6a record in the .cub region file: a dword copied to the volume's
   +0x10, an unaligned dword copied to +0x18, and the eight frustum corners
   scaled into world units. */
struct W8CubRegionRecord {
    w8_ulong value_00;
    unsigned char pad_04[2];
    w8_ulong value_06;
    srVector3T<float> corners[8];
};
#pragma pack(pop)
static_assert(sizeof(W8CubRegionRecord) == 0x6a, "W8CubRegionRecord_must_be_0x6a");

/* Load the optional .cub region file beside the level: a version -5 header
   word precedes the region count, and each record supplies the eight frustum
   corners of one region volume, scaled by the world scale. The first point of
   each volume becomes the corner average, then the corners are sorted and the
   six frustum planes built. Answers the region bound, or zero on any read or
   allocation failure. */
// FUNCTION: WIZ8 0x004b0c90
unsigned short OctBuildPreTree::LoadRegionFile(const char* stem)
{
    spatial.m_region_volumes = nullptr;
    m_region_volumes.clear();
    spatial.m_region_count = 0;
    spatial.m_region_id_bound = 0;
    char path[1024];
    sprintf(path, "%s.cub", stem);
    std::unique_ptr<wiz8::File> file;
    try {
        file = wiz8::open_file(path);
    } catch (const std::exception&) {
        ReportBuildStatus(6, "\nWARNING: Could not find and\\or open region file.\n\n");
        return 0;
    }
    ReportBuildStatus(6, "\nReading Region File...\n");
    auto read = [&](void* data, std::size_t bytes) {
        try {
            return file->read(data, bytes).bytes == bytes;
        } catch (const std::exception&) {
            return false;
        }
    };
    int count;
    bool ok = read(&count, 4);
    if (ok == 0) {
        return 0;
    }
    if (count < 0) {
        if (count != -5) {
            ReportBuildStatus(7, "Wrong version for .cub file--get new plug-in!\n");
            return 0;
        }
        ok = read(&count, 4);
        if (ok == 0) {
            return 0;
        }
    }
    if (count <= 0 || count >= 0xffff) {
        return 0;
    }
    const auto bound = static_cast<unsigned short>(count + 1);
    std::vector<W8OctRegionVolume> volumes(bound + 1);
    for (unsigned short region = 1; region < bound; ++region) {
        W8CubRegionRecord record;
        ok = read(&record, sizeof(record));
        if (ok == 0) {
            return 0;
        }
        W8OctRegionVolume* volume = volumes.data() + region;
        volume->value_10 = record.value_00;
        volume->m_polygon_count = 0;
        volume->m_region = region;
        volume->m_region_bit = region;
        for (int corner = 0; corner != 8; ++corner) {
            volume->m_points[corner + 1] = record.corners[corner] * g_world_scale;
            volume->m_points[0] += record.corners[corner] * g_world_scale * 0.125f;
        }
        volume->value_18 = record.value_06;
        std::array<glm::vec3, 8> points;
        for (unsigned corner = 0; corner < points.size(); ++corner)
            points[corner] = volume->m_points[corner + 1].glmValue();
        const auto sorted = sortedFrustumCorners(points);
        for (unsigned corner = 0; corner < sorted.size(); ++corner)
            volume->m_points[corner + 1] = sorted[corner];
        const auto planes = BuildFrustumPlanes(sorted);
        std::ranges::copy(planes, volume->m_planes);
    }
    if (file) file->close();
    file.reset();
    ReportBuildStatus(6, path);
    m_region_volumes = std::move(volumes);
    spatial.m_region_volumes = m_region_volumes.data();
    spatial.m_region_id_bound = bound;
    spatial.m_region_count = bound;
    return spatial.m_region_id_bound;
}

/* Fill the leaf's region-id list with every enabled region volume overlapping
   `bounds`. The list is a malloc'd run of up to 0x32 ids terminated by a zero
   slot; the first allocation reports failure through the build log. */
// FUNCTION: WIZ8 0x004b1090
void OctBuildPreTree::FindLeafRegions(W8OctBuildNode* node, const W8BoundingBox* bounds)
{
    for (unsigned short region = 1; region < spatial.m_region_count; ++region) {
        if ((spatial.m_region_volumes[region].flags & 4) == 0 &&
            BoundsInsideFrustum(spatial.m_region_volumes[region],
                                {bounds->minimum.glmValue(), bounds->maximum.glmValue()})) {
            if (node->region_arrays[1] == 0) {
                unsigned short* list = static_cast<unsigned short*>(malloc(50 * sizeof(*list)));
                if (list == 0) {
                    ReportBuildStatus(7, "Could not allocate region list in FindLeafRegions.\n");
                    return;
                }
                for (int i = 0; i != 50; ++i) {
                    list[i] = 0;
                }
                node->region_arrays[1] = list;
            }
            unsigned short* list = node->region_arrays[1];
            unsigned short slot = 0;
            if (list[0] != 0) {
                do {
                    if (0x31 < slot) {
                        ReportBuildStatus(7, "Too many regions in FindLeafRegions.\n");
                        return;
                    }
                    ++slot;
                } while (list[slot] != 0);
            }
            list[slot] = region;
            if (max_leaf_regions < slot + 1) {
                max_leaf_regions = slot + 1;
            }
            ++region_assignments;
        }
    }
}

/* Assign a polygon's region: test its representative point against each
   region volume's frustum, then fall back to the corner vertices' assigned
   regions. Multiple hits mark the polygon shared; a single assignment counts
   on the volume's polygon total. */
// FUNCTION: WIZ8 0x004b1190
void OctBuildPreTree::AssignPolygonRegion(W8OctRegionPolygon* polygon)
{
    unsigned short hits = 0;
    if (spatial.m_region_volumes == 0) {
        return;
    }
    if (polygon->region != 0) {
        return;
    }
    unsigned short region = 1;
    if (1 < spatial.m_region_id_bound) {
        do {
            if (polygon->InsideFrustumPlanes(spatial.m_region_volumes[region].m_planes)) {
                ++hits;
                if (polygon->region == 0) {
                    polygon->region = region;
                }
                polygon->flags |= W8OctRegionPolygon::CENTER_IN_REGION;
            }
            ++region;
        } while (region < spatial.m_region_id_bound);
    }
    if (hits == 0) {
        for (int corner = 0; corner != 3; ++corner) {
            W8OctPreTreeVertex* vertex = polygon->vertices[corner];
            short vertex_region = vertex->m_region;
            if (vertex_region != 0 || (vertex->flags & W8OctPreTreeVertex::MULTIPLE_REGIONS) != 0) {
                ++hits;
                if (polygon->region == 0) {
                    polygon->region = vertex_region;
                }
                if ((vertex->flags & W8OctPreTreeVertex::MULTIPLE_REGIONS) != 0) {
                    polygon->flags |= W8OctRegionPolygon::MULTIPLE_REGIONS;
                }
            }
        }
    }
    if (1 < hits) {
        polygon->flags |= W8OctRegionPolygon::MULTIPLE_REGIONS;
    }
    if (polygon->region != 0 && (polygon->flags & W8OctRegionPolygon::MULTIPLE_REGIONS) == 0) {
        ++spatial.m_region_volumes[polygon->region].m_polygon_count;
    }
}

/* Region assignment pass run by SortGeometry: builds each vertex's
   polygon-reference run, assigns vertex regions from the region volumes that
   contain them, assigns polygon regions, resolves shared polygons, compacts
   emptied regions through the region_remap remap table and finishes in
   BuildRegions. */
// FUNCTION: WIZ8 0x004b1280
unsigned char OctBuildPreTree::AssignPolygonRegions(W8OctPreTreeGeometry* geometry)
{
    ReportBuildStatus(6, "Inserting polygons and vertices into regions...\n");
    geometry->m_max_face_count = 0;
    if (1 < geometry->vertex_count) {
        for (w8_ulong vertex = 1; vertex < geometry->vertex_count; ++vertex) {
            if (geometry->m_max_face_count < geometry->m_vertices[vertex].m_normal_count) {
                geometry->m_max_face_count = geometry->m_vertices[vertex].m_normal_count;
            }
        }
    }
    w8_ulong polygon;
    for (polygon = 1; polygon < geometry->m_polygon_count; ++polygon) {
        W8OctRegionPolygon* poly = &geometry->m_polygons[polygon];
        for (int corner = 0; corner != 3; ++corner) {
            W8OctPreTreeVertex* vertex =
                &geometry->m_vertices[poly->vertices[corner]->m_vertex_index];
            if (vertex->face_count == 65535 ||
                (vertex->face_count % 5 == 0 &&
                 !geometry->CheckArrayLength(&vertex->face_indices, vertex->face_count, 5))) {
                ReportBuildStatus(
                    7, "AssignPolygonRegions: Face run too long or allocation failed.\n");
                return 0;
            }
            vertex->face_indices[vertex->face_count] = polygon;
            ++vertex->face_count;
        }
    }
    if (spatial.m_region_volumes != 0) {
        w8_ulong vertex;
        for (vertex = 1; vertex < geometry->vertex_count; ++vertex) {
            W8OctPreTreeVertex* vert = &geometry->m_vertices[vertex];
            unsigned short hits = 0;
            for (unsigned short region = 1; region < spatial.m_region_id_bound; ++region) {
                if (PointInsideFrustum(vert->position, spatial.m_region_volumes[region].m_planes)) {
                    ++hits;
                    vert->m_region = region;
                }
            }
            if (1 < hits) {
                vert->flags |= W8OctPreTreeVertex::MULTIPLE_REGIONS;
                vert->m_region = 0;
            }
        }
        for (polygon = 1; polygon < geometry->m_polygon_count; ++polygon) {
            AssignPolygonRegion(&geometry->m_polygons[polygon]);
            geometry->m_polygons[polygon].visited = false;
        }
        for (polygon = 1; polygon < geometry->m_polygon_count; ++polygon) {
            W8OctRegionPolygon* poly = &geometry->m_polygons[polygon];
            if ((poly->flags & W8OctRegionPolygon::MULTIPLE_REGIONS) != 0) {
                SplitSharedPolygon(geometry, polygon);
                poly->flags &=
                    ~(W8OctRegionPolygon::MULTIPLE_REGIONS | W8OctRegionPolygon::CENTER_IN_REGION);
            }
        }
        unsigned short bound = spatial.m_region_id_bound;
        unsigned short region = 1;
        unsigned short next = 2;
        w8_ulong slot = 1;
        if (1 < bound) {
            do {
                if (spatial.m_region_volumes[slot].m_polygon_count == 0) {
                    if (region_remap == 0) {
                        region_remap = static_cast<unsigned short*>(
                            malloc(bound * sizeof(*region_remap) + sizeof(*region_remap)));
                        memset(region_remap, 0,
                               bound * sizeof(*region_remap) + sizeof(*region_remap));
                        for (unsigned short id = 0; id < spatial.m_region_count; ++id) {
                            region_remap[id] = id;
                        }
                    }
                    region_remap[slot] = 0;
                    for (unsigned short id = slot + 1; id < spatial.m_region_count; ++id) {
                        --region_remap[id];
                    }
                    for (unsigned short i = next; i < spatial.m_region_id_bound; ++i) {
                        spatial.m_region_volumes[i - 1] = spatial.m_region_volumes[i];
                        spatial.m_region_volumes[i - 1].m_region = i - 1;
                        spatial.m_region_volumes[i - 1].m_region_bit = i - 1;
                    }
                    for (polygon = 1; polygon < geometry->m_polygon_count; ++polygon) {
                        unsigned short* poly_region = &geometry->m_polygons[polygon].region;
                        if (region < *poly_region) {
                            --*poly_region;
                        }
                    }
                    for (vertex = 1; vertex < geometry->vertex_count; ++vertex) {
                        unsigned short* vert_region = &geometry->m_vertices[vertex].m_region;
                        if (region < *vert_region) {
                            --*vert_region;
                        }
                    }
                    --region;
                    --next;
                    --spatial.m_region_id_bound;
                }
                bound = spatial.m_region_id_bound;
                ++region;
                ++next;
                ++slot;
            } while (region < bound);
        }
    }
    BuildRegions();
    w8_ulong count = spatial.m_polygon_count;
    for (polygon = 1; polygon < count; ++polygon) {
        if (geometry->m_polygons[polygon].region == 0) {
            char message[256];
            sprintf(message, "Poly %d not found in ANY region.\n", static_cast<int>(polygon));
            ReportBuildStatus(6, message);
        }
        count = spatial.m_polygon_count;
    }
    return 1;
}

/* Rewrite every leaf's region list through the region_remap remap table,
   dropping ids that remap to zero and re-terminating the list. Called with a
   null node once the table is ready: it reruns against the real root and then
   frees the table. Depth counts down against the leaf level and never walks
   past 0x10. */
// FUNCTION: WIZ8 0x004b16b0
void OctBuildPreTree::RemapNodeRegions(W8OctBuildNode* node, int depth)
{
    if (node == 0) {
        if (region_remap != 0) {
            RemapNodeRegions(spatial.m_root, 0);
            free(region_remap);
            region_remap = 0;
        }
        return;
    }
    if (static_cast<short>(depth) < 0x10) {
        if (static_cast<short>(depth) < spatial.m_depth) {
            for (int child_index = 0; child_index != 8; ++child_index) {
                if (node->children[child_index] != 0) {
                    RemapNodeRegions(node->children[child_index], depth + 1);
                }
            }
            return;
        }
        unsigned short* list = node->region_arrays[1];
        if (list != 0) {
            int count = 0;
            int kept = 0;
            unsigned short* read = list;
            unsigned short* write = list;
            for (; *read != 0 && count < 0x32; ++count) {
                unsigned short region = region_remap[*read];
                *write = region;
                if (region != 0) {
                    ++kept;
                    ++write;
                }
                ++read;
            }
            list[kept] = 0;
        }
    }
}

/* Resolve a shared polygon (flags bit2): histogram the neighboring polygons'
   regions collected through the corner vertices' face runs, pick the most
   frequent region the polygon actually touches, assign it and count it on
   the volume. Answers the polygon's region; recurses into unvisited
   neighbors so shared regions propagate. */
// FUNCTION: WIZ8 0x004b1780
unsigned short OctBuildPreTree::SplitSharedPolygon(W8OctPreTreeGeometry* geometry, int index)
{
    W8OctRegionPolygon* polygon = &geometry->m_polygons[index];
    if ((polygon->flags & W8OctRegionPolygon::MULTIPLE_REGIONS) == 0) {
        return polygon->region;
    }
    unsigned short regions[20];
    unsigned short counts[20];
    memset(regions, 0, sizeof(regions));
    polygon->visited = true;
    memset(counts, 0, sizeof(counts));
    unsigned short found = 0;
    for (int corner = 0; corner != 3; ++corner) {
        W8OctPreTreeVertex* vertex = polygon->vertices[corner];
        if (!vertex->m_visited) {
            vertex->m_visited = true;
            int* face = vertex->face_indices;
            for (unsigned int n = vertex->face_count; n != 0; --n) {
                if (geometry->m_polygons[*face].visited == 0) {
                    unsigned short region = SplitSharedPolygon(geometry, *face);
                    if (region != 0) {
                        unsigned short slot;
                        for (slot = 0; slot < found; ++slot) {
                            if (regions[slot] == region) {
                                break;
                            }
                        }
                        if (slot == found) {
                            ++counts[found];
                            regions[found] = region;
                            ++found;
                        } else {
                            ++counts[slot];
                        }
                    }
                }
                ++face;
            }
            vertex->m_visited = false;
        }
    }
    for (unsigned short pass = 0; pass < found; ++pass) {
        for (unsigned short slot = pass; slot < found; ++slot) {
            if (counts[slot] < counts[slot + 1]) {
                unsigned short swap = counts[slot];
                counts[slot] = counts[slot + 1];
                counts[slot + 1] = swap;
                swap = regions[slot];
                regions[slot] = regions[slot + 1];
                regions[slot + 1] = swap;
            }
        }
    }
    for (unsigned short slot = 0; slot < found; ++slot) {
        unsigned short region = regions[slot];
        unsigned char inside;
        if ((polygon->flags & W8OctRegionPolygon::CENTER_IN_REGION) == 0) {
            inside = 0;
            for (int corner = 0; corner < 3; ++corner) {
                if (PointInsideFrustum(polygon->vertices[corner]->position,
                                       spatial.m_region_volumes[region].m_planes)) {
                    inside = 1;
                    break;
                }
            }
        } else {
            inside = polygon->InsideFrustumPlanes(spatial.m_region_volumes[region].m_planes);
        }
        if (inside != 0) {
            polygon->flags &=
                ~(W8OctRegionPolygon::MULTIPLE_REGIONS | W8OctRegionPolygon::CENTER_IN_REGION);
            polygon->region = region;
            ++spatial.m_region_volumes[region].m_polygon_count;
            slot = found;
        }
    }
    if ((polygon->flags & W8OctRegionPolygon::MULTIPLE_REGIONS) == 0) {
        polygon->visited = false;
    }
    return polygon->region;
}

/* Choose the region-tree depth that fits the requested path capacity, build
   the temporary spatial hierarchy, assign every discovered path to its build
   node, and then derive the persistent region metadata. */
// FUNCTION: WIZ8 0x004b19f0
unsigned short OctBuildPreTree::BuildRegions()
{
    W8OctSpatialState working(&spatial);

    if (spatial.m_region_id_bound == 0) {
        spatial.m_region_id_bound = 1;
    }
    spatial.m_region_count = spatial.m_region_id_bound;
    selected_depth = spatial.m_depth;
    region_paths.clear();

    while (path_capacity < level_counts[selected_depth]) {
        --selected_depth;
    }

    spatial.m_leaf_level = 0;
    int last_level = selected_depth - 1;
    float extent = spatial.m_extent;
    while (static_cast<int>(spatial.m_leaf_level) < last_level) {
        float next_extent = extent * g_float_half;
        if (fabs(spatial.m_region_grid_cell - extent) <
            fabs(spatial.m_region_grid_cell - next_extent)) {
            break;
        }
        ++spatial.m_leaf_level;
        extent = next_extent;
    }

    w8_ulong level_count = level_counts[spatial.m_leaf_level];
    region_paths.reserve(level_count + 2);
    region_centers.resize(level_count + 2 + spatial.m_region_id_bound);

    spatial.m_region_grid_cell = working.m_cell_size;
    region_path_map.clear();
    AssignInitialRegions(&working);

    for (const auto path : region_paths) {
        W8OctBuildNode* node = FindNode(path);
        if (node->leaf_kind != 0 && node->leaf_kind < 25) {
            MergeAdjacentRegion(node, path);
        }
    }

    for (w8_ulong polygon = 1; polygon < game_data->m_polygon_count; ++polygon) {
        if (game_data->m_polygons[polygon].region == 0) {
            char message[252];
            sprintf(message, "Poly %d not found in ANY region.\n", static_cast<int>(polygon));
            ReportBuildStatus(6, message);
        }
    }

    FinalizeRegionMapping();
    AssignRegionFromSurfaces(&working);
    ValidatePolygonRegions();

    region_centers.clear();

    if (spatial.m_region_count == 1) {
        spatial.m_region_count = 0;
    }
    return spatial.m_region_id_bound;
}

/* Assign a new region to every selected-depth cell containing an unclaimed
   polygon.  The packed path and center are retained for the later adjacency
   merge, while the farthest referenced vertex establishes the region radius
   used by that merge. */
// FUNCTION: WIZ8 0x004b1d90
void OctBuildPreTree::AssignInitialRegions(const W8OctSpatialState* arg_spatial)
{
    W8OctSpatialState child(arg_spatial);
    if (arg_spatial->m_depth >= 16) {
        return;
    }

    if (arg_spatial->m_depth == this->spatial.m_leaf_level) {
        W8OctBuildNode* node = arg_spatial->m_root;
        const auto surfaces = node->linkedSurfaces(spatial.m_leaf_level, spatial.m_depth, 2);

        int contained_count = 0;
        for (auto* surface : surfaces) {
            auto* polygon = static_cast<W8OctRegionPolygon*>(surface);
            if (polygon->region == 0 && polygon->containsPoint(arg_spatial->bounds())) {
                ++contained_count;
                polygon->region = this->spatial.m_region_id_bound;
            }
        }

        if (contained_count != 0) {
            auto& center = region_centers[this->spatial.m_region_id_bound];
            center = arg_spatial->bounds().center();

            region_paths.push_back(arg_spatial->m_node_index);

            for (auto* surface : surfaces) {
                auto* polygon = static_cast<W8OctRegionPolygon*>(surface);
                if (polygon->region == this->spatial.m_region_id_bound) {
                    for (int vertex_index = 0; vertex_index != 3; ++vertex_index) {
                        const glm::vec3& position = polygon->vertices[vertex_index]->position;
                        float distance = glm::distance(center, position);
                        if (this->spatial.m_max_region_radius < distance) {
                            this->spatial.m_max_region_radius = distance;
                        }
                    }
                }
            }

            InsertIndexValue(region_path_map, this->spatial.m_region_id_bound,
                             arg_spatial->m_node_index);
            node->region = this->spatial.m_region_id_bound;
            node->provisional_region = node->region;
            ++this->spatial.m_region_id_bound;
            node->leaf_kind = contained_count;
        }
        return;
    }

    w8_ulong path = arg_spatial->m_node_index;
    int high = (static_cast<int>(static_cast<signed char>(path >> 23)) & ~1) + 1;
    int x_base = static_cast<int>(static_cast<signed char>(path >> 15)) & ~1;
    int y_base = static_cast<int>(static_cast<signed char>(path >> 7)) & ~1;
    signed char z_base = static_cast<signed char>(static_cast<signed char>(path) << 1);
    visitBuildChildren(*arg_spatial, child, [&](W8OctSpatialState& candidate, unsigned octant) {
        const int x = octant >> 2;
        const int y = (octant >> 1) & 1;
        const int z = octant & 1;
        candidate.m_node_index =
            ((high * 0x100 + x_base + x) * 0x100 + y_base + y) * 0x100 + z_base + z;
        AssignInitialRegions(&candidate);
    });
}

/* Follow the active depth bits in a packed octree path. Each active bit
   contributes one x/y/z child selector, from the highest level down. */
// FUNCTION: WIZ8 0x004b23f0
W8OctBuildNode* OctBuildPreTree::FindNode(unsigned int path)
{
    W8OctBuildNode* node = spatial.m_root;
    unsigned int active_levels = path >> 24;
    unsigned char x = static_cast<unsigned char>(path >> 16);
    unsigned char y = static_cast<unsigned char>(path >> 8);
    unsigned int z = path & 0xff;
    for (unsigned int level = 0x80; node != 0 && level != 0; level >>= 1) {
        if ((active_levels & level) != 0) {
            int child = 0;
            if ((x & level) != 0) {
                child = 4;
            }
            if ((y & level) != 0) {
                child += 2;
            }
            if ((z & level) != 0) {
                child += 1;
            }
            node = node->children[child];
        }
    }
    return node;
}

/* Try the negative and positive neighbor along each path axis, stopping after
   the first region that can be merged. The high byte remains the depth mask;
   the other three bytes are the x/y/z cell coordinates. */
// FUNCTION: WIZ8 0x004b2450
unsigned char OctBuildPreTree::MergeAdjacentRegion(W8OctBuildNode* node, unsigned int path)
{
    int cell[4];
    cell[0] = path & 0xff000000;
    cell[1] = (path >> 16) & 0xff;
    cell[2] = (path >> 8) & 0xff;
    cell[3] = path & 0xff;

    for (int axis = 1; axis <= 3; ++axis) {
        if (cell[axis] != 0) {
            --cell[axis];
            if (MergeRegion(node, cell) != 0) return 1;
            ++cell[axis];
        }
        ++cell[axis];
        if ((cell[axis] & (1 << (spatial.m_leaf_level + 1))) == 0 && MergeRegion(node, cell) != 0)
            return 1;
        if (axis != 3) --cell[axis];
    }
    return 0;
}

/* Merge one neighboring region into the region carried by the adjacent build
   node. Every packed path is re-keyed in the region table and the surviving
   center becomes the population-weighted average of both groups. */
// FUNCTION: WIZ8 0x004b25c0
unsigned char OctBuildPreTree::MergeRegion(W8OctBuildNode* node, const int* cell)
{
    w8_ulong neighbor_path = ((cell[1] * 0x100 + cell[2]) * 0x100 + cell[3]) + cell[0];
    W8OctBuildNode* neighbor = FindNode(neighbor_path);
    if (neighbor == 0 || neighbor->leaf_kind == 0 || neighbor->leaf_kind >= 100) {
        return 0;
    }

    unsigned short neighbor_region = neighbor->region;
    unsigned short node_region = node->region;
    if (neighbor_region == node_region) {
        return 0;
    }

    const auto& node_center = region_centers[node_region];
    auto& neighbor_center = region_centers[neighbor_region];
    float distance = glm::distance(node_center, neighbor_center);
    if (!(distance < spatial.m_region_grid_cell * g_float_three)) {
        return 0;
    }

    unsigned short combined_count = node->leaf_kind + neighbor->leaf_kind;

    w8_ulong neighbor_count = 0;
    auto position = region_path_map.end();
    while ((position = NextIndexValue(region_path_map, neighbor_region, position)) !=
           region_path_map.end()) {
        ++neighbor_count;
        W8OctBuildNode* member = FindNode(position->second);
        member->leaf_kind = combined_count;
    }

    w8_ulong moved_count = 0;
    w8_ulong path = LookupIndexValue(region_path_map, node_region);
    while (path != 0) {
        ++moved_count;
        RemoveIndexValue(region_path_map, node_region, path);
        W8OctBuildNode* member = FindNode(path);
        member->leaf_kind = combined_count;
        member->region = neighbor_region;
        InsertIndexValue(region_path_map, neighbor_region, path);
        path = LookupIndexValue(region_path_map, node_region);
    }

    // Keep float rounding after each weighted term and the sum.
    const auto weighted = glm::vec3(glm::dvec3(node_center) * double(moved_count)) +
                          glm::vec3(glm::dvec3(neighbor_center) * double(neighbor_count));
    neighbor_center =
        glm::vec3(glm::dvec3(weighted) * (1.0 / double(moved_count + neighbor_count)));
    return 1;
}

/* Compact provisional submesh ids into the final region range, rewrite every
   polygon and path-table entry through that map, and derive aggregate bounds
   for the validation pass. */
// FUNCTION: WIZ8 0x004b2a20
void OctBuildPreTree::FinalizeRegionMapping()
{
    std::vector<unsigned short> region_map(spatial.m_region_id_bound);

    unsigned short next_region = spatial.m_region_count;
    for (const auto path : region_paths) {
        W8OctBuildNode* node = FindNode(path);
        if (node->leaf_kind != 0) {
            unsigned short provisional = node->provisional_region;
            if (node->region == provisional) {
                region_map[provisional] = next_region++;
            } else {
                region_map[provisional] = node->region;
            }
        }
    }

    unsigned short final_region_count = next_region;
    std::vector<srBoundingBox> region_bounds(
        final_region_count, srBoundingBox{glm::vec3(1000000.0f), glm::vec3(-1000000.0f)});

    for (w8_ulong polygon_index = 1; polygon_index < game_data->m_polygon_count; ++polygon_index) {
        W8OctRegionPolygon& polygon = game_data->m_polygons[polygon_index];
        unsigned short region = polygon.region;
        if (region >= spatial.m_region_count) {
            if (LookupIndexValue(region_path_map, region) != 0) {
                polygon.region = region_map[region];
            } else {
                polygon.region = region_map[region_map[region]];
            }
        }

        region = polygon.region;
        if (region >= final_region_count) {
            char message[256];
            sprintf(message, "Polygon %d in invalid submesh %d\n", static_cast<int>(polygon_index),
                    static_cast<unsigned int>(region));
            ReportBuildStatus(6, message);
        }

        for (const auto* vertex : polygon.vertices) region_bounds[region].include(vertex->position);
    }

    for (const auto path : region_paths) {
        W8OctBuildNode* node = FindNode(path);
        unsigned short old_region = node->region;
        RemoveIndexValue(region_path_map, old_region, path);
        node->region = region_map[old_region];
        InsertIndexValue(region_path_map, node->region, path);
        if (node->region >= final_region_count) {
            char message[256];
            sprintf(message, "Invalid submesh %d\n", static_cast<unsigned int>(node->region));
            ReportBuildStatus(6, message);
        }
    }

    spatial.submesh_count = final_region_count;
    ValidatePolygonRegions();
    ValidateRegionBounds(region_bounds);
}

/* Descend to the selected region depth, collect the surfaces below each
   unassigned node, and choose the most frequent nonzero surface region. */
// FUNCTION: WIZ8 0x004b3050
void OctBuildPreTree::AssignRegionFromSurfaces(const W8OctSpatialState* arg_spatial)
{
    W8OctSpatialState child(arg_spatial);
    if (arg_spatial->m_depth > 15) {
        return;
    }

    if (arg_spatial->m_depth != this->spatial.m_leaf_level) {
        visitBuildChildren(*arg_spatial, child, [&](W8OctSpatialState& candidate, unsigned) {
            AssignRegionFromSurfaces(&candidate);
        });
        return;
    }

    W8OctBuildNode* node = arg_spatial->m_root;
    if (node->region != 0) {
        return;
    }

    const auto surfaces = node->linkedSurfaces(spatial.m_leaf_level, spatial.m_depth, 2);

    unsigned short regions[20] = {0};
    unsigned short counts[20] = {0};
    unsigned short selected_region = 0;
    unsigned short selected_count = 0;
    for (auto* surface : surfaces) {
        unsigned short region = static_cast<W8OctRegionPolygon*>(surface)->region;
        short slot = 0;
        while (regions[slot] != 0 && regions[slot] != region) {
            ++slot;
        }
        if (regions[slot] == 0) {
            regions[slot] = region;
        }
        ++counts[slot];
        if (selected_count < counts[slot]) {
            selected_region = regions[slot];
            selected_count = counts[slot];
        }
    }

    /* Retail stores a zero count when no nonzero region won the vote. */
    if (selected_region == 0) {
        selected_count = 0;
    }

    node->region = selected_region;
    node->leaf_kind = selected_count;
}

/* Verify that each polygon carrying a generated region id resolves back to a
   build node with that id. A coordinate just below a grid plane is checked in
   both the truncated cell and its negative neighbor. */
// FUNCTION: WIZ8 0x004b3330
void OctBuildPreTree::ValidatePolygonRegions()
{
    unsigned int active_levels = 0;
    for (unsigned int depth = 0; depth < spatial.m_leaf_level; ++depth) {
        active_levels |= 1 << (depth + 24);
    }

    for (w8_ulong polygon_index = 1; polygon_index < game_data->m_polygon_count; ++polygon_index) {
        W8OctRegionPolygon& polygon = game_data->m_polygons[polygon_index];
        if (polygon.region < spatial.m_region_count) {
            continue;
        }

        const float position[] = {polygon.position.x, polygon.position.y, polygon.position.z};
        const auto& minimum = spatial.m_bounds.minimum;
        int cell[3];
        short count[3];
        for (int axis = 0; axis < 3; ++axis) {
            const float relative = position[axis] - minimum[axis];
            cell[axis] = static_cast<int>(relative / spatial.m_region_grid_cell);
            count[axis] = 1;
            if (g_region_boundary_tolerance < cell[axis] * spatial.m_region_grid_cell - relative) {
                --cell[axis];
                count[axis] = 2;
            }
        }

        bool found = false;
        for (int x_offset = 0; x_offset < count[0]; ++x_offset) {
            for (int y_offset = 0; y_offset < count[1]; ++y_offset) {
                for (int z_offset = 0; z_offset < count[2]; ++z_offset) {
                    w8_ulong path = active_levels +
                                    (((cell[0] + x_offset) * 0x100 + (cell[1] + y_offset)) * 0x100 +
                                     (cell[2] + z_offset));
                    W8OctBuildNode* node = FindNode(path);
                    if (node->region == polygon.region) {
                        found = true;
                    }
                }
            }
        }

        if (!found) {
            char message[256];
            sprintf(message, "Poly %d not found in correct region.\n",
                    static_cast<int>(polygon_index));
            ReportBuildStatus(6, message);
        }
    }
}

/* Check every path assigned to a region against both the build-node region id
   and the aggregate automesh bounds produced for that region. */
// FUNCTION: WIZ8 0x004b35b0
void OctBuildPreTree::ValidateRegionBounds(std::span<const srBoundingBox> region_bounds)
{
    for (w8_ulong region_index = 1; region_index < spatial.submesh_count; ++region_index) {
        unsigned short region = static_cast<unsigned short>(region_index);
        auto entry = region_path_map.end();
        while ((entry = NextIndexValue(region_path_map, region, entry)) != region_path_map.end()) {
            w8_ulong path = entry->second;
            const auto cell_bounds = spatial.regionCellBounds(path);

            W8OctBuildNode* node = FindNode(path);
            if (node != 0) {
                if (node->region != region) {
                    ReportBuildStatus(7, "Region has wrong automesh.");
                }
                const auto& bounds = region_bounds[region_index];
                if (!cell_bounds.overlaps(bounds)) {
                    ReportBuildStatus(7, "AutoMesh has no vertices inside region. You probably "
                                         "have an old .cub file!");
                }
            }
        }
    }
}

// The first zero-terminated run contains unassigned items. Each region then
// contributes one run, with zero lookup offsets for regions that have no items.
OctreeMeshLinks OctRegionObjectLinks::packedLinks(std::size_t region_count) const
{
    constexpr auto limit = std::numeric_limits<unsigned short>::max();
    if (region_count > limit) throw std::length_error("Too many octree mesh regions");
    OctreeMeshLinks links;
    links.lookup.resize(region_count);
    const auto append = [&](unsigned short id) {
        if (links.records.size() == limit)
            throw std::length_error("Octree mesh links exceed the disk record limit");
        links.records.push_back(id);
    };
    for (const auto id : always_visible) append(static_cast<unsigned short>(id));
    append(0);
    for (std::size_t index = 1; index < region_count; ++index) {
        const auto [first, last] = regions.equal_range(static_cast<unsigned short>(index));
        if (first == last) continue;
        links.lookup[index] = static_cast<unsigned short>(links.records.size());
        for (auto entry = first; entry != last; ++entry)
            append(static_cast<unsigned short>(entry->second));
        append(0);
    }
    return links;
}

bool OctBuildPreTree::assignPointToVolumes(const glm::vec3& point, short object,
                                           OctRegionObjectLinks& links)
{
    bool mapped = false;
    for (unsigned short index = 1; index < spatial.m_region_count; ++index) {
        const auto& volume = spatial.m_region_volumes[index];
        if (!volume.ContainsPoint(point)) continue;
        InsertUniqueIndexValue(links.regions, volume.m_region, object);
        mapped = true;
    }
    return mapped;
}

/* Associate every non-cloud particle with the regions containing its origin
   or any corner of its serialized bounds. Particles still outside the built
   regions lead the compact list, followed by one null-terminated list per
   region; the lookup table stores each region's offset into that list. */
// FUNCTION: WIZ8 0x004b3820
unsigned char
OctBuildPreTree::BuildParticleRegions(std::span<const W8LevelFileParticleSystem> particles)
{
    particle_count = static_cast<int>(particles.size());
    particle_regions.clear();
    int particle_number = 0;

    for (const auto& system : particles) {
        const W8LevelParticleRecord& particle = system.particle;
        char name[64];
        strcpy(name, particle.name);
        SDL_strupr(name);
        if (strncmp(name, "CLOUD", 5) == 0) {
            continue;
        }

        ++particle_number;
        short particle_value = static_cast<short>(particle_number);
        const glm::vec3 position = particle.location.glmValue() * g_world_scale;

        bool mapped = assignPointToVolumes(position, particle_value, particle_regions);
        if (mapped) {
            continue;
        }

        if (updateRegionForGeometry(position, particle_value) == 0) {
            glm::vec3 extent(0);
            bool has_bounds = false;
            if (particle.bounds_mode == 1) {
                extent = particle.bounds_extent.glmValue() * g_startup_near_limit;
                has_bounds = true;
            } else if (particle.bounds_mode == 2 && particle.bounds_radius > g_float_zero) {
                extent = particle.bounds_origin.glmValue() * g_world_scale;
                has_bounds = true;
            }

            if (has_bounds) {
                const srBoundingBox bounds{position - extent, position + extent};
                for (unsigned index = 0; index < 8; ++index) {
                    const auto corner = bounds.corner(index);
                    const bool in_volume =
                        assignPointToVolumes(corner, particle_value, particle_regions);
                    if (in_volume || updateRegionForGeometry(corner, particle_value)) mapped = true;
                }
            }

            if (!mapped) {
                particle_regions.always_visible.push_back(particle_value);
            }
        }
    }

    mesh_particle_links = particle_regions.packedLinks(spatial.submesh_count);
    particle_regions.clear();
    return 1;
}

/* Accumulate the two preprocessing geometry banks into one region-to-prop
   table owned by this builder. The final call appends the second bank and
   returns the compact lookup/list pair before clearing temporary membership. */
// FUNCTION: WIZ8 0x004b3f90
unsigned char OctBuildPreTree::BuildGeometryRegions(std::span<const W8LevelFileProp> records,
                                                    int base_index, bool finalize)
{
    if (!finalize) {
        prop_regions.clear();
    }

    const int record_count = static_cast<int>(records.size());
    for (int record_index = 0; record_index < record_count; ++record_index) {
        const W8LevelFileProp& record = records[record_index];
        short value = static_cast<short>(base_index + 1 + record_index);
        bool mapped = false;

        srBoundingBox aggregate{glm::vec3(1000000.0f), glm::vec3(-1000000.0f)};
        for (const auto& stored : record.anim_obj.bounds) {
            const srBoundingBox bounds{stored.minimum.glmValue() * g_world_scale,
                                       stored.maximum.glmValue() * g_world_scale};
            aggregate.include(bounds);
            for (unsigned corner = 0; corner < 8; ++corner)
                if (assignPointToVolumes(bounds.corner(corner), value, prop_regions)) mapped = true;
        }

        if (updateRegionForGeometry(aggregate, value) == 0 && !mapped) {
            prop_regions.always_visible.push_back(value);
        }
    }

    if (finalize) {
        mesh_prop_links = prop_regions.packedLinks(spatial.submesh_count);
        prop_count = record_count + base_index;
        prop_regions.clear();
    }
    return 1;
}

/* Build the compact runtime pre-tree, transfer the auxiliary ownership that
   already has runtime form, consume the counted build-node hierarchy, and
   populate the dense cell-to-leaf lookup. */
// FUNCTION: WIZ8 0x004b4640
OctPreTree* OctBuildPreTree::BuildOctPreTree()
{
    OctPreTree* tree = new OctPreTree;
    if (tree == 0) {
        return 0;
    }

    tree->m_branches.resize(g_build_node_instances + 2);
    tree->m_leaves.resize(leaf_count + 2);
    tree->m_polygon_index_stream.resize(leaf_polygon_count * 2);
    tree->m_region_index_stream.resize(leaf_count * 40);
    tree->m_gd_surface_index_stream.resize(gd_surface_count * 2);

    tree->m_spatial.m_region_count = spatial.m_region_count;
    tree->m_spatial.m_leaf_level = spatial.m_leaf_level;
    tree->m_spatial.submesh_count = spatial.submesh_count;
    tree->m_spatial.m_region_id_bound = spatial.m_region_id_bound;

    tree->m_particle_links = std::move(mesh_particle_links);
    tree->m_prop_links = std::move(mesh_prop_links);
    tree->m_ulNumParticles = particle_count;
    tree->m_ulNumProps = prop_count;

    tree->m_spatial.m_depth = spatial.m_depth;
    tree->m_spatial.m_node_extent = spatial.m_node_extent;
    tree->m_spatial.m_max_region_radius = spatial.m_max_region_radius < g_region_radius_limit
                                              ? spatial.m_max_region_radius
                                              : g_region_radius_limit;
    while (selected_depth < tree->m_spatial.m_depth) {
        ReportBuildStatus(6, "Collapsing tree by one level.\n");
        --tree->m_spatial.m_depth;
        tree->m_spatial.m_node_extent += tree->m_spatial.m_node_extent;
    }

    tree->m_spatial.m_extent = spatial.m_extent;
    tree->m_spatial.m_cell_size = spatial.m_cell_size;
    tree->m_region_volumes = std::move(m_region_volumes);
    tree->m_spatial.m_region_volumes = tree->m_region_volumes.data();
    spatial.m_region_volumes = nullptr;
    tree->game_data = game_data;
    tree->unknown_3a8 = unknown_138;
    tree->unknown_3ac = unknown_13c;
    tree->deepest_link_list = deepest_link_list;
    tree->m_spatial.m_item_count = spatial.m_item_count;
    tree->m_spatial.m_polygon_count = spatial.m_polygon_count;
    tree->m_visited_polygon_bits.assign(spatial.m_polygon_count, false);
    tree->m_spatial.m_region_grid_cell = spatial.m_region_grid_cell;
    tree->m_spatial.m_region_cells_per_axis = spatial.m_region_cells_per_axis;
    tree->automesh_cells = std::move(region_path_map);

    tree->m_spatial.m_bounds = spatial.m_bounds;
    tree->m_spatial.m_clipped_bounds = spatial.m_clipped_bounds;

    tree->m_branch_count = 1;
    tree->m_leaf_count = 1;
    tree->polygon_cursor = 1;
    tree->m_region_list_len = 1;
    tree->m_gd_surface_stream_len = 1;
    tree->m_unknown_13c = 0;
    tree->m_trigger_count = 0;

    W8OctBuildNode* root = spatial.m_root;
    root->ConvertToOctPreTree(0, tree);
    delete root;
    spatial.m_root = 0;
    if (tree->m_region_list_len == 1) {
        tree->m_region_list_len = 0;
    }

    for (int grid_axis = 0; grid_axis != 3; ++grid_axis) {
        tree->m_leaf_grid_dimensions[grid_axis] =
            static_cast<int>((tree->m_spatial.m_clipped_bounds.maximum[grid_axis] -
                              tree->m_spatial.m_clipped_bounds.minimum[grid_axis]) /
                             tree->m_spatial.m_node_extent) +
            1;
    }
    tree->m_leaf_lookup.resize(tree->m_leaf_grid_dimensions.z * tree->m_leaf_grid_dimensions.x *
                               tree->m_leaf_grid_dimensions.y);
    w8_ulong cell_index = 0;
    glm::ivec3 point;
    for (point.x = 0; point.x < static_cast<int>(tree->m_leaf_grid_dimensions.x); ++point.x) {
        for (point.y = 0; point.y < static_cast<int>(tree->m_leaf_grid_dimensions.y); ++point.y) {
            for (point.z = 0; point.z < static_cast<int>(tree->m_leaf_grid_dimensions.z);
                 ++point.z) {
                tree->m_leaf_lookup[cell_index] = tree->FindLeaf(point);
                if (tree->m_leaf_count < tree->m_leaf_lookup[cell_index]) {
                    tree->m_leaf_lookup[cell_index] = 0;
                }
                ++cell_index;
            }
        }
    }
    tree->m_spatial.m_leaf_grid_stride_y = tree->m_leaf_grid_dimensions.z;
    tree->m_spatial.m_leaf_grid_stride_x =
        tree->m_leaf_grid_dimensions.z * tree->m_leaf_grid_dimensions.y;
    return tree;
}

// FUNCTION: WIZ8 0x004afe90
int GetBuildNodeInstanceCount(void)
{
    return g_build_node_instances;
}

/* Region-path entries have 16-bit keys and four-byte values (Lookup caller
   0x004B25C0; Grow owner 0x004B19F0). Region maps at +0x12C/+0x130 have word
   keys/values (callers 0x004B3F90 and 0x004B07E0). These flows establish storage
   families, not unsigned-long spelling or word-value signedness. */
