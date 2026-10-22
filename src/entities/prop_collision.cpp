#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string_view>

#include "geometry.h"
#include "glm/ext/matrix_float4x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "navigation_flags.h"
#include "wiz8/containers.h"
#include "wiz8/entities/locks.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/surface_geometry.h"
#include "wiz8/world/world.h"
/* Engine Code\Prop.cpp */

#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/octree_geometry.h"

void Prop::initializeCollision(NavigationMesh* pathing, MeshInstance* instance,
                               std::string_view path_name, unsigned short prop_number,
                               W8FootstepSurface footstep_surface,
                               W8FootstepMaterial footstep_material)
{
    collision_unattached = false;
    collision_ready = true;
    m_path_handle = 0;
    collision_frame = 0;
    collision_trigger = 0;
    m_path_range.sentinel = -10000000.0f;
    m_path_bounds.max_z = 0;
    m_path_bounds.min_z = 0;
    m_path_bounds.max_x = 0;
    m_path_bounds.min_x = 0;

    if (pathing != nullptr) {
        m_path_handle = pathing->FindPathHandle(path_name, &m_path_bounds, &m_path_range);
    }

    if (instance != 0) {
        if (m_path_handle != 0 && pathing != nullptr) {
            pathing->LinkSurfaces(this);
            pathing->LinkEdges(this);
        }
        updateCollision(pathing, instance, true, prop_number, footstep_surface, footstep_material);
    }
}

/* Resize geometry as one owned pair of tables, preserving existing records
   when the linked mesh's counts are unchanged. */
void Prop::PrepareGeometry(MeshInstance* instance)
{
    int surface_count = 0;
    int vertex_count = 0;
    for (auto* mesh : meshParts(static_cast<Mesh*>(instance->getModel()))) {
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
void Prop::updateCollision(NavigationMesh* pathing, MeshInstance* instance, bool attach,
                           unsigned short prop_number, W8FootstepSurface footstep_surface,
                           W8FootstepMaterial footstep_material)
{
    if (!attach) {
        collision_unattached = true;
    } else {
        collision_unattached = false;
        collision_frame = prop_number;
    }

    PrepareGeometry(instance);
    auto world_matrix = instance->transform.matrix<glm::mat4>();

    int vertex_base = 0;
    int surface_total = 0;
    Mesh* mesh = static_cast<Mesh*>(instance->getModel());
    for (auto* mesh_part : meshParts(mesh)) {
        unsigned int mesh_flags = mesh_part->flags;
        glm::ivec3* polygon_vertices = mesh_part->getPolyVertex();
        for (int polygon = 0; polygon < mesh_part->polygon_count; ++polygon) {
            W8GDSurface* surface = &m_pGDSurfaces[surface_total + polygon];
            surface->vertex_indices[0] = polygon_vertices[polygon].x + vertex_base;
            surface->vertex_indices[1] = polygon_vertices[polygon].y + vertex_base;
            surface->vertex_indices[2] = polygon_vertices[polygon].z + vertex_base;
        }
        surface_total += mesh_part->polygon_count;

        glm::vec3* source_vertices = mesh_part->getVertexLoc();
        surfaceGeometry::transformPositions(
            {m_pVertices.data() + vertex_base,
             static_cast<std::size_t>(mesh_part->vertex_location_count)},
            glm::mat4x3(world_matrix), [&](std::size_t vertex) { return source_vertices[vertex]; });
        vertex_base += mesh_part->vertex_location_count;

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
    }

    if (m_path_handle != 0 && pathing != 0) {
        if (!attach) {
            if (collision_frame != 0xffff) {
                collision_frame = 0xffff;
                pathing->SetConditionalPathFrame(m_path_handle, 0xffff);
            }
        } else {
            pathing->SetConditionalPathFrame(m_path_handle, collision_frame);
            if (!m_path_waypoints.empty()) {
                pathing->CheckConditionalWayPtStatus(m_path_waypoints);
            }
            if (!m_path_edges.empty()) {
                pathing->CheckConditionalLinkStatus(m_path_edges);
            }
        }
    }

    Trigger* owner = collision_trigger;
    if (owner != 0 && attach) {
        if (const auto* door = owner->doorData()) {
            unsigned int flags = W8_PATH_CELL_DOOR;
            if ((owner->lock_state.lock_type != LockType::none &&
                 owner->lock_state.device_state.completed == 0) ||
                (owner->flags & W8_TRIGGER_ENABLED) == 0 || door->open || door->locked) {
                flags = W8_PATH_CELL_DOOR | W8_PATH_CELL_BLOCKED;
            }
            if (pathing != 0) {
                pathing->UpdateConditionalPathFlags(m_path_handle, collision_frame, flags);
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
void Prop::BindTrigger(Trigger* owner, NavigationMesh* pathing)
{
    collision_trigger = owner;
    if (const auto* door = owner->doorData()) {
        unsigned int path_flags = W8_PATH_CELL_DOOR;
        if ((owner->lock_state.lock_type == LockType::none ||
             owner->lock_state.device_state.completed != 0) &&
            (owner->flags & W8_TRIGGER_ENABLED) != 0 && !door->open && !door->locked) {
        } else {
            path_flags = W8_PATH_CELL_DOOR | W8_PATH_CELL_BLOCKED;
        }
        if (pathing != 0) {
            pathing->UpdateConditionalPathFlags(m_path_handle, collision_frame, path_flags);
        }
    }
}

/* Scan the transformed vertices for the axis-aligned bounds. The cached box
   feeds the path bookkeeping range/sentinel and is returned as a native value. */
BoundingBox Prop::computeBounds()
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
bool Prop::ContainsPathCoordinate(unsigned short x, unsigned short y) const
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
bool Prop::RegisterPathSurface(unsigned int index, const glm::ivec2& point)
{
    if (static_cast<unsigned short>(point.x) < m_path_bounds.min_x ||
        static_cast<unsigned short>(point.x) > m_path_bounds.max_x ||
        static_cast<unsigned short>(point.y) < m_path_bounds.min_z ||
        static_cast<unsigned short>(point.y) > m_path_bounds.max_z) {
        return false;
    }

    m_path_waypoints.push_back(static_cast<unsigned short>(index));
    return true;
}

/* Record a link index when the segment between the two grid points touches
   the path bounds. The clip walks the segment's major axis: the crossed
   minor bound interpolates the other coordinate, which must land within one
   cell of the rectangle. The vertical branch re-tests its first crossing
   instead of interpolating the second — a faithful retail oddity. */
bool Prop::RegisterPathVertex(unsigned int index, const glm::ivec2& point, const glm::ivec2& second)
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
    if (!found) return false;
    m_path_edges.push_back(static_cast<unsigned short>(index));
    return true;
}

/* Register the item on the collidable prop's sector list, building the path
   representation and the list itself on first use. */
void AddItemToSector(int sector, W8WorldItem* item)
{
    if (item != 0 && sector >= 0) {
        Prop* prop = GetWorld()->collidable_props[sector];
        Prop* gd = (prop->collision_ready ? prop : nullptr);
        if (gd == 0) {
            prop->BuildOrRefreshPathingRepresentation(GetWorldPathing());
            gd = (prop->collision_ready ? prop : nullptr);
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
void RemoveItemFromSector(int sector, W8WorldItem* item)
{
    if (item != 0 && sector >= 0) {
        Prop* prop = GetWorld()->collidable_props[sector];
        if (prop != 0 && prop->collision_ready) {
            wiz8::erase_first(prop->m_supported_items, item);
        }
    }
}

/* Whether any world items are supported by this prop. */
bool Prop::HasSupportedItems()
{
    return !m_supported_items.empty();
}
