#ifndef WIZ8_ENGINE_CODE_OCTREE_SPATIAL_H
#define WIZ8_ENGINE_CODE_OCTREE_SPATIAL_H

#include <cstdint>
#include <span>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/geometry.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/spatial/bounding_box.h"

struct W8GDSurface;

/* One 0xe8-byte region volume from the spatial state's region array. The
   region id at +4 is consumed by the particle-region builder, and the six
   plane equations at +0x88 are consumed by 0x0049E460. */
struct W8OctRegionVolume {
    std::uint32_t flags;
    unsigned short m_region;
    unsigned char positional[6];
    /* The bit the visibility pass tests and sets for this volume. */
    unsigned int m_region_bit;
    /* Two record dwords LoadRegionFile copies from the .cub record at +0x10
       and +0x18, and the dword it zeroes at +0x14. */
    std::uint32_t value_10;
    /* Region polygon count: the assignment pass increments it per contained
       polygon, and compaction remaps away regions where it stays zero. */
    std::uint32_t m_polygon_count;
    std::uint32_t value_18;
    /* Nine 12-byte points from +0x1c to +0x88; 0x004301C0 projects the first
       against the camera and falls back to the other eight. */
    glm::vec3 m_points[9]{};
    W8Plane m_planes[6];

    bool ContainsPoint(const glm::vec3& point) const;
};

// Decoded grid, bounds and region metadata for one compiled level.
struct W8OctSpatialState {
    BoundingBox bounds() const { return m_bounds; }
    BoundingBox GetClippedBounds() const { return m_clipped_bounds; }

    std::uint32_t flags{};
    float m_extent{};
    float m_cell_size{};
    BoundingBox m_bounds{};
    BoundingBox m_clipped_bounds{};
    unsigned short m_depth{};
    unsigned short m_region_count{};
    /* Packed auto-region cell coordinate bound per axis, derived from the
       leaf level when a loaded octree is initialized. */
    unsigned short m_region_cells_per_axis{};
    /* The bottom octree level: insertion stops and masks saturate here. */
    unsigned short m_leaf_level{};
    /* Auto-region grid pitch: packed (x<<16|y<<8|z) cell coordinates scale
       to world units through it. */
    float m_region_grid_cell{};
    W8OctRegionVolume* m_region_volumes{}; // Borrowed from the owning level.
    /* Maximum vertex distance from its region's center across the
       auto-regions. */
    float m_max_region_radius{};
    /* Leaf-grid strides: dim_y*dim_z for one x step, dim_z for one y
       step. */
    std::uint32_t m_leaf_grid_stride_x{};
    std::uint32_t m_leaf_grid_stride_y{};
    float m_node_extent{};
    /* Emitted submesh record bound: the build packs kind-0 then kind-1
       records beneath it. */
    std::uint32_t submesh_count{};
};

bool triangleIntersectsBox(const BoundingBox& box, std::span<const glm::vec3, 3> vertices,
                           const glm::vec3& normal);
bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const glm::vec3> vertices,
                          const BoundingBox& box);

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
    unsigned short m_region_count;
    /* The spatial state's leaf level; the reader derives the region mask and
       packed cell bound from it. */
    unsigned short m_leaf_level;
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
    unsigned short pad_aa;
    float m_region_cell;
    std::uint32_t m_edge_node_count;
    /* Path probe-clearance height. Retail never assigns this
       field - the file carries stack garbage - but the reader still loads
       it into +0x17c and feeds it to ConfigureForLevel. */
    float m_path_clearance;
    unsigned char m_prop_sun_bits;
    float m_max_region_radius;
    std::uint32_t m_prop_count;
    std::uint32_t m_particle_count;
    unsigned short m_particle_len;
    unsigned short m_prop_len;
    unsigned char pad_c9[0x2c];
};
#pragma pack(pop)

#endif
