#include "wiz8/engine_code/OctPreTree.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <ranges>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/common.hpp> // NOLINT(misc-include-cleaner)
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/vector_relational.hpp> // NOLINT(misc-include-cleaner)

#include "ReadMesh.h"
#include "asset_paths.h"
#include "compat/compiler.h"
#include "geometry.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "index_algorithms.h"
#include "navigation_flags.h"
#include "packed_bits.h"
#include "surrender/mesh_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/filesystem.h"
#include "wiz8/float_constants.h"
#include "wiz8/octree_walk.h"
#include "wiz8/retail_geometry_records.h" // IWYU pragma: keep - W8OctRegionVolume codec specialization.
#include "wiz8/retail_text_records.h"

#define OCTPRETREE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\OctPreTree.cpp"

/* Same 50.0f descent step Octree.cpp emits at 0x005EC02C; the linker folds the
   identical constants into one address. */
static const float NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE = 50.0f;
// GLOBAL: WIZ8 0x005ebc28
const float g_float_five = 5.0f;
// GLOBAL: WIZ8 0x005ebc70
const double g_double_one_ten_thousandth = 0.0001;
// GLOBAL: WIZ8 0x005ebc90
const float g_float_one_ten_thousandth = 9.999999747378752e-05f;
// GLOBAL: WIZ8 0x005ec410
const float g_float_one_third = 0.3333333432674408f;
// GLOBAL: WIZ8 0x005ec414
const float g_region_axis_alignment_threshold = 0.9998999834060669f;

// GLOBAL: WIZ8 0x00659c74
OctPreTree* g_oct_pre_tree = 0;

/* The build-time runtime tree extends the ordinary 0x29c octree with transfer
   bookkeeping and one separately owned pointer vector.  Its only recovered
   construction caller allocates exactly 0x3bc bytes. */
// FUNCTION: WIZ8 0x004679e0
OctPreTree::OctPreTree() : W8Octree(0, 0)
{
    game_data = 0;
    deepest_link_list = 0;
    m_region_cell = 0.0f;
    path_node_extent = 0;
    g_oct_pre_tree = this;
}

/* Tears down the automesh cell map, the owned pre-pathing service and the
   registered-prop vector, then clears the global before the octree base. */
// FUNCTION: WIZ8 0x00467ab0
OctPreTree::~OctPreTree()
{
    g_oct_pre_tree = 0;
}

/* Copies the working bounds straight into the spatial state; the build tree
   conversion calls it once its clipped bounds are known. */
// FUNCTION: WIZ8 0x00467b70
void W8OctSpatialState::setWorkingBounds(const srBoundingBox& bounds)
{
    m_working_bounds = bounds;
}

/* Resets the collected-id run and appends every not-yet-seen polygon id the
   leaf under `cell` lists.  The trace walk inlines this sequence at each of
   the six cells it probes. */
void OctPreTree::CollectLeafPolygons(const glm::ivec3& cell)
{
    m_gd_result_count = 0;
    unsigned int leaf_index = LeafIndexForCell(cell);
    if (leaf_index != 0 && m_leaves[leaf_index].polygon_offset != 0) {
        const w8_ulong* stream =
            m_polygon_index_stream.data() + m_leaves[leaf_index].polygon_offset;
        for (int remaining = *stream; remaining != 0; --remaining) {
            ++stream;
            if (!SetBit(m_visited_polygon_bits, *stream)) {
                m_aulGDObjs[m_gd_result_count] = *stream;
                ++m_gd_result_count;
            }
        }
    }
}

/* Walks the `from`-`to` segment through the leaf grid, collecting each
   visited leaf's region-polygon ids and plane/slab-testing them.  Answers
   whether the segment is unobstructed; the light-visibility callers
   accumulate its result. */
// FUNCTION: WIZ8 0x00467bb0
bool OctPreTree::SegmentClear(const glm::vec3& from, const glm::vec3& to)
{
    W8OctreeTrace trace(from, to);
    const auto cell = cellAt(from);
    const auto end_cell = cellAt(to);
    bool blocked = false;

    m_gd_result_count = 0;
    std::ranges::fill(m_visited_polygon_bits, false);
    visitOctreeSegment(
        cell, end_cell,
        [&] {
            auto walk = cellWalk(from, to);
            // Preprocessing uses its world-quantized starting cell.
            walk.cell = cell;
            return walk;
        },
        [&](const glm::ivec3& current, bool adjacent) {
            CollectLeafPolygons(current);
            if (m_gd_result_count != 0 || adjacent) blocked = TestCollectedPolygons(&trace);
            return !blocked;
        });
    return !blocked;
}

/* Tests the collected region polygons' planes against the trace segment.  A
   polygon blocks only when its plane faces the ray, the crossing lies inside
   the segment's `length - 1.0f` window, and either the start point sits
   within 1.0f of the plane or the ray exits at least 5.0f behind it, with the
   resulting contact point landing inside the polygon. */
// FUNCTION: WIZ8 0x004681e0
bool OctPreTree::TestCollectedPolygons(W8OctreeTrace* trace)
{
    float limit = trace->length - g_float_one;
    bool blocked = false;

    for (w8_ulong index = 0; index < m_gd_result_count; ++index) {
        if (blocked) {
            break;
        }
        W8OctRegionPolygon* polygon = &game_data->m_polygons[m_aulGDObjs[index]];
        const W8Plane* plane = &polygon->plane;
        const auto contact =
            planeContact(glm::vec4(plane->normal, plane->w),
                         {trace->start, trace->end, trace->step, trace->length, limit},
                         PlaneContactMode::Occlusion);
        if (contact) {
            const RuntimeTriangle vertices{polygon->vertices[0]->position,
                                           polygon->vertices[1]->position,
                                           polygon->vertices[2]->position};
            if (PointInsideTriangle(vertices, polygon->flags & W8OctRegionPolygon::AXIS_MASK,
                                    contact->point)) {
                blocked = true;
            }
        }
    }
    return blocked;
}

/* Serializes the finished octree to NewLevel.oct: the 0xf5-byte header, then
   the branch/leaf arrays, polygon streams, uiLeaf grid, lookup and region
   tables, the submesh index, optional alpha/mesh-link tables, path nodes,
   prop sunlight bits and finally the game-data block.  The header's +0xb4
   dword and the +0xc9..+0xf4 tail stay unwritten, exactly as retail leaves
   them. */
// FUNCTION: WIZ8 0x004683f0
unsigned char OctPreTree::WriteOctFile(W8OctPreTreeGeometry* geometry, W8GameData* game_data)
try {
    unsigned char written;
    std::unique_ptr<wiz8::File> file;
    w8_ulong sentinel = 0xffffffff;
    W8OctFileHeader header;

    if (m_leaf_count != 0) {
        for (w8_ulong index = 0; index < m_leaf_count; ++index) {
            m_leaves[index].flags &= 0xfffffffe;
        }
    }
    header.m_extent = m_spatial.m_extent;
    header.m_cell_size = m_spatial.m_cell_size;
    header.m_node_extent = m_spatial.m_node_extent;
    header.version = W8OctFileHeader::VERSION;
    header.m_bounds[0] = {m_spatial.m_bounds.minimum.x, m_spatial.m_bounds.minimum.y,
                          m_spatial.m_bounds.minimum.z};
    header.m_bounds[1] = {m_spatial.m_bounds.maximum.x, m_spatial.m_bounds.maximum.y,
                          m_spatial.m_bounds.maximum.z};
    header.m_bounds[2] = {m_spatial.m_clipped_bounds.minimum.x,
                          m_spatial.m_clipped_bounds.minimum.y,
                          m_spatial.m_clipped_bounds.minimum.z};
    header.m_bounds[3] = {m_spatial.m_clipped_bounds.maximum.x,
                          m_spatial.m_clipped_bounds.maximum.y,
                          m_spatial.m_clipped_bounds.maximum.z};
    header.m_bounds[4] = {m_spatial.m_working_bounds.minimum.x,
                          m_spatial.m_working_bounds.minimum.y,
                          m_spatial.m_working_bounds.minimum.z};
    header.m_bounds[5] = {m_spatial.m_working_bounds.maximum.x,
                          m_spatial.m_working_bounds.maximum.y,
                          m_spatial.m_working_bounds.maximum.z};
    header.m_grid_dims = {m_leaf_grid_dimensions.x, m_leaf_grid_dimensions.y,
                          m_leaf_grid_dimensions.z};
    header.m_depth = m_spatial.m_depth;
    header.m_region_id_bound = m_spatial.m_region_id_bound;
    header.m_root_mesh_count = m_root_mesh_count;
    header.m_submesh_count = m_spatial.submesh_count;
    header.m_branch_count = m_branch_count;
    header.m_region_count = m_spatial.m_region_count;
    header.m_leaf_level = m_spatial.m_leaf_level;
    header.m_mesh_total = m_meshCount;
    header.m_leaf_count = m_leaf_count;
    header.m_polygon_count = geometry->m_polygons.size();
    header.m_vertex_count = geometry->m_vertices.size();
    header.m_surface_count = game_data->SurfacesCount();
    header.m_gd_surface_stream_len = m_gd_surface_stream_len;
    header.m_leaf_polygon_stream_len = polygon_cursor;
    header.m_trigger_count = m_trigger_count;
    header.m_region_list_len = m_region_list_len;
    header.m_region_cell = m_region_cell;
    header.m_max_region_radius = m_spatial.m_max_region_radius;
    header.m_particle_len = static_cast<unsigned short>(m_particle_links.records.size());
    header.m_region_grid_cell = m_spatial.m_region_grid_cell;
    header.m_prop_len = static_cast<unsigned short>(m_prop_links.records.size());
    header.m_prop_count = m_ulNumProps;
    header.m_prop_sun_bits = !m_sunlit_props.empty();
    header.m_particle_count = m_ulNumParticles;
    header.m_kind1_submesh_count = m_kind1_submesh_count;
    header.m_zero = 0;
    if (pre_pathing == 0) {
        header.m_path_nodes = 0;
        header.m_edge_node_count = 0;
    } else {
        header.m_path_nodes = pre_pathing->path_node_count;
        header.m_edge_node_count = pre_pathing->edge_node_count;
    }
    file = [&]() {
        try {
            return wiz8::open_file(wiz8::user_path("NewLevel.oct"), wiz8::OpenMode::replace);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (file == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't create file.\n");
        return 0;
    }
    /* Every write-failure path below returns without FileClose: retail leaks
       the handle on each of them (verified at 0x4686b4 et seq.). */
    if ((file->write(&header, sizeof(header)), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write tree info.\n");
        return 0;
    }
    file->write(&sentinel, 4);
    if ((file->write(m_branches.data(), header.m_branch_count * sizeof(W8OctPreTreeBranch)),
         true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Node info.\n");
        return 0;
    }
    if ((file->write(m_leaves.data(), header.m_leaf_count * sizeof(W8OctPreTreeLeaf)), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Leaves info.\n");
        return 0;
    }
    if ((file->write(m_polygon_index_stream.data(), header.m_leaf_polygon_stream_len * 4), true) ==
        0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Poly List info.\n");
        return 0;
    }
    unsigned int grid_cells =
        m_leaf_grid_dimensions.z * m_leaf_grid_dimensions.y * m_leaf_grid_dimensions.x;
    if (grid_cells < 250000 && (file->write(m_leaf_lookup.data(), grid_cells * 4), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write uiLeafGrid info.\n");
        return 0;
    }
    if ((file->write(m_aulPolyLookup.data(), header.m_polygon_count * 4), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Poly Lookup table.\n");
        return 0;
    }
    if (header.m_region_list_len != 0 &&
        (file->write(m_region_index_stream.data(), header.m_region_list_len * 2), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write region list.\n");
        return 0;
    }
    if (header.m_gd_surface_stream_len != 0 &&
        (file->write(m_gd_surface_index_stream.data(), header.m_gd_surface_stream_len * 4), true) ==
            0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Game Data Poly List.\n");
        return 0;
    }
    /* Retail writes this trigger list as 4-byte elements while ReadOctFile
       reads it back as 2-byte elements (0x4688c3 vs 0x42c351): the asymmetry
       is authentic.  In practice the count is always zero. */
    if (header.m_trigger_count != 0 &&
        (file->write(m_trigger_indices.data(), header.m_trigger_count * 4), true) == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Trigger list.\n");
        return 0;
    }
    if (header.m_region_count > 1) {
        for (unsigned int i = 0; i < header.m_region_count; ++i)
            wiz8::retail::write(*file, m_spatial.m_region_volumes[i]);
    }
    file->write(&sentinel, 4);
    if (header.m_submesh_count != 0) {
        if ((file->write(m_pSubmeshes.data(), (header.m_submesh_count + 1) * sizeof(W8OctSubmesh)),
             true) == 0) {
            ReportBuildStatus(7, "WriteOctFile: Couldn't write submesh array.\n");
            return 0;
        }
        if (m_meshCount != 0 && !m_alpha_bits.empty() && !SavePackedBits(*file, m_alpha_bits)) {
            srAssertFail("m_pAlphaBits->Save(hOctFile)", OCTPRETREE_CPP, 0x224,
                         "ReadOctFile: Failure writing Alpha Bits.");
        }
        if (m_ulNumParticles != 0) m_particle_links.write(*file, header.m_mesh_total);
        if (m_ulNumProps != 0) m_prop_links.write(*file, header.m_mesh_total);
    }
    /* Every section terminator is the same 0xffffffff dword: retail keeps one
       -1 local for all of them (verified at 0x468406). */
    if ((file->write(&sentinel, 4), true) == 0) {
        ReportBuildStatus(7,
                          "WriteOctFile: Couldn't write Terminator after Mesh Prop Link Table.\n");
        return 0;
    }
    if (pre_pathing != 0 && !pre_pathing->WritePathNodes(file.get())) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Path Nodes.\n");
        return 0;
    }
    if (m_ulNumProps != 0 && !m_sunlit_props.empty() && !SavePackedBits(*file, m_sunlit_props)) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Prop Sunlight bits array.\n");
        return 0;
    }
    written = (file->write(&sentinel, 4), true);
    if (written == 0) {
        ReportBuildStatus(7, "WriteOctFile: Couldn't write Terminator field.\n");
        return 0;
    }
    /* game_data was already dereferenced unconditionally filling the header
       above (verified at 0x468572); this null check is authentic but
       unreachable-with-null. */
    if (game_data != 0 && header.m_gd_surface_stream_len != 0) {
        written = game_data->WriteGameData(file.get());
        /* Retail bitwise-ORs the game-data and terminator results: a failed
           game-data write followed by a successful four-byte write reports
           success.  Verified at 0x468be0-0x468bec. */
        written |= (file->write(&sentinel, 4), true);
        if (written == 0) {
            ReportBuildStatus(7, "WriteOctFile: Couldn't write final Terminator field.\n");
            return 0;
        }
    }
    if (file) file->close();
    file.reset();
    return written;
} catch (const std::exception&) {
    return false;
}

/* Builds the OctMeshModel array from the split records: partitions the
   geometry through AllocateSubMesh/SplitMeshes, transfers the per-submesh
   vertex/polygon data, counts the per-kind totals and marks the alpha-bit
   rows whose packed header is nonzero. */
// FUNCTION: WIZ8 0x00468c30
std::vector<OctMeshModel> OctPreTree::CreateSubMeshes(W8OctPreTreeGeometry* geometry)
{
    std::vector<W8OctSubmeshBuild> storage(4 * m_spatial.submesh_count);
    auto* records = storage.data();
    unsigned int kind_counts[4];
    unsigned int root_count;
    unsigned int record_index;
    unsigned int model_index;

    if (m_spatial.submesh_count == 0) {
        return {};
    }
    m_vertex_count = geometry->m_vertices.size();
    VerifyPolygonRegions();
    // Each source can emit three kind records before the empty sources are compacted.
    {
        AllocateSubMesh(records);
        SplitMeshes(geometry, records);
        m_aulPolyLookup.resize(geometry->m_polygons.size() + 1);

        m_pSubmeshes.resize(m_spatial.submesh_count + 1);
        std::vector<OctMeshModel> models(m_spatial.submesh_count + 1);

        root_count = 0;
        kind_counts[3] = 0;
        kind_counts[2] = 0;
        kind_counts[1] = 0;
        kind_counts[0] = 0;
        record_index = 1;
        model_index = 0;
        if (m_spatial.submesh_count > 1) {
            W8OctSubmeshBuild* record = records + 1;
            W8OctSubmesh* submesh = m_pSubmeshes.data() + 1;
            /* Retail writes models from index 0: records[1..] and
                       submeshes[1..] are one-based, but the model cursor
                       starts at models[0]. */
            OctMeshModel* model = models.data();
            do {
                if (record->m_polygon_count == 0) {
                    ReportBuildStatus(7, "CreateSubMeshes: Found mesh with no polys.\n");
                    return {};
                }
                submesh->mesh = model_index;
                submesh->polygon_count = record->m_polygon_count;
                submesh->next_link = record->m_next_link;
                model->m_packed_header = record->m_kind;
                model->version = m_sun_count;
                kind_counts[record->m_kind] += 1;
                model->m_vertex_count = record->vertex_count;
                model->m_map_count = record->m_map_count;
                model->m_polygon_count = record->m_polygon_count;
                model->m_link_index = record->m_prev_link - 1;
                if (record->m_prev_link == 0) {
                    ++root_count;
                }
                model->next_link = record->m_next_link - 1;
                model->m_vertex_locations.resize(record->vertex_count);
                model->m_vertex_map = std::move(record->m_uv_map);
                model->m_poly_vertices = std::move(record->m_poly_vertices);
                model->m_poly_uv_index = std::move(record->m_poly_uv_index);
                model->m_poly_equations.resize(record->m_polygon_count);
                model->m_vertex_normals.resize(record->vertex_count);
                model->m_vertex_lights.resize(record->vertex_count);
                model->m_vertex_materials.resize(record->vertex_count);
                model->m_poly_textures.resize(record->m_polygon_count);
                model->m_sun_lights.resize(m_sun_count, std::vector<float>(record->vertex_count));
                for (w8_ulong polygon = 0; polygon < record->m_polygon_count; ++polygon) {
                    w8_ulong id = record->m_polygon_ids[polygon];
                    m_aulPolyLookup[id] = model_index * 0x10000 + polygon;
                    model->m_poly_textures[polygon] = geometry->m_polygons[id].texture;
                    model->m_poly_equations[polygon].x = geometry->m_polygons[id].plane.normal.x;
                    model->m_poly_equations[polygon].y = geometry->m_polygons[id].plane.normal.y;
                    model->m_poly_equations[polygon].z = geometry->m_polygons[id].plane.normal.z;
                    model->m_poly_equations[polygon].w = geometry->m_polygons[id].plane.w;
                }
                model->m_material_index = geometry->m_vertices[record->m_vertex_ids[0]].m_material;
                for (w8_ulong vertex = 0; vertex < record->vertex_count; ++vertex) {
                    W8OctPreTreeVertex* source =
                        &geometry->m_vertices[record->m_vertex_ids[vertex]];
                    model->m_vertex_locations[vertex] = source->position;
                    model->m_vertex_normals[vertex] = source->m_normal;
                    model->m_vertex_lights[vertex] = source->m_light;
                    model->m_vertex_materials[vertex] = source->m_material;
                    for (short sun = 0; sun < static_cast<short>(m_sun_count); ++sun) {
                        model->m_sun_lights[sun][vertex] = source->m_sun_lights[sun];
                    }
                    if (model->m_vertex_materials[vertex] != model->m_material_index) {
                        model->m_material_index = -1;
                    }
                }
                ++record;
                ++submesh;
                ++model;
                ++model_index;
                ++record_index;
            } while (record_index < m_spatial.submesh_count);
        }
        m_spatial.submesh_count = model_index;
        m_meshCount = root_count;
        m_root_mesh_count = kind_counts[0];
        m_kind1_submesh_count = kind_counts[1];
        m_alpha_bits.assign(m_meshCount, false);
        if (root_count > 1) {
            for (unsigned int index = 1; index < root_count; ++index) {
                if (models[index].m_packed_header != 0) {
                    SetBit(m_alpha_bits, index);
                }
            }
        }
        models.resize(model_index);
        return models;
    }
}

/* Second CreateSubMeshes phase: partitions every non-empty automesh record's
   polygon list by polygon kind (1..3) into new records appended through the
   +0x0c/+0x10 link chain, compacts the record array preserving chain order,
   then rebuilds each record's deduplicated vertex id list (sorted by
   material), its corner-to-vertex map (sorted by texture) and its UV map via
   SplitUVMaps.  Retail leaks the five sort arrays on the allocation-failure
   paths; kept as-is. */
// FUNCTION: WIZ8 0x00469670
w8_ulong OctPreTree::SplitMeshes(W8OctPreTreeGeometry* geometry, W8OctSubmeshBuild* records)
{
    w8_ulong count = m_spatial.submesh_count;
    if (count == 0) {
        return 0;
    }

    w8_ulong max_polygons = 0;
    if (count > 1) {
        for (w8_ulong index = 1; index < count; ++index) {
            if (max_polygons < records[index].m_polygon_count) {
                max_polygons = records[index].m_polygon_count;
            }
        }
    }
    std::vector<w8_ulong> staging(max_polygons + 5);
    std::vector<int> vertex_ids((max_polygons + 5) * 3);
    std::vector<w8_ulong> keys(vertex_ids.size()), scratch(vertex_ids.size()),
        order(vertex_ids.size());

    m_vertex_count = geometry->m_vertices.size();
    count = m_spatial.submesh_count;
    m_meshCount = count;
    m_root_mesh_count = count;
    w8_ulong kind_counts[4]{};
    kind_counts[0] = count;
    kind_counts[1] = 0;
    kind_counts[2] = 0;

    w8_ulong next_free = count;
    for (w8_ulong kind = 1; kind < 4; ++kind) {
        for (w8_ulong index = 1; index < count; ++index) {
            W8OctSubmeshBuild* record = records + index;
            W8OctSubmeshBuild* target = records + next_free;
            if (record->m_polygon_count == 0) {
                if (kind == 1) {
                    --m_root_mesh_count;
                }
                continue;
            }
            record->index = index;
            record->m_kind = 0;
            record->m_prev_link = 0;
            for (w8_ulong face = 0; face < record->m_polygon_count; ++face) {
                w8_ulong id = record->m_polygon_ids[face];
                if (geometry->m_polygons[id].kind == kind) {
                    staging[target->m_polygon_count] = id;
                    ++target->m_polygon_count;
                    record->m_polygon_ids[face] = 0;
                }
            }
            if (target->m_polygon_count != 0) {
                target->m_polygon_ids.assign(staging.begin(),
                                             staging.begin() + target->m_polygon_count);
                target->m_kind = kind;
                w8_ulong tail = index;
                w8_ulong link = record->m_next_link;
                while (link != 0) {
                    tail = link;
                    link = records[tail].m_next_link;
                }
                records[tail].m_next_link = next_free;
                target->m_prev_link = tail;
            }
            w8_ulong kept = 0;
            for (w8_ulong i = 0; i < record->m_polygon_count; ++i) {
                int id = record->m_polygon_ids[i];
                if (id != 0) {
                    record->m_polygon_ids[kept] = id;
                    ++kept;
                }
            }
            record->m_polygon_count = kept;
            if (kept == 0) {
                --m_root_mesh_count;
            }
            if (target->m_polygon_count != 0) {
                ++next_free;
                ++kind_counts[kind];
            }
        }
    }

    if (m_spatial.submesh_count == m_root_mesh_count) {
        m_spatial.submesh_count = next_free;
    } else {
        m_spatial.submesh_count = next_free;
        w8_ulong new_index = 1;
        if (next_free > 1) {
            for (w8_ulong index = 1; index < next_free; ++index) {
                w8_ulong slot = index;
                while (slot != 0) {
                    W8OctSubmeshBuild* candidate = records + slot;
                    if (candidate->m_polygon_count == 0) {
                        --kind_counts[candidate->m_kind];
                        --m_spatial.submesh_count;
                        slot = candidate->m_next_link;
                        candidate = records + slot;
                        candidate->flags |= 1;
                        if (candidate->m_prev_link != 0) {
                            candidate->m_prev_link = records[candidate->m_prev_link].m_prev_link;
                        }
                        continue;
                    }
                    if (slot != index || (candidate->flags & 1) == 0) {
                        if (slot > 1) {
                            for (w8_ulong i = 1; i < slot; ++i) {
                                if (records[i].m_next_link == slot) {
                                    records[i].m_next_link = new_index;
                                }
                            }
                        }
                        if (slot + 1 < next_free) {
                            for (w8_ulong i = slot + 1; i < next_free; ++i) {
                                if (records[i].m_prev_link == slot) {
                                    records[i].m_prev_link = new_index;
                                }
                            }
                        }
                        /* The copy lands at the new_index cursor, not at
                           index - when a chain produces no live record (all
                           emptied, or a promoted record already in place)
                           index runs ahead of new_index. */
                        if (new_index != slot) records[new_index] = std::move(records[slot]);
                        ++new_index;
                    }
                    break;
                }
            }
        }
    }

    w8_ulong total_maps = 0;
    w8_ulong total_vertices = 0;
    if (m_spatial.submesh_count > 1) {
        for (w8_ulong index = 1; index < m_spatial.submesh_count; ++index) {
            W8OctSubmeshBuild* record = records + index;
            record->m_poly_vertices.resize(record->m_polygon_count);
            w8_ulong vertex_count = 0;
            for (w8_ulong poly = 0; poly < record->m_polygon_count; ++poly) {
                w8_ulong id = record->m_polygon_ids[poly];
                w8_ulong texture = geometry->m_polygons[id].texture;
                scratch[poly] = texture;
                keys[poly] = texture;
                for (w8_ulong corner = 0; corner < 3; ++corner) {
                    w8_ulong vertex_id = geometry->m_polygons[id].vertices[corner]->m_vertex_index;
                    w8_ulong slot = 0;
                    while (slot < vertex_count && vertex_ids[slot] != static_cast<int>(vertex_id)) {
                        ++slot;
                    }
                    if (slot == vertex_count) {
                        vertex_ids[vertex_count] = vertex_id;
                    }
                    record->m_poly_vertices[poly][corner] = slot;
                    if (slot == vertex_count) {
                        ++vertex_count;
                    }
                }
            }
            record->vertex_count = vertex_count;

            std::ranges::sort(
                std::views::zip(std::span(keys).first(record->m_polygon_count),
                                std::span(record->m_polygon_ids).first(record->m_polygon_count)),
                {}, [](const auto& entry) { return std::get<0>(entry); });
            std::ranges::sort(std::views::zip(std::span(scratch).first(record->m_polygon_count),
                                              std::span(record->m_poly_vertices)),
                              {}, [](const auto& entry) { return std::get<0>(entry); });

            for (w8_ulong i = 0; i < vertex_count; ++i) {
                w8_ulong material = geometry->m_vertices[vertex_ids[i]].m_material;
                scratch[i] = material;
                keys[i] = material;
                order[i] = i;
            }
            std::ranges::sort(std::views::zip(std::span(scratch).first(vertex_count),
                                              std::span(vertex_ids).first(vertex_count)),
                              {}, [](const auto& entry) { return std::get<0>(entry); });
            std::copy_n(order.begin(), vertex_count, scratch.begin());
            std::ranges::sort(std::views::zip(std::span(keys).first(vertex_count),
                                              std::span(scratch).first(vertex_count)),
                              {}, [](const auto& entry) { return std::get<0>(entry); });
            std::ranges::sort(std::views::zip(std::span(scratch).first(vertex_count),
                                              std::span(order).first(vertex_count)),
                              {}, [](const auto& entry) { return std::get<0>(entry); });

            for (w8_ulong face = 0; face < record->m_polygon_count; ++face) {
                record->m_poly_vertices[face] = srMeshMath::remapTriangle(
                    record->m_poly_vertices[face], std::span(order).first(vertex_count));
            }

            record->m_vertex_ids.assign(vertex_ids.begin(), vertex_ids.begin() + vertex_count);
            total_vertices += vertex_count;
            total_maps += SplitUVMaps(record, geometry);
        }
    }

    char text[1024];
    sprintf(text,
            "Vertex UV Map Split: %d original vertices, %d final UV map Entries.\n\t"
            "Split Ratio: %3.1f to 1.\n",
            static_cast<int>(total_vertices), static_cast<int>(total_maps),
            static_cast<double>(total_maps) / static_cast<int>(total_vertices));
    ReportBuildStatus(6, text);
    m_root_mesh_count = kind_counts[0];
    m_kind1_submesh_count = kind_counts[1] + kind_counts[0];
    VerifyAutoMeshes(geometry, records);
    return m_spatial.submesh_count;
}

/* UV dedup pool for SplitUVMaps: slots 0..vertex_count-1 are per-vertex
   heads (link -1 = unused, 0 = chain end, N = next pool index); overflow
   entries chain from the running uv count upward. */
struct W8OctUvPoolEntry {
    w8_long link;
    float u;
    float v;
};

/* Builds one record's UV map: walks the three corners of every polygon,
   deduplicates uvs through the pool and emits the corner-to-uv index
   triplets plus the final glm::vec2 map.  Returns the uv count. */

// FUNCTION: WIZ8 0x0046a4b0
w8_ulong OctPreTree::SplitUVMaps(W8OctSubmeshBuild* record, W8OctPreTreeGeometry* geometry)
{
    std::vector<W8OctUvPoolEntry> table(record->m_polygon_count * 4);
    std::vector<glm::ivec3> uv_index(record->m_polygon_count);
    for (w8_ulong i = 0; i < record->vertex_count; ++i) {
        table[i].link = -1;
    }
    w8_ulong uv_count = record->vertex_count;
    for (w8_ulong poly = 0; poly < record->m_polygon_count; ++poly) {
        const glm::vec2* uvs =
            geometry->m_polygons[record->m_polygon_ids[poly]].face.texture_coordinates;
        for (w8_ulong corner = 0; corner < 3; ++corner) {
            int vertex = record->m_poly_vertices[poly][corner];
            int chain = vertex + 1;
            int last = vertex;
            if (chain != 0 && table[chain - 1].link >= 0) {
                while (table[chain - 1].u != uvs[corner].x || table[chain - 1].v != uvs[corner].y) {
                    last = chain - 1;
                    if (table[last].link == 0) {
                        /* End of the seam chain: allocate a new uv entry; only
                           an exact u/v match may reuse one. */
                        chain = 0;
                        break;
                    }
                    chain = table[last].link + 1;
                    if (chain == 0) {
                        break;
                    }
                }
                if (chain != 0) {
                    uv_index[poly][corner] = chain - 1;
                    continue;
                }
            }
            W8OctUvPoolEntry* entry = &table[last];
            if (table[last].link < 0) {
                uv_index[poly][corner] = last;
                entry->u = uvs[corner].x;
                entry->v = uvs[corner].y;
                entry->link = 0;
            } else {
                if (static_cast<int>(record->m_polygon_count * 4) <= static_cast<int>(uv_count)) {
                    ReportBuildStatus(7, "SplitUVMaps: UV Map count too high.\n");
                    return 0;
                }
                uv_index[poly][corner] = uv_count;
                table[uv_count].link = 0;
                table[uv_count].u = uvs[corner].x;
                table[uv_count].v = uvs[corner].y;
                if (static_cast<int>(record->m_polygon_count * 4) <= last || last < 0) {
                    ReportBuildStatus(7, "SplitUVMaps: Counter value iLast too high.\n");
                    return 0;
                }
                entry->link = uv_count;
                ++uv_count;
            }
        }
    }
    record->m_poly_uv_index = std::move(uv_index);
    record->m_map_count = uv_count;
    record->m_uv_map.resize(uv_count);
    for (w8_ulong uv = 0; uv < uv_count; ++uv) {
        record->m_uv_map[uv] = glm::vec2(table[uv].u, table[uv].v);
    }
    return uv_count;
}

/* First CreateSubMeshes phase: counts each region's polygons into its build
   record, allocates the polygon id run and refills it while tracking the
   corner bounds, then checks every automesh cell keyed by the region id
   overlaps those bounds. */
// FUNCTION: WIZ8 0x0046a790
w8_ulong OctPreTree::AllocateSubMesh(W8OctSubmeshBuild* records)
{
    if (m_spatial.m_polygon_count > 1) {
        for (w8_ulong poly = 1; poly < m_spatial.m_polygon_count; ++poly) {
            unsigned short region = game_data->m_polygons[poly].region;
            if (region == 0 || region >= m_spatial.submesh_count) {
                char text[1024];
                sprintf(text, "Polygon %d in invalid submesh %d\n", static_cast<int>(poly),
                        static_cast<unsigned int>(region));
                ReportBuildStatus(6, text);
            } else {
                ++records[region].m_polygon_count;
            }
        }
    }
    for (w8_ulong index = 1; index < m_spatial.submesh_count; ++index) {
        W8OctSubmeshBuild* record = records + index;
        srBoundingBox bounds{glm::vec3(1e+06f), glm::vec3(-1e+06f)};
        const auto& minimum = bounds.minimum;
        const auto& maximum = bounds.maximum;
        if (record->m_polygon_count != 0) {
            record->m_polygon_ids.resize(record->m_polygon_count);
            unsigned short found = 0;
            if (m_spatial.m_polygon_count > 1) {
                for (w8_ulong poly = 1; poly < m_spatial.m_polygon_count; ++poly) {
                    if (game_data->m_polygons[poly].region == index) {
                        record->m_polygon_ids[found] = poly;
                        ++found;
                        if (found == 5000) {
                            ReportBuildStatus(
                                6, "One of your regions has more than 5000 polys in it!\n");
                        }
                        for (auto& vertex : game_data->m_polygons[poly].vertices) {
                            const glm::vec3* position = &vertex->position;
                            bounds.include(*position);
                        }
                    }
                }
            }
            if (record->m_polygon_count != found) {
                ReportBuildStatus(
                    6, "Mismatch between expected number of mesh polys and actual number");
            }
            record->m_polygon_count = found;
        }
        unsigned short key = static_cast<unsigned short>(index);
        auto& cells = automesh_cells;
        auto slot = cells.equal_range(key).first;
        while (slot != cells.end()) {
            w8_ulong cell = slot->second;
            if (!m_spatial.regionCellBounds(cell).overlaps({minimum, maximum})) {
                ReportBuildStatus(7, "AutoMesh has no vertices inside region.");
            }
            slot = NextIndexValue(cells, key, slot);
        }
    }
    return m_spatial.submesh_count;
}

/* Rebuilds the leaf-level mask, then for every polygon assigned to an
   auto-region (region >= region_count) descends to its position's leaf and
   reports when the leaf's region differs from the polygon's. */
// FUNCTION: WIZ8 0x0046abf0
void OctPreTree::VerifyPolygonRegions()
{
    m_region_mask = 0;
    for (w8_ulong level = m_spatial.m_leaf_level; level != 0; --level) {
        m_region_mask = m_region_mask * 2 + 1;
    }
    for (w8_ulong poly = 1; poly < game_data->m_polygons.size(); ++poly) {
        W8OctRegionPolygon* polygon = &game_data->m_polygons[poly];
        if (polygon->region >= m_spatial.m_region_count) {
            unsigned int cell[4];
            cell[0] = m_region_mask;
            cell[1] =
                static_cast<unsigned int>((polygon->position.x - m_spatial.m_bounds.minimum.x) /
                                          m_spatial.m_region_grid_cell);
            cell[2] =
                static_cast<unsigned int>((polygon->position.y - m_spatial.m_bounds.minimum.y) /
                                          m_spatial.m_region_grid_cell);
            cell[3] =
                static_cast<unsigned int>((polygon->position.z - m_spatial.m_bounds.minimum.z) /
                                          m_spatial.m_region_grid_cell);
            int node = DescendByMask(cell);
            if (m_branches[node].region != polygon->region) {
                char text[256];
                sprintf(text, "Poly %d not found in correct region.\n", static_cast<int>(poly));
                ReportBuildStatus(6, text);
            }
        }
    }
}

/* Per root automesh (1..m_meshCount): walks every cell hashed under the
   mesh id, descends to its node, reports a region mismatch, and checks the
   cell overlaps the vertex bounds of the whole split-record chain. */
// FUNCTION: WIZ8 0x0046ad10
void OctPreTree::VerifyAutoMeshes(W8OctPreTreeGeometry* geometry, W8OctSubmeshBuild* records)
{
    for (w8_ulong mesh = 1; mesh < m_meshCount; ++mesh) {
        unsigned short key = static_cast<unsigned short>(mesh);
        auto slot = automesh_cells.equal_range(key).first;
        while (slot != automesh_cells.end()) {
            unsigned int cell[4];
            w8_ulong packed = slot->second;
            cell[0] = packed >> 0x18;
            cell[1] = packed >> 0x10 & 0xff;
            cell[2] = packed >> 8 & 0xff;
            cell[3] = packed & 0xff;
            int node = DescendByMask(cell);
            if (node != 0) {
                if (m_branches[node].region != key) {
                    ReportBuildStatus(7, "Region has wrong automesh.");
                }
                srBoundingBox bounds{glm::vec3(g_float_one_million, 1e+06f, 1e+06f),
                                     glm::vec3(-1e+06f)};
                for (w8_ulong link = mesh; link != 0; link = records[link].m_next_link) {
                    W8OctSubmeshBuild* record = records + link;
                    for (w8_ulong i = 0; i < record->vertex_count; ++i) {
                        const glm::vec3* position =
                            &geometry->m_vertices[record->m_vertex_ids[i]].position;
                        bounds.include(*position);
                    }
                }
                if (!m_spatial.regionCellBounds(packed).overlaps(bounds)) {
                    ReportBuildStatus(7, "AutoMesh has no vertices inside region.");
                }
            }
            slot = NextIndexValue(automesh_cells, key, slot);
        }
    }
}

/* Grid-walks the level bounds cell by cell: snaps each candidate to the
   ground, runs the obstruction probe, appends a 0x10-byte node record to the
   pre-pathing chunk table, and cross-links conditional props. The node map
   keys each (z<<16 | x) cell so one pass emits at most one node per cell
   unless a duplicate key arrives with an empty value. */
// FUNCTION: WIZ8 0x0046b060
unsigned char OctPreTree::BuildPathLists(W8GameData* game_data, W8LevelFile& level,
                                         unsigned int min_component_percent)
{
    std::unordered_map<unsigned int, int> node_map;
    std::unordered_multimap<unsigned int, CondPathNode> cond_map;
    char message[0x400];
    auto registry = std::make_unique<W8OctreeObjectRegistry>();
    std::vector<W8PreProp> preprops;
    const struct RestoreBuildState {
        OctPreTree& tree;
        W8OctreeObjectRegistry* registry;
        W8GameData* game_data;
        std::size_t props;
        ~RestoreBuildState()
        {
            tree.props.resize(props);
            tree.object_registry = registry;
            SetOctreeGameData(game_data);
        }
    } restore{*this, object_registry, g_octree_game_data, props.size()};
    glm::vec3 node;

    object_registry = registry.get();
    SetOctreeGameData(game_data);
    m_visited_object_bits.assign(m_spatial.m_item_count + 0x14, false);
    pre_pathing = std::make_unique<PrePathing>();
    pre_pathing->SnapNamedPositions(level.named_positions, min_component_percent, this);
    ReportBuildStatus(6, "\nBuilding Path Lists:\n=======================\n");
    path_node_extent = m_region_cell + m_region_cell;
    float level_height =
        (m_spatial.m_bounds.maximum.y - m_spatial.m_bounds.minimum.y) * g_path_span_scale;
    int x_cells = static_cast<int>((m_spatial.m_bounds.maximum.x - m_spatial.m_bounds.minimum.x) /
                                   m_region_cell) +
                  1;
    int z_cells = static_cast<int>((m_spatial.m_bounds.maximum.z - m_spatial.m_bounds.minimum.z) /
                                   m_region_cell) +
                  1;

    preprops = CreatePathProps(level);
    W8PrePathNode* record = pre_pathing->GetPathNode();
    W8PrePathNode* head = record;
    path_node_count = 1;
    int last_percent = 0;
    if (x_cells > 0) {
        float x_cells_f = static_cast<float>(x_cells);
        for (int x = 0; x < x_cells; ++x) {
            int percent = static_cast<int>(x * 100.0f / x_cells_f);
            if (last_percent < percent) {
                ++last_percent;
                sprintf(message, "  %d%% Complete:  %d Path Nodes Created \r", last_percent,
                        path_node_count);
                ReportStartupMessage(message);
            }
            unsigned int cell = static_cast<unsigned int>(x);
            node.x = (x + g_float_half) * m_region_cell + m_spatial.m_bounds.minimum.x;
            for (int z = 0; z < z_cells; ++z) {
                node.z = (z + g_float_half) * m_region_cell + m_spatial.m_bounds.minimum.z;
                node.y = m_spatial.m_bounds.maximum.y;
                while (SnapToGround(&node, 1)) {
                    m_lNumBlocks = 0;
                    m_lNumSupports = 0;
                    if (current_prop >= 0) {
                        float snapped = node.y;
                        if (!SnapToGround(&node, 0) ||
                            g_float_one_hundred_twenty_five < fabsf(node.y - snapped)) {
                            m_lSupports[m_lNumSupports] = current_prop;
                            ++m_lNumSupports;
                        }
                        node.y = snapped;
                    }
                    if (PathNodeObstructed(&node) != 1) {
                        W8PrePathNode* next = pre_pathing->GetPathNode();
                        record->next = next;
                        record = next;
                        record->cell = cell;
                        record->y = node.y;
                        record->level_flags =
                            static_cast<unsigned int>(static_cast<int>(
                                (node.y - m_spatial.m_bounds.minimum.y) / level_height)) +
                            1;
                        if (InsertConditionalNodes(cond_map, cell, record->level_flags, preprops)) {
                            record->level_flags |= W8_PATH_CELL_CONDITIONAL;
                        }
                        auto slot = node_map.equal_range(cell).first;
                        if (slot == node_map.end() || slot->second == 0) {
                            node_map.insert_or_assign(cell, path_node_count);
                            record->level_flags |= W8_PATH_CELL_INACTIVE;
                        }
                        ++path_node_count;
                    }
                    node.y -=
                        m_lNumSupports != 0 ? NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE : g_world_scale;
                }
                cell += 0x10000;
            }
        }
    }
    sprintf(message, "%d Path Nodes Created               \n", path_node_count);
    ReportBuildStatus(6, message);
    if (path_node_count != 0) {
        if (pre_pathing == 0) {
            ReportBuildStatus(7, "Could not create PrePathing object\n");
        }
        pre_pathing->ConfigureForLevel(path_node_count, m_region_cell, m_spatial.bounds(),
                                       m_path_stem.c_str());
        if (!pre_pathing->BuildPathList(head, &node_map)) {
            return 0;
        }
        pre_pathing->LinkCollideableProps(preprops, cond_map);
        pre_pathing->CreateAutomapNodes(level);
    }
    return 1;
}

/* Probes one node candidate: box-tests the cell against surfaces/props, then
   checks the four footprint corners for ground contact. Sector ids that can
   support a node are deduplicated into m_lSupports (result 6); a prop-blocked
   or floating node yields 1. */
// FUNCTION: WIZ8 0x0046b700
char OctPreTree::PathNodeObstructed(const glm::vec3* node)
{
    glm::vec3 bounds_min, bounds_max;
    glm::vec3 corner;
    char result;
    bool probe;

    float clearance = m_path_clearance;
    bounds_min.y = node->y + clearance * g_navigator_mode3_scale;
    bounds_max.y = bounds_min.y + clearance;
    float half = m_region_cell * g_float_half;
    bounds_min.x = node->x - half;
    bounds_max.x = node->x + half;
    bounds_min.z = node->z - half;
    bounds_max.z = bounds_min.z + half + half;

    result = TestPathPropBounds({bounds_min, bounds_max});
    if (result != 1) {
        for (int i = 0; i < 4; ++i) {
            char out = 1;
            if (result == 1) break;
            switch (i) {
            case 0:
                corner.x = bounds_min.x;
                corner.z = bounds_min.z;
                break;
            case 1:
                corner.x = bounds_min.x;
                corner.z = bounds_max.z;
                break;
            case 2:
                corner.x = bounds_max.x;
                corner.z = bounds_min.z;
                break;
            case 3:
                corner.x = bounds_max.x;
                corner.z = bounds_max.z;
                break;
            }
            corner.y = bounds_min.y + g_world_scale;
            out = SnapToGround(&corner, 1);
            if (out == 0 ||
                !(current_prop != -1 ||
                  fabsf(node->y - corner.y) <= clearance * static_cast<float>(g_double_half))) {
                out = 1;
            } else {
                out = result;
                if (current_prop >= 0) {
                    corner.y = bounds_min.y + g_world_scale;
                    probe = SnapToGround(&corner, 0);
                    if (!probe ||
                        clearance * static_cast<float>(g_double_half) < fabsf(node->y - corner.y)) {
                        for (int b = 0; b < m_lNumBlocks && result != 1; ++b) {
                            if (current_prop == m_lBlocks[b]) result = 1;
                        }
                        out = result;
                        if (result != 1) {
                            bool absent = true;
                            for (int s = 0; s < m_lNumSupports; ++s) {
                                if (current_prop == m_lSupports[s]) absent = false;
                            }
                            if (absent) {
                                m_lSupports[m_lNumSupports] = current_prop;
                                ++m_lNumSupports;
                            }
                            out = 6;
                        }
                    }
                }
            }
            result = out;
        }
    }
    /* Verified retail order: the appends above write m_lSupports[30]/
       m_lBlocks[30] before this check ever runs, so a 31st entry goes out
       of bounds before the assertion fires. */
    if (m_lNumSupports > 29 || m_lNumBlocks > 29) {
        srAssertFail("(m_lNumSupports < 30 && m_lNumBlocks < 30)", OCTPRETREE_CPP, 0x7ab,
                     "PathNodeObstructed: Too many props affecting one pathnode.");
    }
    return result;
}

/* Cross-links each support/blocker prop id to the pre-prop record that
   produced it and stores a CondPathNode {value, cell} pair in the
   conditional-node map keyed by (stop-mesh frame << 16 | preprop index + 1).
   Blocker nodes are flagged 0x2000000. */
// FUNCTION: WIZ8 0x0046b9d0
unsigned char
OctPreTree::InsertConditionalNodes(std::unordered_multimap<unsigned int, CondPathNode>& nodes,
                                   unsigned int cell, unsigned int node,
                                   std::span<const W8PreProp> preprops)
{
    if (m_lNumSupports == 0 && m_lNumBlocks == 0) return 0;

    const auto insert = [&](std::span<const int> ids, unsigned flags) {
        for (const int prop_id : ids) {
            bool found = false;
            for (std::size_t index = 0; index < preprops.size(); ++index) {
                const auto& prop = preprops[index];
                if (prop.stop_meshes.empty() || prop_id < prop.first_prop_number ||
                    static_cast<std::size_t>(prop_id - prop.first_prop_number) >=
                        prop.stop_meshes.size())
                    continue;
                const unsigned key =
                    (static_cast<unsigned>(
                         prop.stop_meshes[prop_id - prop.first_prop_number].m_prop_number)
                     << 16) |
                    static_cast<unsigned>(index + 1);
                InsertIndexValue(nodes, key, CondPathNode{node | flags, cell});
                found = true;
                break;
            }
            if (!found)
                srAssertFail("FALSE", OCTPRETREE_CPP, 0x7e8,
                             "Could not find prop and frame for conditional node.");
        }
    };
    insert({m_lSupports, static_cast<std::size_t>(m_lNumSupports)}, 0);
    insert({m_lBlocks, static_cast<std::size_t>(m_lNumBlocks)}, W8_PATH_CELL_BLOCKING_FRAME);
    return 1;
}

// FUNCTION: WIZ8 0x0046bec0
char OctPreTree::TestPathPropBounds(const srBoundingBox& bounds)
{
    std::vector<w8_ulong> ids;
    unsigned char hit = 0;

    ids = queryObjects(bounds, 3, -1);
    int count = ids.size();
    for (int i = 0; i < count && hit == 0; ++i) {
        W8GDSurface* surface = g_octree_game_data->m_pSurfaces.data() + ids[i];
        if ((surface->flags & W8_GD_SURFACE_CROSSING_MASK) == 0) {
            hit = SurfaceIntersectsBox(*surface, g_octree_game_data->m_pVertices, bounds);
        }
    }
    current_prop = -1;
    if (hit != 0) return 1;

    ids = queryObjects(bounds, 8, -1);
    count = ids.size();
    bool prop_hit = false;
    for (int k = 0; k < count; ++k) {
        int id = ids[k];
        GDProp* prop = props[id];
        if (prop->boundsOverlap(bounds)) {
            prop_hit = true;
            if (prop->m_flags.always_blocks_path) return 1;
            /* Dead in retail: current_prop was just set to -1 above and
               QueryObjects never republishes it, while support entries are
               registered prop ids >= 0 - the comparison can never fire. */
            if (m_lNumSupports != 0 && current_prop == m_lSupports[0]) return 1;
            m_lBlocks[m_lNumBlocks] = id;
            ++m_lNumBlocks;
        }
    }
    if (prop_hit) return 3;
    return 0;
}

/* Own the per-frame stop meshes for each collidable prop. Their addresses stay
   stable while the build's spatial tables borrow them. */
// FUNCTION: WIZ8 0x0046c0f0
std::vector<W8PreProp> OctPreTree::CreatePathProps(const W8LevelFile& level)
{
    int count = static_cast<int>(level.props.size());
    unsigned short prop_number = 0;
    std::vector<W8PreProp> records(count);
    {
        for (int i = 0; i < count; ++i) {
            const W8LevelFileProp* prop = &level.props[i];
            W8PreProp* record = &records[i];
            if ((prop->flags & W8_PROP_COLLIDABLE) == 0) continue;
            const auto registerFrame = [&](unsigned mesh_index, unsigned short frame) {
                auto& mesh = record->stop_meshes[mesh_index];
                mesh.ApplyAnimFrame(frame, prop->anim_obj);
                AddCollidablePropBounds(static_cast<unsigned short>(prop_number + mesh_index),
                                        mesh.computeBounds());
                props.push_back(&mesh);
            };
            if (prop->frame_positions.empty()) {
                record->stop_meshes.resize(PropFramesDiffer(prop->anim_obj, 0, 0xffff) ? 2 : 1);
                record->first_prop_number = prop_number;
                strcpy(record->name, prop->name);
                registerFrame(0, 0);
                if (record->stop_meshes.size() == 2) {
                    registerFrame(1, 0xffff);
                    prop_number += 2;
                } else {
                    record->stop_meshes[0].m_flags.always_blocks_path = true;
                    record->stop_meshes[0].last_frame = prop->anim_obj.ClampFrame(0xffff);
                    ++prop_number;
                }
            } else {
                record->stop_meshes.resize(prop->frame_positions.size());
                record->first_prop_number = prop_number;
                strcpy(record->name, prop->name);
                for (unsigned short j = 0; j < record->stop_meshes.size(); ++j) {
                    unsigned short frame = prop->frame_positions[j].frame;
                    if (static_cast<unsigned short>(prop->frame_count) <= frame) {
                        srAssertFail(
                            "(pLVL->pProps[i].usFrame_Pos[j*2] < "
                            "(UINT16)(pLVL->pProps[i].bNumFrames))", /* c-style-cast-ok: verbatim
                                retail assertion text, kept for .rdata match */
                            OCTPRETREE_CPP, 0x8b4,
                            fmt::format("{} Prop Error:Segment frame number {:d} is out of range",
                                        prop->name, frame));
                    }
                    registerFrame(j, frame);
                }
                prop_number += record->stop_meshes.size();
            }
        }
    }
    return records;
}

/* Compares two animation frames across every transform channel: a prop
   differs when the sampled position moves more than half a unit, the frame
   quaternions disagree beyond the snap epsilon (unless they are the mirrored
   same rotation), or scaled-path tails drift. */
// FUNCTION: WIZ8 0x0046c6a0
char OctPreTree::PropFramesDiffer(const W8LevelFileAnimObj& anim, unsigned short first,
                                  unsigned short last)
{
    last = anim.ClampFrame(last);
    bool differ = false;
    for (const auto& transform : anim.transforms) {
        const auto a = transform.pathAI.Node(first);
        const auto b = transform.pathAI.Node(last);
        if (transform.pathAI.IsScaled() && (g_camera_snap_epsilon < fabsf(a.scale.x - b.scale.x) ||
                                            g_camera_snap_epsilon < fabsf(a.scale.y - b.scale.y) ||
                                            g_camera_snap_epsilon < fabsf(a.scale.z - b.scale.z))) {
            differ = true;
        }
        if (g_float_half < fabsf(a.path.position.x - b.path.position.x) ||
            g_float_half < fabsf(a.path.position.y - b.path.position.y) ||
            g_float_half < fabsf(a.path.position.z - b.path.position.z)) {
            differ = true;
        }
        if (g_camera_snap_epsilon < fabsf(a.path.angle - b.path.angle) ||
            g_camera_snap_epsilon < fabsf(a.path.axis.x - b.path.axis.x) ||
            g_camera_snap_epsilon < fabsf(a.path.axis.y - b.path.axis.y) ||
            g_camera_snap_epsilon < fabsf(a.path.axis.z - b.path.axis.z)) {
            // A mirrored axis with complementary angles is the same rotation.
            if (!(fabsf((b.path.angle + a.path.angle) - g_camera_half_pi) <= g_float_one_tenth &&
                  fabsf(b.path.axis.x + a.path.axis.x) <= g_camera_snap_epsilon &&
                  fabsf(b.path.axis.y + a.path.axis.y) <= g_camera_snap_epsilon &&
                  fabsf(b.path.axis.z + a.path.axis.z) <= g_camera_snap_epsilon)) {
                differ = true;
            }
        }
    }
    return differ;
}

/* Construct the spatial value used by both the runtime octree and the level
   build tree.  A source value describes the next child: its extent halves and
   its depth advances only when the source is the root-kind record. */
// FUNCTION: WIZ8 0x0046ccc0
W8OctSpatialState::W8OctSpatialState(const W8OctSpatialState* source)
{
    Reset();
    m_level_kind = 1;
    if (source != 0) {
        m_bounds = source->m_bounds;
        m_clipped_bounds = source->m_clipped_bounds;
        if (source->m_level_kind == 1) {
            m_extent = source->m_extent * g_float_half;
            m_depth = source->m_depth + 1;
        } else {
            m_extent = source->m_extent;
            m_depth = source->m_depth;
        }
        m_cell_size = source->m_cell_size;
        m_polygon_count = source->m_polygon_count;
        m_region_id_bound = source->m_region_id_bound;
        m_leaf_grid_stride_x = source->m_leaf_grid_stride_x;
        m_leaf_grid_stride_y = source->m_leaf_grid_stride_y;
        m_node_extent = source->m_node_extent;
        m_root = source->m_root;
        m_node_index = source->m_node_index;
        m_triangle_vertices = source->m_triangle_vertices;
        flags = source->flags;
        m_item_count = source->m_item_count;
        submesh_count = source->submesh_count;
        m_region_count = source->m_region_count;
        m_leaf_level = source->m_leaf_level;
        m_region_volumes = source->m_region_volumes;
        m_node_extent = source->m_node_extent;
        m_max_region_radius = source->m_max_region_radius;
    }
}

// FUNCTION: WIZ8 0x0046cdc0
void W8OctSpatialState::Reset()
{
    memset(this, 0, sizeof(*this));
}

// FUNCTION: WIZ8 0x0046cdf0
srBoundingBox W8OctSpatialState::GetWorkingBounds() const
{
    return m_working_bounds;
}

// FUNCTION: WIZ8 0x0046ce30
srBoundingBox W8OctSpatialState::GetClippedBounds() const
{
    return m_clipped_bounds;
}

/* Test a triangle against an axis-aligned box.  The inexpensive containment
   and separating-axis checks precede explicit triangle-edge intersections
   with all six box faces. */
bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const glm::vec3> vertices,
                          const srBoundingBox& box)
{
    return triangleIntersectsBox(box, indexedTriangle(vertices, surface.vertex_indices),
                                 surface.plane.normal);
}

// FUNCTION: WIZ8 0x0046ce60
bool triangleIntersectsBox(const srBoundingBox& box, std::span<const glm::vec3, 3> vertices,
                           const glm::vec3& normal)
{
    const auto& minimum = box.minimum;
    const auto& maximum = box.maximum;

    for (const auto& vertex : vertices) {
        if (box.contains(vertex)) return true;
    }

    const auto triangleMinimum = glm::min(glm::min(vertices[0], vertices[1]), vertices[2]);
    const auto triangleMaximum = glm::max(glm::max(vertices[0], vertices[1]), vertices[2]);
    if (glm::any(glm::lessThan(triangleMaximum, minimum)) ||
        glm::any(glm::greaterThan(triangleMinimum, maximum)))
        return false;

    const bool nearAxis = glm::any(
        glm::greaterThan(glm::abs(normal), glm::vec3(g_region_axis_alignment_threshold)) &&
        glm::greaterThanEqual(vertices[0], minimum) && glm::lessThanEqual(vertices[0], maximum));
    if (!nearAxis) {
        const auto centroid = (vertices[0] + vertices[1] + vertices[2]) * g_float_one_third;
        // The two support corners give the extrema formerly found by scanning eight corners.
        const auto positive = glm::greaterThanEqual(normal, glm::vec3(0));
        const auto nearest = glm::mix(maximum, minimum, positive);
        const auto farthest = glm::mix(minimum, maximum, positive);
        if (glm::dot(nearest - centroid, normal) > 0 || glm::dot(farthest - centroid, normal) < 0)
            return false;
    }

    RuntimeTriangle edge_delta;
    for (unsigned edge = 0; edge < vertices.size(); ++edge)
        edge_delta[edge] = vertices[(edge + 1) % vertices.size()] - vertices[edge];

    for (short face_axis = 0; face_axis < 3; ++face_axis) {
        short first_axis = face_axis == 2 ? 0 : face_axis + 1;
        short second_axis = face_axis == 0 ? 2 : face_axis - 1;
        for (short side = 0; side < 2; ++side) {
            glm::vec2 intersection[2];
            short intersection_count = 0;
            float face = side == 0 ? minimum[face_axis] : maximum[face_axis];

            for (short edge = 0; edge < 3; ++edge) {
                if (static_cast<float>(g_double_one_ten_thousandth) <
                    static_cast<float>(fabs(edge_delta[edge][face_axis]))) {
                    float amount = (face - vertices[edge][face_axis]) / edge_delta[edge][face_axis];
                    if (g_float_zero <= amount && amount <= g_float_one) {
                        const auto crossing = vertices[edge] + amount * edge_delta[edge];
                        const float first = crossing[first_axis];
                        const float second = crossing[second_axis];
                        if (minimum[first_axis] <= first && first <= maximum[first_axis] &&
                            minimum[second_axis] <= second && second <= maximum[second_axis]) {
                            return true;
                        }
                        if (intersection_count < 2) {
                            intersection[intersection_count].x = first;
                            intersection[intersection_count].y = second;
                        }
                        ++intersection_count;
                    }
                }
            }

            if (intersection_count == 2) {
                const auto delta = intersection[1] - intersection[0];
                short rectangle_axes[2] = {first_axis, second_axis};
                for (short coordinate = 0; coordinate < 2; ++coordinate) {
                    if (g_float_one_ten_thousandth < static_cast<float>(fabs(delta[coordinate]))) {
                        short other = coordinate == 0 ? 1 : 0;
                        for (short edge_side = 0; edge_side < 2; ++edge_side) {
                            float boundary = edge_side == 0 ? minimum[rectangle_axes[coordinate]]
                                                            : maximum[rectangle_axes[coordinate]];
                            float amount =
                                (boundary - intersection[0][coordinate]) / delta[coordinate];
                            if (g_float_zero <= amount && amount <= g_float_one) {
                                float crossing = intersection[0][other] + amount * delta[other];
                                if (minimum[rectangle_axes[other]] <= crossing &&
                                    crossing <= maximum[rectangle_axes[other]]) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}
