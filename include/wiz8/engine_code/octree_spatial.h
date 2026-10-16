#ifndef WIZ8_ENGINE_CODE_OCTREE_SPATIAL_H
#define WIZ8_ENGINE_CODE_OCTREE_SPATIAL_H

#include <cstdint>
#include <span>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/geometry.h"
#include "wiz8/retail_geometry_types.h"

struct W8OctBuildNode;
struct W8GDSurface;

/* One 0xe8-byte region volume from the spatial state's region array. The
   region id at +4 is consumed by the particle-region builder, and the six
   plane equations at +0x88 are consumed by 0x0049E460. */
struct W8OctRegionVolume {
    w8_ulong flags;
    unsigned short m_region;
    unsigned char positional[6];
    /* The bit the visibility pass tests and sets for this volume. */
    unsigned int m_region_bit;
    /* Two record dwords LoadRegionFile copies from the .cub record at +0x10
       and +0x18, and the dword it zeroes at +0x14. */
    w8_ulong value_10;
    /* Region polygon count: the assignment pass increments it per contained
       polygon, and compaction remaps away regions where it stays zero. */
    w8_ulong m_polygon_count;
    w8_ulong value_18;
    /* Nine 12-byte points from +0x1c to +0x88; 0x004301C0 projects the first
       against the camera and falls back to the other eight. */
    glm::vec3 m_points[9]{};
    W8Plane m_planes[6];

    bool ContainsPoint(const glm::vec3& point) const;
};

// Shared runtime spatial values for loaded octrees and the collision index.
struct W8OctSpatialState {
    explicit W8OctSpatialState(const W8OctSpatialState* source = 0);

    srBoundingBox bounds() const { return m_bounds; }
    void setChildBounds(const W8OctSpatialState& parent, unsigned octant, float extent)
    {
        const glm::vec3 offset(octant >> 2, (octant >> 1) & 1, octant & 1);
        const auto minimum = parent.m_bounds.minimum + offset * extent;
        m_bounds.minimum = minimum;
        m_bounds.maximum = minimum + extent;
    }

    srBoundingBox regionCellBounds(w8_ulong path) const
    {
        const glm::vec3 cell((path >> 16) & 0xff, (path >> 8) & 0xff, path & 0xff);
        const auto minimum = cell * m_region_grid_cell + m_bounds.minimum;
        return {minimum, minimum + m_region_grid_cell};
    }

    void Reset();
    srBoundingBox GetClippedBounds() const;

    w8_ulong flags;
    float m_extent;
    float m_cell_size;
    srBoundingBox m_bounds;
    srBoundingBox m_clipped_bounds;
    /* Mode-2 insertions increment this once per region polygon; the build
       conversion copies it to the pre-tree and sizes m_visited_polygon_bits from it. */
    w8_ulong m_polygon_count;
    w8_ulong m_item_count;
    unsigned short m_depth;
    unsigned short m_region_count;
    unsigned short m_item_limit;
    /* Packed auto-region cell coordinate bound per axis, derived from the
       leaf level when a loaded octree is initialized. */
    unsigned short m_region_cells_per_axis;
    /* The bottom octree level: insertion stops and masks saturate here. */
    unsigned short m_leaf_level;
    /* Auto-region grid pitch: packed (x<<16|y<<8|z) cell coordinates scale
       to world units through it. */
    float m_region_grid_cell;
    /* Auto-region id allocator bound: each new region takes this value and
       bumps it; m_region_count mirrors it during the build. */
    unsigned short m_region_id_bound;
    W8OctRegionVolume* m_region_volumes; // Borrowed from the root runtime or build tree.
    /* Maximum vertex distance from its region's center across the
       auto-regions. */
    float m_max_region_radius;
    /* Leaf-grid strides: dim_y*dim_z for one x step, dim_z for one y
       step. */
    w8_ulong m_leaf_grid_stride_x;
    w8_ulong m_leaf_grid_stride_y;
    unsigned short m_level_kind;
    float m_node_extent;
    /* Emitted submesh record bound: the build packs kind-0 then kind-1
       records beneath it. */
    w8_ulong submesh_count;
    srBoundingBox m_working_bounds;
    /* The build octree root: W8OctBuildNode or the counted subclass
       when the owning build tree counts surfaces per node. */
    W8OctBuildNode* m_root;
    w8_ulong m_node_index;
    /* The working triangle's three vertices, borrowed from the inserter's
       stack for the recursion's bounds tests. */
    const glm::vec3* m_triangle_vertices;
};

bool triangleIntersectsBox(const srBoundingBox& box, std::span<const glm::vec3, 3> vertices,
                           const glm::vec3& normal);
bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const glm::vec3> vertices,
                          const srBoundingBox& box);

#pragma pack(push, 1)
struct W8OctFileHeader {
    enum { VERSION = 0x22 };
    unsigned short version; /* written 0x22 */
    float m_extent;
    float m_cell_size;
    float m_node_extent;
    wiz8::retail::Vec3f m_bounds[6];
    /* The uiLeaf grid dimensions: three integer cell counts serialized in the
       vector slot. */
    struct {
        std::uint32_t x, y, z;
    } m_grid_dims;
    unsigned short m_depth;
    /* Auto-region id bound; the reader sizes the region-indexed
       m_pfRegsVisited array from it. */
    unsigned short m_region_id_bound;
    /* The finished submesh record bound (the octree's +0x74), not a
       mesh-file count. */
    w8_ulong m_submesh_count;
    w8_ulong m_branch_count;
    w8_ulong m_leaf_count;
    w8_ulong m_polygon_count;
    w8_ulong m_vertex_count;
    w8_ulong m_surface_count;
    w8_ulong m_path_nodes;
    /* Dword length of the count-prefixed per-leaf polygon-id stream. */
    w8_ulong m_leaf_polygon_stream_len;
    /* Dword length of the per-leaf GD surface-id stream; doubles as the
       GameData-block presence gate on the read side. */
    w8_ulong m_gd_surface_stream_len;
    /* Trigger-list element count; always 0 - the machinery is vestigial and
       the reader even uses a different element size than the writer. */
    w8_ulong m_trigger_count;
    w8_ulong m_zero;
    /* u16 length of the per-leaf 0-terminated region-id stream. */
    w8_ulong m_region_list_len;
    unsigned short m_region_count;
    /* The spatial state's leaf level; the reader derives the region mask and
       packed cell bound from it. */
    unsigned short m_leaf_level;
    /* Kind-0 emitted submesh count; carries the same value as
       m_mesh_total. */
    w8_ulong m_root_mesh_count;
    w8_ulong m_mesh_total;
    /* Kind-1 emitted submesh count.  Serialized for information only - the
       lookup tables on both sides size from m_mesh_total. */
    w8_ulong m_kind1_submesh_count;
    /* +0xa6 and +0xb9 serialize m_spatial's +0x54/+0x60 floats; retail
       copies them with plain movs, which requires float-typed fields. */
    float m_region_grid_cell;
    unsigned short pad_aa;
    float m_region_cell;
    w8_ulong m_edge_node_count;
    /* Path probe-clearance height. Retail never assigns this
       field - the file carries stack garbage - but the reader still loads
       it into +0x17c and feeds it to ConfigureForLevel. */
    float m_path_clearance;
    unsigned char m_prop_sun_bits;
    float m_max_region_radius;
    w8_ulong m_prop_count;
    w8_ulong m_particle_count;
    unsigned short m_particle_len;
    unsigned short m_prop_len;
    unsigned char pad_c9[0x2c];
};
#pragma pack(pop)

#endif
