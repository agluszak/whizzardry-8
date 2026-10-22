#pragma once

/* The .wgd game-data surface record. Live surfaces carry a hit-plane
   pointer; on disk that slot is a presence word. */

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/geometry.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"

struct LevelEnvironment;
struct W8OctRegionVolume;

namespace wiz8::retail {

#pragma pack(push, 1)
// These records contain values only. Live GLM vectors never enter raw transfers.
struct Plane {
    Vec3f normal;
    float w;
};

/* The on-disk W8GDSurface; field meanings are documented there. */
struct GDSurface {
    unsigned int flags;
    unsigned int index;
    int trigger_index;
    int edge_link[3];
    int vertex_indices[3];
    Plane plane;
    float distance;
    std::uint32_t hit_plane;
    unsigned char footstep_surface;
    unsigned char footstep_material;
    unsigned char positional[2];
    float contact_margin;
    unsigned int chance;
    float slope;
};

struct Environ {
    std::int32_t value_00;
    std::uint8_t ground_latch, airborne, pad_06[2];
    std::int32_t value_08;
    float scale, gravity_x, gravity_y, gravity_z, motion_step, motion_factor;
    Vec3f vector;
    float world_height, forward_scale, motion_limit, momentum_scale, value_40;
};
struct RegionVolume {
    std::uint32_t flags;
    std::uint16_t m_region;
    std::uint8_t positional[6];
    std::uint32_t m_region_bit, value_10, m_polygon_count, value_18;
    Vec3f m_points[9];
    Plane m_planes[6];
};
#pragma pack(pop)
static_assert(sizeof(Vec3f) == 12 && alignof(Vec3f) == 1);
static_assert(sizeof(Plane) == 16 && alignof(Plane) == 1);
static_assert(sizeof(Environ) == 0x44);
static_assert(sizeof(RegionVolume) == 0xe8);
static_assert(sizeof(GDSurface) == 0x4c);
static_assert(std::is_trivially_copyable_v<GDSurface>);

template<> struct Record<W8GDSurface> {
    using Disk = GDSurface;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text text)
    {
        const auto field = [&](std::size_t offset, std::size_t size) {
            return bytes.subspan(offset, size);
        };
        value(&runtime.flags, field(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.index, field(offsetof(Disk, index), sizeof(Disk::index)));
        value(&runtime.trigger_index,
              field(offsetof(Disk, trigger_index), sizeof(Disk::trigger_index)));
        value(&runtime.edge_link, field(offsetof(Disk, edge_link), sizeof(Disk::edge_link)));
        value(&runtime.vertex_indices,
              field(offsetof(Disk, vertex_indices), sizeof(Disk::vertex_indices)));
        value(&runtime.plane.normal.x, field(offsetof(Disk, plane) + 0, 4));
        value(&runtime.plane.normal.y, field(offsetof(Disk, plane) + 4, 4));
        value(&runtime.plane.normal.z, field(offsetof(Disk, plane) + 8, 4));
        value(&runtime.plane.w, field(offsetof(Disk, plane) + 12, 4));
        value(&runtime.distance, field(offsetof(Disk, distance), sizeof(Disk::distance)));
        // Runtime contact planes are per movement step; disk pointer slots are discarded.
        std::uint32_t unused_contact = 0;
        value(&unused_contact, field(offsetof(Disk, hit_plane), sizeof(Disk::hit_plane)));
        value(&runtime.footstep_surface,
              field(offsetof(Disk, footstep_surface), sizeof(Disk::footstep_surface)));
        value(&runtime.footstep_material,
              field(offsetof(Disk, footstep_material), sizeof(Disk::footstep_material)));
        value(&runtime.positional, field(offsetof(Disk, positional), sizeof(Disk::positional)));
        value(&runtime.contact_margin,
              field(offsetof(Disk, contact_margin), sizeof(Disk::contact_margin)));
        value(&runtime.chance, field(offsetof(Disk, chance), sizeof(Disk::chance)));
        value(&runtime.slope, field(offsetof(Disk, slope), sizeof(Disk::slope)));
    }
};

template<> struct Record<LevelEnvironment> {
    using Disk = Environ;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        value(&runtime.value_00, bytes.subspan(offsetof(Disk, value_00), sizeof(Disk::value_00)));
        value(&runtime.ground_latch,
              bytes.subspan(offsetof(Disk, ground_latch), sizeof(Disk::ground_latch)));
        value(&runtime.airborne, bytes.subspan(offsetof(Disk, airborne), sizeof(Disk::airborne)));
        value(&runtime.serialized.pad_06,
              bytes.subspan(offsetof(Disk, pad_06), sizeof(Disk::pad_06)));
        value(&runtime.value_08, bytes.subspan(offsetof(Disk, value_08), sizeof(Disk::value_08)));
        value(&runtime.scale, bytes.subspan(offsetof(Disk, scale), sizeof(Disk::scale)));
        value(&runtime.gravity_x,
              bytes.subspan(offsetof(Disk, gravity_x), sizeof(Disk::gravity_x)));
        value(&runtime.gravity_y,
              bytes.subspan(offsetof(Disk, gravity_y), sizeof(Disk::gravity_y)));
        value(&runtime.gravity_z,
              bytes.subspan(offsetof(Disk, gravity_z), sizeof(Disk::gravity_z)));
        value(&runtime.motion_step,
              bytes.subspan(offsetof(Disk, motion_step), sizeof(Disk::motion_step)));
        value(&runtime.motion_factor,
              bytes.subspan(offsetof(Disk, motion_factor), sizeof(Disk::motion_factor)));
        value(&runtime.vector.x, bytes.subspan(offsetof(Disk, vector) + 0, 4));
        value(&runtime.vector.y, bytes.subspan(offsetof(Disk, vector) + 4, 4));
        value(&runtime.vector.z, bytes.subspan(offsetof(Disk, vector) + 8, 4));
        value(&runtime.world_height,
              bytes.subspan(offsetof(Disk, world_height), sizeof(Disk::world_height)));
        value(&runtime.forward_scale,
              bytes.subspan(offsetof(Disk, forward_scale), sizeof(Disk::forward_scale)));
        value(&runtime.motion_limit,
              bytes.subspan(offsetof(Disk, motion_limit), sizeof(Disk::motion_limit)));
        value(&runtime.momentum_scale,
              bytes.subspan(offsetof(Disk, momentum_scale), sizeof(Disk::momentum_scale)));
        value(&runtime.value_40, bytes.subspan(offsetof(Disk, value_40), sizeof(Disk::value_40)));
    }
};

template<> struct Record<W8OctRegionVolume> {
    using Disk = RegionVolume;
    template<class Runtime, class Bytes, class Value, class Text>
    static void fields(Runtime& runtime, Bytes bytes, Value value, Text)
    {
        value(&runtime.flags, bytes.subspan(offsetof(Disk, flags), sizeof(Disk::flags)));
        value(&runtime.m_region, bytes.subspan(offsetof(Disk, m_region), sizeof(Disk::m_region)));
        value(&runtime.positional,
              bytes.subspan(offsetof(Disk, positional), sizeof(Disk::positional)));
        value(&runtime.m_region_bit,
              bytes.subspan(offsetof(Disk, m_region_bit), sizeof(Disk::m_region_bit)));
        value(&runtime.value_10, bytes.subspan(offsetof(Disk, value_10), sizeof(Disk::value_10)));
        value(&runtime.m_polygon_count,
              bytes.subspan(offsetof(Disk, m_polygon_count), sizeof(Disk::m_polygon_count)));
        value(&runtime.value_18, bytes.subspan(offsetof(Disk, value_18), sizeof(Disk::value_18)));
        for (std::size_t i = 0; i < 9; ++i) {
            value(&runtime.m_points[i].x, bytes.subspan(offsetof(Disk, m_points) + i * 12 + 0, 4));
            value(&runtime.m_points[i].y, bytes.subspan(offsetof(Disk, m_points) + i * 12 + 4, 4));
            value(&runtime.m_points[i].z, bytes.subspan(offsetof(Disk, m_points) + i * 12 + 8, 4));
        }
        for (std::size_t i = 0; i < 6; ++i) {
            value(&runtime.m_planes[i].normal.x,
                  bytes.subspan(offsetof(Disk, m_planes) + i * 16 + 0, 4));
            value(&runtime.m_planes[i].normal.y,
                  bytes.subspan(offsetof(Disk, m_planes) + i * 16 + 4, 4));
            value(&runtime.m_planes[i].normal.z,
                  bytes.subspan(offsetof(Disk, m_planes) + i * 16 + 8, 4));
            value(&runtime.m_planes[i].w, bytes.subspan(offsetof(Disk, m_planes) + i * 16 + 12, 4));
        }
    }
};

} // namespace wiz8::retail
