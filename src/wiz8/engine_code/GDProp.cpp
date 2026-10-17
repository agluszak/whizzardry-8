#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>

#include "geometry.h"
#include "glm/ext/matrix_float4x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "navigation_flags.h"
#include "surrender/srBoundingBox.h"
#include "wiz8/containers.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/surface_geometry.h"
/* Engine Code\GDProp.cpp */

#include "surrender/srModelInstance.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/octree_spatial.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/layouts/world.h"

// FUNCTION: WIZ8 0x004b6e00
GDProp::GDProp(srModelInstance* instance, const char* path_name, unsigned short prop_number,
               unsigned char footstep_surface, unsigned char footstep_material)
{
    Flags initial_flags = {false, false, false, false, 0, 0};
    m_flags = initial_flags;
    m_path_handle = 0;
    m_prop_number = 0;
    m_trigger = 0;
    m_path_range.sentinel = -10000000.0f;
    m_path_bounds.max_z = 0;
    m_path_bounds.min_z = 0;
    m_path_bounds.max_x = 0;
    m_path_bounds.min_x = 0;

    if (g_octree != 0 && g_octree->pathing != 0) {
        m_path_handle = g_octree->pathing->FindPathHandle(path_name, &m_path_bounds, &m_path_range);
    }

    if (instance != 0) {
        if (m_path_handle != 0 && g_octree->pathing != 0) {
            g_octree->pathing->LinkSurfaces(this);
            g_octree->pathing->LinkEdges(this);
        }
        Initialize(instance, true, prop_number, footstep_surface, footstep_material);
    }
}

/* Resize geometry as one owned pair of tables, preserving existing records
   when the linked mesh's counts are unchanged. */
void GDProp::PrepareGeometry(srModelInstance* instance)
{
    int surface_count = 0;
    int vertex_count = 0;
    for (auto* mesh = static_cast<stMeshModel*>(instance->getModel()); mesh; mesh = mesh->Next()) {
        surface_count += mesh->polygon_count;
        vertex_count += mesh->vertex_location_count;
    }
    if (m_pGDSurfaces.size() != static_cast<std::size_t>(surface_count))
        m_pGDSurfaces.assign(surface_count, W8GDSurface{});
    if (m_pVertices.size() != static_cast<std::size_t>(vertex_count))
        m_pVertices.assign(vertex_count, glm::vec3(0));
}

/* Rebuild the prop's collision geometry from its current linked mesh and
   publish the corresponding conditional path state. Vertices are transformed
   into world space before each accumulated triangle receives its plane,
   dominant axis, slope classification and caller-provided material bytes. */
// FUNCTION: WIZ8 0x004b7060
void GDProp::Initialize(srModelInstance* instance, bool attach, unsigned short prop_number,
                        unsigned char footstep_surface, unsigned char footstep_material)
{
    if (!attach) {
        m_flags.unattached = true;
    } else {
        m_flags.unattached = false;
        m_prop_number = prop_number;
    }

    PrepareGeometry(instance);
    auto world_matrix = instance->getWorldSpaceMatrix<glm::mat4>();

    int vertex_base = 0;
    int surface_total = 0;
    stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());
    while (mesh != 0) {
        unsigned int mesh_flags = mesh->flags;
        glm::ivec3* polygon_vertices = mesh->getPolyVertex();
        for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
            W8GDSurface* surface = &m_pGDSurfaces[surface_total + polygon];
            surface->vertex_indices[0] = polygon_vertices[polygon].x + vertex_base;
            surface->vertex_indices[1] = polygon_vertices[polygon].y + vertex_base;
            surface->vertex_indices[2] = polygon_vertices[polygon].z + vertex_base;
        }
        surface_total += mesh->polygon_count;

        glm::vec3* source_vertices = mesh->getVertexLoc();
        surfaceGeometry::transformPositions(
            {m_pVertices.data() + vertex_base,
             static_cast<std::size_t>(mesh->vertex_location_count)},
            glm::mat4x3(world_matrix), [&](std::size_t vertex) { return source_vertices[vertex]; });
        vertex_base += mesh->vertex_location_count;

        for (int surface_index = 0; surface_index < surface_total; ++surface_index) {
            W8GDSurface* surface = &m_pGDSurfaces[surface_index];
            surfaceGeometry::initializePropSurface(*surface, m_pVertices);
            surface->footstep_surface = footstep_surface;
            surface->footstep_material = footstep_material;
            if ((mesh_flags & W8_MESH_SORTED_RENDERING) != 0) {
                surface->flags |= W8_GD_SURFACE_SKIP_FILTERED_TRACE;
            }

            if (0.5f <= surface->plane.normal.y) {
                if (0.75f < surface->plane.normal.y) {
                    surface->slope = 1.0f;
                } else {
                    surface->slope = surface->plane.normal.y;
                }
            }
        }
        mesh = mesh->Next();
    }

    W8PathingService* pathing = g_octree->pathing;
    if (m_path_handle != 0 && pathing != 0) {
        if (!attach) {
            if (m_prop_number != 0xffff) {
                m_prop_number = 0xffff;
                pathing->SetConditionalPathFrame(m_path_handle, 0xffff);
            }
        } else {
            pathing->SetConditionalPathFrame(m_path_handle, m_prop_number);
            if (!m_path_waypoints.empty()) {
                pathing->CheckConditionalWayPtStatus(m_path_waypoints);
            }
            if (!m_path_edges.empty()) {
                pathing->CheckConditionalLinkStatus(m_path_edges);
            }
        }
    }

    Trigger* owner = m_trigger;
    if (owner != 0 && attach) {
        W8TriggerActionData* action = owner->m_pActionData.get();
        if (action != 0 && action->type == W8_TRIGGER_PAYLOAD_DOOR) {
            unsigned int flags = W8_PATH_CELL_DOOR;
            if ((owner->lock_state.lock_type != 0 &&
                 owner->lock_state.device_state.completed == 0) ||
                ((owner->flags & W8_TRIGGER_ENABLED) == 0 ||
                 static_cast<W8DoorTriggerActionData*>(action)->open ||
                 static_cast<W8DoorTriggerActionData*>(action)->locked)) {
                flags = W8_PATH_CELL_DOOR | W8_PATH_CELL_BLOCKED;
            }
            if (pathing != 0) {
                pathing->UpdateConditionalPathFlags(m_path_handle, m_prop_number, flags);
            }
        }
    }

    for (W8WorldItem* item : m_supported_items) {
        if (item) {
            SetWorldItemFalling(item, true);
        }
    }
}

/* Attach the product-side prop owner and immediately mirror its active state
   into both the local GD flags and the conditional path table. */
// FUNCTION: WIZ8 0x004b7470
void GDProp::BindTrigger(Trigger* owner)
{
    m_trigger = owner;
    W8TriggerActionData* action = owner->m_pActionData.get();
    if (action != 0 && action->type == W8_TRIGGER_PAYLOAD_DOOR) {
        if (action != 0) {
            m_flags.door = true;
            unsigned int path_flags = W8_PATH_CELL_DOOR;
            if ((owner->lock_state.lock_type == 0 ||
                 owner->lock_state.device_state.completed != 0) &&
                (owner->flags & W8_TRIGGER_ENABLED) != 0 &&
                !static_cast<W8DoorTriggerActionData*>(action)->open &&
                !static_cast<W8DoorTriggerActionData*>(action)->locked) {
                m_flags.door_usable = true;
            } else {
                path_flags = W8_PATH_CELL_DOOR | W8_PATH_CELL_BLOCKED;
                m_flags.door_usable = false;
            }

            W8PathingService* pathing = g_octree->pathing;
            if (pathing != 0) {
                pathing->UpdateConditionalPathFlags(m_path_handle, m_prop_number, path_flags);
            }
        }
    }
}

/* Scan the transformed vertices for the axis-aligned bounds. The cached box
   feeds the path bookkeeping range/sentinel and is returned as a native value. */
// FUNCTION: WIZ8 0x004b7500
srBoundingBox GDProp::computeBounds()
{
    const auto bounds = boundingBox(std::span<const glm::vec3>(m_pVertices));
    m_bound_max = bounds.maximum;
    m_bound_min = bounds.minimum;

    m_path_range.minimum = m_bound_min.y;
    m_path_range.maximum = m_bound_max.y;
    m_path_range.sentinel = (m_bound_min.y + m_bound_max.y) * 0.5f;

    return bounds;
}

/* The pathing record supplies inclusive unsigned coordinate bounds at
   +0x4c..+0x52. */
// FUNCTION: WIZ8 0x004B75F0
bool GDProp::ContainsPathCoordinate(unsigned short x, unsigned short y) const
{
    if (x >= m_path_bounds.min_x && x <= m_path_bounds.max_x && y >= m_path_bounds.min_z &&
        y <= m_path_bounds.max_z) {
        return true;
    }
    return false;
}

/* Record a waypoint index for a grid point that lies inside the path bounds.
   The prop owns a counted waypoint list consumed by
   CheckConditionalWayPtStatus. */
// FUNCTION: WIZ8 0x004b7730
unsigned char GDProp::RegisterPathSurface(unsigned int index, const glm::ivec2& point)
{
    if (static_cast<unsigned short>(point.x) < m_path_bounds.min_x ||
        static_cast<unsigned short>(point.x) > m_path_bounds.max_x ||
        static_cast<unsigned short>(point.y) < m_path_bounds.min_z ||
        static_cast<unsigned short>(point.y) > m_path_bounds.max_z) {
        return 0;
    }

    m_path_waypoints.push_back(static_cast<unsigned short>(index));
    return 1;
}

/* Record a link index when the segment between the two grid points touches
   the path bounds. The clip walks the segment's major axis: the crossed
   minor bound interpolates the other coordinate, which must land within one
   cell of the rectangle. The vertical branch re-tests its first crossing
   instead of interpolating the second — a faithful retail oddity. */
// FUNCTION: WIZ8 0x004b7830
unsigned char GDProp::RegisterPathVertex(unsigned int index, const glm::ivec2& point,
                                         const glm::ivec2& second)
{
    const glm::vec2 from(static_cast<unsigned short>(point.x),
                         static_cast<unsigned short>(point.y));
    const glm::vec2 to(static_cast<unsigned short>(second.x),
                       static_cast<unsigned short>(second.y));
    const auto delta = to - from;
    const int major = std::abs(delta.x) > std::abs(delta.y) ? 0 : 1;
    const int minor = 1 - major;
    const glm::vec2 minimum(m_path_bounds.min_x, m_path_bounds.min_z);
    const glm::vec2 maximum(m_path_bounds.max_x, m_path_bounds.max_z);
    // The vertical policy tests the first boundary twice, as before.
    const glm::vec2 boundaries(minimum[major], major == 0 ? maximum.x : minimum.y);
    const auto crossings = (boundaries - from[major]) * (delta[minor] / delta[major]) + from[minor];
    bool found = false;
    for (float cross : {crossings.x, crossings.y})
        found |= cross > minimum[minor] - 1 && cross < maximum[minor] + 1;
    if (!found) return 0;
    m_path_edges.push_back(static_cast<unsigned short>(index));
    return 1;
}

/* Register the item on the collidable prop's sector list, building the path
   representation and the list itself on first use. */
// FUNCTION: WIZ8 0x004b7ad0
void AddItemToSector(int sector, W8WorldItem* item)
{
    if (item != 0 && sector >= 0) {
        W8Prop* prop = g_world->collidable_props[sector];
        GDProp* gd = prop->m_gd_prop.get();
        if (gd == 0) {
            prop->BuildOrRefreshPathingRepresentation();
            gd = prop->m_gd_prop.get();
            if (gd == 0) {
                return;
            }
        }
        if (!std::ranges::contains(gd->m_supported_items, item)) {
            gd->m_supported_items.push_back(item);
        }
    }
}

/* Drop the item from the collidable prop's sector list when both exist. */
// FUNCTION: WIZ8 0x004b7b50
void RemoveItemFromSector(int sector, W8WorldItem* item)
{
    if (item != 0 && sector >= 0) {
        W8Prop* prop = g_world->collidable_props[sector];
        if (prop != 0 && prop->m_gd_prop != 0) {
            wiz8::erase_first(prop->m_gd_prop->m_supported_items, item);
        }
    }
}

/* Whether any world items are supported by this prop. */
// FUNCTION: WIZ8 0x004B7BA0
bool GDProp::HasSupportedItems()
{
    return !m_supported_items.empty();
}
