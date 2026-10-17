#ifndef WIZ8_ENGINE_CODE_GDPROP_H
#define WIZ8_ENGINE_CODE_GDPROP_H

#include <span>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "surrender/srBoundingBox.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/geometry.h"

class srModelInstance;
class Trigger;
struct W8WorldItem;
struct W8GDSurface;

/* Engine Code\GDProp.cpp. Prop.cpp allocates 0x58 bytes for this object,
   constructs it at 0x004B6E00, and owns it at Prop+0x38. Assertions in the
   same-object method at 0x004B6F30 retain the original m_pGDSurfaces and
   m_pVertices member names and establish their offsets. */
class GDProp {
    struct Flags {
        bool always_blocks_path : 1;
        bool door : 1;
        bool unattached : 1;
        bool door_usable : 1;
        unsigned char reserved : 4;
        unsigned char reserved_byte;
    };

    friend class W8Prop;
    friend class W8PathingService;
    /* W8Octree's AABB occupancy test and W8GameData's prop-surface trace read
       the geometry members directly in retail. */
    friend class W8Octree;
    friend struct W8GameData;
    /* The sector item lists are built by the GDProp.cpp free helpers. */
    friend void AddItemToSector(int sector, W8WorldItem* item);
    friend void RemoveItemFromSector(int sector, W8WorldItem* item);

public:
    GDProp()
    {
        Flags initial_flags = {false, false, false, false, 0, 0};
        m_flags = initial_flags;
        m_prop_number = 0;
        m_trigger = 0;
        m_path_range.sentinel = -10000000.0f;
        m_path_bounds.max_z = 0;
        m_path_bounds.min_z = 0;
        m_path_bounds.max_x = 0;
        m_path_bounds.min_x = 0;
    }
    GDProp(srModelInstance* instance, const char* path_name, unsigned short prop_number,
           unsigned char footstep_surface, unsigned char footstep_material); /* 0x004B6E00 */
    GDProp(GDProp&&) noexcept = default;
    GDProp& operator=(GDProp&&) noexcept = default;
    GDProp(const GDProp&) = delete;
    GDProp& operator=(const GDProp&) = delete;
    void BindTrigger(Trigger* owner);
    bool ContainsPathCoordinate(unsigned short x, unsigned short y) const;
    bool HasSupportedItems();
    /* Rebuild the cached vertex and path bounds and return the complete box. */
    srBoundingBox computeBounds();
    std::span<const unsigned short> conditionalEdges() const { return m_path_edges; }
    std::span<const unsigned short> conditionalWaypoints() const { return m_path_waypoints; }
    /* Appends the index to the waypoint list when the grid point lies inside
       the path bounds; the prop owns the counted list. */
    unsigned char RegisterPathSurface(unsigned int index, const glm::ivec2& point);
    /* Appends the index to the link list when the segment touches the path
       bounds (one-cell tolerance on the crossed sides). */
    unsigned char RegisterPathVertex(unsigned int index, const glm::ivec2& point,
                                     const glm::ivec2& second);

private:
    void Initialize(srModelInstance* instance, bool attach, unsigned short prop_number,
                    unsigned char footstep_surface,
                    unsigned char footstep_material); /* 0x004B7060 */
    void PrepareGeometry(srModelInstance* instance);

    Flags m_flags;                        /* 0x00 */
    unsigned short m_prop_number;         /* 0x02 */
    unsigned int m_path_handle{};           /* 0x04 */
    std::vector<unsigned short> m_path_edges;
    std::vector<unsigned short> m_path_waypoints;
    std::vector<W8GDSurface> m_pGDSurfaces;
    std::vector<glm::vec3> m_pVertices;
    Trigger* m_trigger;                   /* 0x24: installed by 0x004B7470 */
    W8PathVerticalRange m_path_range;     /* Vertex AABB cached by computeBounds and tested by
       boundsOverlap. */
    glm::vec3 m_bound_min{0.0f};  /* 0x34 */
    glm::vec3 m_bound_max{0.0f};  /* 0x40 */
    W8PathGridBounds
        m_path_bounds; /* Owns the list buffer; item records belong to the world item list. */
    std::vector<W8WorldItem*> m_supported_items; /* 0x54 */
}; /* 0x58 */

void AddItemToSector(int sector, W8WorldItem* item);      /* 0x004B7AD0 */
void RemoveItemFromSector(int sector, W8WorldItem* item); /* 0x004B7B50 */

#endif
