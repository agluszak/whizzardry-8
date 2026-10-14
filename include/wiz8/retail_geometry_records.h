#pragma once

/* The .wgd game-data surface record. Live surfaces carry a hit-plane
   pointer; on disk that slot is a presence word. */

#include <cstdint>
#include <type_traits>

#include "wiz8/geometry.h"
#include "wiz8/retail_text_records.h"

namespace wiz8::retail {

#pragma pack(push, 1)
/* The on-disk W8GDSurface; field meanings are documented there. */
struct GDSurface {
    unsigned int flags;
    unsigned int index;
    int trigger_index;
    int edge_link[3];
    int vertex_indices[3];
    W8Plane plane;
    float distance;
    std::uint32_t hit_plane;
    unsigned char footstep_surface;
    unsigned char footstep_material;
    unsigned char positional[2];
    float contact_margin;
    unsigned int chance;
    float slope;
};
#pragma pack(pop)
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
        value(&runtime.plane, field(offsetof(Disk, plane), sizeof(Disk::plane)));
        value(&runtime.distance, field(offsetof(Disk, distance), sizeof(Disk::distance)));
        pointer_slot(runtime.hit_plane, field(offsetof(Disk, hit_plane), sizeof(Disk::hit_plane)));
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

} // namespace wiz8::retail
