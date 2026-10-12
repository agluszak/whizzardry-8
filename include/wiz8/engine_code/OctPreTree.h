#include <cstdint>
#include "glm/ext/vector_int3.hpp"
#ifndef WIZ8_ENGINE_CODE_OCT_PRE_TREE_H
#define WIZ8_ENGINE_CODE_OCT_PRE_TREE_H

#include "surrender/srMath.h"
#include "surrender/srBoundingBox.h"
#include "glm/ext/vector_float3.hpp"
#include <span>
#include "wiz8/geometry.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"

struct W8OctBuildNode;
struct W8GDSurface;

/* One 0xe8-byte region volume from the spatial state's region array. The
   region id at +4 is consumed by the particle-region builder, and the six
   plane equations at +0x88 are consumed by 0x0049E460. */
struct W8OctRegionVolume {
    std::uint32_t flags;
    std::uint16_t m_region;
    std::uint8_t positional[6];
    /* The bit the visibility pass tests and sets for this volume. */
    std::uint32_t m_region_bit;
    /* Two record dwords LoadRegionFile copies from the .cub record at +0x10
       and +0x18, and the dword it zeroes at +0x14. */
    std::uint32_t value_10;
    /* Region polygon count: the assignment pass increments it per contained
       polygon, and compaction remaps away regions where it stays zero. */
    std::uint32_t m_polygon_count;
    std::uint32_t value_18;
    /* Nine 12-byte points from +0x1c to +0x88; 0x004301C0 projects the first
       against the camera and falls back to the other eight. */
    srVector3T<float> m_points[9];
    W8Plane m_planes[6];

    bool ContainsPoint(const glm::vec3& point) const;
};

/* Native spatial state derived from the retail 0x9c record. Octree.cpp constructs one
   at the start of its 0x29c owner, while OctBuildTree.cpp constructs the same
   value at the start of its 0xbc build tree and uses standalone copies while
   inserting surfaces.  Same-address construction proves the common value
   boundary, but not whether either owner used inheritance or a first member.
   Copies borrow the region/root/triangle pointers; the destructor only clears
   them. W8Octree releases its region allocation separately. */
struct W8OctSpatialState {
    explicit W8OctSpatialState(const W8OctSpatialState* source = 0);

    srBoundingBox bounds() const { return m_bounds; }
    void setChildBounds(const W8OctSpatialState& parent, std::uint32_t octant, float extent)
    {
        const glm::vec3 offset(octant >> 2, (octant >> 1) & 1, octant & 1);
        const auto minimum = parent.m_bounds.minimum + offset * extent;
        m_bounds.minimum = minimum;
        m_bounds.maximum = minimum + extent;
    }

    srBoundingBox regionCellBounds(std::uint32_t path) const
    {
        const glm::vec3 cell((path >> 16) & 0xff, (path >> 8) & 0xff, path & 0xff);
        const auto minimum = cell * m_region_grid_cell + m_bounds.minimum;
        return {minimum, minimum + m_region_grid_cell};
    }

    void Reset();
    srBoundingBox GetClippedBounds() const;
    srBoundingBox GetWorkingBounds() const;
    void setWorkingBounds(const srBoundingBox& bounds);

    std::uint32_t flags;
    float m_extent;
    float m_cell_size;
    srBoundingBox m_bounds;
    srBoundingBox m_clipped_bounds;
    /* Mode-2 insertions increment this once per region polygon; the build
       conversion copies it to the pre-tree and sizes m_visited_polygon_bits from it. */
    std::uint32_t m_polygon_count;
    std::uint32_t m_item_count;
    std::uint16_t m_depth;
    std::uint16_t m_region_count;
    std::uint16_t m_item_limit;
    /* Packed auto-region cell coordinate bound per axis, derived from the
       leaf level when a loaded octree is initialized. */
    std::uint16_t m_region_cells_per_axis;
    /* The bottom octree level: insertion stops and masks saturate here. */
    std::uint16_t m_leaf_level;
    /* Auto-region grid pitch: packed (x<<16|y<<8|z) cell coordinates scale
       to world units through it. */
    float m_region_grid_cell;
    /* Auto-region id allocator bound: each new region takes this value and
       bumps it; m_region_count mirrors it during the build. */
    std::uint16_t m_region_id_bound;
    W8OctRegionVolume* m_region_volumes;
    /* Maximum vertex distance from its region's center across the
       auto-regions. */
    float m_max_region_radius;
    /* Leaf-grid strides: dim_y*dim_z for one x step, dim_z for one y
       step. */
    std::uint32_t m_leaf_grid_stride_x;
    std::uint32_t m_leaf_grid_stride_y;
    std::uint16_t m_level_kind;
    float m_node_extent;
    /* Emitted submesh record bound: the build packs kind-0 then kind-1
       records beneath it. */
    std::uint32_t submesh_count;
    srBoundingBox m_working_bounds;
    /* The build octree root: W8OctBuildNode or the counted subclass
       when the owning build tree counts surfaces per node. */
    W8OctBuildNode* m_root;
    std::uint32_t m_node_index;
    /* The working triangle's three vertices, borrowed from the inserter's
       stack for the recursion's bounds tests. */
    const glm::vec3* m_triangle_vertices;
};

bool triangleIntersectsBox(const srBoundingBox& box, std::span<const glm::vec3, 3> vertices,
                           const glm::vec3& normal);
bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const srVector3> vertices,
                          const srBoundingBox& box);

struct W8OctRegionPolygon;

/* The 0x60-byte build vertex the 0x00493120 driver collects into the shared
   geometry context.  CreateSubMeshes reads the position at +0x0c, material
   at +0x1c, normal at +0x24, light at +0x30 and the per-sun light array
   pointer at +0x3c; the driver also clears flag bytes at +0x0a and +0x6a. */
struct W8OctPreTreeVertex {
    enum { EXCLUDED = 1u, NORMAL_SPLIT = 2u, MULTIPLE_REGIONS = 4u };
    /* bit0: excluded from compaction (welded or unused); for welded vertices
       m_vertex_index holds the redirect; bit1: split normal copy; bit2: sits inside more than one
       region volume (m_region zeroed). */
    std::uint32_t flags;
    /* Ordinal into the geometry vertex array; on welded vertices the merge
       helper stores the surviving vertex's index here. */
    std::uint32_t m_vertex_index;
    /* Owning auto-region id written by the region assignment pass. */
    std::uint16_t m_region;
    /* Per-vertex traversal latch the shared-polygon split raises so each
       vertex's face run is walked once per pass. */
    bool m_visited;
    std::uint8_t padding_0b;
    glm::vec3 position;
    /* Number of polygons referencing this vertex; SortGeometry counts and
       the region pass tracks the run bound. */
    std::int16_t m_normal_count;
    std::uint8_t padding_1a[2];
    std::int32_t m_material;
    /* The polygon automesh kind SplitVertices copies onto material-split
       duplicates. */
    std::uint32_t m_kind;
    glm::vec3 m_normal;
    glm::vec3 m_light;
    float* m_sun_lights;
    std::uint16_t face_count;
    std::uint8_t padding_42[2];
    /* Growable run of polygon ordinals sharing this vertex, built by the
       region pass; consecutive vertices may share one allocation. */
    std::int32_t* face_indices;
    /* Second owned run freed by the geometry cleanup. */
    std::int32_t* owned;
    /* Corner texture coordinate written when a polygon vertex is split. */
    glm::vec2 m_uv;
    /* Unscaled level-file position kept beside the engine-scaled
       position. */
    glm::vec3 m_original_position;
};

/* The shared geometry context the 0x00493120 driver builds and hands to
   CreateSubMeshes, SplitMeshes and WriteOctFile: the deduplicated build
   vertices plus the 0x74-byte region polygons.  The material sort appends
   the canonical material/texture group counts; +0x10/+0x14 hold an owned
   buffer the cleanup releases. */
struct W8OctPreTreeGeometry {
    bool splitVerticesByMaterial();
    bool compact();
    std::uint32_t vertex_count;
    W8OctPreTreeVertex* m_vertices;
    std::uint32_t m_polygon_count;
    W8OctRegionPolygon* m_polygons;
    std::uint32_t positional2;
    void* owned;
    std::uint32_t m_material_count;
    std::uint32_t m_texture_count;
    /* Largest per-vertex polygon reference count, refreshed by the region
       assignment pass. */
    std::uint16_t m_max_face_count;
    std::uint8_t positional6[2];

    /* Grow an owned face-index run at capacity boundaries. Retail narrows
       count + capacity to 16 bits before allocation and returns a byte. */
    std::uint8_t CheckArrayLength(std::int32_t** run, std::uint16_t count, std::uint16_t capacity);

    /* Frees the per-vertex polygon runs, both arrays and the owned +0x14
       buffer; the preprocessing driver runs it during cleanup. */
    void Release();
};

/* The 0x34-byte per-submesh build record SplitMeshes partitions polygons and
   vertices into.  Records chain through +0x0c/+0x10 (the OctMeshModel
   m_link_index/next_link link fields are emitted as these indices minus
   one) and +0x08 carries the 1..3 mesh kind that becomes m_packed_header. */
struct W8OctSubmeshBuild {
    std::uint32_t flags;
    std::uint32_t index;
    std::uint32_t m_kind;
    std::uint32_t m_prev_link;
    std::uint32_t m_next_link;
    std::uint32_t vertex_count;
    std::uint32_t m_map_count;
    std::uint32_t m_polygon_count;
    std::int32_t* m_vertex_ids;
    std::int32_t* m_polygon_ids;
    /* Per-corner vertex indices (three per polygon); becomes the model's
       m_psrPolyVertex. */
    glm::ivec3* m_poly_vertices;
    /* Per-corner deduplicated uv indices; becomes the model's
       m_psrPolyUVIndex. */
    glm::ivec3* m_poly_uv_index;
    /* The deduplicated uv pairs SplitUVMaps emits; becomes m_psrMap. */
    glm::vec2* m_uv_map;
};

/* The 0xf5-byte NewLevel.oct file header shared by WriteOctFile and
   ReadOctFile. The offsetof assertions pin the authoritative byte offsets. The
   seven consecutive vectors hold the spatial state's minimum, maximum,
   clipped and working bounds plus the octree's +0xa4 vector.  +0xb4 is left
   unwritten by retail; ReadOctFile still loads it into +0x17c. */
#pragma pack(push, 1)
struct W8OctFileHeader {
    enum { VERSION = 0x22 };
    std::uint16_t version; /* written 0x22 */
    float m_extent;
    float m_cell_size;
    float m_node_extent;
    srVector3T<float> m_bounds[6];
    /* The uiLeaf grid dimensions: three integer cell counts serialized in the
       vector slot. */
    srVector3T<std::uint32_t> m_grid_dims;
    std::uint16_t m_depth;
    /* Auto-region id bound; the reader sizes the region-indexed
       m_pfRegsVisited array from it. */
    std::uint16_t m_region_id_bound;
    /* The finished submesh record bound (the octree's +0x74), not a
       mesh-file count. */
    std::uint32_t m_submesh_count;
    std::uint32_t m_branch_count;
    std::uint32_t m_leaf_count;
    std::uint32_t m_polygon_count;
    std::uint32_t m_vertex_count;
    std::uint32_t m_surface_count;
    std::uint32_t m_path_nodes;
    /* Dword length of the count-prefixed per-leaf polygon-id stream. */
    std::uint32_t m_leaf_polygon_stream_len;
    /* Dword length of the per-leaf GD surface-id stream; doubles as the
       GameData-block presence gate on the read side. */
    std::uint32_t m_gd_surface_stream_len;
    /* Trigger-list element count; always 0 - the machinery is vestigial and
       the reader even uses a different element size than the writer. */
    std::uint32_t m_trigger_count;
    std::uint32_t m_zero;
    /* u16 length of the per-leaf 0-terminated region-id stream. */
    std::uint32_t m_region_list_len;
    std::uint16_t m_region_count;
    /* The spatial state's leaf level; the reader derives the region mask and
       packed cell bound from it. */
    std::uint16_t m_leaf_level;
    /* Kind-0 emitted submesh count; carries the same value as
       m_mesh_total. */
    std::uint32_t m_root_mesh_count;
    std::uint32_t m_mesh_total;
    /* Kind-1 emitted submesh count.  Serialized for information only - the
       lookup tables on both sides size from m_mesh_total. */
    std::uint32_t m_kind1_submesh_count;
    /* +0xa6 and +0xb9 serialize m_spatial's +0x54/+0x60 floats; retail
       copies them with plain movs, which requires float-typed fields. */
    float m_region_grid_cell;
    std::uint16_t pad_aa;
    float m_region_cell;
    std::uint32_t m_edge_node_count;
    /* Path probe-clearance height. Retail never assigns this
       field - the file carries stack garbage - but the reader still loads
       it into +0x17c and feeds it to ConfigureForLevel. */
    float m_path_clearance;
    std::uint8_t m_prop_sun_bits;
    float m_max_region_radius;
    std::uint32_t m_prop_count;
    std::uint32_t m_particle_count;
    std::uint16_t m_particle_len;
    std::uint16_t m_prop_len;
    std::uint8_t pad_c9[0x2c];
};
#pragma pack(pop)

#endif
