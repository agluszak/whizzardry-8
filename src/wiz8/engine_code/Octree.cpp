#include "wiz8/engine_code/Octree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <glm/common.hpp> // NOLINT(misc-include-cleaner)
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtc/quaternion.hpp>
#include <glm/vector_relational.hpp> // NOLINT(misc-include-cleaner)

#include "asset_paths.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_int3_sized.hpp"
#include "ground_probe.h"
#include "index_algorithms.h"
#include "input.h"
#include "layouts/gameplay_databases.h"
#include "layouts/world.h"
#include "random.h"
#include "surrender/plane_math.h"
#include "surrender/segment_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srCamera.h"
#include "surrender/srMath.h"
#include "surrender/srModel.h"
#include "surrender/srModelInstance.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/swept_bounds.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/mesh_links.h"
#include "wiz8/engine_code/octree_spatial.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/geometry.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/octree_walk.h"
#include "wiz8/packed_bits.h"
#include "wiz8/placement_math.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world_cursor.h"

constexpr float kMonsterProximityRadiusScale = 15.0f;
constexpr float kRegionAxisAlignmentThreshold = 0.9998999834060669f;
constexpr double kRegionLinkCameraTilt = -0.26179999113082886;
constexpr float kRegionLinkCircleCoverage = 6.282185077667236f;
constexpr float kScatterInnerRingJitterScale = 0.00020000000949949026f;
constexpr float kScatterOuterRingJitterScale = 0.00040000001899898052f;

// FUNCTION: WIZ8 0x00433a70
void W8Octree::GetPathSurfaceNormal(const glm::vec3* position, glm::vec3* normal)
{
    if (pathing != 0) {
        pathing->GetPathSurfaceNormal(position, normal);
        return;
    }
    (*normal) = glm::vec3(0.0f, 1.0f, 0.0f);
}

// FUNCTION: WIZ8 0x00434170

// GLOBAL: WIZ8 0x00659760
static W8OctreeIndex::iterator g_octree_query_slot;
// GLOBAL: WIZ8 0x00659766
static unsigned short g_octree_query_id;

// GLOBAL: WIZ8 0x00659770

// GLOBAL: WIZ8 0x00659774
static unsigned int g_octree_query_cell;

// GLOBAL: WIZ8 0x00659778
static std::vector<std::string> g_octree_file_search;
static std::size_t g_octree_file_search_index;

// GLOBAL: WIZ8 0x006598b2
static unsigned char g_octree_file_search_active;

// GLOBAL: WIZ8 0x00606810
static char g_octree_point_extension[] = ".pts";

// GLOBAL: WIZ8 0x006068a0
static char g_octree_file_search_wildcard[] = "*";

// GLOBAL: WIZ8 0x00659890
w8_ulong* g_octree_state;
// GLOBAL: WIZ8 0x00659894
// GLOBAL: WIZ8 0x00659898
// GLOBAL: WIZ8 0x00659899

/* Build a packed four-byte colour from four components and answer its
   address. The receiver is the output slot. */
/* Draw the probe box through the world camera. */

// GLOBAL: WIZ8 0x006598a4
W8Octree* g_octree;

// GLOBAL: WIZ8 0x006068a4
static char g_region_link_extension[] = ".rlk";

// GLOBAL: WIZ8 0x006598a8
// GLOBAL: WIZ8 0x006598b0
static unsigned short g_octree_region_debug_last;

/* Renderer switches toggled across a region-link build: while sampling,
   meshes render without baked vertex lighting or textures and with front
   faces culled.  The inverted-depth switch is read by the surface-capture
   path (RenderWorldToSurface and the tri-mesh renderer) but no Wiz8 code
   ever writes it. */
// GLOBAL: WIZ8 0x0065a0ec
bool g_render_unlit;
// GLOBAL: WIZ8 0x0065a0ed
bool g_render_cull_front;
// GLOBAL: WIZ8 0x0065a0ee
// GLOBAL: WIZ8 0x0065a146
bool g_render_untextured;

// FUNCTION: WIZ8 0x0042bc00

// GLOBAL: WIZ8 0x005ec02c
static const float NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE = 50.0f;

void W8OctreeView::captureCamera(const srCamera& camera)
{
    camera_location = glm::vec3(camera.getLocation());
    camera_dof = glm::vec3(camera.getWorldSpaceDOF());
    const auto rotation = camera.getRotation<glm::mat3>();
    rotation_column0 = rotation[0];
    rotation_column1 = rotation[1];
    horizontal_fov = static_cast<float>(camera.getHorizontalFOV());
    vertical_fov = static_cast<float>(camera.getVerticalFOV());
    horizontal_fov_cosine = static_cast<float>(cos(horizontal_fov));
    vertical_fov_cosine = static_cast<float>(cos(vertical_fov));
}

// FUNCTION: WIZ8 0x0042f7e0
void W8Octree::UpdateCameraVisibility()
{
    W8World* world = GetWorld();
    if (m_visibility_suspended) {
        return;
    }

    view.far_clip = static_cast<float>(WorldGetFarClip(world));
    view.captureCamera(*world->camera);
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_projected_regions, false);
    UpdateVisibility();
}

// FUNCTION: WIZ8 0x004304a0
void W8Octree::UpdateVisibility()
{
    unsigned short mesh_index;
    unsigned int index;
    if (m_reset_visibility) {
        m_reset_visibility = false;
        for (mesh_index = 0; mesh_index < m_meshCount; ++mesh_index) {
            if (g_world->psrMeshes[mesh_index] != 0) {
                m_pSubmeshes
                    [static_cast<stModelInstance*>(g_world->psrMeshes[mesh_index])->mesh_index + 1]
                        .mesh = mesh_index;
                m_pSubmeshes[mesh_index + 1].flags &= 0xffffffc7;
                stModelInstance* mesh =
                    static_cast<stModelInstance*>(g_world->psrMeshes[mesh_index]);
                if (mesh != 0) {
                    mesh->setFlag(srNode::FLAG_DISABLE);
                    mesh->setFlag(srNode::FLAG_TERMINATE);
                }
            }
        }
        for (index = 0; index < m_props.size(); ++index) {
            m_props[index]->SetActivationState(0);
        }
        for (const auto id : m_prop_links.alwaysVisible()) m_props[id - 1]->SetActivationState(1);
        for (index = 0; index < m_particles.size(); ++index) {
            m_particles[index]->SetTraversalEnabled(false);
        }
        for (const auto id : m_particle_links.alwaysVisible())
            m_particles[id - 1]->SetTraversalEnabled(true);
        ValidateRegionMeshLinks();
        std::ranges::fill(m_previous_regions, false);
        std::ranges::fill(m_accumulated_regions, false);
        if (m_ulNumParticles != 0) {
            std::ranges::fill(m_visible_particles, false);
        }
        if (m_ulNumProps != 0) {
            std::ranges::fill(m_visible_props, false);
        }
    }

    std::ranges::fill(m_current_regions, false);
    std::ranges::fill(m_projected_regions, false);
    if (m_ulNumParticles != 0) {
        std::ranges::fill(m_linked_particles, false);
    }
    if (m_ulNumProps != 0) {
        std::ranges::fill(m_linked_props, false);
    }
    m_projected_regions_valid = false;
    view.visible_cells = projectRegionsAt(view.camera_location).cell;
    CollectVisibleCells();
    for (std::size_t region = 0; region < m_current_regions.size(); ++region) {
        if (m_projected_regions_valid && !m_projected_regions[region]) {
            m_current_regions[region] = false;
        }
        if (m_current_regions[region]) {
            m_accumulated_regions[region] = true;
        }
    }
    for (std::size_t region = 1; region < m_previous_regions.size(); ++region) {
        if (m_previous_regions[region] && !m_current_regions[region]) {
            W8OctSubmesh* submesh = &m_pSubmeshes[region];
            submesh->flags &= 0xfffffff7;
            stModelInstance* mesh =
                static_cast<stModelInstance*>(g_world->psrMeshes[submesh->mesh]);
            if (mesh != 0) {
                mesh->setFlag(srNode::FLAG_DISABLE);
                mesh->setFlag(srNode::FLAG_TERMINATE);
            }
        }
    }
    for (std::size_t region = 0; region < m_current_regions.size(); ++region) {
        if (!m_current_regions[region]) {
            continue;
        }
        if (!m_previous_regions[region] && region != 0) {
            W8OctSubmesh* submesh = &m_pSubmeshes[region];
            int mesh_index = submesh->mesh;
            submesh->flags |= 0x28;
            stModelInstance* mesh = static_cast<stModelInstance*>(g_world->psrMeshes[mesh_index]);
            if (mesh != 0) {
                mesh->clearFlag(srNode::FLAG_DISABLE);
                mesh->clearFlag(srNode::FLAG_TERMINATE);
            }
        }
        MarkMeshLinksVisible(static_cast<unsigned int>(region));
    }
    if (m_ulNumParticles != 0) {
        for (const auto id : m_particle_links.alwaysVisible()) SetBit(m_linked_particles, id - 1);
        for (std::size_t particle = 0; particle < m_linked_particles.size(); ++particle) {
            if (m_linked_particles[particle] && !m_visible_particles[particle]) {
                m_particles[particle]->SetTraversalEnabled(true);
            } else if (!m_linked_particles[particle] && m_visible_particles[particle]) {
                m_particles[particle]->SetTraversalEnabled(false);
            }
        }
        m_visible_particles = m_linked_particles;
    }
    if (m_ulNumProps != 0) {
        for (const auto id : m_prop_links.alwaysVisible()) SetBit(m_linked_props, id - 1);
        for (std::size_t prop = 0; prop < m_linked_props.size(); ++prop) {
            if (m_linked_props[prop] && !m_visible_props[prop]) {
                m_props[prop]->SetActivationState(1);
            } else if (!m_linked_props[prop] && m_visible_props[prop]) {
                m_props[prop]->SetActivationState(0);
            }
        }
        m_visible_props = m_linked_props;
    }
    m_previous_regions = m_current_regions;
}

/* Collect mesh and prop instances whose world-space bounds intersect the query sphere.
   The grid and pre-generated frusta supply candidate regions. */
// FUNCTION: WIZ8 0x0042f9a0
std::vector<stModelInstance*> W8Octree::modelsNearPoint(const glm::vec3& center, float radius,
                                                        unsigned int flags, bool only_accumulated)
{
    std::vector<stModelInstance*> out;
    if (!(radius >= 0) || glm::any(glm::isnan(center)) || glm::any(glm::isinf(center)) ||
        m_spatial.m_region_cells_per_axis == 0 || !(m_spatial.m_region_grid_cell > 0))
        return out;
    const auto relative = (center - m_spatial.m_bounds.minimum) / m_spatial.m_region_grid_cell;
    const float reach = radius / m_spatial.m_region_grid_cell;
    const glm::vec3 limit(m_spatial.m_region_cells_per_axis - 1);
    const glm::ivec3 first(glm::clamp(glm::floor(relative - reach) - 1.0f, glm::vec3(0), limit));
    const glm::ivec3 last(glm::clamp(glm::floor(relative + reach) + 1.0f, glm::vec3(0), limit));
    const auto intersects = [&](srModelInstance* instance) {
        if (!instance || !instance->getModel()) return false;
        const auto bounds = instance->getModel()->getBounds().box;
        const auto world = instance->getWorldSpaceMatrix<glm::mat4>();
        return bounds.transformed(world).intersectsSphere(center, radius);
    };
    const unsigned int level_mask = m_region_mask;
    std::ranges::fill(m_current_regions, false);
    visitOctreeCellBox(first, last, [&](const glm::ivec3& cell) {
        unsigned int masked_cell[4];
        masked_cell[0] = level_mask;
        masked_cell[1] = cell.x;
        masked_cell[2] = cell.y;
        masked_cell[3] = cell.z;
        int node = DescendByMask(masked_cell);
        if (node != 0) {
            unsigned int region = m_branches[node].region;
            if (region != 0) {
                SetBit(m_current_regions, region);
            }
        }
    });

    unsigned int region;
    for (region = 1; region < m_spatial.m_region_count; ++region) {
        if (SphereInsideFrustum(center, radius, m_spatial.m_region_volumes[region].m_planes) &&
            m_spatial.m_region_volumes[region].m_region_bit != 0) {
            SetBit(m_current_regions, m_spatial.m_region_volumes[region].m_region_bit);
        }
    }

    if (m_ulNumProps == 0) {
        flags &= 0xffffff00;
    } else if ((flags & 0xff) != 0) {
        std::ranges::fill(m_linked_props, false);
    }
    for (unsigned int submesh = 0; submesh < m_current_regions.size(); ++submesh) {
        if (only_accumulated && !m_accumulated_regions[submesh]) {
            m_current_regions[submesh] = false;
        }
        if (m_current_regions[submesh] && submesh != 0) {
            stModelInstance* instance =
                static_cast<stModelInstance*>(g_world->psrMeshes[m_pSubmeshes[submesh].mesh]);
            if (intersects(instance)) out.push_back(instance);
            if ((flags & 0xff) != 0) {
                MarkMeshLinksVisible(submesh);
            }
        }
    }

    if ((flags & 0xff) != 0) {
        for (std::size_t bit = 1; bit <= m_linked_props.size(); ++bit) {
            if (!m_linked_props[bit - 1]) {
                continue;
            }
            srModelInstance* instance = m_props[bit - 1]->ToggleRepAnimationDefault();
            if (intersects(instance)) out.push_back(static_cast<stModelInstance*>(instance));
        }
    }
    return out;
}

/* Add every particle and prop linked to one mesh to the live visibility sets.

   The lookup gives a 1-based start into a packed link table; each run is
   terminated by a zero. A linked entry whose slot is empty is skipped. */
// FUNCTION: WIZ8 0x00430a70
void W8Octree::MarkMeshLinksVisible(unsigned int mesh)
{
    if (mesh > m_meshCount) return;
    m_particle_links.markVisible(mesh, std::span<stParticle* const>(m_particles),
                                 m_linked_particles);
    m_prop_links.markVisible(mesh, std::span<W8Prop* const>(m_props), m_linked_props);
}

/* Expand the camera box over the spatial levels and collect the regions the
   camera can occupy.

   The camera cell is quantized twice, once against the node extent and once
   against the leaf cell size. The walk keeps the camera inside an expanding
   box while the node extent shrinks per level; the deepest level looks the
   camera cell up in the region-link table and adds every linked region to the
   projected set. The leaf the walk ends on contributes its region list through
   ProjectLinkedRegionsForLocation. */
// FUNCTION: WIZ8 0x00431050
short W8Octree::ProjectLinkedRegionsForLocation(glm::vec3* location, unsigned short* region_list)
{
    if (!m_region_links_ready) {
        return 0;
    }

    short match_count = 0;
    char expected_regions[256];
    expected_regions[0] = '\0';
    char region_label[128];

    if (region_list != 0 && region_list[0] != 0) {
        for (unsigned short* cursor = region_list; *cursor != 0; ++cursor) {
            unsigned short region_index = *cursor;
            sprintf(region_label, "%d  ", region_index);
            strcat(expected_regions, region_label);

            W8OctRegionVolume* volume = &m_spatial.m_region_volumes[region_index];
            if (!PointInsideFrustum((*location), volume->m_planes)) {
                continue;
            }
            if (match_count == 0) {
                std::ranges::fill(m_projected_regions, false);
                m_projected_regions_valid = false;
            }
            ++match_count;
            unsigned int key = region_index;
            for (auto slot = m_pRegionLinks->equal_range(key).first; slot != m_pRegionLinks->end();
                 slot = NextIndexValue(*m_pRegionLinks, key, slot)) {
                SetBit(m_projected_regions, slot->second);
                m_projected_regions_valid = true;
            }
        }
        if (match_count != 0) {
            return match_count;
        }
    }

    char point_region_label[128];
    strcpy(point_region_label, expected_regions);

    if (m_spatial.m_region_count < 2) {
        return 0;
    }

    unsigned short reported_region = 0;
    for (unsigned short region_index = 1; region_index < m_spatial.m_region_count; ++region_index) {
        if (match_count != 0) {
            break;
        }
        W8OctRegionVolume* volume = &m_spatial.m_region_volumes[region_index];
        if (!PointInsideFrustum((*location), volume->m_planes)) {
            continue;
        }
        if (reported_region == 0 && region_index != g_octree_region_debug_last) {
            sprintf(expected_regions, "Point in region %d, expected regions: %s\n", region_index,
                    point_region_label);
            g_octree_region_debug_last = region_index;
            reported_region = region_index;
        }
        std::ranges::fill(m_projected_regions, false);
        m_projected_regions_valid = false;
        match_count = 1;
        unsigned int key = region_index;
        for (auto slot = m_pRegionLinks->equal_range(key).first; slot != m_pRegionLinks->end();
             slot = NextIndexValue(*m_pRegionLinks, key, slot)) {
            SetBit(m_projected_regions, slot->second);
            m_projected_regions_valid = true;
        }
    }

    if (match_count == 0) {
        return 0;
    }
    return match_count;
}

// FUNCTION: WIZ8 0x00430BF0
unsigned int W8Octree::GetSectorForPosition(const glm::vec3* position)
{
    glm::vec3 point = *position;
    for (unsigned int region = 1; region < m_spatial.m_region_count; ++region) {
        if (PointInsideFrustum(point, m_spatial.m_region_volumes[region].m_planes)) {
            return m_spatial.m_region_volumes[region].m_region_bit;
        }
    }

    unsigned int cell[4];
    cell[0] = m_region_mask;
    for (int axis = 0; axis < 3; ++axis) {
        int coordinate = static_cast<int>(((*position)[axis] - m_spatial.m_bounds.minimum[axis]) /
                                          m_spatial.m_region_grid_cell);
        if (coordinate < 0 || coordinate >= m_spatial.m_region_cells_per_axis) {
            return 0;
        }
        cell[axis + 1] = coordinate;
    }

    int node = DescendByMask(cell);
    if (node == 0) {
        return 0;
    }
    return m_branches[node].region;
}

OctreeCellCoordinates W8Octree::cellCoordinates(const glm::vec3& location) const
{
    const auto minimum = m_spatial.m_bounds.minimum;
    auto cell = cellAt(location);
    // Retain the visibility/swept-query policy even at exact negative cell
    // multiples; it is intentionally different from truncating cellAt().
    for (int axis = 0; axis < 3; ++axis)
        if (location[axis] < minimum[axis]) --cell[axis];
    const auto offset = (location - (glm::vec3(cell) * m_spatial.m_node_extent + minimum)) /
                            m_spatial.m_node_extent -
                        0.5f;
    const bool inside = !glm::any(glm::lessThan(location, minimum)) &&
                        !glm::any(glm::greaterThan(location, m_spatial.m_bounds.maximum));
    return {cell, offset, inside};
}

// FUNCTION: WIZ8 0x00430d50
OctreeCellCoordinates W8Octree::projectRegionsAt(const glm::vec3& position)
{
    const auto coordinates = cellCoordinates(position);
    if (!coordinates.inside) return coordinates;
    const glm::ivec3 link_cell((position - m_spatial.m_bounds.minimum) /
                               m_spatial.m_region_grid_cell);
    glm::vec3 box_min = m_spatial.m_bounds.minimum;
    glm::vec3 box_max = m_spatial.m_bounds.maximum;
    glm::vec3 location_record(position);
    auto* location = &location_record;
    float span = m_spatial.m_extent * 0.5f;
    int node = 1;
    for (short level = 0; level < m_spatial.m_depth; ++level) {
        unsigned int child_index = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float edge = span + box_min[axis];
            if ((*location)[axis] < edge) {
                box_max[axis] = edge;
            } else {
                box_min[axis] = edge;
                child_index |= 1 << (2 - axis);
            }
        }
        if (level == m_spatial.m_leaf_level) {
            unsigned int key =
                ((m_region_mask * 0x100 + link_cell.x) * 0x100 + link_cell.y) * 0x100 + link_cell.z;
            auto slot = m_pRegionLinks->equal_range(key).first;
            while (slot != m_pRegionLinks->end()) {
                SetBit(m_projected_regions, slot->second);
                slot = NextIndexValue(*m_pRegionLinks, key, slot);
                m_projected_regions_valid = true;
            }
        }
        if (node != 0) {
            node = static_cast<int>(m_branches[node].children[child_index]);
        }
        span *= 0.5f;
    }
    if (node != 0) {
        w8_ulong region_offset = m_leaves[node].region_offset;
        if (region_offset != 0) {
            ProjectLinkedRegionsForLocation(location, m_region_index_stream.data() + region_offset);
            return coordinates;
        }
    }
    ProjectLinkedRegionsForLocation(location, 0);
    return coordinates;
}

/* Project each candidate region volume against the camera frustum and add the
   visible ones to the current region set. The first corner is tested against
   the far-clip sphere before the eight remaining corners are projected. */
// FUNCTION: WIZ8 0x004301c0
void W8Octree::MarkVisibleRegions()
{
    float radius = view.far_clip;
    float radius_squared = radius * radius;

    for (int index = 1; index < m_spatial.m_region_count; ++index) {
        W8OctRegionVolume* volume = &m_spatial.m_region_volumes[index];

        if (m_projected_regions_valid && !TestBit(m_projected_regions, volume->m_region_bit)) {
            continue;
        }
        const auto delta = view.camera_location - volume->m_points[0];

        if (glm::dot(delta, delta) >= radius_squared) {
            continue;
        }
        bool visible = srPlaneMath::containsSphere(view.frustum_planes, volume->m_points[0], 0);

        for (int point = 1; !visible && point < 9; ++point) {
            visible = srPlaneMath::containsSphere(view.frustum_planes, volume->m_points[point], 0);
        }
        if (visible && volume->m_region_bit != 0) {
            SetBit(m_current_regions, volume->m_region_bit);
        }
    }
}

/* Build the four side frustum planes from the camera basis and far clip, and
   accumulate the two far-plane offsets the region projection reads. */
// FUNCTION: WIZ8 0x004302e0
void W8Octree::BuildFrustumPlanes()
{
    float fov = view.horizontal_fov * 0.5f;
    float extent = m_spatial.m_max_region_radius;
    float far_clip = view.far_clip;
    float backoff = extent / sin(fov);
    float ratio = extent / cos(fov);
    double tangent = tan(fov);
    double tangent_vertical = tan(view.vertical_fov * 0.5f);
    const auto location = view.camera_location;
    const auto dof = view.camera_dof;
    const auto width = glm::vec3((tangent * far_clip + ratio) * glm::dvec3(view.rotation_column0));
    const auto height =
        glm::vec3((tangent_vertical * far_clip + ratio) * glm::dvec3(view.rotation_column1));
    const auto forward = far_clip * dof;
    const auto apex = location - backoff * dof;
    const std::array<glm::vec3, 4> far_corners{
        {(forward - width) + height + location, (forward + height) + width + location,
         (forward - width) - height + location, (forward + width) - height + location}};
    view.frustum_planes[4] = {dof, -glm::dot(dof, apex)};
    view.frustum_planes[5] = {-dof, glm::dot(dof, far_corners[0])};
    constexpr std::array<std::array<unsigned, 2>, 4> sides{{{1, 0}, {0, 2}, {3, 1}, {2, 3}}};
    for (unsigned side = 0; side < sides.size(); ++side)
        view.frustum_planes[side] =
            srPlaneMath::triangle(apex, far_corners[sides[side][0]], far_corners[sides[side][1]]);
    for (int index = 0; index < 6; ++index) {
        view.frustum_planes[index].w += m_spatial.m_region_grid_cell;
    }
}

/* Collect the cells around the camera into the current region set.

   Two setup helpers refresh the frame state. The leaf cell radius comes from
   the far clip over the cell size, and the camera cell is quantized against
   the same size. Every cell within that radius of the camera cell that stays
   inside the spatial extent is considered: the near cells descend the branch
   array directly, while the far cells filter through
   PointInsideFrustum first. Either way the reached node contributes
   the region stored at its branch head. */
// FUNCTION: WIZ8 0x0042fe90
void W8Octree::CollectVisibleCells()
{
    BuildFrustumPlanes();
    MarkVisibleRegions();
    short radius =
        static_cast<short>((static_cast<int>((view.far_clip / m_spatial.m_region_grid_cell)) + 1));
    glm::i16vec3 center(0);

    for (int axis = 0; axis < 3; ++axis) {
        center[axis] = static_cast<short>(
            static_cast<int>(((view.camera_location[axis] - m_spatial.m_bounds.minimum[axis]) /
                              m_spatial.m_region_grid_cell)));
    }
    unsigned int region_base = m_region_mask;
    for (short x = -radius; x <= radius; ++x) {
        short cell_x = center.x + x;
        if (cell_x < 0 || cell_x >= m_spatial.m_region_cells_per_axis) {
            continue;
        }
        for (short y = -radius; y <= radius; ++y) {
            short cell_y = center.y + y;
            if (cell_y < 0 || cell_y >= m_spatial.m_region_cells_per_axis) {
                continue;
            }
            for (short z = -radius; z <= radius; ++z) {
                short cell_z = center.z + z;
                if (cell_z < 0 || cell_z >= m_spatial.m_region_cells_per_axis) {
                    continue;
                }
                if (abs(x) >= 2 || abs(y) >= 2 || abs(z) >= 2) {
                    float offset = m_spatial.m_region_grid_cell * 0.5f;
                    glm::vec3 point;
                    point.x = cell_x * m_spatial.m_region_grid_cell + offset +
                              m_spatial.m_bounds.minimum.x;
                    point.y = cell_y * m_spatial.m_region_grid_cell + m_spatial.m_bounds.minimum.y +
                              offset;
                    point.z = cell_z * m_spatial.m_region_grid_cell + m_spatial.m_bounds.minimum.z +
                              offset;
                    if (!srPlaneMath::containsSphere(view.frustum_planes, point, 0)) {
                        continue;
                    }
                }
                unsigned int cell[4];
                cell[0] = region_base;
                cell[1] = cell_x;
                cell[2] = cell_y;
                cell[3] = cell_z;
                int node = DescendByMask(cell);
                if (node != 0) {
                    unsigned short region = m_branches[node].region;
                    if (region != 0) {
                        SetBit(m_current_regions, region);
                    }
                }
            }
        }
    }
}

// FUNCTION: WIZ8 0x004329a0
static unsigned char FindNextLevelFile(std::string* name)
{
    if (name == 0) {
        g_octree_file_search_active = 0;
        g_octree_file_search.clear();
        return 0;
    }
    if (!g_octree_file_search_active) {
        std::string search = *name;
        const auto extension_start = search.find_last_of('.');
        const auto extension =
            extension_start == std::string::npos ? std::string{} : search.substr(extension_start);
        if (extension_start != std::string::npos) {
            search.resize(extension_start);
        }
        search += g_octree_file_search_wildcard;
        search += extension;
        const auto separator = search.find_last_of("\\/");
        g_octree_file_search = wiz8::list_directory(
            wiz8::asset_path(separator == std::string::npos ? "." : search.substr(0, separator)),
            separator == std::string::npos ? search : search.substr(separator + 1));
        g_octree_file_search_index = 0;
        g_octree_file_search_active = !g_octree_file_search.empty();
    } else {
        g_octree_file_search_active = ++g_octree_file_search_index < g_octree_file_search.size();
    }
    if (!g_octree_file_search_active) {
        g_octree_file_search.clear();
        return 0;
    }
    const auto separator = name->find_last_of("\\/");
    name->resize(separator == std::string::npos ? 0 : separator + 1);
    *name += g_octree_file_search[g_octree_file_search_index];
    return 1;
}

// FUNCTION: WIZ8 0x00432b80
bool W8Octree::LoadPointFiles(const char* level_name)
try {
    std::string stem = level_name;
    if (const auto extension = stem.find_last_of('.'); extension != std::string::npos) {
        stem.resize(extension);
    }
    stem += g_octree_point_extension;
    std::string name = std::move(stem);

    FindNextLevelFile(0);
    std::vector<glm::vec3> points;
    unsigned int count = 0;
    unsigned int files = 0;
    while (FindNextLevelFile(&name) != 0) {
        auto file = wiz8::open_file(wiz8::asset_path(name));
        file->read_exact(&count, sizeof(count));
        const auto bytes = std::uint64_t{count} * sizeof(wiz8::retail::Vec3f);
        if (bytes > static_cast<std::uint64_t>(file->size() - file->tell()) ||
            std::uint64_t{count} + 1 >
                std::numeric_limits<std::size_t>::max() / sizeof(wiz8::retail::Vec3f)) {
            return false;
        }
        std::vector<glm::vec3> next(count);
        for (auto& point : next) wiz8::retail::read_exact(*file, point);
        points = std::move(next);
        ++files;
    }
    if (files == 0) {
        return false;
    }
    m_sample_points = std::move(points);
    m_points_dirty |= files > 1;
    return true;
} catch (const std::exception&) {
    return false;
}

/* Write the octree's point count and records to the mutable overlay. */
// FUNCTION: WIZ8 0x00432d60
bool W8Octree::SavePoints(const char* path)
try {
    if (m_sample_points.empty() || m_sample_points.size() > UINT32_MAX) {
        return false;
    }
    std::string name = path;
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += g_octree_point_extension;
    auto file = wiz8::open_file(wiz8::user_path(name), wiz8::OpenMode::replace);
    const auto count = static_cast<std::uint32_t>(m_sample_points.size());
    file->write(&count, sizeof(count));
    for (const auto& point : m_sample_points) wiz8::retail::write(*file, point);
    file->close();
    return true;
} catch (const std::exception&) {
    return false;
}

// FUNCTION: WIZ8 0x00432e90
bool W8Octree::ReadRegionLinkFile(const char* level_name)
try {
    std::string name = level_name;
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += g_region_link_extension;
    auto file = wiz8::open_file(wiz8::asset_path(name));
    unsigned int count;
    file->read_exact(&count, sizeof(count));
    const auto bytes = std::uint64_t{count} * (sizeof(unsigned int) + sizeof(unsigned short));
    if (bytes > static_cast<std::uint64_t>(file->size() - file->tell()) ||
        std::uint64_t{count} > std::numeric_limits<std::size_t>::max() / sizeof(unsigned int)) {
        return false;
    }
    std::vector<unsigned int> keys(count);
    std::vector<unsigned short> values(count);
    file->read_exact(keys.data(), keys.size() * sizeof(keys[0]));
    file->read_exact(values.data(), values.size() * sizeof(values[0]));
    if (m_pRegionLinks == 0) {
        m_pRegionLinks = new std::unordered_multimap<unsigned int, unsigned short>;
    }
    for (unsigned int index = 0; index < count; ++index) {
        RemoveIndexValue(*m_pRegionLinks, keys[index], values[index]);
        InsertIndexValue(*m_pRegionLinks, keys[index], values[index]);
    }
    m_region_links_ready = true;
    return true;
} catch (const std::exception&) {
    return false;
}

/* The region volume containing `point` as its 1-based index into the spatial
   array, else the packed auto-region key - the region-grid level in the top
   byte over the point's cell coordinates. Region zero is never tested: it is
   the no-region sentinel. */
// FUNCTION: WIZ8 0x00432720
unsigned int W8Octree::RegionKeyForPoint(const glm::vec3& point)
{
    unsigned int region = 0;
    if (1 < m_spatial.m_region_count) {
        unsigned int index = 1;
        do {
            if (region != 0) {
                return region;
            }
            if (m_spatial.m_region_volumes[index].ContainsPoint(point)) {
                region = index;
            }
            ++index;
        } while (index < m_spatial.m_region_count);
        if (region != 0) {
            return region;
        }
    }
    glm::ivec3 cell;
    for (int axis = 0; axis < 3; ++axis) {
        cell[axis] = static_cast<int>((point[axis] - m_spatial.m_bounds.minimum[axis]) /
                                      m_spatial.m_region_grid_cell);
    }
    return (cell.x * 0x100 + cell.y) * 0x100 + cell.z + m_region_mask * 0x1000000;
}

/* Record `region_key` against every mesh marked in m_projected_regions:
   unseen (region, mesh) pairs are inserted into m_pRegionLinks and the
   link list is logged once per call. */
// FUNCTION: WIZ8 0x004327f0
void W8Octree::RecordRegionMeshLinks(unsigned int region_key)
{
    bool reported = false;
    if (region_key == 0) {
        return;
    }
    char text[256];
    for (std::size_t bit = 0; bit < m_projected_regions.size(); ++bit) {
        if (!m_projected_regions[bit]) {
            continue;
        }
        unsigned short mesh = static_cast<unsigned short>(bit);
        auto slot = m_pRegionLinks->equal_range(region_key).first;
        while (slot != m_pRegionLinks->end() && slot->second != mesh) {
            slot = NextIndexValue(*m_pRegionLinks, region_key, slot);
        }
        if (slot == m_pRegionLinks->end()) {
            if (!reported) {
                if (region_key < m_spatial.m_region_count) {
                    sprintf(text, "Pre-generated Region %d linked to meshes: ", region_key);
                } else {
                    sprintf(text, "Auto Region (%d, %d, %d) linked to meshes: ",
                            (region_key >> 0x10) & 0xff, (region_key >> 8) & 0xff,
                            region_key & 0xff);
                }
                reported = true;
            }
            sprintf(text, "%d, ", mesh);
            InsertIndexValue(*m_pRegionLinks, region_key, mesh);
        }
    }
    if (reported) {
    }
}

/* Sample `point` by rotating the camera through enough evenly spaced
   directions to cover the circle, collecting each direction's visible cells
   and render-probing their meshes. Cells whose mesh (or its first-child
   chain when `descend` is set) still draws more than nine faces are marked
   into the projected region set; `region_key` nonzero selects the region
   directly and zero resolves it through RegionKeyForPoint. `clear_sets`
   empties the projected and previous sets first. Returns the region key when
   at least one cell linked, zero otherwise. */
// FUNCTION: WIZ8 0x00431e10
unsigned int W8Octree::SampleRegionLinks(const glm::vec3& point, bool descend, bool clear_sets,
                                         unsigned int region_key)
{
    bool linked = false;
    W8World* world = GetWorld();
    std::vector<stModelInstance*> meshes;
    std::vector<int> cells;
    view.camera_location = point;
    view.far_clip = m_spatial.m_extent;
    char text[104];
    sprintf(text, "Sample point: %f, %f, %f, Links Found: ", point.x, point.y, point.z);
    glm::dvec3 location;
    location = glm::dvec3(point);
    world->camera->setLocation(location);
    view.horizontal_fov = static_cast<float>(world->camera->getHorizontalFOV());
    view.vertical_fov = static_cast<float>(world->camera->getVerticalFOV());
    if (region_key == 0) {
        region_key = RegionKeyForPoint(point);
    }
    unsigned int key = region_key;
    if (clear_sets) {
        std::ranges::fill(m_projected_regions, false);
        std::ranges::fill(m_previous_regions, false);
    }
    m_projected_regions_valid = false;
    short samples = static_cast<short>(
        static_cast<int>(kCameraAnglePeriod / view.horizontal_fov + kCameraSnapEpsilon));
    if (samples * view.horizontal_fov < kRegionLinkCircleCoverage) {
        ++samples;
    }
    if (samples > 0) {
        double cos_tilt = cos(kRegionLinkCameraTilt);
        double sin_tilt = sin(kRegionLinkCameraTilt);
        for (int direction = 0; direction < samples; ++direction) {
            float angle = direction * view.horizontal_fov;
            glm::mat3 frame;
            frame = glm::mat3(1);
            frame *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
            frame *= glm::mat3_cast(
                glm::angleAxis(float(std::atan2(sin_tilt, cos_tilt)), glm::vec3(1, 0, 0)));
            world->camera->setRotation(frame);
            view.captureCamera(*world->camera);
            std::ranges::fill(m_current_regions, false);
            CollectVisibleCells();
            bool all_known = true;
            {
                for (unsigned int cell = 0; cell < m_current_regions.size(); ++cell) {
                    if (!m_current_regions[cell]) {
                        continue;
                    }
                    meshes.push_back(
                        static_cast<stModelInstance*>(g_world->psrMeshes[m_pSubmeshes[cell].mesh]));
                    if (!TestBit(m_projected_regions, cell)) {
                        auto slot = m_pRegionLinks->equal_range(key).first;
                        while (slot != m_pRegionLinks->end() &&
                               slot->second != static_cast<unsigned short>(cell)) {
                            slot = NextIndexValue(*m_pRegionLinks, key, slot);
                        }
                        if (slot == m_pRegionLinks->end()) {
                            if (cell != 0) {
                                m_pSubmeshes[cell].flags |= 0x28;
                                srNode* node = g_world->psrMeshes[m_pSubmeshes[cell].mesh];
                                if (node != 0) {
                                    node->clearFlag(srNode::FLAG_DISABLE);
                                    if (descend) {
                                        node->clearFlag(srNode::FLAG_TERMINATE);
                                    }
                                }
                            }
                            cells.push_back(static_cast<int>(cell));
                            all_known = false;
                        } else {
                            goto known;
                        }
                    } else {
                    known:
                        if (cell != 0) {
                            m_pSubmeshes[cell].flags &= 0xfffffff7;
                            srNode* node = g_world->psrMeshes[m_pSubmeshes[cell].mesh];
                            if (node != 0) {
                                node->setFlag(srNode::FLAG_DISABLE);
                                node->setFlag(srNode::FLAG_TERMINATE);
                            }
                        }
                        cells.push_back(0);
                    }
                }
                if (!all_known) {
                    BeginRenderProbe();
                    for (int i = 0; i < static_cast<int>(meshes.size()); ++i) {
                        stModelInstance* mesh = meshes[i];
                        if (mesh != 0) {
                            unsigned int faces = MeasureNodeRender(mesh);
                            all_known = 9 < faces;
                            srNode* child = mesh->getChild();
                            while (child != 0 && descend) {
                                faces = MeasureNodeRender(child);
                                if (9 < faces) {
                                    all_known = true;
                                }
                                child = child->getChild();
                            }
                            if (!all_known) {
                                unsigned int candidate = cells[i];
                                if (candidate != 0) {
                                    m_pSubmeshes[candidate].flags &= 0xfffffff7;
                                    srNode* node = g_world->psrMeshes[m_pSubmeshes[candidate].mesh];
                                    if (node != 0) {
                                        node->setFlag(srNode::FLAG_DISABLE);
                                        node->setFlag(srNode::FLAG_TERMINATE);
                                    }
                                }
                                cells[i] = 0;
                            }
                        }
                    }
                    for (int index = 0; index < static_cast<int>(meshes.size()); ++index) {
                        stModelInstance* mesh = meshes[index];
                        if (mesh != 0) {
                            unsigned int cell = static_cast<unsigned int>(cells[index]);
                            if (cell != 0) {
                                unsigned int faces = MeasureNodeRender(mesh);
                                if (9 < faces) {
                                    sprintf(text, "%d, ", cell);
                                    SetBit(m_projected_regions, cell);
                                    linked = true;
                                }
                                srNode* child = mesh->getChild();
                                while (child != 0 && descend) {
                                    faces = MeasureNodeRender(child);
                                    if (9 < faces) {
                                        SetBit(m_projected_regions, cell);
                                        linked = true;
                                    }
                                    child = child->getChild();
                                }
                                m_pSubmeshes[cell].flags &= 0xfffffff7;
                                srNode* node = g_world->psrMeshes[m_pSubmeshes[cell].mesh];
                                if (node != 0) {
                                    node->setFlag(srNode::FLAG_DISABLE);
                                    node->setFlag(srNode::FLAG_TERMINATE);
                                }
                            }
                        }
                    }
                    EndRenderProbe();
                }
            }
        }
        region_key = key;
    }
    if (!linked) {
        region_key = 0;
    }
    return region_key;
}

/* Rebuild the region-link table. With `rebuild_all` clear the x/z sample
   grid steps three region cells and checkerboards the z start, then the
   stored point list is re-sampled; with it set every cell is walked and the
   .pts point file and stored list are discarded. Either path then samples
   every camera path node. The old link table is swapped for a scratch table
   during the build so a failure or Esc abort can restore it; the scratch
   becomes the live table once the links save. */
// FUNCTION: WIZ8 0x004314c0
void W8Octree::BuildRegionLinks(bool rebuild_all)
{
    W8World* world = GetWorld();
    bool aborted = false;
    unsigned int last_key = 0;
    time_t started = time(0);
    std::unordered_multimap<unsigned int, unsigned short>* links =
        new std::unordered_multimap<unsigned int, unsigned short>;
    unsigned short z_step = rebuild_all ? 1 : 3;
    float stride = z_step * m_region_cell;
    unsigned short rows =
        static_cast<unsigned short>(static_cast<int>(m_spatial.m_extent / stride));
    for (unsigned int prop = 0; prop < m_props.size(); ++prop) {
        m_props[prop]->SetActivationState(0);
    }
    for (unsigned int particle = 0; particle < m_particles.size(); ++particle) {
        m_particles[particle]->SetTraversalEnabled(false);
    }
    SetViewportMode(0);
    SetScaledViewport({0, 0, 0x140, 0xf0});
    glm::vec3 saved_location;
    glm::mat3 saved_rotation;
    saved_location = glm::vec3(world->camera->getLocation());
    saved_rotation = world->camera->getRotation<glm::mat3>();
    g_light_update_flags &= ~1u;
    g_render_untextured = true;
    SetCameraLightMode(2);
    g_render_unlit = true;
    g_render_cull_front = true;
    double saved_width = world->camera->getHorizontalFOV();
    double saved_height = world->camera->getVerticalFOV();
    world->camera->setViewPlane(2.094395102, 2.094395102);
    DisableAllRenderOptions();
    for (unsigned int mesh = 0; mesh < m_meshCount; ++mesh) {
        srNode* node = g_world->psrMeshes[mesh];
        if (node != 0) {
            node->setFlag(srNode::FLAG_DISABLE);
            node->setFlag(srNode::FLAG_TERMINATE);
        }
    }
    links->clear();
    std::unordered_multimap<unsigned int, unsigned short>* saved_links = m_pRegionLinks;
    m_pRegionLinks = links;
    if (rows != 0) {
        for (unsigned short x_index = 0; x_index < rows; ++x_index) {
            if (aborted) {
                break;
            }
            glm::vec3 point;
            point.x = m_region_cell * 0.5f + x_index * stride + m_spatial.m_bounds.minimum.x;
            unsigned short z_index = !rebuild_all && (x_index & 1) != 0 ? z_step : 0;
            if (!(m_spatial.m_working_bounds.minimum.x < point.x &&
                  point.x < m_spatial.m_working_bounds.maximum.x)) {
                continue;
            }
            for (; z_index < rows; z_index += z_step) {
                if (aborted) {
                    break;
                }
                point.z = m_region_cell * 0.5f + z_index * stride + m_spatial.m_bounds.minimum.z;
                if (!(m_spatial.m_working_bounds.minimum.z < point.z &&
                      point.z < m_spatial.m_working_bounds.maximum.z)) {
                    continue;
                }
                const bool received_events = PumpGameEvents();
                if (!gfProgramIsRunning) {
                    aborted = true;
                    g_build_level_links = false;
                    break;
                }
                if (received_events) {
                    if (auto input = PollInputEvent();
                        input && input->type == InputEventType::KeyDown) {
                        if (input->key == SDLK_RETURN) {
                            x_index += 2;
                            z_index = 0;
                        } else if (input->key == SDLK_ESCAPE) {
                            aborted = true;
                            if (g_build_level_links) {
                                g_build_level_links = false;
                            }
                            continue;
                        }
                    }
                }
                point.y = m_spatial.m_bounds.minimum.y + m_spatial.m_extent;
                while (pathing->SnapToLowerPathCell(&point, true)) {
                    point.y += g_default_world_height;
                    unsigned int key = RegionKeyForPoint(point);
                    if (key != last_key) {
                        if (last_key != 0) {
                            RecordRegionMeshLinks(last_key);
                        }
                        std::ranges::fill(m_projected_regions, false);
                        std::ranges::fill(m_previous_regions, false);
                        last_key = key;
                    }
                    SampleRegionLinks(point, false, false, key);
                    point.y -= g_default_world_height + kStartupNearLimit;
                }
            }
        }
    }
    g_render_untextured = false;
    EnableAllRenderOptions();
    std::unordered_multimap<unsigned int, unsigned short>* stale;
    if (!rebuild_all) {
        if (!aborted) {
            for (const auto& point : m_sample_points) {
                unsigned int key = SampleRegionLinks(point, true, true, 0);
                RecordRegionMeshLinks(key);
            }
        }
    } else {
        char point_path[256];
        strcpy(point_path, m_path_stem.c_str());
        char* extension = strrchr(point_path, '.');
        if (extension != 0) {
            *extension = '\0';
        }
        strcat(point_path, g_octree_point_extension);
        if ([&]() {
                const auto status = wiz8::file_status(wiz8::asset_path(point_path));
                return status && status->info.type == SDL_PATHTYPE_FILE;
            }()) {
            wiz8::remove_file(wiz8::user_path(point_path));
        }
        m_sample_points.clear();
    }
    world->camera->setViewPlane(saved_width, saved_height);
    unsigned int camera_count = world->plsCameras.size();
    if (!aborted && camera_count != 0) {
        for (int camera_index = 0; camera_index < static_cast<int>(camera_count); ++camera_index) {
            W8CameraPath* entry = GetWorldCameraPath(world, camera_index);
            if (entry != 0 && entry->path != 0) {
                for (int node_index = 0; node_index < static_cast<int>(entry->path->nodes.size());
                     ++node_index) {
                    unsigned int key =
                        SampleRegionLinks(entry->path->nodes[node_index], true, true, 0);
                    RecordRegionMeshLinks(key);
                }
            }
        }
    }
    glm::dvec3 restore_location;
    restore_location = glm::dvec3(saved_location);
    world->camera->setLocation(restore_location);
    world->camera->setRotation(saved_rotation);
    g_light_update_flags |= 1u;
    SetCameraLightMode(3);
    g_render_unlit = false;
    g_render_cull_front = false;
    SetScaledViewport({0, 0, 0x280, 0x1e0});
    m_region_links_dirty = true;
    m_region_links_ready = true;
    m_reset_visibility = true;
    unsigned int elapsed = static_cast<unsigned int>(static_cast<int>(difftime(time(0), started)));
    unsigned int minutes = elapsed / 60;
    unsigned int hours = 0;
    if (minutes > 0x3b) {
        hours = minutes / 60;
        minutes %= 60;
    }
    if (aborted) {
        stale = m_pRegionLinks;
        m_pRegionLinks = saved_links;
        SDL_Log("Region linking aborted");
    } else {
        SaveRegionLinks(m_path_stem.c_str());
        SDL_Log("Region linking took %u h %u min %u s", hours, minutes, elapsed % 60);
        stale = saved_links;
    }
    delete stale;
    MarkRendererReady();
}

/* Write the octree's region-link table to the .rlk companion file.

   The collected keys are the region ids from one up to the spatial region
   count, followed by the cell keys of the region grid offset by the link id.
   Each key's table values are appended in chain order; the count is written
   first, then the keys and their short values. */
// FUNCTION: WIZ8 0x004331f0
bool W8Octree::SaveRegionLinks(const char* path)
try {
    if (m_pRegionLinks == 0 || path == 0 || m_pRegionLinks->empty()) {
        return false;
    }
    std::string name = path;
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += g_region_link_extension;
    std::vector<unsigned int> keys;
    std::vector<unsigned short> values;
    for (unsigned int key = 1; key < m_spatial.m_region_count; ++key) {
        for (auto slot = m_pRegionLinks->equal_range(key).first; slot != m_pRegionLinks->end();
             slot = NextIndexValue(*m_pRegionLinks, key, slot)) {
            keys.push_back(key);
            values.push_back(slot->second);
        }
    }
    const unsigned int extent = m_spatial.m_region_cells_per_axis;
    const unsigned int base = m_region_mask * 0x1000000;
    for (unsigned int x = 0; x < extent; ++x) {
        for (unsigned int y = 0; y < extent; ++y) {
            for (unsigned int z = 0; z < extent; ++z) {
                unsigned int key = (x << 16) + (y << 8) + z + base;
                for (auto slot = m_pRegionLinks->equal_range(key).first;
                     slot != m_pRegionLinks->end();
                     slot = NextIndexValue(*m_pRegionLinks, key, slot)) {
                    keys.push_back(key);
                    values.push_back(slot->second);
                }
            }
        }
    }
    if (keys.size() > std::numeric_limits<unsigned int>::max()) {
        return false;
    }
    const unsigned int count = keys.size();
    auto file = wiz8::open_file(wiz8::user_path(name), wiz8::OpenMode::replace);
    file->write(&count, sizeof(count));
    file->write(keys.data(), keys.size() * sizeof(keys[0]));
    file->write(values.data(), values.size() * sizeof(values[0]));
    file->close();
    return true;
} catch (const std::exception&) {
    return false;
}

/* Draw the octree cell the camera currently occupies as a wire box. The cell
   is derived on each axis by quantizing the camera position relative to the
   spatial minimum, and the box spans one cell from there. */
// FUNCTION: WIZ8 0x00433eb0

/* Pack normalized alpha/red/green/blue components using the current x87
   rounding mode, then retain each result's low byte. */
// FUNCTION: WIZ8 0x00433fb0
w8_ulong* PackColourToLong(w8_ulong* color, double alpha, double red, double green, double blue)
{
    unsigned char* bytes =
        reinterpret_cast<unsigned char*>(color); // reinterpret-ok: packed colour storage
    bytes[3] = static_cast<unsigned char>(srFloatToInt(alpha * kColorByteScale));
    bytes[2] = static_cast<unsigned char>(srFloatToInt(red * kColorByteScale));
    bytes[1] = static_cast<unsigned char>(srFloatToInt(green * kColorByteScale));
    bytes[0] = static_cast<unsigned char>(srFloatToInt(blue * kColorByteScale));
    return color;
}

/* Validate the current octree's region-to-mesh links against a scratch copy
   of the spatial state. The scratch copy is flattened to the leaf level with
   all region bounds enabled, and every reported bad link is posted as one
   notice. */
// FUNCTION: WIZ8 0x00433ab0
unsigned char W8Octree::ValidateRegionMeshLinks()
{
    W8OctSpatialState spatial(&m_spatial);
    spatial.m_depth = 0;
    spatial.m_node_index = 1;
    spatial.m_level_kind = 1;
    int bad_links = CountBadRegionMeshLinks(&spatial);
    if (bad_links != 0) {
        SDL_Log("%d bad region-mesh links", bad_links);
        return 0;
    }
    return 1;
}

/* Recount a node's bad region links.  Leaf nodes union the bounding boxes of
   the linked mesh model's chain and flag the link when the union is disjoint
   from the cell; branch nodes subdivide the cell among the eight children and
   accumulate their counts.  Depths past the record limit count as one bad
   link. */
// FUNCTION: WIZ8 0x00433B90
int W8Octree::CountBadRegionMeshLinks(W8OctSpatialState* spatial)
{
    W8OctSpatialState local(spatial);
    int bad_links = 0;
    if (spatial->m_depth < 0x10) {
        if (spatial->m_depth == m_spatial.m_leaf_level) {
            unsigned short link = m_branches[spatial->m_node_index].region;
            if (link != 0) {
                stModelInstance* mesh =
                    static_cast<stModelInstance*>(g_world->psrMeshes[m_pSubmeshes[link].mesh]);
                if (mesh != 0) {
                    if (auto* model = mesh->getModel()) {
                        const auto bounds = model->getBounds().box;
                        const auto world = mesh->getWorldSpaceMatrix<glm::mat4>();
                        const auto cell = spatial->bounds();
                        bad_links = !bounds.transformed(world).overlaps(cell);
                    }
                }
            }
        } else {
            const auto minimum = spatial->m_bounds.minimum;
            for (unsigned child = 0; child < 8; ++child) {
                local.m_node_index = m_branches[spatial->m_node_index].children[child];
                if (!local.m_node_index) continue;
                const glm::vec3 offset((child >> 2) & 1, (child >> 1) & 1, child & 1);
                const auto corner = minimum + offset * local.m_extent;
                local.m_bounds.minimum = corner;
                local.m_bounds.maximum = corner + local.m_extent;
                bad_links += CountBadRegionMeshLinks(&local);
            }
        }
    } else {
        bad_links = 1;
    }
    return bad_links;
}

/* Flip or clear the octree update suspension.

   A null world only clears the retained render node and the flag. A real world
   toggles the flag: resuming forces the next update to rebuild all visibility
   state, while suspending drops every mesh from view, clears the visited
   region bytes, and lazily attaches the render node to the static scene. */
// FUNCTION: WIZ8 0x00434020

// FUNCTION: WIZ8 0x00434220
unsigned char W8Octree::TestNoiseLineOfSight(const glm::vec3* from, glm::vec3* to, float* range,
                                             int* hops)
{
    return pathing->MeasureAttachmentPath(from, to, range, hops);
}

// FUNCTION: WIZ8 0x00434250
unsigned char W8Octree::PrepareNavigatorTarget(W8NavigatorMovementState* movement, float radius,
                                               float separation)
{
    unsigned char result = 0;
    movement->target_position = settleToGround(movement->target_position, true, 500).position;
    if (pathing == 0) {
        return 1;
    }
    glm::vec3 delta = movement->target_position - movement->position;
    delta.y = 0.0f;
    if (glm::length(((glm::vec2(delta.x, delta.z)))) < NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE) {
        return 0;
    }
    if ((movement->attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
        glm::vec3 target = movement->target_position;
        if (pathing->FindPathCell(&target, 0, true) != 0) {
            if (!pathing->TestWaypointSpan(&movement->position, &target, false, false)) {
                movement->attachment->InitializeSegment(&movement->position, &target);
                movement->attachment->separation = separation;
                result = pathing->BuildAttachmentPath(movement->attachment.get(),
                                                      movement->navigation_filter);
                if (result != 0) {
                    W8NavigatorAttachment* attachment = movement->attachment.get();
                    attachment->path[attachment->path_position_index].position =
                        movement->target_position;
                    attachment->path_destination =
                        attachment->path[attachment->path_position_index].position;
                    pathing->AdvanceAttachmentWaypoint(&movement->position, attachment);
                    movement->target_position = movement->attachment->GetNextPosition();
                    return result;
                }
                result = pathing->ProbeAttachmentPath(movement->attachment.get());
                if (result != 0) {
                    W8NavigatorAttachment* attachment = movement->attachment.get();
                    attachment->path[attachment->path_position_index].position =
                        movement->target_position;
                    attachment->path_destination =
                        attachment->path[attachment->path_position_index].position;
                    return result;
                }
            } else {
                movement->attachment->InitializeSegment(&movement->position,
                                                        &movement->target_position);
                result = 1;
            }
        }
        return result;
    }
    delta = movement->target_position - movement->position;
    float length = glm::length(delta);
    float gap = length - separation;
    if (gap < 0.0f) {
        movement->attachment->InitializeSegment(&movement->position, &movement->position);
        return 1;
    }
    if (gap < kWorldScale) {
        delta = glm::dot(delta, delta) == 0
                    ? delta
                    : glm::normalize(delta) * static_cast<float>(gap * 1.01f);
        delta += movement->position;
        movement->attachment->InitializeSegment(&movement->position, &delta);
        return 1;
    }
    movement->attachment->InitializeSegment(&movement->position, &movement->target_position);
    movement->attachment->separation = separation;
    return pathing->PlanMovement(movement, radius, separation) != 0;
}

// FUNCTION: WIZ8 0x004347d0
bool IsNavigatorAtTarget(W8NavigatorMovementState* movement)
{
    glm::vec3 target;
    if (movement->attachment != 0) {
        W8NavigatorAttachment* attachment = movement->attachment.get();
        if (attachment->path_cursor < attachment->path_position_index ||
            (attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
            return false;
        }
        target = attachment->GetNextPosition();
    } else {
        target = movement->target_position;
    }
    if (movement->movement_scale * kWorldScale < glm::length((target - movement->position))) {
        return false;
    }
    return true;
}

// FUNCTION: WIZ8 0x00434880
unsigned char W8Octree::PrepareNavigatorPatrol(W8NavigatorMovementState* movement, float minimum,
                                               float maximum)
{
    unsigned char result = 0;
    if (pathing != 0) {
        glm::vec3 velocity = movement->velocity;
        movement->attachment->InitializeSegment(&movement->position, &movement->target_position);
        result = pathing->BuildPatrolPath(movement->attachment.get(), movement->navigation_filter,
                                          &movement->target_position, minimum, &velocity, maximum);
        if (result == 0) {
            return 0;
        }
        double step = movement->movement_scale * kWorldScale;
        movement->target_position = movement->attachment->GetNextPosition();
        glm::vec3 delta;
        delta = movement->target_position - movement->position;
        if (step < glm::length(delta)) {
            delta = glm::dot(delta, delta) == 0 ? delta
                                                : glm::normalize(delta) * static_cast<float>(step);
        }
        movement->target_position = delta + movement->position;
    }
    return result;
}

// FUNCTION: WIZ8 0x00434a00
unsigned char W8Octree::LinkNavigatorTarget(W8NavigatorMovementState* movement,
                                            const glm::vec3* target, float separation)
{
    if (pathing != 0) {
        return pathing->LinkAttachmentTarget(movement->attachment.get(),
                                             movement->navigation_filter, target, separation);
    }
    return 0;
}

/* The cell-walk probes and the trace helpers the two line-of-sight bodies use.
   None of their bodies are recovered, so they keep address-qualified names. */

// GLOBAL: WIZ8 0x005ebcd0
const float g_octree_cell_scale = 100.0f;
/* Follow one child bit per axis and level through the compact 9-word branch
   records.  Zero is the missing-child sentinel; live leaves start at one. */
// FUNCTION: WIZ8 0x00433660
w8_ulong W8Octree::FindLeaf(const glm::ivec3& point)
{
    int level = m_spatial.m_depth;
    int mask = 1 << m_spatial.m_depth;
    w8_ulong node = 1;

    do {
        if (level < 1) {
            break;
        }
        mask /= 2;
        int child = 0;
        if ((point.x & mask) != 0) {
            child = 4;
        }
        if ((point.y & mask) != 0) {
            child += 2;
        }
        if ((point.z & mask) != 0) {
            child += 1;
        }
        node = m_branches[node].children[child];
        --level;
    } while (node != 0);
    if (m_leaf_count < node) {
        return 0;
    }
    return node;
}

/* Descend the branch tree while `masked_cell`'s leading mask word keeps the
   current level bit set, choosing the octant from the three coordinate words.
   The region validators compare the returned node's region field. */
// FUNCTION: WIZ8 0x004336d0
int W8Octree::DescendByMask(const unsigned int* masked_cell)
{
    int node = 1;
    int bit = 1 << m_spatial.m_depth;
    while (bit != 0 && node != 0) {
        if ((masked_cell[0] & bit) != 0) {
            int octant = 0;
            if ((bit & masked_cell[1]) != 0) {
                octant = 4;
            }
            if ((masked_cell[2] & bit) != 0) {
                octant += 2;
            }
            if ((masked_cell[3] & bit) != 0) {
                octant += 1;
            }
            node = m_branches[node].children[octant];
        }
        bit /= 2;
    }
    return node;
}

/* Bounds-checked cell -> leaf index shared by the cell probes: the direct
   leaf grid when one exists, else the same masked branch descent. */
// FUNCTION: WIZ8 0x00433730
unsigned int W8Octree::LeafIndexForCell(const glm::ivec3& cell)
{
    if (cell.x < 0 || static_cast<int>(m_leaf_grid_dimensions.x) <= cell.x || cell.y < 0 ||
        static_cast<int>(m_leaf_grid_dimensions.y) <= cell.y || cell.z < 0 ||
        static_cast<int>(m_leaf_grid_dimensions.z) <= cell.z) {
        return 0;
    }
    if (!m_leaf_lookup.empty()) {
        return m_leaf_lookup[m_spatial.m_leaf_grid_stride_x * cell.x + cell.z +
                             m_spatial.m_leaf_grid_stride_y * cell.y];
    }
    return FindLeaf(cell);
}

/* Settle a point onto the geometry below it: seed a downward trace from the
   point raised by `limit` and march the cell column downward, optionally
   testing the cell's props first (a prop hit is remembered in current_prop;
   a level-surface hit clears it) and always testing the level surfaces.
   The point's y drops to the contact on a hit and keeps its input value on a
   miss, after clamping to the ceiling. Both values are returned together. */
// FUNCTION: WIZ8 0x00433820
GroundProbe W8Octree::settleToGround(const glm::vec3& input, bool test_props, float limit)
{
    glm::vec3 position = input;
    W8OctreeTrace trace;
    glm::ivec3 cell;
    glm::vec3 start;
    glm::vec3 end;
    bool prop_hit = false;
    bool hit = false;

    m_gd_result_count = 0;
    if (test_props != 0) {
        current_prop = -1;
    }
    std::ranges::fill(m_visited_object_bits, false);
    if (m_spatial.m_clipped_bounds.maximum.y < position.y) {
        position.y = m_spatial.m_clipped_bounds.maximum.y;
    }
    end = position;
    start = position;
    start.y = position.y + limit;
    cell = cellAt(start);
    end.y = ((cell.y - 1)) * m_spatial.m_node_extent + m_spatial.m_bounds.minimum.y;
    trace.reset(start, end);
    if (-1 < cell.y) {
        do {
            if (hit) {
                goto done;
            }
            if (test_props != 0) {
                g_octree_state = m_aulGDObjs.data();
                m_gd_result_count = 0;
                w8_ulong before = m_gd_result_count;
                CollectObjectsInCell(cell, W8_OCTREE_KIND_PROP);
                if (m_gd_result_count != before) {
                    int prop = g_octree_game_data->TestPropSurfaces(
                        m_gd_result_count, m_aulGDObjs.data(), &trace, 0, 0);
                    current_prop = prop;
                    if (prop >= 0) {
                        prop_hit = true;
                    }
                }
            }
            g_octree_game_data->trace_flag4_gate = true;
            if (ProbeCellForTrace(cell) == 0) {
                goto descend;
            }
            hit = g_octree_game_data->TestTraceResult(m_gd_result_count, m_aulGDObjs.data(), &trace,
                                                      0, 0);
            if (!hit) {
            descend:
                if (prop_hit) {
                    hit = true;
                } else {
                    float level = static_cast<float>(cell.y);
                    --cell.y;
                    start.y = level * m_spatial.m_node_extent + m_spatial.m_bounds.minimum.y;
                    end.y -= m_spatial.m_node_extent;
                    trace.reset(start, end);
                }
            } else if (prop_hit) {
                current_prop = -1;
            }
            g_octree_game_data->trace_flag4_gate = false;
        } while (-1 < cell.y);
        if (hit) {
        done:
            position.y = trace.end.y;
            goto out;
        }
    }
    trace.end.y = position.y;
out:
    return {position, hit};
}

/* Snap a point onto the surface below: clamp it to the octree's clipped
   ceiling, probe the ground one world-scale unit lower and keep the settled
   height on a hit. The path builders and navigator placement use it to drop
   points onto terrain. */
// FUNCTION: WIZ8 0x00431d20
bool W8Octree::SnapToGround(glm::vec3* position, char mode)
{
    bool hit = false;
    if (position->y > m_spatial.m_clipped_bounds.maximum.y) {
        position->y = m_spatial.m_clipped_bounds.maximum.y;
    }
    glm::vec3 probe = *position;
    probe.y = position->y - kWorldScale;
    const auto ground_probe = settleToGround(probe, mode, 500.0f);
    probe = ground_probe.position;
    hit = ground_probe.hit;
    if (hit) {
        position->y = probe.y;
    }
    return hit;
}

// FUNCTION: WIZ8 0x00434b60
bool W8Octree::HasLineOfSight(const glm::vec3* from, glm::vec3* to, bool allow_fallback)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;
    unsigned char blocked = 0;

    W8OctreeTrace trace((*from), (*to));

    m_gd_result_count = 0;
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_current_regions, false);
    cell = cellAt((*from));
    end_cell = cellAt((*to));

    visitOctreeSegment(
        cell, end_cell, [&] { return cellWalk((*from), (*to)); },
        [&](const glm::ivec3& current, bool adjacent) {
            if (ProbeCellForBlockers(current) != 0 || adjacent) blocked = TestProbeResult(&trace);
            return blocked == 0;
        });
    if (blocked != 0) {
        *to = trace.end;
    } else if (allow_fallback) {
        if (const auto hit = traceProps((*from), (*to), 1, 1)) {
            *to = hit->position;
            blocked = 1;
        }
    }
    return blocked == 0;
}

// FUNCTION: WIZ8 0x00434f20
short W8Octree::TraceLineOfSight(const glm::vec3* from, glm::vec3* to, bool trace_world,
                                 int from_location_id, int to_location_id, bool visit_octree,
                                 int trace_mode)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;

    bool blocked = false;
    int result;

    W8OctreeTrace trace((*from), (*to));
    result = 0;

    if (visit_octree) {
        m_gd_result_count = 0;
        std::ranges::fill(m_visited_object_bits, false);
        cell = cellAt((*from));
        end_cell = cellAt((*to));

        visitOctreeSegment(
            cell, end_cell, [&] { return cellWalk((*from), (*to)); },
            [&](const glm::ivec3& current, bool adjacent) {
                if (ProbeCellForTrace(current) != 0 || adjacent)
                    blocked = g_octree_game_data->TestTraceResult(
                        m_gd_result_count, m_aulGDObjs.data(), &trace, m_trace_skip_flag, 0);
                return !blocked;
            });
        const auto prop_hit = trace_world ? traceProps((*from), trace.end, 0, 0) : std::nullopt;
        if (!prop_hit) {
            if (!blocked) {
                goto resolve;
            }
        } else {
            trace.end = prop_hit->position;
            blocked = true;
        }
        result = 1;
        if (blocked) {
            *to = trace.end;
            return 1;
        }
    }
resolve:
    if (from_location_id > -3) {
        if (const auto hit = traceSphereHit(trace.start, trace.end, from_location_id,
                                            to_location_id, to_location_id, 0, trace_mode)) {
            *to = hit->position;
            return -1;
        }
    }
    return static_cast<short>(result);
}

/* Trace location spheres and then the camera. The winning location id
   retains the historical last-collider-offset behavior. */
// FUNCTION: WIZ8 0x004353f0
std::optional<W8Octree::SphereTraceHit>
W8Octree::traceSphereHit(const glm::vec3& from, const glm::vec3& to, int excluded, int target,
                         int excluded_location, unsigned int flags, bool noise_adjust)
{
    unsigned int index = 0;
    std::vector<w8_ulong> ids;
    unsigned int best_index = 0;
    double best = -1.0;
    float inflate;
    W8Navigator* navigator;
    float radius;
    unsigned int probe_set = 0;
    glm::vec3 offset;
    const float segment_length = noise_adjust ? glm::distance(from, to) : 0;
    const auto sphereDistance = [&](const glm::vec3& center) {
        const auto projection = srSegmentMath::project(center, from, to);
        float distance = projection.distance;
        if (noise_adjust) {
            distance -= (glm::distance(projection.point, from) / segment_length * 0.15f + 0.1f) *
                        kWorldScale;
            if (distance < 0.0f) distance = 0.0f;
        }
        return distance;
    };
    const auto sphereOffset = [&](float sphere_radius, float distance) {
        const auto direction = to - from;
        const double length2 = glm::dot(direction, direction);
        if (length2 == 0.0) return direction;
        const double length = std::sqrt(length2);
        const double fraction =
            (length - std::sqrt(sphere_radius * sphere_radius - distance * distance)) / length;
        return glm::vec3(glm::dvec3(direction) * fraction);
    };
    inflate = g_position_height_epsilon;
    if (g_runtime_world_scale > inflate) {
        inflate = g_runtime_world_scale;
    }
    const auto reversed = glm::greaterThan(from, to);
    const glm::vec3 low(glm::mix(from, to, reversed) - inflate);
    const glm::vec3 high(glm::mix(to, from, reversed) + inflate);
    if (target == -1) {
        radius = g_startup_world->movement.alternate_radius;
        navigator = g_startup_world;
    } else {
        if (target < 1) {
            goto no_probes;
        }
        W8MonsterInfo* info = FindMonster(target);
        if (info == 0 || info->p3D == 0) {
            goto no_probes;
        }
        navigator = info->p3D;
        radius = navigator->movement.alternate_radius;
    }
    if (navigator != 0 && pathing != 0) {
        probe_set = pathing->CollectPathProbes(&navigator->movement, radius);
    }
no_probes:;
    ids = g_octree->queryObjects({low, high}, W8_OCTREE_KIND_LOCATION, -1);
    unsigned int count = ids.size(); /* c-style-cast-ok:
            the shared query count field is stored unsigned */
    if (count != 0) {
        do {
            int id = ids[index];
            if (((excluded < 0) || (excluded != id)) &&
                ((target < 0) || (excluded_location != id)) &&
                (probe_set == 0 || !pathing->MatchesPathProbe(id, 0, 0))) {
                W8MonsterInfo* info = FindMonster(id);
                if (info != 0) {
                    W8Monster* monster = info->p3D;
                    if (monster != 0 && monster->active && (monster->trace_mask & flags) == 0) {
                        const glm::vec3 center(monster->movement.position.x,
                                               monster->movement.position.y +
                                                   monster->movement.height_offset,
                                               monster->movement.position.z);
                        const float distance = sphereDistance(center);
                        float monster_radius = monster->radius;
                        if (distance < monster_radius) {
                            offset = sphereOffset(monster_radius, distance);
                            if (0.0 <= best) {
                                float span = offset.x * offset.x;
                                if (sqrt(offset.z * offset.z + offset.y * offset.y + span) < best) {
                                    best = sqrt(offset.y * offset.y + offset.z * offset.z + span);
                                    best_index = index;
                                }
                            } else {
                                best = std::sqrt(glm::dot(offset, offset));
                                best_index = index;
                            }
                        }
                    }
                }
            }
            ++index;
        } while (index < count);
        /* Retail verified: `offset` holds the last in-radius collider's
           offset, not necessarily the best one's — best/best_index update
           only on a shorter hit while `to` is written from the current
           `offset`, so the two can describe different monsters. */
        if (0.0 <= best) {
            return SphereTraceHit{offset + from, static_cast<int>(ids[best_index])};
        }
    }
    if (excluded != -1 && excluded_location != -1) {
        const float distance = sphereDistance(cameraLocation());
        const float camera_radius = g_startup_world->radius * g_camera_sphere_radius_scale;
        if (distance < camera_radius)
            return SphereTraceHit{sphereOffset(camera_radius, distance) + from, 0};
    }
    return std::nullopt;
}

/* Second Remove emission (0x00438C90 above is the other), serving the
   keyboard/automap tables' callers. */

// FUNCTION: WIZ8 0x00436b90
unsigned char W8OctreeObjectRegistry::MoveObjectToCell(W8OctreeObjectKind kind, int id,
                                                       const glm::ivec3& point)
{
    unsigned int object_key = PackOctreeObjectKey(kind, id);
    unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
    int object_value = static_cast<int>(object_key);
    int cell_value = static_cast<int>(cell_key);

    RemoveIndexValue(by_object, object_key, cell_value);
    InsertIndexValue(by_object, object_key, cell_value);
    RemoveIndexValue(by_cell, cell_key, object_value);
    InsertIndexValue(by_cell, cell_key, object_value);
    return 1;
}

// FUNCTION: WIZ8 0x00436dc0
unsigned char W8OctreeObjectRegistry::UnregisterObject(W8OctreeObjectKind kind, int id)
{
    unsigned int object_key = PackOctreeObjectKey(kind, id);
    int object_value = static_cast<int>(object_key);
    const auto slot = by_object.equal_range(object_key).first;
    if (slot == by_object.end()) {
        return 0;
    }
    const auto cell_key = static_cast<unsigned int>(slot->second);
    by_object.erase(slot);
    RemoveIndexValue(by_cell, cell_key, object_value);
    // Retail advances through the erased slot's free-list link and normally
    // ends here. Preserve the single-pair removal without dereferencing erase.
    return 1;
}

/* Record that one object now occupies one cell.

   The object's key is its kind in the high half and its id in the low half.
   When it is already registered somewhere, the cell it was in is compared
   against the one being queued using the degenerate retail coordinate test
   below; otherwise the old pairing comes out of both indexes first. A new
   location registration additionally maintains a pairing under the navigator
   kind.

   The two indexes are the same pair AddCollidablePropBounds keeps: one keyed by
   cell, one keyed by object. */
// FUNCTION: WIZ8 0x00437000
unsigned char W8OctreeObjectRegistry::RegisterObjectCell(W8OctreeObjectKind kind, int id,
                                                         const glm::ivec3& point)
{
    unsigned int object_key = PackOctreeObjectKey(kind & 0xffff, id);
    int occupied = LookupIndexValue(by_object, object_key);

    if (occupied != 0) {
        /* Retail only recognizes a repeat of (0,0,z): x is not decoded,
           and the old y byte is shifted too far. Preserve that test. */
        if (point.x == 0 && (((occupied - 1) & 0xff00) << 8) == point.y &&
            OctreeCellKeyZ(occupied) == point.z) {
            return 1;
        }
        unsigned int occupied_key = static_cast<unsigned int>(occupied);
        int object_value = static_cast<int>(object_key);
        RemoveIndexValue(by_object, object_key);
        RemoveIndexValue(by_cell, occupied_key, object_value);
    } else if (static_cast<short>(kind) == W8_OCTREE_KIND_LOCATION) {
        unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
        int cell_value = static_cast<int>(cell_key);
        unsigned int tagged_key = PackOctreeObjectKey(W8_OCTREE_KIND_NAVIGATOR, id);
        int tagged_value = static_cast<int>(tagged_key);
        RemoveIndexValue(by_object, tagged_key, cell_value);
        InsertIndexValue(by_object, tagged_key, cell_value);
        RemoveIndexValue(by_cell, cell_key, tagged_value);
        InsertIndexValue(by_cell, cell_key, tagged_value);
    }

    unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
    int cell_value = static_cast<int>(cell_key);
    int object_value = static_cast<int>(object_key);
    RemoveIndexValue(by_object, object_key, cell_value);
    InsertIndexValue(by_object, object_key, cell_value);
    RemoveIndexValue(by_cell, cell_key, object_value);
    InsertIndexValue(by_cell, cell_key, object_value);
    return 1;
}

/* Register one collidable prop against every octree cell its bounding box
   touches.

   Two indexes are kept in step: one keyed by cell so a cell can name its props,
   one keyed by prop so a prop can name its cells. Each cell first drops any
   matching pairing in both directions before the new one goes in. The cell key
   packs x, y and z into one dword a byte apart, and the prop key carries its id
   in the low half with a tag above it. */
// FUNCTION: WIZ8 0x0042eab0
void W8Octree::AddCollidablePropBounds(int index, const srBoundingBox& bounds)
{
    const auto minimum = cellAt(bounds.minimum);
    const auto maximum = cellAt(bounds.maximum);
    const auto prop_key = PackOctreeObjectKey(W8_OCTREE_KIND_PROP, index + 1);
    visitOctreeCellBox(minimum, maximum, [&](const glm::ivec3& cell) {
        const auto cell_key = PackOctreeCellKey(cell.x, cell.y, cell.z);
        const int cell_value = static_cast<int>(cell_key);
        const int prop_value = static_cast<int>(prop_key);
        auto& by_prop = object_registry->by_object;
        RemoveIndexValue(by_prop, prop_key, cell_value);
        InsertIndexValue(by_prop, prop_key, cell_value);
        auto& by_cell = object_registry->by_cell;
        RemoveIndexValue(by_cell, cell_key, prop_value);
        InsertIndexValue(by_cell, cell_key, prop_value);
    });
}

/* Build the cell walk between two world points.

   The three axis deltas are taken in cells; the longest of them drives, and the
   other two each get an error triple seeded from where inside its cell the line
   starts. The step count covers the driving axis plus the partial cell at each
   end, which is why the remainder decides between one and two extra. */
// FUNCTION: WIZ8 0x004362d0
W8OctreeWalk W8Octree::cellWalk(const glm::vec3& from, const glm::vec3& to) const
{
    W8OctreeWalk walk{};
    const float cell_size = m_spatial.m_node_extent * g_octree_cell_scale;
    const int cell = static_cast<int>(cell_size);
    const glm::ivec3 from_cell((from - m_spatial.m_bounds.minimum) * g_octree_cell_scale);
    const glm::ivec3 to_cell((to - m_spatial.m_bounds.minimum) * g_octree_cell_scale);
    const glm::vec3 delta(to_cell - from_cell);
    glm::vec3 fraction;
    int longest = 0;
    for (int axis = 0; axis < 3; ++axis) {
        int span = to_cell[axis] - from_cell[axis];
        fraction[axis] = (from_cell[axis] % cell) / cell_size;
        if (span < 0) {
            walk.step[axis] = -1;
            span = -span;
        } else {
            walk.step[axis] = 1;
            fraction[axis] = 1.0f - fraction[axis];
        }
        if (longest < span) {
            walk.major_axis = axis;
            longest = span;
        }
    }
    const int major = walk.major_axis;
    const int minor_0 = (major + 1) % 3;
    const int minor_1 = (major + 2) % 3;
    // A stationary segment visits its cell once, without dividing by zero.
    if (longest != 0) {
        walk.error_delta0 = static_cast<int>(glm::abs(delta[minor_0] / delta[major]) * cell_size);
        walk.error_delta1 = static_cast<int>(glm::abs(delta[minor_1] / delta[major]) * cell_size);
    }
    walk.error0 = static_cast<int>(cell * fraction[minor_0] - walk.error_delta0 * fraction[major]);
    walk.error1 = static_cast<int>(cell * fraction[minor_1] - walk.error_delta1 * fraction[major]);
    walk.count = longest % cell == 0 ? longest / cell + 1 : longest / cell + 2;
    walk.minor_axis0 = minor_0;
    walk.minor_axis1 = minor_1;
    walk.error_reset0 = cell;
    walk.error_reset1 = cell;
    walk.cell = from_cell / cell;
    return walk;
}

/* Walk the segment's cells collecting every kind-8 (collidable prop) registry
   entry into the shared buffer, then hand the accumulated id list to the
   GameData prop trace. The hit returns its prop id and contact together;
   current_prop keeps the selected id for existing registry consumers. */
// FUNCTION: WIZ8 0x00436510
std::optional<W8Octree::PropTraceHit>
W8Octree::traceProps(const glm::vec3& from, const glm::vec3& to, int skip_flag, int gate)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;

    W8OctreeTrace trace(from, to);
    g_octree_state = m_aulGDObjs.data();
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    cell = cellAt(from);
    end_cell = cellAt(to);
    visitOctreeSegment(
        cell, end_cell, [&] { return cellWalk(from, to); },
        [&](const glm::ivec3& current, bool) {
            g_octree_state = m_aulGDObjs.data();
            CollectObjectsInCell(current, W8_OCTREE_KIND_PROP);
            return true;
        });
    current_prop = g_octree_game_data->TestPropSurfaces(m_gd_result_count, m_aulGDObjs.data(),
                                                        &trace, skip_flag, gate);
    if (current_prop < 0) {
        return std::nullopt;
    }
    return PropTraceHit{trace.end, current_prop};
}

/* The cell probes share one lookup: bounds-check the cell against the grid,
   resolve its leaf index through the direct leaf grid when one exists else by
   descending the branch tree one octant per level, then drain the chosen leaf
   stream into m_aulGDObjs.data() through the m_visited_object_bits dedupe set. ProbeCellForTrace
   reads the leaf's gd_polygon_offset into the m_gd_surface_index_stream surface-id stream;
   ProbeCellForBlockers reads polygon_offset into the m_polygon_index_stream index stream
   and maps each entry through m_aulPolyLookup to a (mesh<<16)|polygon key, marking
   the mesh in m_current_regions. The Append variant skips the result-count
   reset so successive cells accumulate. */
/* Cell probes share LeafIndexForCell; their retail bodies expand this lookup. */
// FUNCTION: WIZ8 0x00435b00
int W8Octree::ProbeCellForTrace(const glm::ivec3& cell)
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    m_gd_result_count = 0;
    if (leaf_index != 0 && m_leaves[leaf_index].gd_polygon_offset != 0) {
        const w8_ulong* stream =
            m_gd_surface_index_stream.data() + m_leaves[leaf_index].gd_polygon_offset;
        int remaining = *stream;
        while (remaining != 0) {
            ++stream;
            if (9999 < m_gd_result_count) {
                break;
            }
            if (!SetBit(m_visited_object_bits, *stream)) {
                m_aulGDObjs[m_gd_result_count] = *stream;
                ++m_gd_result_count;
            }
            --remaining;
        }
    }
    return m_gd_result_count;
}

// FUNCTION: WIZ8 0x00435c40
int W8Octree::ProbeCellForBlockers(const glm::ivec3& cell)
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    m_gd_result_count = 0;
    if (leaf_index != 0 && m_leaves[leaf_index].polygon_offset != 0) {
        const w8_ulong* stream =
            m_polygon_index_stream.data() + m_leaves[leaf_index].polygon_offset;
        AppendBlockerStream(stream);
    }
    return m_gd_result_count;
}

void W8Octree::AppendBlockerStream(const w8_ulong* stream)
{
    for (int remaining = *stream; remaining != 0; --remaining) {
        ++stream;
        if (!SetBit(m_visited_polygon_bits, *stream)) {
            if (9999 < m_gd_result_count) {
                break;
            }
            unsigned int key = m_aulPolyLookup[*stream];
            m_aulGDObjs[m_gd_result_count] = key;
            ++m_gd_result_count;
            SetBit(m_current_regions, key >> 0x10);
        }
    }
}

// FUNCTION: WIZ8 0x00435da0
int W8Octree::ProbeCellForBlockersAppend(const glm::ivec3& cell)
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    if (leaf_index != 0 && m_leaves[leaf_index].polygon_offset != 0) {
        const w8_ulong* stream =
            m_polygon_index_stream.data() + m_leaves[leaf_index].polygon_offset;
        AppendBlockerStream(stream);
    }
    return m_gd_result_count;
}

/* March the buffered (mesh<<16)|polygon keys, ray-test each live mesh
   instance's triangle against the record and keep the nearest contact: the
   record's end returns the hit position and hit_limit the distance. */
// FUNCTION: WIZ8 0x00435f00
unsigned char W8Octree::TestProbeResult(W8OctreeTrace* trace)
{
    bool hit = false;
    glm::vec3 best_contact;

    for (unsigned int index = 0; index < m_gd_result_count; ++index) {
        unsigned int mesh_index = m_aulGDObjs[index] >> 0x10;
        if (mesh_index < m_meshCount && g_world->psrMeshes[mesh_index] != 0 &&
            !TestBit(m_alpha_bits, mesh_index)) {
            stMeshModel* model =
                static_cast<stMeshModel*>(g_world->psrMeshes[mesh_index]->getModel());
            const glm::vec4* planes = model->getPolyEq();
            unsigned int polygon = m_aulGDObjs[index] & 0xffff;
            const glm::vec4* plane = planes + polygon;
            const auto contact = planeContact(
                *plane, {trace->start, trace->end, trace->step, trace->length, trace->hit_limit},
                PlaneContactMode::Collision);
            if (contact) {
                const auto axis = dominantPlaneAxis(glm::vec3(*plane));
                const glm::ivec3* poly_vertex = model->getPolyVertex() + polygon;
                const glm::vec3* vertices = model->getVertexLoc();
                const RuntimeTriangle triangle{vertices[poly_vertex->x], vertices[poly_vertex->y],
                                               vertices[poly_vertex->z]};
                if (PointInsideTriangle(triangle, axis, contact->point) &&
                    contact->distance < trace->hit_limit) {
                    trace->hit_limit = contact->distance;
                    hit = true;
                    best_contact = contact->point;
                }
            }
        }
    }
    if (hit) {
        trace->end = best_contact;
    }
    return hit;
}

/* Collect the (mesh<<16)|polygon keys whose triangles touch the box
   (x±radius, y-height..y, z±radius) around `center`: every cell in the box is
   probed through ProbeCellForBlockersAppend, each key is then dropped when the
   mesh is gone, alpha-flagged or facing away, and when its triangle fails the
   spatial bounds test. The survivors are compacted, sorted for ordered
   consumption and zero-terminated; the shared buffer is returned. */
// FUNCTION: WIZ8 0x00438780
std::vector<w8_ulong> W8Octree::polygonsNearPoint(const glm::vec3& position, float radius,
                                                  float height)
{
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_current_regions, false);

    const srBoundingBox box{position - glm::vec3(radius, height, radius),
                            position + glm::vec3(radius, 0, radius)};
    const auto start = cellAt(box.minimum);
    const auto end = cellAt(box.maximum);
    visitOctreeCellBox(start, end,
                       [&](const glm::ivec3& cell) { ProbeCellForBlockersAppend(cell); });

    std::vector<w8_ulong> polygons;
    polygons.reserve(m_gd_result_count);
    for (const auto key : std::span(m_aulGDObjs).first(m_gd_result_count)) {
        const auto mesh_index = key >> 16;
        if (key == 0 || mesh_index >= m_meshCount || !g_world->psrMeshes[mesh_index] ||
            TestBit(m_alpha_bits, mesh_index))
            continue;
        auto& model = *static_cast<stMeshModel*>(g_world->psrMeshes[mesh_index]->getModel());
        const auto polygon = key & 0xffff;
        const auto plane = model.getPolyEq()[polygon];
        if (plane.y < 0.5f) continue;
        const auto& corners = model.getPolyVertex()[polygon];
        const auto* vertices = model.getVertexLoc();
        const RuntimeTriangle triangle{vertices[corners.x], vertices[corners.y],
                                       vertices[corners.z]};
        if (triangleIntersectsBox(box, triangle, glm::vec3(plane))) polygons.push_back(key);
    }
    std::ranges::sort(polygons);
    return polygons;
}

/* A location the octree cannot resolve, or one whose submesh has no live model
   instance, clears the monster's cached mesh rather than leaving a stale one. */
// FUNCTION: WIZ8 0x0042e540
void W8Octree::UpdateMonsterLocation(unsigned short location_id, const glm::vec3* position)
{
    int queue_id = location_id + 1;
    W8MonsterInfo* info;
    W8Monster* monster;
    int sector;
    srModelInstance* mesh;
    glm::ivec3 point;

    if (location_id == 0) {
        return;
    }
    info = FindMonster(location_id);
    if (info != 0 && info->p3D != 0) {
        monster = info->p3D;
        sector = GetSectorForPosition(position);
        if (sector == 0 || (mesh = g_world->psrMeshes[m_pSubmeshes[sector].mesh]) == 0) {
            monster->sector_mesh = 0;
        } else {
            monster->sector_mesh = mesh;
        }
    }
    point = cellAt((*position));
    object_registry->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, queue_id, point);
}

bool W8Octree::SetPathStem(const char* path)
{
    if (*path == '\0') {
        return false;
    }
    std::string stem(path);
    const auto extension = stem.find_last_of('.');
    if (extension != std::string::npos && extension + 6 > stem.size()) {
        stem.resize(extension);
    }
    m_path_stem = std::move(stem);
    return true;
}

/* Read one .oct file into a fresh octree.

   ReadOctFile is the recovered name. Counts are checked before allocation,
   and exact-read failures set the load error bit tested by LoadWorld. Members
   retain ownership of partial loads for the normal destructor cleanup.

   A null path builds an empty octree, and a level whose preprocessed files
   cannot be built runs on the LVL file alone with the same error bit set. */
// FUNCTION: WIZ8 0x0042bc10
W8Octree::W8Octree(const char* path, std::unique_ptr<W8GameData>* game_data)
{
    Reset();
    if (game_data != 0) {
        *game_data = 0;
    }
    try {
        if (path == 0) {
            Initialize(0);
            return;
        }
        const int asset_status = CheckLevelAssetSet(path);
        if (asset_status < 0) {
            throw std::runtime_error("Octree assets unavailable.");
        }
        auto hOctFile = wiz8::open_file(wiz8::asset_path(path));
        W8OctFileHeader header;
        hOctFile->read_exact(&header, sizeof(header));
        unsigned int uiTerminator;
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff || header.version != W8OctFileHeader::VERSION) {
            throw std::runtime_error("Invalid octree header.");
        }

        const auto remaining = static_cast<std::uint64_t>(hOctFile->size() - hOctFile->tell());
        const auto grid_yz = std::uint64_t{header.m_grid_dims.y} * header.m_grid_dims.z;
        if (header.m_grid_dims.x == 0 || header.m_grid_dims.y == 0 || header.m_grid_dims.z == 0 ||
            grid_yz > std::numeric_limits<int>::max() ||
            grid_yz >
                static_cast<unsigned int>(std::numeric_limits<int>::max()) / header.m_grid_dims.x ||
            header.m_depth >= 31 || header.m_leaf_level > 8 || header.m_prop_count > 0xffff ||
            header.m_particle_count > 0xffff ||
            header.m_path_nodes > std::numeric_limits<int>::max() ||
            header.m_surface_count > std::numeric_limits<int>::max() ||
            header.m_mesh_total > header.m_submesh_count ||
            header.m_submesh_count >= std::numeric_limits<unsigned int>::max() ||
            (header.m_gd_surface_stream_len != 0 && game_data == 0)) {
            throw std::runtime_error("Octree dimensions or counts exceed runtime limits.");
        }
        const std::size_t grid_count = grid_yz * header.m_grid_dims.x;
        const auto required =
            std::uint64_t{header.m_branch_count} * sizeof(W8OctPreTreeBranch) +
            std::uint64_t{header.m_leaf_count} * sizeof(W8OctPreTreeLeaf) +
            std::uint64_t{header.m_leaf_polygon_stream_len} * sizeof(w8_ulong) +
            (grid_count < 250000 ? grid_count * sizeof(w8_ulong) : 0) +
            std::uint64_t{header.m_polygon_count} * sizeof(w8_ulong) +
            std::uint64_t{header.m_region_list_len} * sizeof(unsigned short) +
            std::uint64_t{header.m_gd_surface_stream_len} * sizeof(w8_ulong) +
            std::uint64_t{header.m_trigger_count} * sizeof(unsigned short) +
            (header.m_region_count > 1
                 ? std::uint64_t{header.m_region_count} * sizeof(wiz8::retail::RegionVolume)
                 : 0) +
            (header.m_submesh_count != 0
                 ? (std::uint64_t{header.m_submesh_count} + 1) * sizeof(W8OctSubmesh)
                 : 0) +
            std::uint64_t{header.m_path_nodes} * sizeof(W8FilePathNode) + 3 * sizeof(uiTerminator);
        if (required > remaining) {
            throw std::runtime_error("Octree tables exceed file size.");
        }
        for (const auto count :
             {std::uint64_t{header.m_polygon_count}, std::uint64_t{header.m_surface_count},
              std::uint64_t{header.m_submesh_count} + 1}) {
            const auto whole_words = static_cast<std::uint64_t>(static_cast<float>(count) / 32.0f);
            if (whole_words * 32 > count || count - whole_words * 32 >= 32) {
                throw std::runtime_error("Octree count exceeds BitArray sizing precision.");
            }
        }
        if ((std::uint64_t{header.m_branch_count} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(W8OctPreTreeBranch) ||
            (std::uint64_t{header.m_leaf_count} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(W8OctPreTreeLeaf) ||
            (std::uint64_t{header.m_submesh_count} + 1) >
                std::numeric_limits<std::size_t>::max() / sizeof(W8OctSubmesh) ||
            (std::uint64_t{header.m_polygon_count} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_leaf_polygon_stream_len} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_gd_surface_stream_len} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_region_list_len} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(unsigned short) ||
            (std::uint64_t{header.m_trigger_count} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(unsigned short)) {
            throw std::runtime_error("Octree allocation exceeds address space.");
        }

        Initialize(&header);
        if (!SetPathStem(path)) {
            throw std::runtime_error("Could not store octree path.");
        }
        m_branches.resize((std::size_t{header.m_branch_count} + 2));
        m_leaves.resize((std::size_t{header.m_leaf_count} + 2));
        m_polygon_index_stream.resize((std::size_t{header.m_leaf_polygon_stream_len} + 2));
        m_aulPolyLookup.resize((std::size_t{header.m_polygon_count} + 2));
        hOctFile->read_exact(m_branches.data(),
                             std::size_t{header.m_branch_count} * sizeof(W8OctPreTreeBranch));
        hOctFile->read_exact(m_leaves.data(),
                             std::size_t{header.m_leaf_count} * sizeof(W8OctPreTreeLeaf));
        hOctFile->read_exact(m_polygon_index_stream.data(),
                             std::size_t{header.m_leaf_polygon_stream_len} * sizeof(w8_ulong));
        if (grid_count < 250000) {
            m_leaf_lookup.resize(grid_count);
            hOctFile->read_exact(m_leaf_lookup.data(), grid_count * sizeof(w8_ulong));
        }
        hOctFile->read_exact(m_aulPolyLookup.data(),
                             std::size_t{header.m_polygon_count} * sizeof(w8_ulong));
        if (header.m_region_list_len != 0) {
            m_region_index_stream.resize((std::size_t{header.m_region_list_len} + 2));
            hOctFile->read_exact(m_region_index_stream.data(),
                                 std::size_t{header.m_region_list_len} * sizeof(unsigned short));
        }
        if (header.m_gd_surface_stream_len != 0) {
            m_gd_surface_index_stream.resize((std::size_t{header.m_gd_surface_stream_len} + 2));
            hOctFile->read_exact(m_gd_surface_index_stream.data(),
                                 std::size_t{header.m_gd_surface_stream_len} * sizeof(w8_ulong));
        }
        if (header.m_trigger_count != 0) {
            m_trigger_indices.resize((std::size_t{header.m_trigger_count} + 2));
            hOctFile->read_exact(m_trigger_indices.data(),
                                 std::size_t{header.m_trigger_count} * sizeof(unsigned short));
        }
        if (header.m_region_count > 1) {
            m_region_volumes.resize(std::size_t{header.m_region_count} + 2);
            m_spatial.m_region_volumes = m_region_volumes.data();
            for (unsigned int i = 0; i < header.m_region_count; ++i)
                wiz8::retail::read_exact(*hOctFile, m_region_volumes[i]);
        }
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff) {
            throw std::runtime_error("Invalid octree region terminator.");
        }
        if (header.m_submesh_count != 0) {
            m_pSubmeshes.resize((std::size_t{header.m_submesh_count} + 1));
            hOctFile->read_exact(m_pSubmeshes.data(),
                                 (std::size_t{header.m_submesh_count} + 1) * sizeof(W8OctSubmesh));
            unsigned int limit = 0;
            for (std::size_t index = 0; index <= header.m_submesh_count; ++index) {
                limit = std::max(limit, m_pSubmeshes[index].polygon_count);
            }
            if (limit >= 9999) {
                throw std::runtime_error("Octree submesh polygon count exceeds scratch capacity.");
            }
            if (m_meshCount != 0) {
                if (!LoadPackedBits(*hOctFile, m_alpha_bits)) {
                    throw std::runtime_error("Could not read octree alpha bits.");
                }
            }
            if (m_ulNumParticles != 0) m_particle_links.read(*hOctFile, m_meshCount);
            if (m_ulNumProps != 0) m_prop_links.read(*hOctFile, m_meshCount);
        }
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff) {
            throw std::runtime_error("Invalid octree mesh terminator.");
        }
        if (header.m_path_nodes != 0) {
            pathing = new W8PathingService;
            pathing->ConfigureForLevel(header.m_path_nodes, header.m_region_cell,
                                       m_spatial.bounds(), m_path_stem.c_str());
            if (!pathing->ReadPathNodes(hOctFile.get())) {
                throw std::runtime_error("Could not read octree path nodes.");
            }
        }
        if (m_ulNumProps != 0 && header.m_prop_sun_bits != 0) {
            if (!LoadPackedBits(*hOctFile, m_sunlit_props)) {
                throw std::runtime_error("Could not read octree prop sun bits.");
            }
        }
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff) {
            throw std::runtime_error("Invalid octree terminator.");
        }
        std::unique_ptr<W8GameData> pGameData;
        if (header.m_gd_surface_stream_len != 0) {
            pGameData = std::make_unique<W8GameData>(hOctFile.get(), false);
            hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
            if (uiTerminator != 0xffffffff) {
                throw std::runtime_error("Invalid octree GameData terminator.");
            }
            pGameData->octree = this;
        }
        SetOctreeGameData(pGameData.get());
        if (game_data != 0) {
            *game_data = std::move(pGameData);
        }
        g_octree = this;
        m_region_links_ready = ReadRegionLinkFile(m_path_stem.c_str());
        LoadPointFiles(m_path_stem.c_str());
        if (pathing != 0) {
            pathing->ReadWaypointFile();
        }
        return;
    } catch (const std::exception&) {
        SetOctreeGameData(0);
        g_octree = 0;
        g_pathing = 0;
        m_spatial.flags |= 0x80000000;
    }
}

// Installed assets are already prepared. Missing OCT/PVL falls back to LVL.
int CheckLevelAssetSet(const char* level_path)
{
    if (std::string_view(level_path).find("Sky") != std::string_view::npos ||
        std::string_view(level_path).find("sky") != std::string_view::npos)
        return -1;
    auto pvl = wiz8::asset_path(level_path);
    pvl.replace_extension(".pvl");
    return wiz8::file_status(wiz8::asset_path(level_path)) && wiz8::file_status(pvl) ? 0 : -1;
}

/* Reset the owned tables and query state before applying an OCT header.
   Retail leaves the region masks and positional flag untouched here. */
// FUNCTION: WIZ8 0x0042d040
void W8Octree::Reset()
{
    g_octree_state = 0;
    m_spatial.Reset();
    m_region_volumes.clear();
    m_branches.clear();
    m_leaves.clear();
    m_leaf_grid_dimensions.z = 0;
    m_leaf_grid_dimensions.y = 0;
    m_leaf_grid_dimensions.x = 0;
    m_leaf_lookup.clear();
    m_branch_count = 0;
    m_leaf_count = 0;
    m_vertex_count = 0;
    m_leaf_polygon_stream_len = 0;
    m_gd_surface_stream_len = 0;
    m_trigger_count = 0;
    m_region_list_len = 0;
    m_polygon_index_stream.clear();
    m_aulPolyLookup.clear();
    m_gd_surface_index_stream.clear();
    m_region_index_stream.clear();
    m_trigger_indices.clear();
    m_pSubmeshes.clear();
    object_registry = 0;
    m_pRegionLinks = 0;
    m_sample_points.clear();
    m_path_stem.clear();
    m_region_cell = 0;
    m_path_clearance = 0;
    pathing = 0;
    m_fAccumulating = false;
    m_particle_links = {};
    m_prop_links = {};
    m_ulNumParticles = 0;
    m_props.clear();
    m_particles.clear();
    m_trace_skip_flag = 0;
    m_root_mesh_count = 0;
    m_kind1_submesh_count = 0;
    m_alpha_polygon_count = 0;
    m_meshCount = 0;
    m_gd_result_count = 0;
    m_aulGDObjs.fill(0);

    view = {};
    m_visibility_suspended = false;
    m_reset_visibility = false;
    m_projected_regions_valid = false;
    m_region_links_ready = false;
    m_region_links_dirty = false;
    m_points_dirty = false;
    m_sun_count = 0;
    prop_sun_base = 0;
    m_ulNumProps = 0;
    current_prop = -1;
}

/* ReadOctFile calls this after Reset. The 0xf5-byte packed record has one
   canonical source model shared with WriteOctFile. */
// FUNCTION: WIZ8 0x0042d2a0
void W8Octree::Initialize(const W8OctFileHeader* header)
{
    unsigned short level;

    if (header != 0) {
        m_spatial.m_extent = header->m_extent;
        m_spatial.m_cell_size = header->m_cell_size;
        m_spatial.m_node_extent = header->m_node_extent;

        m_spatial.m_bounds.minimum = {header->m_bounds[0].x, header->m_bounds[0].y,
                                      header->m_bounds[0].z};
        m_spatial.m_bounds.maximum = {header->m_bounds[1].x, header->m_bounds[1].y,
                                      header->m_bounds[1].z};
        m_spatial.m_clipped_bounds.minimum = {header->m_bounds[2].x, header->m_bounds[2].y,
                                              header->m_bounds[2].z};
        m_spatial.m_clipped_bounds.maximum = {header->m_bounds[3].x, header->m_bounds[3].y,
                                              header->m_bounds[3].z};
        m_spatial.m_working_bounds.minimum = {header->m_bounds[4].x, header->m_bounds[4].y,
                                              header->m_bounds[4].z};
        m_spatial.m_working_bounds.maximum = {header->m_bounds[5].x, header->m_bounds[5].y,
                                              header->m_bounds[5].z};
        m_leaf_grid_dimensions = {header->m_grid_dims.x, header->m_grid_dims.y,
                                  header->m_grid_dims.z};

        m_spatial.m_leaf_grid_stride_x = m_leaf_grid_dimensions.y * m_leaf_grid_dimensions.z;
        m_spatial.m_leaf_grid_stride_y = m_leaf_grid_dimensions.z;
        m_spatial.m_depth = header->m_depth;
        m_spatial.m_region_id_bound = header->m_region_id_bound;
        m_spatial.m_region_count = header->m_region_count;
        m_spatial.m_leaf_level = header->m_leaf_level;
        m_spatial.submesh_count = header->m_submesh_count;
        m_root_mesh_count = header->m_root_mesh_count;
        m_kind1_submesh_count = header->m_kind1_submesh_count;
        m_meshCount = header->m_mesh_total;
        m_region_mask = 0;
        m_spatial.m_region_cells_per_axis = 1;
        for (level = 0; level < m_spatial.m_leaf_level; ++level) {
            m_region_mask = m_region_mask * 2 + 1;
            m_spatial.m_region_cells_per_axis <<= 1;
        }
        m_depth_mask = 0;
        for (level = 0; level < m_spatial.m_depth; ++level) {
            m_depth_mask = m_depth_mask * 2 + 1;
        }
        m_branch_count = header->m_branch_count;
        m_leaf_count = header->m_leaf_count;
        m_spatial.m_polygon_count = header->m_polygon_count;
        m_vertex_count = header->m_vertex_count;
        m_spatial.m_item_count = header->m_surface_count;
        m_leaf_polygon_stream_len = header->m_leaf_polygon_stream_len;
        m_gd_surface_stream_len = header->m_gd_surface_stream_len;
        m_trigger_count = header->m_trigger_count;
        m_region_list_len = header->m_region_list_len;
        m_spatial.m_region_grid_cell = header->m_region_grid_cell;
        m_region_cell = header->m_region_cell;
        m_path_clearance = header->m_path_clearance;
        m_spatial.m_max_region_radius = header->m_max_region_radius;
        m_ulNumProps = header->m_prop_count;
        m_particle_links.records.resize(header->m_particle_len);
        m_prop_links.records.resize(header->m_prop_len);
        m_ulNumParticles = header->m_particle_count;

        if (m_ulNumParticles != 0) {
            m_linked_particles.assign(m_ulNumParticles, false);
            m_visible_particles.assign(m_ulNumParticles, false);
            m_particles.reserve(m_ulNumParticles);
        }
        if (m_ulNumProps != 0) {
            m_linked_props.assign(m_ulNumProps, false);
            m_visible_props.assign(m_ulNumProps, false);
            m_props.reserve(m_ulNumProps);
        }

        m_visited_polygon_bits.assign(header->m_polygon_count, false);
        m_projected_regions.assign(header->m_submesh_count + 1, false);
        m_current_regions.assign(header->m_submesh_count + 1, false);
        m_previous_regions.assign(header->m_submesh_count + 1, false);
        m_visited_object_bits.assign(
            header->m_surface_count < 5000 ? 5000 : header->m_surface_count, false);
        m_accumulated_regions.assign(header->m_submesh_count + 1, false);

        object_registry = new W8OctreeObjectRegistry;
        m_pRegionLinks = new std::unordered_multimap<unsigned int, unsigned short>;
        m_pRegionLinks->clear();

        m_reset_visibility = true;
    }

    m_spatial.m_level_kind = 3;
    m_fAccumulating = true;
}

void W8Octree::AddLoadedProp(W8Prop* prop)
{
    if (!m_fAccumulating) {
        return;
    }
    if (m_props.size() >= m_ulNumProps) {
        throw std::runtime_error("Too many props loaded for octree.");
    }
    m_props.push_back(prop);
}

void W8Octree::AddLoadedParticles(std::span<stParticle* const> particles)
{
    if (!m_fAccumulating) {
        return;
    }
    if (m_particles.size() > m_ulNumParticles ||
        particles.size() > m_ulNumParticles - m_particles.size()) {
        throw std::runtime_error("Too many particles loaded for octree.");
    }
    m_particles.insert(m_particles.end(), particles.begin(), particles.end());
}

/* Complete non-deleting teardown. LoadWorld and DestroyWorld both perform the
   matching operator delete only after this method returns. */
// FUNCTION: WIZ8 0x0042de60
W8Octree::~W8Octree()
{
    if (m_pRegionLinks != 0) {
        m_pRegionLinks->clear();
        delete m_pRegionLinks;
        m_pRegionLinks = 0;
    }

    m_particle_links = {};
    m_prop_links = {};

    delete object_registry;

    delete pathing;
    pathing = 0;
    g_octree = 0;
    ReleaseOctMeshDefaults();
}

/* Running index into the prop-sunlight bit stream, shared across the octree
   so consecutive levels pick up where the previous one's props ended. */

// GLOBAL: WIZ8 0x006598ac
int g_prop_sun_index;

/* Attach the prop-sunlight bit array, but only one that has been sized. */
// FUNCTION: WIZ8 0x0042e3e0
void W8Octree::SetPropSunBits(std::vector<bool> bits)
{
    if (!bits.empty()) {
        m_sunlit_props = std::move(bits);
    }
}

/* Test whether prop `offset` past the octree's base index has its sunlight
   bit set; the tested index also becomes the shared running index. A
   negative offset instead checkpoints the shared index into the base, which
   is how ReadWorldProps starts each level's props on a fresh range. With no
   array attached the answer is always no. */
// FUNCTION: WIZ8 0x0042e400
bool W8Octree::TestPropSunBit(int offset)
{
    if (offset < 0) {
        prop_sun_base = g_prop_sun_index;
        return false;
    }
    g_prop_sun_index = prop_sun_base + offset;
    if (TestBit(m_sunlit_props, g_prop_sun_index)) {
        return true;
    }
    return false;
}

/* Visit a point handed over by address, copied to the stack first so the
   caller's copy is not the one the traversal holds. */
// FUNCTION: WIZ8 0x0042e620
void W8Octree::VisitPointCopy(unsigned short location_id, glm::vec3* position)
{
    glm::vec3 copy;

    copy = *position;
    UpdateMonsterLocation(location_id, &copy);
}

/* Remove every kind-12 and kind-13 registry pairing owned by one location
   id; each by_object hit also drops the mirrored by_cell entry.
   MonsterManager/NPC Manager drive this during location teardown.
   Retail verified at 0x0042E650/0x00438D50: the just-removed slot is passed
   back as the FindNextEntry cursor even though RemoveAt has pushed that
   entry onto the free list; the walk then follows the overwritten
   next_index, which is the free head (-1 in practice), ending the loop. */
// FUNCTION: WIZ8 0x0042e650
void W8Octree::UnregisterLocationObjects(unsigned int location_id)
{
    UnregisterLocationObject(location_id, W8_OCTREE_KIND_LOCATION);
    UnregisterLocationObject(location_id, W8_OCTREE_KIND_NAVIGATOR);
}

/* Remove the registered pairings for one (location, kind) object key — used
   by Navigator teardown with kind 0xd.  The removed-slot cursor reuse is
   retail verified (the same sequence is inlined at 0x0042E880): FindNextEntry
   follows entries[slot].next_index after RemoveAt has overwritten it with
   the free-list head. */
// FUNCTION: WIZ8 0x0042e880
void W8Octree::UnregisterLocationObject(unsigned int location_id, W8OctreeObjectKind kind)
{
    object_registry->UnregisterObject(kind, location_id + 1);
}

/* Query the expanded segment box and own its ordered, deduplicated IDs. */
// FUNCTION: WIZ8 0x0042ed60
std::vector<w8_ulong> W8Octree::queryAlongSegment(const glm::vec3& origin, const glm::vec3& delta,
                                                  float extent, unsigned short kind)
{
    const auto bounds = srSegmentMath::sweptBounds(origin, delta, extent);
    const auto lower = cellCoordinates(bounds.minimum).cell;
    const auto upper = cellCoordinates(bounds.maximum).cell;
    g_octree_state = m_aulGDObjs.data();
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    visitClippedOctreeCellBox(lower, upper, glm::ivec3(m_leaf_grid_dimensions),
                              [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind); });
    g_octree_state = nullptr;
    return {m_aulGDObjs.data(), m_aulGDObjs.data() + m_gd_result_count};
}

/* The kind-12 convenience query: a zero exclusion maps to -1 (none). OctPath
   uses it to list the location ids near a mover. */

// FUNCTION: WIZ8 0x0042ef00
std::vector<w8_ulong> W8Octree::locationsInBox(const srBoundingBox& bounds,
                                               unsigned short exclusion)
{
    return queryObjects(bounds, W8_OCTREE_KIND_LOCATION, exclusion == 0 ? -1 : exclusion);
}

/* AABB occupancy test: GD triangles, kind-12 location objects (each
   monster's navigator radius inflates the box) and collidable-prop surfaces
   all answer "occupied". */
// FUNCTION: WIZ8 0x0042ef30
unsigned char W8Octree::TestBoxOccupied(const glm::vec3* lower, const glm::vec3* upper)
{
    auto objects = queryObjects({(*lower), (*upper)}, W8_OCTREE_KIND_SURFACE, -1);
    unsigned int count = objects.size();
    unsigned int index;
    for (index = 0; index < count; ++index) {
        W8GDSurface* surface = g_octree_game_data->m_pSurfaces.data() + objects[index];
        if (SurfaceIntersectsBox(*surface, g_octree_game_data->m_pVertices, {(*lower), (*upper)})) {
            return 1;
        }
    }
    objects = queryObjects({(*lower), (*upper)}, W8_OCTREE_KIND_LOCATION, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        W8MonsterInfo* info = FindMonster(objects[index]);
        if (info != 0 && info->p3D != 0) {
            glm::vec3 position = info->p3D->GetPosition();
            float radius = info->p3D->radius;
            if (lower->x - radius < position.x && position.x < radius + upper->x &&
                lower->y - radius < position.y && position.y < radius + upper->y &&
                lower->z - radius < position.z && position.z < radius + upper->z) {
                return 1;
            }
        }
    }
    objects = queryObjects({(*lower), (*upper)}, W8_OCTREE_KIND_PROP, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        W8Prop* prop = g_world->collidable_props[objects[index]];
        if (prop->GetActivationState() != 0 && prop->m_gd_prop != 0) {
            GDProp* gd_prop = prop->m_gd_prop.get();
            for (int surface_index = 0;
                 surface_index < static_cast<int>(gd_prop->m_pGDSurfaces.size()); ++surface_index) {
                W8GDSurface* surface = gd_prop->m_pGDSurfaces.data() + surface_index;
                if (SurfaceIntersectsBox(*surface, gd_prop->m_pVertices, {(*lower), (*upper)})) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* Query the inclusive cell box and copy the deduplicated IDs out of the
   scratch buffer before any subsequent query can overwrite them. */
// FUNCTION: WIZ8 0x0042f280
std::vector<w8_ulong> W8Octree::queryObjects(const srBoundingBox& bounds, unsigned short kind,
                                             int excluded)
{
    const auto start = cellAt(bounds.minimum);
    const auto end = cellAt(bounds.maximum);
    g_octree_state = m_aulGDObjs.data();
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    if (excluded >= 0) SetBit(m_visited_object_bits, excluded);
    visitClippedOctreeCellBox(start, end, glm::ivec3(m_leaf_grid_dimensions),
                              [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind); });
    g_octree_state = nullptr;
    return {m_aulGDObjs.data(), m_aulGDObjs.data() + m_gd_result_count};
}

/* Append the objects of `kind` inside one cell to the shared query buffer.
   Kind 3 walks the static tree (or the direct leaf grid when one exists) to a
   leaf whose gd-polygon stream lists surface ids. Kinds 8, 9 and 12 query the
   object registry's cell hash; kinds above 12 accept both the 12 and 13
   halfwords packed into each registry value. Every append deduplicates
   through m_visited_object_bits and the buffer stops at 10000 entries. */
// FUNCTION: WIZ8 0x0042f400
unsigned int W8Octree::CollectObjectsInCell(const glm::ivec3& cell, unsigned short kind)
{
    unsigned int found = 0;
    unsigned int leaf_index;
    W8OctreeIndex::iterator slot;

    switch (kind) {
    case W8_OCTREE_KIND_SURFACE:
        if (m_leaf_lookup.empty()) {
            leaf_index = FindLeaf(cell);
        } else {
            leaf_index = m_leaf_lookup[m_spatial.m_leaf_grid_stride_y * cell.y +
                                       m_spatial.m_leaf_grid_stride_x * cell.x + cell.z];
        }
        if (leaf_index != 0 && m_leaves[leaf_index].gd_polygon_offset != 0) {
            const w8_ulong* stream =
                m_gd_surface_index_stream.data() + m_leaves[leaf_index].gd_polygon_offset;
            found = *stream;
            for (unsigned int index = 0; index < found; ++index) {
                if (9999 < m_gd_result_count) {
                    return found;
                }
                ++stream;
                if (!SetBit(m_visited_object_bits, *stream)) {
                    g_octree_state[m_gd_result_count] = *stream;
                    ++m_gd_result_count;
                }
            }
        }
        break;
    default:
        g_octree_query_cell = PackOctreeCellKey(cell.x, cell.y, cell.z);
        g_octree_query_slot = object_registry->by_cell.end();
        slot = NextIndexValue(object_registry->by_cell, g_octree_query_cell, g_octree_query_slot);
        g_octree_query_slot = slot;
        while (slot != object_registry->by_cell.end() && m_gd_result_count < 10000) {
            unsigned int packed = static_cast<unsigned int>(slot->second);
            g_octree_query_id = static_cast<unsigned short>(packed);
            const unsigned short entry_kind = OctreeKeyKind(packed);
            const bool matches = kind > W8_OCTREE_KIND_LOCATION
                                     ? (entry_kind == W8_OCTREE_KIND_LOCATION ||
                                        entry_kind == W8_OCTREE_KIND_NAVIGATOR)
                                     : entry_kind == kind;
            if (matches && !SetBit(m_visited_object_bits, OctreeKeyId(packed) - 1)) {
                g_octree_state[m_gd_result_count] = g_octree_query_id - 1;
                ++m_gd_result_count;
            }
            slot =
                NextIndexValue(object_registry->by_cell, g_octree_query_cell, g_octree_query_slot);
            g_octree_query_slot = slot;
        }
        break;
    }
    return found;
}

/* Register the navigator's world position in the spatial cell registry.
   Navigator IDs are stored with a one-based bias. */
// FUNCTION: WIZ8 0x0042e810
void W8Octree::RegisterNavigatorCell(int id, const glm::vec3* position)
{
    glm::ivec3 point;

    point = cellAt((*position));
    object_registry->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, id + 1, point);
}

/* Quantize the world point relative to the spatial minimum. This preserves
   truncation toward zero, including points below the minimum. */
// FUNCTION: WIZ8 0x00431440
glm::ivec3 W8Octree::cellAt(const glm::vec3& position) const
{
    return glm::ivec3((position - m_spatial.m_bounds.minimum) / m_spatial.m_node_extent);
}

/* Step one navigator's movement tail towards its target.

   With a pathing service present the work belongs to that service, and which of
   its two entry points runs is the link mode's decision. Without one this walks
   the target itself. The direction is the full vector to the target with its
   height component dropped, so the step stays in the horizontal plane; its
   length is clamped to the distance remaining, and reaching the target is what
   the return value reports. The new position is mirrored into the attachment's
   own three copies. */
// FUNCTION: WIZ8 0x00434620
unsigned int W8Octree::AdvanceNavigator(W8NavigatorMovementState* movement, float radius,
                                        float separation)
{
    glm::vec3 vecDir;
    glm::vec3 vecPos;
    W8NavigatorAttachment* attachment;
    float distance;
    float step;
    bool reached = true;

    if (pathing != 0) {
        if (g_navigator_link_mode == 0) {
            return pathing->StepAlongPath(movement, radius, separation);
        }
        return pathing->StepMonsterAlongPath(movement, radius, separation);
    }
    if (g_navigator_link_mode != 0) {
        return 1;
    }
    vecDir = movement->target_position - movement->position;
    vecDir.y = 0.0f;
    distance = glm::length(((glm::vec2(vecDir.x, vecDir.z))));
    step =
        g_game_time_accumulator->GetFrameDelta() * movement->movement_scale * g_rate * kWorldScale;
    if (step >= distance) {
        step = distance;
    } else {
        reached = false;
    }
    if ((vecDir.x * vecDir.x + vecDir.z * vecDir.z) != 0.0) {
        vecDir = glm::dot(vecDir, vecDir) == 0 ? vecDir
                                               : glm::normalize(vecDir) * static_cast<float>(step);
    }
    vecPos = vecDir + movement->position;
    movement->position = vecPos;
    attachment = movement->attachment.get();
    attachment->path[0].position = vecPos;
    attachment->path_length_origin = attachment->path[0].position;
    attachment->segment_start = attachment->path[0].position;
    return reached;
}

/* Settle both ends of a portal transition onto the ground before the move is
   applied. Each end is capped at the octree's height ceiling first, and its
   settled height is only taken when the drop actually hit something. The pair
   is then handed on together, which is why one body carries both. */
// FUNCTION: WIZ8 0x00434a30

/* Release the location-variable names and empty their parallel value and
   level vectors. Trigger.cpp creates the names as copied character arrays. */

std::vector<w8_ulong> W8Octree::nearbyLocations(const glm::vec3& position, float spacing)
{
    const float expand = spacing * kMonsterProximityRadiusScale;
    return queryObjects({position - expand, position + expand}, W8_OCTREE_KIND_LOCATION, -1);
}

/* Lateral column offsets for the scatter search below: the inner loop walks
   three columns (0, +1, -1) per ring, or all five when more than ten
   positions are wanted. */
// GLOBAL: WIZ8 0x00605ae8
static float g_scatter_column_offsets[5] = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f};

/* Ring-scatter search for up to `count` clear positions around `position`.
   Each ring steps `spacing` units out along the `yaw`-rotated frame and
   columns offset laterally through g_scatter_column_offsets; the
   first accepted candidate becomes the reference the remaining candidates
   must span to. With `proximity_check` set each candidate is also rejected
   inside the startup navigator's or any nearby monster's radius. `flatten_y`
   relaxes the tight vertical bound during search and rewrites every accepted
   y to the original source height afterwards. */
static bool OverlapsNearbyMonster(const glm::vec3& point, float reach,
                                  std::span<const w8_ulong> locations, bool alternate_radius)
{
    for (const auto id : locations) {
        auto* monster = GetMonsterByLocationID(id);
        const auto delta = monster->GetPosition() - point;
        const float clearance =
            (alternate_radius ? monster->movement.alternate_radius : monster->radius) + reach;
        if (glm::dot(delta, delta) < clearance * clearance) return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x00437980
std::vector<glm::vec3> W8Octree::scatterPositions(const glm::vec3& position, float yaw,
                                                  float spacing, unsigned int count,
                                                  bool proximity_check, bool flatten_y)
{
    float source_y = position.y;
    std::vector<glm::vec3> positions;
    unsigned int found = 0;
    std::vector<w8_ulong> candidates;
    unsigned int columns = 3;
    if (count > 10) {
        columns = 5;
    }
    float angle = NormalizeAngle(yaw + g_monster_rotation_offset);
    const wiz8::placement::Grid grid(angle, spacing);
    glm::vec3 source = position;
    if (proximity_check) {
        candidates = nearbyLocations(position, spacing);
    }
    unsigned int ring;
    for (ring = 0; ring < 10; ++ring) {
        if (count <= found) {
            break;
        }
        if (columns != 0) {
            const float* column_offset = g_scatter_column_offsets;
            unsigned int column = 0;
            do {
                if (count <= found) {
                    break;
                }
                float jitter_ring;
                float jitter_column;
                if (found == 0) {
                    jitter_ring = 0.0f;
                    jitter_column = 0.0f;
                } else {
                    jitter_ring =
                        Random(1000) * kScatterOuterRingJitterScale - g_generator_jitter_fraction;
                    jitter_column =
                        Random(1000) * kScatterOuterRingJitterScale - g_generator_jitter_fraction;
                }
                const auto probe =
                    grid.candidate(source, {ring + jitter_ring, jitter_column + *column_offset});
                if (const auto settled = wiz8::placement::grounded(
                        probe, probeGround(probe).position.y, flatten_y)) {
                    glm::vec3 candidate = *settled;
                    if (!proximity_check) {
                        if (pathing->SnapWaypointPosition(&candidate, false)) {
                            goto accept;
                        }
                    } else {
                        float reach = spacing * 0.5f;
                        if (pathing->TestPathCellClearance(&candidate, reach, false)) {
                            glm::vec3 navigator = g_startup_world->GetPosition();
                            const auto delta = navigator - candidate;
                            float separation = g_startup_world->radius + reach;
                            if (separation * separation <= glm::dot(delta, delta)) {
                                if (OverlapsNearbyMonster(candidate, reach, candidates, false))
                                    goto next_cell;
                                goto accept;
                            }
                        }
                    }
                    goto next_cell;
                accept:
                    if (found == 0) {
                        positions.push_back(candidate);
                        source = candidate;
                        found = 1;
                    } else if (pathing == 0 ||
                               pathing->TestWaypointSpan(&candidate, &source, false, false)) {
                        positions.push_back(candidate);
                        ++found;
                    }
                }
            next_cell:
                ++column;
                ++column_offset;
            } while (column < columns);
        }
    }
    if (flatten_y && found != 0) {
        for (unsigned int index = 0; index < found; ++index) {
            positions[index].y = source_y;
        }
    }
    return positions;
}

// FUNCTION: WIZ8 0x00437f30
NavigatorPlacement W8Octree::navigatorPositions(const glm::vec3& input, float yaw, float radius,
                                                unsigned int count, bool first_only,
                                                bool settle_any_height, bool avoid_triggers,
                                                int mode, bool require_waypoint_span)
{
    unsigned int found = 0;
    glm::vec3 source = input;
    std::vector<glm::vec3> positions;
    int found_i = 9999;
    int found_j = 9999;
    float source_y = source.y;
    std::vector<w8_ulong> candidates;
    float camera_radius = g_startup_world->movement.alternate_radius;
    bool placed = false;
    float separation = (CalcRangeDistance(W8_RANGE_TOUCH) + radius) * 0.5f + camera_radius;
    float angle = NormalizeAngle(yaw + g_monster_rotation_offset);
    const wiz8::placement::Grid grid(angle, radius);
    source.y += kWorldScale;
    if (const auto settled =
            wiz8::placement::grounded(source, probeGround(source).position.y, settle_any_height))
        source = *settled;
    if (first_only) {
        candidates = nearbyLocations(source, radius);
    }
    int ring_upper = 0;
    if (-1 < mode) {
        int lower = 0;
        do {
            if (count <= found) {
                break;
            }
            int i = lower;
            int upper = ring_upper;
            if (lower <= ring_upper) {
                do {
                    if (count <= found) {
                        break;
                    }
                    int j = lower;
                    if (placed) {
                        goto advance_ring;
                    }
                    do {
                        if (count <= found || placed) {
                            break;
                        }
                        if ((i <= lower || upper <= i || j <= lower || upper <= j) &&
                            (i != found_i || j != found_j)) {
                            float jitter_i;
                            float jitter_j;
                            if (found == 0) {
                                jitter_i = 0.0f;
                                jitter_j = 0.0f;
                            } else {
                                jitter_i = Random(1000) * kScatterInnerRingJitterScale - 0.1f;
                                jitter_j = Random(1000) * kScatterInnerRingJitterScale - 0.1f;
                            }
                            const auto probe = grid.candidate(source, {i + jitter_i, jitter_j + j});
                            if (const auto settled = wiz8::placement::grounded(
                                    probe, probeGround(probe).position.y, settle_any_height)) {
                                glm::vec3 candidate = *settled;
                                unsigned char clear;
                                if (!first_only) {
                                    clear = pathing->SnapWaypointPosition(&candidate, false);
                                } else {
                                    clear = pathing->TestPathCellClearance(&candidate,
                                                                           radius * 0.5f, false);
                                }
                                if (clear != 0 && (!avoid_triggers ||
                                                   !InsideDestinationTrigger(
                                                       candidate.x, candidate.y, candidate.z))) {
                                    if (first_only) {
                                        glm::vec3 camera = g_startup_world->GetPosition();
                                        const auto delta = camera - candidate;
                                        if (glm::dot(delta, delta) < separation * separation) {
                                            goto next_cell;
                                        }
                                        if (OverlapsNearbyMonster(candidate, radius * 0.5f,
                                                                  candidates, true))
                                            goto next_cell;
                                    }
                                    if (!require_waypoint_span || pathing == 0 ||
                                        pathing->TestWaypointSpan(&candidate, &source, false,
                                                                  false)) {
                                        if (found == 0) {
                                            placed = true;
                                            found_i = i;
                                            found_j = j;
                                        }
                                        positions.push_back(candidate);
                                        ++found;
                                    } else if (found != 0) {
                                        for (auto& point : positions) {
                                            glm::vec3 existing = point;
                                            const bool reachable = pathing->TestWaypointSpan(
                                                &candidate, &existing, false, false);
                                            point = existing;
                                            if (reachable) {
                                                positions.push_back(candidate);
                                                ++found;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    next_cell:
                        ++j;
                    } while (j <= ring_upper);
                    ++i;
                } while (i <= upper);
            }
            if (placed) {
            advance_ring:
                --upper;
                ++lower;
                placed = false;
            }
            ring_upper = upper + 1;
            --lower;
        } while (ring_upper <= mode);
    }
    if (settle_any_height)
        for (auto& point : positions) point.y = source_y;
    return {source, std::move(positions)};
}

void OctreeMeshLinks::read(wiz8::File& file, std::size_t mesh_count)
{
    lookup.resize(mesh_count + 1);
    file.read_exact(lookup.data(), lookup.size() * sizeof(unsigned short));
    file.read_exact(records.data(), records.size() * sizeof(unsigned short));
}

void OctreeMeshLinks::write(wiz8::File& file, std::size_t mesh_count) const
{
    file.write(lookup.data(), (mesh_count + 1) * sizeof(unsigned short));
    file.write(records.data(), records.size() * sizeof(unsigned short));
}

W8OctSpatialState::W8OctSpatialState(const W8OctSpatialState* source)
{
    m_level_kind = 1;
    if (source != 0) {
        m_bounds = source->m_bounds;
        m_clipped_bounds = source->m_clipped_bounds;
        if (source->m_level_kind == 1) {
            m_extent = source->m_extent * 0.5f;
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

void W8OctSpatialState::Reset()
{
    *this = W8OctSpatialState{};
    m_level_kind = 0;
}

srBoundingBox W8OctSpatialState::GetClippedBounds() const
{
    return m_clipped_bounds;
}

bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const glm::vec3> vertices,
                          const srBoundingBox& box)
{
    return triangleIntersectsBox(box, indexedTriangle(vertices, surface.vertex_indices),
                                 surface.plane.normal);
}

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
        glm::greaterThan(glm::abs(normal), glm::vec3(kRegionAxisAlignmentThreshold)) &&
        glm::greaterThanEqual(vertices[0], minimum) && glm::lessThanEqual(vertices[0], maximum));
    if (!nearAxis) {
        const auto centroid = (vertices[0] + vertices[1] + vertices[2]) * 0.33333334f;
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
                if (static_cast<float>(0.0001) <
                    static_cast<float>(fabs(edge_delta[edge][face_axis]))) {
                    float amount = (face - vertices[edge][face_axis]) / edge_delta[edge][face_axis];
                    if (0.0f <= amount && amount <= 1.0f) {
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
                    if (0.0001f < static_cast<float>(fabs(delta[coordinate]))) {
                        short other = coordinate == 0 ? 1 : 0;
                        for (short edge_side = 0; edge_side < 2; ++edge_side) {
                            float boundary = edge_side == 0 ? minimum[rectangle_axes[coordinate]]
                                                            : maximum[rectangle_axes[coordinate]];
                            float amount =
                                (boundary - intersection[0][coordinate]) / delta[coordinate];
                            if (0.0f <= amount && amount <= 1.0f) {
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
