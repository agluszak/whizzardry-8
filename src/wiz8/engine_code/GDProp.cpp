#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <span>

#include "surrender/srMath.h"
#include "surrender/srBoundingBox.h"
#include "wiz8/containers.h"
#include "wiz8/local_code/ItemManager.h"
#include "compat/ptr32.h"
#include "geometry.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "navigation_flags.h"
#include "sr_api.h"
/* Engine Code\GDProp.cpp */

#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/LevelFile.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/float_constants.h"
#include "wiz8/layouts/world.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "surrender/srModelInstance.h"

// GLOBAL: WIZ8 0x005ebccc
const float g_float_three_quarters = 0.75f;

// FUNCTION: WIZ8 0x004b6e00
GDProp::GDProp(srModelInstance* instance, const char* path_name, unsigned short prop_number,
               unsigned char footstep_surface, unsigned char footstep_material)
{
    Flags initial_flags = {false, false, false, false, 0, 0};
    m_flags = initial_flags;
    m_path_handle = 0;
    m_prop_number = 0;
    m_vertex_count = 0;
    m_surface_count = 0;
    m_pGDSurfaces = 0;
    m_pVertices = 0;
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

// FUNCTION: WIZ8 0x004b6ed0
GDProp::~GDProp()
{
    if (m_pGDSurfaces != 0) {
        free(m_pGDSurfaces);
    }
    if (m_pVertices != 0) {
        delete[] m_pVertices;
    }
}

/* Resize the two geometry tables to the complete linked mesh. Surfaces use
   the CRT heap, while vertices retain SurRender's heap ownership. */
// FUNCTION: WIZ8 0x004b6f30
void GDProp::PrepareGeometry(srModelInstance* instance)
{
    int surface_count = 0;
    int vertex_count = 0;
    stMeshModel* mesh = static_cast<stMeshModel*>(instance->getModel());
    while (mesh != 0) {
        surface_count += mesh->polygon_count;
        vertex_count += mesh->vertex_location_count;
        mesh = mesh->Next();
    }

    if (m_surface_count != surface_count) {
        m_surface_count = surface_count;
        if (m_pGDSurfaces != 0) {
            free(m_pGDSurfaces);
        }
        m_pGDSurfaces = static_cast<W8GDSurface*>(malloc(m_surface_count * sizeof(W8GDSurface)));
        if (m_pGDSurfaces == 0) {
            srAssertFail("m_pGDSurfaces", "C:\\Projects\\Wizardry 8\\Engine Code\\GDProp.cpp", 0xa0,
                         0);
        }
        memset(m_pGDSurfaces, 0, m_surface_count * sizeof(W8GDSurface));
    }

    if (m_vertex_count != vertex_count) {
        m_vertex_count = vertex_count;
        if (m_pVertices != 0) {
            delete[] m_pVertices;
        }
        m_pVertices = new srVector3T<float>[m_vertex_count];
        if (m_pVertices == 0) {
            srAssertFail("m_pVertices", "C:\\Projects\\Wizardry 8\\Engine Code\\GDProp.cpp", 0xae,
                         0);
        }
        memset(m_pVertices, 0, m_vertex_count * sizeof(srVector3T<float>));
    }
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
        srVector3i* polygon_vertices = mesh->getPolyVertex();
        for (int polygon = 0; polygon < mesh->polygon_count; ++polygon) {
            W8GDSurface* surface = &m_pGDSurfaces[surface_total + polygon];
            surface->vertex_indices[0] = polygon_vertices[polygon].x + vertex_base;
            surface->vertex_indices[1] = polygon_vertices[polygon].y + vertex_base;
            surface->vertex_indices[2] = polygon_vertices[polygon].z + vertex_base;
        }
        surface_total += mesh->polygon_count;

        glm::vec3* source_vertices = mesh->getVertexLoc();
        for (int vertex = 0; vertex < mesh->vertex_location_count; ++vertex) {
            m_pVertices[vertex_base + vertex] =
                glm::vec3(world_matrix * glm::vec4(source_vertices[vertex], 1));
        }
        vertex_base += mesh->vertex_location_count;

        for (int surface_index = 0; surface_index < surface_total; ++surface_index) {
            W8GDSurface* surface = &m_pGDSurfaces[surface_index];
            surface->plane = trianglePlane(m_pVertices[surface->vertex_indices[0]].glmValue(),
                m_pVertices[surface->vertex_indices[1]].glmValue(), m_pVertices[surface->vertex_indices[2]].glmValue());

            const auto dominant_axis = dominantPlaneAxis(surface->plane.normal.glmValue());
            surface->flags = dominant_axis + W8_GD_SURFACE_PROP_GEOMETRY;
            surface->footstep_surface = footstep_surface;
            surface->footstep_material = footstep_material;
            surface->hit_plane = 0;
            if ((mesh_flags & W8_MESH_SORTED_RENDERING) != 0) {
                surface->flags |= W8_GD_SURFACE_SKIP_FILTERED_TRACE;
            }

            if (g_float_half <= surface->plane.normal.y) {
                surface->contact_margin = 500.0f;
                surface->flags |= W8_GD_SURFACE_WALKABLE;
                if (g_float_three_quarters < surface->plane.normal.y) {
                    surface->slope = 1.0f;
                } else {
                    surface->slope = surface->plane.normal.y;
                }
            } else {
                surface->slope = 0.0f;
                surface->contact_margin = 500.0f;
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
        W8TriggerActionData* action = owner->m_pActionData;
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
    W8TriggerActionData* action = owner->m_pActionData;
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
    const auto bounds = boundingBox(std::span<const srVector3T<float>>{
        m_pVertices, static_cast<std::size_t>(m_vertex_count)});
    m_bound_max = bounds.maximum;
    m_bound_min = bounds.minimum;

    m_path_range.minimum = m_bound_min.y;
    m_path_range.maximum = m_bound_max.y;
    m_path_range.sentinel = (m_bound_min.y + m_bound_max.y) * g_float_half;

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

/* Test each surface's runtime triangle against the query box. */
// FUNCTION: WIZ8 0x004b7620
bool GDProp::boundsOverlap(const srBoundingBox& box)
{
    bool hit = false;
    for (int index = 0; index < m_surface_count && hit == 0; ++index) {
        W8GDSurface* surface = &m_pGDSurfaces[index];
        hit = triangleIntersectsBox(box,
            indexedTriangle(std::span<const srVector3>(m_pVertices, m_vertex_count), surface->vertex_indices),
            surface->plane.normal.glmValue());
    }
    return hit;
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
    const glm::vec2 from(static_cast<unsigned short>(point.x), static_cast<unsigned short>(point.y));
    const glm::vec2 to(static_cast<unsigned short>(second.x), static_cast<unsigned short>(second.y));
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
        GDProp* gd = prop->m_gd_prop;
        if (gd == 0) {
            prop->BuildOrRefreshPathingRepresentation();
            gd = prop->m_gd_prop;
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

/* The array-element ctor for `new GDPreProp[n]`; the base default runs
   inlined, then the extra frame field is zeroed. */
// FUNCTION: WIZ8 0x004b7bc0
GDPreProp::GDPreProp()
{
    last_frame = 0;
}

/* Rebuild the collision geometry for one animation frame. The frame index is
   clamped against every transform channel's path count and stored as the
   conditional frame number; the counts accumulated for allocation are then
   reset so each transform's fill re-counts the appended geometry. */
// FUNCTION: WIZ8 0x004b7c00
void GDProp::ApplyAnimFrame(unsigned short frame, W8LevelFileAnimObj* anim)
{
    signed char index;

    for (index = 0; index < anim->num_transforms; ++index) {
        W8LevelFileTransform* transform = &anim->pTransforms[index];
        if (frame >= transform->pathAI.path_count) {
            frame = transform->pathAI.path_count - 1;
        }
    }
    m_prop_number = frame;

    for (index = 0; index < anim->num_transforms; ++index) {
        W8LevelFileMesh* mesh = &anim->pTransforms[index].LODMesh.pFrames->mesh;
        if ((mesh->flags & W8_LEVEL_MESH_LOD_VERTICES) != 0) {
            srAssertFail("FALSE", "C:\\Projects\\Wizardry 8\\Engine Code\\GDProp.cpp", 0x355,
                         "Transform prop made collideable!!!");
            m_surface_count += mesh->num_lods * mesh->num_faces;
            m_vertex_count += mesh->num_lods * mesh->num_vertices;
        } else {
            m_surface_count += mesh->num_faces;
            m_vertex_count += mesh->num_vertices;
        }
    }

    m_pVertices = new srVector3T<float>[m_vertex_count];
    m_pGDSurfaces = static_cast<W8GDSurface*>(malloc(m_surface_count * sizeof(W8GDSurface)));
    if (m_pGDSurfaces == 0) {
        srAssertFail("m_pGDSurfaces", "C:\\Projects\\Wizardry 8\\Engine Code\\GDProp.cpp", 0x364,
                     0);
    }
    memset(m_pGDSurfaces, 0, m_surface_count * sizeof(W8GDSurface));
    m_vertex_count = 0;
    m_surface_count = 0;

    for (index = 0; index < anim->num_transforms; ++index) {
        W8LevelFileTransform* transform = &anim->pTransforms[index];
        W8LevelFileScaledPathNode node;
        if (transform->pathAI.scaled == 2) {
            node = transform->pathAI.pScaledPaths[frame];
        } else {
            node.path = transform->pathAI.pPaths[frame];
            node.scale.x = node.scale.y = node.scale.z = 1.0f;
        }
        TransformMeshGeometry(&node, &transform->LODMesh.pFrames->mesh);
    }
}

/* Retail selects the face representation with the short-LOD bit. */
void GDProp::AppendMeshFaces(W8LevelFileMesh* mesh, int vertex_base)
{
    if ((mesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0) {
        W8LevelFileCompressedFace* faces = mesh->pstCompFaces;
        for (int face = 0; face < mesh->num_faces; ++face) {
            W8GDSurface* surface = &m_pGDSurfaces[m_surface_count];
            ++m_surface_count;
            surface->vertex_indices[0] = faces[face].vertex_indices[0] + vertex_base;
            surface->vertex_indices[1] = faces[face].vertex_indices[1] + vertex_base;
            surface->vertex_indices[2] = faces[face].vertex_indices[2] + vertex_base;
        }
    } else {
        W8SerializedMeshFace* faces = mesh->pstFaces;
        for (int face = 0; face < mesh->num_faces; ++face) {
            W8GDSurface* surface = &m_pGDSurfaces[m_surface_count];
            ++m_surface_count;
            surface->vertex_indices[0] = faces[face].vertices[0] + vertex_base;
            surface->vertex_indices[1] = faces[face].vertices[1] + vertex_base;
            surface->vertex_indices[2] = faces[face].vertices[2] + vertex_base;
        }
    }
}

/* Append one transform channel's geometry under the path record's affine
   transform: position and scale convert through the world scale (LOD meshes
   carry their own compression factor), the axis/angle supplies the rotation,
   and every appended surface then receives its plane, dominant axis and slope
   classification. */
// FUNCTION: WIZ8 0x004b7e50
void GDProp::TransformMeshGeometry(const W8LevelFileScaledPathNode* node, W8LevelFileMesh* mesh)
{
    int surface_base = m_surface_count;

    srVector3T<float> axis = node->path.axis;
    glm::mat3 rotation;
    glm::mat4x3 matrix;
    srVector3T<float> translation;
    srVector3T<float> scale;
    float factor;

    rotation = glm::mat3(1);
    if (node->path.angle != g_double_zero) {
        rotation *= glm::mat3_cast(
            glm::angleAxis(float(std::atan2(sin(node->path.angle), cos(node->path.angle))),
                           normalizedOrZero(glm::vec3(axis.glmValue()))));
    }
    translation.Set(node->path.position.x * g_double_five_hundred,
                    node->path.position.y * g_double_five_hundred,
                    node->path.position.z * g_double_five_hundred);

    if ((mesh->flags & W8_LEVEL_MESH_LOD_VERTICES) != 0 &&
        (mesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0) {
        factor = mesh->lod_scale * g_world_scale;
    } else {
        factor = static_cast<float>(g_double_five_hundred);
    }
    scale = node->scale * factor;

    matrix = glm::mat4x3(rotation);
    matrix[3] = translation.glmValue();
    {
        for (int column = 0; column < 3; ++column)
            matrix[column] *= scale.glmValue()[column];
    };

    if ((mesh->flags & W8_LEVEL_MESH_LOD_VERTICES) != 0) {
        for (int lod = 0; lod < mesh->num_lods; ++lod) {
            int vertex_base = m_vertex_count;
            if ((mesh->flags & W8_LEVEL_MESH_SHORT_LOD_VERTICES) != 0) {
                short* vertices = mesh->lod_shorts[lod];
                for (int vertex = 0; vertex < mesh->num_vertices; ++vertex) {
                    m_pVertices[m_vertex_count] = glm::vec3(
                        glm::mat4(matrix) *
                        glm::vec4((srVector3T<float>(static_cast<float>(vertices[vertex * 3]),
                                                     static_cast<float>(vertices[vertex * 3 + 1]),
                                                     static_cast<float>(vertices[vertex * 3 + 2])))
                                      .glmValue(),
                                  1));
                    ++m_vertex_count;
                }
            } else {
                const srVector3T<float>* vertices = mesh->lods[lod];
                for (int vertex = 0; vertex < mesh->num_vertices; ++vertex) {
                    m_pVertices[m_vertex_count] =
                        glm::vec3(glm::mat4(matrix) * glm::vec4(vertices[vertex].glmValue(), 1));
                    ++m_vertex_count;
                }
            }
            AppendMeshFaces(mesh, vertex_base);
        }
    } else {
        int vertex_base = m_vertex_count;
        const srVector3T<float>* vertices = mesh->pstVertices;
        for (int vertex = 0; vertex < mesh->num_vertices; ++vertex) {
            m_pVertices[m_vertex_count] =
                glm::vec3(glm::mat4(matrix) * glm::vec4(vertices[vertex].glmValue(), 1));
            ++m_vertex_count;
        }
        AppendMeshFaces(mesh, vertex_base);
    }

    for (int index = surface_base; index < m_surface_count; ++index) {
        W8GDSurface* surface = &m_pGDSurfaces[index];
        surface->plane = trianglePlane(m_pVertices[surface->vertex_indices[0]].glmValue(),
            m_pVertices[surface->vertex_indices[1]].glmValue(), m_pVertices[surface->vertex_indices[2]].glmValue());

        const auto dominant_axis = dominantPlaneAxis(surface->plane.normal.glmValue());
        surface->flags = dominant_axis + W8_GD_SURFACE_PROP_GEOMETRY;
        surface->hit_plane = 0;
        if (g_float_half <= surface->plane.normal.y) {
            surface->contact_margin = 500.0f;
            surface->flags |= W8_GD_SURFACE_WALKABLE;
        } else {
            surface->slope = 0.0f;
            surface->contact_margin = 500.0f;
        }
    }
}

/* glm::mat4x3::SetTranslation/Scale emitted for this TU by
   TransformMeshGeometry; the primary templates live in srMath.h. */
