#include "wiz8/octree_walk.h"
#include <fmt/format.h>
#include <SDL3/SDL_filesystem.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <span>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <exception>
#include <new>
#include <optional>
#include <utility>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/vector_relational.hpp"
#include "surrender/srBoundingBox.h"
#include "surrender/segment_math.h"
#include "surrender/swept_bounds.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "surrender/srCamera.h"
#include "surrender/srMath.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/float_constants.h"
#include "wiz8/geometry.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/startup_world.h"
#include "wiz8/sr_api.h"
#include "wiz8/filesystem.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/local_code/CombatRange.h"
#include "random.h"
#include "wiz8/fonts.h"
#include "wiz8/monster_generators.h"
#include "wiz8/utility.h"
#include "wiz8/world_cursor.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/packed_bits.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/local_screens/MainGameScreen.h"
#include "input.h"
#include "surrender/srModelInstance.h"
#include "OctPreTree.h"
#include "compat/ptr32.h"
#include "file_time.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "index_algorithms.h"
#include "layouts/gameplay_databases.h"
#include "layouts/world.h"
#include "surrender/srModel.h"
#include "surrender/srNode.h"

#define OCTREE_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp"

// FUNCTION: WIZ8 0x00433a70
void W8Octree::GetPathSurfaceNormal(const srVector3T<float>* position, srVector3T<float>* normal)
{
    if (pathing != 0) {
        pathing->GetPathSurfaceNormal(position, normal);
        return;
    }
    normal->Set(0.0f, 1.0f, 0.0f);
}

// FUNCTION: WIZ8 0x00434170
void W8Octree::UpdatePathVisualization()
{
    if (pathing != 0) {
        srVector3T<float> cursor;
        GetWorldCursorPosition(&cursor);
        /* Retail reads the y component twice (proven in disassembly: both
           fcomp loads use the y slot) and never tests x. */
        if (cursor.y != g_float_zero || cursor.y != g_float_zero || cursor.z != g_float_zero) {
            srVector3T<float> point = cursor;
            pathing->UpdatePathVisualization(&point, &view.camera_dof);
            return;
        }
        pathing->UpdatePathVisualization(&view.camera_location, &view.camera_dof);
    }
}

// GLOBAL: WIZ8 0x00659760
static W8OctreeIndex::iterator g_octree_query_slot;
// GLOBAL: WIZ8 0x00659766
static unsigned short g_octree_query_id;

// GLOBAL: WIZ8 0x00659770
unsigned int* g_octree_storage_;

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
stModelInstance* g_octree_trace_node;
// GLOBAL: WIZ8 0x00659898
bool g_octree_update_suspended;
// GLOBAL: WIZ8 0x00659899
bool g_octree_trace_enabled;

/* Build a packed four-byte colour from four components and answer its
   address. The receiver is the output slot. */
/* Draw the probe box through the world camera. */

// GLOBAL: WIZ8 0x006598a4
W8Octree* g_octree;

// GLOBAL: WIZ8 0x006068a4
static char g_region_link_extension[] = ".rlk";

// GLOBAL: WIZ8 0x006598a8
bool g_octree_disabled;
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
bool g_inverted_depth_render;
// GLOBAL: WIZ8 0x0065a146
bool g_render_untextured;

// FUNCTION: WIZ8 0x0042bc00
void NoOct(void)
{
    g_octree_disabled = true;
}

// GLOBAL: WIZ8 0x005ec02c
static const float NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE = 50.0f;

/* Noise falloff on the trace resolver's sphere-distance penalty: the farther
   the probe is along the ray, the more the candidate's effective distance is
   discounted. Shared with CreateTraceModel. */
// GLOBAL: WIZ8 0x005ebc78
const float g_float_fifteen_hundredths = 0.15000000596046448f;
/* Vertical snap ceiling for navigator placement: the source may rise or fall
   at most this many units before a candidate is rejected outright. */
// GLOBAL: WIZ8 0x005ec038
const double g_navigator_vertical_snap_limit = 5000.0;
/* Fixed camera tilt (15 degrees below horizontal) the region-link projector
   applies to every sampled direction. */
// GLOBAL: WIZ8 0x005ec008
const double g_region_link_camera_tilt = -0.26179999113082886;
/* Circle-coverage bound just under 2*pi: when samples*fov still falls short,
   one more direction is added. */
// GLOBAL: WIZ8 0x005ec010
const float g_region_link_circle_coverage = 6.282185077667236f;
/* Jitter scale applied to the Random(1000) roll for scatter-ring candidates
   past the first; 0.0004 * 1000 spans 0.4 units. */
// GLOBAL: WIZ8 0x005ec044
const float g_scatter_outer_ring_jitter_scale = 0.00040000001899898052f;
/* Multiplier on the placement radius that gives the monster-proximity query
   box its extent. */
// GLOBAL: WIZ8 0x005ec048
const float g_monster_proximity_radius_scale = 15.0f;
/* Jitter scale applied to the Random(1000) roll for ring candidates past the
   first; 0.0002 * 1000 spans 0.2 units. */
// GLOBAL: WIZ8 0x005ec050
const float g_scatter_inner_ring_jitter_scale = 0.00020000000949949026f;

// FUNCTION: WIZ8 0x0042f7e0
void W8Octree::UpdateCameraVisibility()
{
    W8World* world = GetWorld();
    if (g_octree_update_suspended || m_visibility_suspended) {
        return;
    }

    view.far_clip = static_cast<float>(WorldGetFarClip(world));
    view.camera_location = glm::vec3(world->camera->getLocation());
    view.horizontal_fov = static_cast<float>(world->camera->getHorizontalFOV());
    view.vertical_fov = static_cast<float>(world->camera->getVerticalFOV());

    auto rotation = world->camera->getRotation<glm::mat3>();
    view.rotation_column0.Set(rotation[0][0], rotation[0][1], rotation[0][2]);
    view.rotation_column1.Set(rotation[1][0], rotation[1][1], rotation[1][2]);

    srVector3T<double> dof = world->camera->getWorldSpaceDOF();
    view.camera_dof = dof;
    view.horizontal_fov_cosine = static_cast<float>(cos(view.horizontal_fov));
    view.vertical_fov_cosine = static_cast<float>(cos(view.vertical_fov));
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_projected_regions, false);
    UpdateVisibility();
    if (g_octree_trace_enabled) {
        UpdateWorldTrace();
    }
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
        for (index = 0; index < m_usNumPropsLoaded; ++index) {
            m_papProps[index]->SetActivationState(0);
        }
        if (m_pusMeshProps != 0) {
            for (index = 0; m_pusMeshProps[index] != 0; ++index) {
                m_papProps[m_pusMeshProps[index] - 1]->SetActivationState(1);
            }
        }
        for (index = 0; index < m_usNumParticlesLoaded; ++index) {
            m_papParticles[index]->SetTraversalEnabled(false);
        }
        if (m_pusMeshParticles != 0) {
            for (index = 0; m_pusMeshParticles[index] != 0; ++index) {
                m_papParticles[m_pusMeshParticles[index] - 1]->SetTraversalEnabled(true);
            }
        }
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
    m_location_region_matched = 0;
    view.visible_cells = projectRegionsAt(view.camera_location.glmValue()).cell;
    CollectVisibleCells();
    if (pathing != 0) {
        srVector3T<float> dof;
        GetWorldCursorPosition(&dof);
        /* Same proven retail quirk as UpdatePathVisualization: y is tested
           twice and x is never examined. */
        if (dof.y != g_float_zero || dof.y != g_float_zero || dof.z != g_float_zero) {
            srVector3T<float> probe = dof;
            pathing->UpdatePathVisualization(&probe, &view.camera_dof);
        } else {
            pathing->UpdatePathVisualization(&view.camera_location, &view.camera_dof);
        }
    }
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
        for (index = 0; m_pusMeshParticles[index] != 0; ++index) {
            SetBit(m_linked_particles, m_pusMeshParticles[index] - 1);
        }
        for (std::size_t particle = 0; particle < m_linked_particles.size(); ++particle) {
            if (m_linked_particles[particle] && !m_visible_particles[particle]) {
                m_papParticles[particle]->SetTraversalEnabled(true);
            } else if (!m_linked_particles[particle] && m_visible_particles[particle]) {
                m_papParticles[particle]->SetTraversalEnabled(false);
            }
        }
        m_visible_particles = m_linked_particles;
    }
    if (m_ulNumProps != 0) {
        for (index = 0; m_pusMeshProps[index] != 0; ++index) {
            SetBit(m_linked_props, m_pusMeshProps[index] - 1);
        }
        for (std::size_t prop = 0; prop < m_linked_props.size(); ++prop) {
            if (m_linked_props[prop] && !m_visible_props[prop]) {
                m_papProps[prop]->SetActivationState(1);
            } else if (!m_linked_props[prop] && m_visible_props[prop]) {
                m_papProps[prop]->SetActivationState(0);
            }
        }
        m_visible_props = m_linked_props;
    }
    m_previous_regions = m_current_regions;
}

/* Collect mesh and prop instances whose world-space bounds intersect the query sphere.
   The grid and pre-generated frusta supply candidate regions. */
// FUNCTION: WIZ8 0x0042f9a0
int W8Octree::CollectModelsNearPoint(std::vector<stModelInstance*>& out,
                                     const srVector3T<float>* point, float radius,
                                     unsigned int flags, bool only_accumulated)
{
    const auto center = point->glmValue();
    if (!(radius >= 0) || glm::any(glm::isnan(center)) || glm::any(glm::isinf(center)) ||
        m_spatial.m_region_cells_per_axis == 0 || !(m_spatial.m_region_grid_cell > 0))
        return static_cast<int>(out.size());
    const auto relative = (center - m_spatial.m_minimum.glmValue()) / m_spatial.m_region_grid_cell;
    const float reach = radius / m_spatial.m_region_grid_cell;
    const glm::vec3 limit(m_spatial.m_region_cells_per_axis - 1);
    const glm::ivec3 first(glm::clamp(glm::floor(relative - reach) - 1.0f, glm::vec3(0), limit));
    const glm::ivec3 last(glm::clamp(glm::floor(relative + reach) + 1.0f, glm::vec3(0), limit));
    const auto intersects = [&](srModelInstance* instance) {
        if (!instance || !instance->getModel())
            return false;
        const auto bounds = instance->getModel()->getBounds().box;
        const auto world = instance->getWorldSpaceMatrix<glm::mat4>();
        return bounds.transformed(world).intersectsSphere(center, radius);
    };
    const unsigned int level_mask = m_region_mask;
    std::ranges::fill(m_current_regions, false);
    glm::ivec3 cell;
    for (cell.x = first.x; cell.x <= last.x; ++cell.x) {
        for (cell.y = first.y; cell.y <= last.y; ++cell.y) {
            for (cell.z = first.z; cell.z <= last.z; ++cell.z) {
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
            }
        }
    }

    unsigned int region;
    for (region = 1; region < m_spatial.m_region_count; ++region) {
        if (SphereInsideFrustum(point, radius, m_spatial.m_region_volumes[region].m_planes) &&
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
            if (intersects(instance))
                out.push_back(instance);
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
            srModelInstance* instance = m_papProps[bit - 1]->ToggleRepAnimationDefault();
            if (intersects(instance))
                out.push_back(static_cast<stModelInstance*>(instance));
        }
    }
    return static_cast<int>(out.size());
}

/* Add every particle and prop linked to one mesh to the live visibility sets.

   The lookup gives a 1-based start into a packed link table; each run is
   terminated by a zero. A linked entry whose slot is empty is skipped. */
// FUNCTION: WIZ8 0x00430a70
void W8Octree::MarkMeshLinksVisible(unsigned int mesh)
{
    if (mesh > m_meshCount) {
        return;
    }
    if (m_ulNumParticles != 0) {
        unsigned short link = m_pusMeshParticleLookup[mesh];
        if (link != 0 && m_pusMeshParticles[link] != 0) {
            do {
                if (m_usMeshParticlesLen < link) {
                    srAssertFail("usProp<=m_usMeshParticlesLen", OCTREE_CPP, 0xa07,
                                 "Particle lookup index out of range");
                }
                unsigned short particle = m_pusMeshParticles[link];
                ++link;
                if (particle <= m_usNumParticlesLoaded && m_papParticles[particle] != 0) {
                    SetBit(m_linked_particles, particle - 1);
                }
            } while (m_pusMeshParticles[link] != 0);
        }
    }
    if (m_ulNumProps != 0) {
        unsigned short link = m_pusMeshPropLookup[mesh];
        if (link != 0 && m_pusMeshProps[link] != 0) {
            do {
                if (m_usMeshPropsLen < link) {
                    srAssertFail("usProp<=m_usMeshPropsLen", OCTREE_CPP, 0xa1b,
                                 "Prop lookup index out of range");
                }
                unsigned short prop = m_pusMeshProps[link];
                ++link;
                if (prop <= m_usNumPropsLoaded && m_papProps[prop] != 0) {
                    SetBit(m_linked_props, prop - 1);
                }
            } while (m_pusMeshProps[link] != 0);
        }
    }
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
short W8Octree::ProjectLinkedRegionsForLocation(srVector3T<float>* location,
                                                unsigned short* region_list)
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
            if (!PointInsideFrustum(location, volume->m_planes)) {
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
            m_location_region_matched = 1;
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
        if (!PointInsideFrustum(location, volume->m_planes)) {
            continue;
        }
        if (reported_region == 0 && region_index != g_octree_region_debug_last) {
            sprintf(expected_regions, "Point in region %d, expected regions: %s\n", region_index,
                    point_region_label);
            NoOp();
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
    m_location_region_matched = 1;
    return match_count;
}

// FUNCTION: WIZ8 0x00430BF0
unsigned int W8Octree::GetSectorForPosition(const srVector3T<float>* position)
{
    srVector3T<float> point = *position;
    for (unsigned int region = 1; region < m_spatial.m_region_count; ++region) {
        if (PointInsideFrustum(&point, m_spatial.m_region_volumes[region].m_planes)) {
            return m_spatial.m_region_volumes[region].m_region_bit;
        }
    }

    unsigned int cell[4];
    cell[0] = m_region_mask;
    for (int axis = 0; axis < 3; ++axis) {
        int coordinate = static_cast<int>(((&position->x)[axis] - (&m_spatial.m_minimum.x)[axis]) /
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
    const auto minimum = m_spatial.m_minimum.glmValue();
    auto cell = cellAt(location);
    // Retain the visibility/swept-query policy even at exact negative cell
    // multiples; it is intentionally different from truncating cellAt().
    for (int axis = 0; axis < 3; ++axis)
        if (location[axis] < minimum[axis]) --cell[axis];
    const auto offset = (location - (glm::vec3(cell) * m_spatial.m_node_extent + minimum)) /
                       m_spatial.m_node_extent - g_float_half;
    const bool inside = !glm::any(glm::lessThan(location, minimum)) &&
                        !glm::any(glm::greaterThan(location, m_spatial.m_maximum.glmValue()));
    return {cell, offset, inside};
}

// FUNCTION: WIZ8 0x00430d50
OctreeCellCoordinates W8Octree::projectRegionsAt(const glm::vec3& position)
{
    const auto coordinates = cellCoordinates(position);
    if (!coordinates.inside) return coordinates;
    const glm::ivec3 link_cell((position - m_spatial.m_minimum.glmValue()) /
                               m_spatial.m_region_grid_cell);
    srVector3T<float> box_min = m_spatial.m_minimum;
    srVector3T<float> box_max = m_spatial.m_maximum;
    srVector3T<float> location_record(position);
    auto* location = &location_record;
    float span = m_spatial.m_extent * g_float_half;
    int node = 1;
    for (short level = 0; level < m_spatial.m_depth; ++level) {
        unsigned int child_index = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float edge = span + (&box_min.x)[axis];
            if ((&location->x)[axis] < edge) {
                (&box_max.x)[axis] = edge;
            } else {
                (&box_min.x)[axis] = edge;
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
        span *= g_float_half;
    }
    if (node != 0) {
        w8_ulong region_offset = m_leaves[node].region_offset;
        if (region_offset != 0) {
            ProjectLinkedRegionsForLocation(location, m_region_index_stream + region_offset);
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
        srVector3T<float> delta = view.camera_location - volume->m_points[0];

        if (delta.LengthSquared() >= radius_squared) {
            continue;
        }
        bool visible = PointInsideFrustum(&volume->m_points[0], view.frustum_planes);

        for (int point = 1; !visible && point < 9; ++point) {
            visible = PointInsideFrustum(&volume->m_points[point], view.frustum_planes);
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
    float fov = view.horizontal_fov * g_float_half;
    float extent = m_spatial.m_max_region_radius;
    float far_clip = view.far_clip;
    float backoff = extent / sin(fov);
    float ratio = extent / cos(fov);
    double tangent = tan(fov);
    double tangent_vertical = tan(view.vertical_fov * g_float_half);
    srVector3T<float> corners[8];

    view.frustum_planes[4].w = 0.0f;
    view.frustum_planes[5].w = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        float dof = (&view.camera_dof.x)[axis];
        float column1 = (&view.rotation_column0.x)[axis];
        float column2 = (&view.rotation_column1.x)[axis];
        float w = far_clip * dof;
        float a = (tangent * far_clip + ratio) * column1;
        float b = (tangent_vertical * far_clip + ratio) * column2;

        (&corners[0].x)[axis] = (&view.camera_location.x)[axis] - backoff * dof;
        (&corners[1].x)[axis] = a;
        (&corners[2].x)[axis] = b;
        (&corners[3].x)[axis] = w;
        (&corners[4].x)[axis] = (w - a) + b;
        (&corners[5].x)[axis] = (w + b) + a;
        (&corners[6].x)[axis] = (w - a) - b;
        (&corners[7].x)[axis] = (w + a) - b;
        (&corners[4].x)[axis] += (&view.camera_location.x)[axis];
        (&corners[5].x)[axis] += (&view.camera_location.x)[axis];
        (&corners[6].x)[axis] += (&view.camera_location.x)[axis];
        (&corners[7].x)[axis] += (&view.camera_location.x)[axis];
        (&view.frustum_planes[4].normal.x)[axis] = dof;
        view.frustum_planes[4].w -= dof * (&corners[0].x)[axis];
        (&view.frustum_planes[5].normal.x)[axis] = -dof;
        view.frustum_planes[5].w -= -dof * (&corners[4].x)[axis];
    }
    view.frustum_planes[0] = trianglePlane(corners[0].glmValue(),
        corners[5].glmValue(), corners[4].glmValue());
    view.frustum_planes[1] = trianglePlane(corners[0].glmValue(),
        corners[4].glmValue(), corners[6].glmValue());
    view.frustum_planes[2] = trianglePlane(corners[0].glmValue(),
        corners[7].glmValue(), corners[5].glmValue());
    view.frustum_planes[3] = trianglePlane(corners[0].glmValue(),
        corners[6].glmValue(), corners[7].glmValue());
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
    srVector3T<short> center;

    for (int axis = 0; axis < 3; ++axis) {
        (&center.x)[axis] = static_cast<short>(
            static_cast<int>((((&view.camera_location.x)[axis] - (&m_spatial.m_minimum.x)[axis]) /
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
                    float offset = m_spatial.m_region_grid_cell * g_float_half;
                    srVector3T<float> point;
                    point.x =
                        cell_x * m_spatial.m_region_grid_cell + offset + m_spatial.m_minimum.x;
                    point.y =
                        cell_y * m_spatial.m_region_grid_cell + m_spatial.m_minimum.y + offset;
                    point.z =
                        cell_z * m_spatial.m_region_grid_cell + m_spatial.m_minimum.z + offset;
                    if (!PointInsideFrustum(&point, view.frustum_planes)) {
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
        const auto extension = extension_start == std::string::npos ? std::string{} : search.substr(extension_start);
        if (extension_start != std::string::npos) {
            search.resize(extension_start);
        }
        search += g_octree_file_search_wildcard;
        search += extension;
        const auto separator = search.find_last_of("\\/");
        g_octree_file_search = wiz8::list_directory(
            separator == std::string::npos ? "." : search.substr(0, separator),
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
try
{
    std::string stem = level_name;
    if (const auto extension = stem.find_last_of('.'); extension != std::string::npos) {
        stem.resize(extension);
    }
    stem += g_octree_point_extension;
    std::string name = std::move(stem);

    FindNextLevelFile(0);
    std::unique_ptr<srVector3T<float>[]> points;
    unsigned int count = 0;
    unsigned int files = 0;
    while (FindNextLevelFile(&name) != 0) {
        auto file = wiz8::open_file(name);
        file->read_exact(&count, sizeof(count));
        const auto bytes = std::uint64_t{count} * sizeof(srVector3T<float>);
        if (bytes > static_cast<std::uint64_t>(file->size() - file->tell()) ||
            std::uint64_t{count} + 1 > std::numeric_limits<std::size_t>::max() / sizeof(srVector3T<float>)) {
            return false;
        }
        auto next = std::make_unique<srVector3T<float>[]>(std::size_t{count} + 1);
        file->read_exact(next.get(), static_cast<std::size_t>(bytes));
        points = std::move(next);
        ++files;
    }
    if (files == 0) {
        return false;
    }
    delete[] m_sample_points;
    m_sample_points = points.release();
    m_point_count = count;
    m_points_dirty |= files > 1;
    return true;
}
catch (const std::exception&) { return false; }

/* Write the octree's point count and records to the mutable overlay. */
// FUNCTION: WIZ8 0x00432d60
bool W8Octree::SavePoints(char* path)
try
{
    if (m_point_count == 0 || m_sample_points == 0) {
        return false;
    }
    std::string name = path;
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += g_octree_point_extension;
    auto file = wiz8::open_file(name, wiz8::OpenMode::replace);
    file->write(&m_point_count, sizeof(m_point_count));
    file->write(m_sample_points, std::size_t{m_point_count} * sizeof(srVector3T<float>));
    file->close();
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00432e90
bool W8Octree::ReadRegionLinkFile(const char* level_name)
try
{
    std::string name = level_name;
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += g_region_link_extension;
    auto file = wiz8::open_file(name);
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
}
catch (const std::exception&) { return false; }

/* The region volume containing `point` as its 1-based index into the spatial
   array, else the packed auto-region key - the region-grid level in the top
   byte over the point's cell coordinates. Region zero is never tested: it is
   the no-region sentinel. */
// FUNCTION: WIZ8 0x00432720
unsigned int W8Octree::RegionKeyForPoint(const srVector3T<float>* point)
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
        cell[axis] = static_cast<int>(((&point->x)[axis] - (&m_spatial.m_minimum.x)[axis]) /
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
                NoOp();
                reported = true;
            }
            sprintf(text, "%d, ", mesh);
            NoOp();
            InsertIndexValue(*m_pRegionLinks, region_key, mesh);
        }
    }
    if (reported) {
        NoOp();
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
unsigned int W8Octree::SampleRegionLinks(const srVector3T<float>* point, bool descend,
                                         bool clear_sets, unsigned int region_key)
{
    bool linked = false;
    W8World* world = GetWorld();
    std::vector<stModelInstance*> meshes;
    std::vector<int> cells;
    view.camera_location = *point;
    view.far_clip = m_spatial.m_extent;
    char text[104];
    sprintf(text, "Sample point: %f, %f, %f, Links Found: ", point->x, point->y, point->z);
    NoOp();
    glm::dvec3 location;
    location = glm::dvec3(point->glmValue());
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
        static_cast<int>(g_camera_angle_period / view.horizontal_fov + g_camera_snap_epsilon));
    if (samples * view.horizontal_fov < g_region_link_circle_coverage) {
        ++samples;
    }
    if (samples > 0) {
        double cos_tilt = cos(g_region_link_camera_tilt);
        double sin_tilt = sin(g_region_link_camera_tilt);
        for (int direction = 0; direction < samples; ++direction) {
            float angle = direction * view.horizontal_fov;
            glm::mat3 frame;
            frame = glm::mat3(1);
            frame *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
            frame *= glm::mat3_cast(
                glm::angleAxis(float(std::atan2(sin_tilt, cos_tilt)), glm::vec3(1, 0, 0)));
            world->camera->setRotation(frame);
            view.rotation_column0.x = frame[0][0];
            srVector3T<float> unit;
            unit.Set(1.0, 0.0, 0.0);
            view.rotation_column0.y = glm::dot(glm::row(frame, 1), unit.glmValue());
            view.rotation_column0.z = glm::dot(glm::row(frame, 2), unit.glmValue());
            float right_y = frame[1][0];
            unit.Set(0.0, 1.0, 0.0);
            view.rotation_column1.x = right_y;
            view.rotation_column1.y = glm::dot(glm::row(frame, 1), unit.glmValue());
            view.rotation_column1.z = glm::dot(glm::row(frame, 2), unit.glmValue());
            srVector3T<double> dof = world->camera->getWorldSpaceDOF();
            view.camera_dof = dof;
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
                        while (slot != m_pRegionLinks->end() && slot->second !=
                                                 static_cast<unsigned short>(cell)) {
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
                            srNode* child = mesh->first_child_;
                            while (child != 0 && descend) {
                                faces = MeasureNodeRender(child);
                                if (9 < faces) {
                                    all_known = true;
                                }
                                child = child->first_child_;
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
                                    NoOp();
                                    SetBit(m_projected_regions, cell);
                                    linked = true;
                                }
                                srNode* child = mesh->first_child_;
                                while (child != 0 && descend) {
                                    faces = MeasureNodeRender(child);
                                    if (9 < faces) {
                                        SetBit(m_projected_regions, cell);
                                        linked = true;
                                    }
                                    child = child->first_child_;
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
    NoOp();
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
    for (unsigned int prop = 0; prop < m_usNumPropsLoaded; ++prop) {
        m_papProps[prop]->SetActivationState(0);
    }
    for (unsigned int particle = 0; particle < m_usNumParticlesLoaded; ++particle) {
        m_papParticles[particle]->SetTraversalEnabled(false);
    }
    SetViewportMode(0);
    SetWorldScaledViewport(0, 0, 0x140, 0xf0);
    srVector3T<float> saved_location;
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
            srVector3T<float> point;
            point.x = m_region_cell * g_float_half + x_index * stride + m_spatial.m_minimum.x;
            unsigned short z_index = !rebuild_all && (x_index & 1) != 0 ? z_step : 0;
            if (!(m_spatial.m_working_minimum.x < point.x &&
                  point.x < m_spatial.m_working_maximum.x)) {
                continue;
            }
            for (; z_index < rows; z_index += z_step) {
                if (aborted) {
                    break;
                }
                point.z = m_region_cell * g_float_half + z_index * stride + m_spatial.m_minimum.z;
                if (!(m_spatial.m_working_minimum.z < point.z &&
                      point.z < m_spatial.m_working_maximum.z)) {
                    continue;
                }
                const bool received_events = PumpGameEvents();
                if (!gfProgramIsRunning) {
                    aborted = true;
                    g_build_level_links = false;
                    break;
                }
                if (received_events) {
                    InputAtom input;
                    if (DequeueEvent(&input) != 0 && input.usEvent == KEY_DOWN) {
                        if (input.usParam == VK_RETURN) {
                            x_index += 2;
                            z_index = 0;
                        } else if (input.usParam == VK_ESCAPE) {
                            aborted = true;
                            if (g_build_level_links) {
                                g_build_level_links = false;
                            }
                            continue;
                        }
                    }
                }
                point.y = m_spatial.m_minimum.y + m_spatial.m_extent;
                while (pathing->SnapToLowerPathCell(&point, true)) {
                    point.y += g_default_world_height;
                    unsigned int key = RegionKeyForPoint(&point);
                    if (key != last_key) {
                        if (last_key != 0) {
                            RecordRegionMeshLinks(last_key);
                        }
                        std::ranges::fill(m_projected_regions, false);
                        std::ranges::fill(m_previous_regions, false);
                        last_key = key;
                    }
                    SampleRegionLinks(&point, false, false, key);
                    point.y -= g_default_world_height + g_startup_near_limit;
                }
            }
        }
    }
    g_render_untextured = false;
    EnableAllRenderOptions();
    std::unordered_multimap<unsigned int, unsigned short>* stale;
    if (!rebuild_all) {
        if (!aborted) {
            for (unsigned int point_index = 0; point_index < m_point_count; ++point_index) {
                unsigned int key = SampleRegionLinks(&m_sample_points[point_index], true, true, 0);
                RecordRegionMeshLinks(key);
            }
        }
    } else {
        char point_path[256];
        strcpy(point_path, m_owned_0c0);
        char* extension = strrchr(point_path, '.');
        if (extension != 0) {
            *extension = '\0';
        }
        strcat(point_path, g_octree_point_extension);
        if ([&]() { const auto status = wiz8::file_status(point_path); return status && status->info.type == SDL_PATHTYPE_FILE; }()) {
            wiz8::remove_file(point_path);
        }
        delete[] m_sample_points;
        m_sample_points = 0;
        m_point_count = 0;
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
                        SampleRegionLinks(&entry->path->nodes[node_index], true, true, 0);
                    RecordRegionMeshLinks(key);
                }
            }
        }
    }
    glm::dvec3 restore_location;
    restore_location = glm::dvec3(saved_location.glmValue());
    world->camera->setLocation(restore_location);
    world->camera->setRotation(saved_rotation);
    g_light_update_flags |= 1u;
    SetCameraLightMode(3);
    g_render_unlit = false;
    g_render_cull_front = false;
    SetWorldScaledViewport(0, 0, 0x280, 0x1e0);
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
        if (gfProgramIsRunning) {
            CreateMessageBox(std::string("  Linking Aborted!  "), g_small_font, 1, true, false, 0);
        }
    } else {
        SaveRegionLinks(m_owned_0c0);
        if (hours == 0) {
            CreateMessageBox(
                fmt::format("  Linking Time: {:d} Min, {:d} Sec  ", minutes, elapsed % 60),
                g_small_font, 1, true, false, 0);
        } else {
            CreateMessageBox(fmt::format("  Linking Time: {:d} Hours, {:d} Min, {:d} Sec  ", hours,
                                         minutes, elapsed % 60),
                             g_small_font, 1, true, false, 0);
        }
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
bool W8Octree::SaveRegionLinks(char* path)
try
{
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
                for (auto slot = m_pRegionLinks->equal_range(key).first; slot != m_pRegionLinks->end();
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
    auto file = wiz8::open_file(name, wiz8::OpenMode::replace);
    file->write(&count, sizeof(count));
    file->write(keys.data(), keys.size() * sizeof(keys[0]));
    file->write(values.data(), values.size() * sizeof(values[0]));
    file->close();
    return true;
}
catch (const std::exception&) { return false; }

/* Draw the octree cell the camera currently occupies as a wire box. The cell
   is derived on each axis by quantizing the camera position relative to the
   spatial minimum, and the box spans one cell from there. */
// FUNCTION: WIZ8 0x00433eb0
unsigned char W8Octree::UpdateWorldTrace()
{
    srVector3T<float> camera;
    glm::ivec3 cell;
    srVector3T<float> minimum;
    srVector3T<float> maximum;
    w8_ulong color;

    camera = cameraLocation();
    cell = cellAt(camera.glmValue());
    minimum.Set(cell.x * m_spatial.m_node_extent + m_spatial.m_minimum.x,
                cell.y * m_spatial.m_node_extent + m_spatial.m_minimum.y,
                cell.z * m_spatial.m_node_extent + m_spatial.m_minimum.z);
    maximum.Set(minimum.x + m_spatial.m_node_extent, minimum.y + m_spatial.m_node_extent,
                minimum.z + m_spatial.m_node_extent);
    w8_ulong* packed = PackColourToLong(&color, 0.0, 1.0, 0.0, 0.0);
    DrawWorldBox(g_world, minimum, maximum, *packed);
    return 1;
}

// GLOBAL: WIZ8 0x005ebf60
const double g_color_byte_scale = 255.0;

/* Pack normalized alpha/red/green/blue components using the current x87
   rounding mode, then retain each result's low byte. */
// FUNCTION: WIZ8 0x00433fb0
w8_ulong* __fastcall PackColourToLong(w8_ulong* color, double alpha, double red,
                                           double green, double blue)
{
    unsigned char* bytes =
        reinterpret_cast<unsigned char*>(color); // reinterpret-ok: packed colour storage
    bytes[3] = static_cast<unsigned char>(srFloatToInt(alpha * g_color_byte_scale));
    bytes[2] = static_cast<unsigned char>(srFloatToInt(red * g_color_byte_scale));
    bytes[1] = static_cast<unsigned char>(srFloatToInt(green * g_color_byte_scale));
    bytes[0] = static_cast<unsigned char>(srFloatToInt(blue * g_color_byte_scale));
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
        CreateMessageBox(fmt::format(" {:d} Bad Region-Mesh Links!", bad_links), g_small_font, 1,
                         true, false, 0);
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
                        const srBoundingBox cell{spatial->m_minimum.glmValue(),
                                                 spatial->m_maximum.glmValue()};
                        bad_links = !bounds.transformed(world).overlaps(cell);
                    }
                }
            }
        } else {
            const auto minimum = spatial->m_minimum.glmValue();
            for (unsigned child = 0; child < 8; ++child) {
                local.m_node_index = m_branches[spatial->m_node_index].children[child];
                if (!local.m_node_index)
                    continue;
                const glm::vec3 offset((child >> 2) & 1, (child >> 1) & 1, child & 1);
                const auto corner = minimum + offset * local.m_extent;
                local.m_minimum = corner;
                local.m_maximum = corner + local.m_extent;
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
void W8Octree::ToggleUpdateSuspension(W8World* world)
{
    if (world == 0) {
        g_octree_trace_node = 0;
        g_octree_update_suspended = false;
        return;
    }
    g_octree_update_suspended = (!g_octree_update_suspended);
    if (!g_octree_update_suspended) {
        g_octree_trace_node->setFlag(srNode::FLAG_DISABLE);
        g_octree_trace_node->setFlag(srNode::FLAG_TERMINATE);
        m_reset_visibility = true;
        MarkRendererReady();
        return;
    }
    for (unsigned short mesh_index = 0; mesh_index < m_meshCount; ++mesh_index) {
        m_pSubmeshes[static_cast<stModelInstance*>(g_world->psrMeshes[mesh_index])->mesh_index + 1]
            .mesh = mesh_index;
        m_pSubmeshes[mesh_index + 1].flags &= 0xffffffc7;
        static_cast<stModelInstance*>(world->psrMeshes[mesh_index])->setFlag(srNode::FLAG_DISABLE);
        static_cast<stModelInstance*>(world->psrMeshes[mesh_index])
            ->setFlag(srNode::FLAG_TERMINATE);
    }
    memset(m_pfRegsVisited, 0, m_spatial.m_region_id_bound + 1);
    if (g_octree_trace_node == 0) {
        g_octree_trace_node = world->static_scene->adoptChild(g_octree_game_data->CreateTraceModel(), 1);
        SetModelInstanceChainExclusionMask(g_octree_trace_node, 2);
    }
    g_octree_trace_node->clearFlag(srNode::FLAG_DISABLE);
    g_octree_trace_node->clearFlag(srNode::FLAG_TERMINATE);
}

// FUNCTION: WIZ8 0x00434220
unsigned char W8Octree::TestNoiseLineOfSight(const srVector3T<float>* from, srVector3T<float>* to,
                                             float* range, int* hops)
{
    return pathing->MeasureAttachmentPath(from, to, range, hops);
}

// FUNCTION: WIZ8 0x00434250
unsigned char W8Octree::PrepareNavigatorTarget(W8NavigatorMovementState* movement, float radius,
                                               float separation)
{
    unsigned char result = 0;
    movement->target_position =
        settleToGround(movement->target_position.glmValue(), true, 500).position;
    if (pathing == 0) {
        return 1;
    }
    srVector3T<float> delta = movement->target_position - movement->position;
    delta.y = 0.0f;
    if (glm::length(((delta.xz()))) < NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE) {
        return 0;
    }
    if ((movement->attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
        srVector3T<float> target = movement->target_position;
        if (pathing->FindPathCell(&target, 0, true) != 0) {
            if (!pathing->TestWaypointSpan(&movement->position, &target, false, false)) {
                movement->attachment->InitializeSegment(&movement->position, &target);
                movement->attachment->separation = separation;
                result =
                    pathing->BuildAttachmentPath(movement->attachment.get(), movement->navigation_filter);
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
    float length = delta.Length();
    float gap = length - separation;
    if (gap < g_float_zero) {
        movement->attachment->InitializeSegment(&movement->position, &movement->position);
        return 1;
    }
    if (gap < g_world_scale) {
        delta.SetLength(gap * g_float_one_and_one_hundredth);
        delta += movement->position;
        movement->attachment->InitializeSegment(&movement->position, &delta);
        return 1;
    }
    movement->attachment->InitializeSegment(&movement->position, &movement->target_position);
    movement->attachment->separation = separation;
    return pathing->PlanMovement(movement, radius, separation) != 0;
}

// FUNCTION: WIZ8 0x004347d0
bool __stdcall IsNavigatorAtTarget(W8NavigatorMovementState* movement)
{
    srVector3T<float> target;
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
    if (movement->movement_scale * g_world_scale < (target - movement->position).Length()) {
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
        srVector3T<float> velocity = movement->velocity;
        movement->attachment->InitializeSegment(&movement->position, &movement->target_position);
        result = pathing->BuildPatrolPath(movement->attachment.get(), movement->navigation_filter,
                                          &movement->target_position, minimum, &velocity, maximum);
        if (result == 0) {
            return 0;
        }
        double step = movement->movement_scale * g_world_scale;
        movement->target_position = movement->attachment->GetNextPosition();
        srVector3T<float> delta;
        delta = movement->target_position - movement->position;
        if (step < delta.Length()) {
            delta.SetLength(step);
        }
        movement->target_position = delta + movement->position;
    }
    return result;
}

// FUNCTION: WIZ8 0x00434a00
unsigned char W8Octree::LinkNavigatorTarget(W8NavigatorMovementState* movement,
                                            const srVector3T<float>* target, float separation)
{
    if (pathing != 0) {
        return pathing->LinkAttachmentTarget(movement->attachment.get(), movement->navigation_filter,
                                             target, separation);
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
    if (m_leaf_lookup != 0) {
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
    srVector3T<float> start;
    srVector3T<float> end;
    bool prop_hit = false;
    bool hit = false;

    m_gd_result_count = 0;
    if (test_props != 0) {
        current_prop = -1;
    }
    std::ranges::fill(m_visited_object_bits, false);
    if (m_spatial.m_clipped_maximum.y < position.y) {
        position.y = m_spatial.m_clipped_maximum.y;
    }
    end = position;
    start = position;
    start.y = position.y + limit;
    cell = cellAt(start.glmValue());
    end.y = ((cell.y - 1)) * m_spatial.m_node_extent + m_spatial.m_minimum.y;
    trace.Reseed(&start, &end);
    if (-1 < cell.y) {
        do {
            if (hit) {
                goto done;
            }
            if (test_props != 0) {
                g_octree_state = m_aulGDObjs;
                m_gd_result_count = 0;
                w8_ulong before = m_gd_result_count;
                CollectObjectsInCell(cell, W8_OCTREE_KIND_PROP);
                if (m_gd_result_count != before) {
                    int prop = g_octree_game_data->TestPropSurfaces(m_gd_result_count, m_aulGDObjs,
                                                                    &trace, 0, 0);
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
            hit = g_octree_game_data->TestTraceResult(m_gd_result_count, m_aulGDObjs, &trace, 0, 0);
            if (!hit) {
            descend:
                if (prop_hit) {
                    hit = true;
                } else {
                    float level = static_cast<float>(cell.y);
                    --cell.y;
                    start.y = level * m_spatial.m_node_extent + m_spatial.m_minimum.y;
                    end.y -= m_spatial.m_node_extent;
                    trace.Reseed(&start, &end);
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
bool W8Octree::SnapToGround(srVector3T<float>* position, char mode)
{
    bool hit = false;
    if (position->y > m_spatial.m_clipped_maximum.y) {
        position->y = m_spatial.m_clipped_maximum.y;
    }
    srVector3T<float> probe = *position;
    probe.y = position->y - g_world_scale;
    const auto ground_probe = settleToGround(probe.glmValue(), mode, 500.0f);
    probe = ground_probe.position;
    hit = ground_probe.hit;
    if (hit) {
        position->y = probe.y;
    }
    return hit;
}

// FUNCTION: WIZ8 0x00434b60
bool W8Octree::HasLineOfSight(const srVector3T<float>* from, srVector3T<float>* to,
                              bool allow_fallback)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;
    unsigned char blocked = 0;

    W8OctreeTrace trace(from, to);

    m_gd_result_count = 0;
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_current_regions, false);
    cell = cellAt(from->glmValue());
    end_cell = cellAt(to->glmValue());

    visitOctreeSegment(cell, end_cell,
        [&] { return cellWalk(from->glmValue(), to->glmValue()); },
        [&](const glm::ivec3& current, bool adjacent) {
            if (ProbeCellForBlockers(current) != 0 || adjacent)
                blocked = TestProbeResult(&trace);
            return blocked == 0;
        });
    if (blocked != 0) {
        *to = trace.end;
    } else if (allow_fallback && TraceAgainstProps(from, to, 1, 1) != 0) {
        blocked = 1;
    }
    return blocked == 0;
}

// FUNCTION: WIZ8 0x00434f20
short W8Octree::TraceLineOfSight(const srVector3T<float>* from, srVector3T<float>* to,
                                 bool trace_world, int from_location_id, int to_location_id,
                                 bool visit_octree, int trace_mode)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;

    bool blocked = false;
    int result;

    W8OctreeTrace trace(from, to);
    result = 0;

    if (visit_octree) {
        m_gd_result_count = 0;
        std::ranges::fill(m_visited_object_bits, false);
        cell = cellAt(from->glmValue());
        end_cell = cellAt(to->glmValue());

        visitOctreeSegment(cell, end_cell,
            [&] { return cellWalk(from->glmValue(), to->glmValue()); },
            [&](const glm::ivec3& current, bool adjacent) {
                if (ProbeCellForTrace(current) != 0 || adjacent)
                    blocked = g_octree_game_data->TestTraceResult(
                        m_gd_result_count, m_aulGDObjs, &trace, m_trace_skip_flag, 0);
                return !blocked;
            });
        if (!trace_world || TraceAgainstProps(from, &trace.end, 0, 0) == 0) {
            if (!blocked) {
                goto resolve;
            }
        } else {
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
        if (const auto hit = traceSphereHit(trace.start.glmValue(), trace.end.glmValue(),
                from_location_id, to_location_id, to_location_id, 0, trace_mode)) {
            *to = hit->position;
            return -1;
        }
    }
    return static_cast<short>(result);
}

/* Trace location spheres and then the camera. The winning location id
   retains the historical last-collider-offset behavior. */
// FUNCTION: WIZ8 0x004353f0
std::optional<W8Octree::SphereTraceHit> W8Octree::traceSphereHit(
    const glm::vec3& from, const glm::vec3& to, int excluded, int target,
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
            distance -= (glm::distance(projection.point, from) / segment_length *
                         g_float_fifteen_hundredths + g_float_one_tenth) * g_world_scale;
            if (distance < g_float_zero) distance = g_float_zero;
        }
        return distance;
    };
    const auto sphereOffset = [&](float sphere_radius, float distance) {
        const auto direction = to - from;
        const double length2 = glm::dot(direction, direction);
        if (length2 == g_double_zero) return direction;
        const double length = std::sqrt(length2);
        const double fraction = (length - std::sqrt(sphere_radius * sphere_radius -
                                                    distance * distance)) / length;
        return glm::vec3(glm::dvec3(direction) * fraction);
    };
    inflate = g_position_height_epsilon;
    if (g_runtime_world_scale > inflate) {
        inflate = g_runtime_world_scale;
    }
    const auto reversed = glm::greaterThan(from, to);
    const srVector3 low(glm::mix(from, to, reversed) - inflate);
    const srVector3 high(glm::mix(to, from, reversed) + inflate);
    if (target == -1) {
        radius = g_startup_world->movement.alternate_radius;
        navigator = g_startup_world;
    } else {
        if (target < 1) {
            goto no_probes;
        }
        unsigned int monster_index = MonsterGetIndexByLocationID(0x1836, OCTREE_CPP, target, true);
        W8MonsterInfo* info = MonsterGetScriptPartByLocationIndex(monster_index);
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
    ids = g_octree->queryObjects({low.glmValue(), high.glmValue()}, W8_OCTREE_KIND_LOCATION, -1);
    unsigned int count = ids.size(); /* c-style-cast-ok:
            the shared query count field is stored unsigned */
    if (count != 0) {
        do {
            int id = ids[index];
            if (((excluded < 0) || (excluded != id)) && ((target < 0) || (excluded_location != id)) &&
                (probe_set == 0 || !pathing->MatchesPathProbe(id, 0, 0))) {
                unsigned int monster_index =
                    MonsterGetIndexByLocationID(0x1851, OCTREE_CPP, id, true);
                W8MonsterInfo* info = MonsterGetScriptPartByLocationIndex(monster_index);
                if (info != 0) {
                    W8Monster* monster = info->p3D;
                    if (monster != 0 && monster->active && (monster->trace_mask & flags) == 0) {
                        const glm::vec3 center(monster->movement.position.x,
                            monster->movement.position.y + monster->movement.height_offset,
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

// FUNCTION: WIZ8 0x00436840
W8OctreeObjectRegistry::W8OctreeObjectRegistry()
    : by_cell(new W8OctreeIndex), by_object(new W8OctreeIndex)
{
}

// FUNCTION: WIZ8 0x00436b20
// FUNCTION: WIZ8 0x00436b90
unsigned char W8OctreeObjectRegistry::MoveObjectToCell(W8OctreeObjectKind kind, int id,
                                                       const glm::ivec3& point)
{
    unsigned int object_key = PackOctreeObjectKey(kind, id);
    unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
    int object_value = static_cast<int>(object_key);
    int cell_value = static_cast<int>(cell_key);

    RemoveIndexValue(*by_object, object_key, cell_value);
    InsertIndexValue(*by_object, object_key, cell_value);
    RemoveIndexValue(*by_cell, cell_key, object_value);
    InsertIndexValue(*by_cell, cell_key, object_value);
    return 1;
}

// FUNCTION: WIZ8 0x00436dc0
unsigned char W8OctreeObjectRegistry::UnregisterObject(W8OctreeObjectKind kind, int id)
{
    unsigned int object_key = PackOctreeObjectKey(kind, id);
    int object_value = static_cast<int>(object_key);
    const auto slot = by_object->equal_range(object_key).first;
    if (slot == by_object->end()) {
        return 0;
    }
    const auto cell_key = static_cast<unsigned int>(slot->second);
    by_object->erase(slot);
    RemoveIndexValue(*by_cell, cell_key, object_value);
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
    int occupied = LookupIndexValue(*by_object, object_key);

    if (occupied != 0) {
        /* Retail only recognizes a repeat of (0,0,z): x is not decoded,
           and the old y byte is shifted too far. Preserve that test. */
        if (point.x == 0 && (((occupied - 1) & 0xff00) << 8) == point.y &&
            OctreeCellKeyZ(occupied) == point.z) {
            return 1;
        }
        unsigned int occupied_key = static_cast<unsigned int>(occupied);
        int object_value = static_cast<int>(object_key);
        RemoveIndexValue(*by_object, object_key);
        RemoveIndexValue(*by_cell, occupied_key, object_value);
    } else if (static_cast<short>(kind) == W8_OCTREE_KIND_LOCATION) {
        unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
        int cell_value = static_cast<int>(cell_key);
        unsigned int tagged_key = PackOctreeObjectKey(W8_OCTREE_KIND_NAVIGATOR, id);
        int tagged_value = static_cast<int>(tagged_key);
        RemoveIndexValue(*by_object, tagged_key, cell_value);
        InsertIndexValue(*by_object, tagged_key, cell_value);
        RemoveIndexValue(*by_cell, cell_key, tagged_value);
        InsertIndexValue(*by_cell, cell_key, tagged_value);
    }

    unsigned int cell_key = PackOctreeCellKey(point.x, point.y, point.z);
    int cell_value = static_cast<int>(cell_key);
    int object_value = static_cast<int>(object_key);
    RemoveIndexValue(*by_object, object_key, cell_value);
    InsertIndexValue(*by_object, object_key, cell_value);
    RemoveIndexValue(*by_cell, cell_key, object_value);
    InsertIndexValue(*by_cell, cell_key, object_value);
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
void W8Octree::AddCollidablePropBounds(int index, const W8BoundingBox* bounds)
{
    srVector3T<int> minimum;
    srVector3T<int> maximum;
    W8OctreeIndex* by_prop;
    W8OctreeIndex* by_cell;
    unsigned int prop_key;
    unsigned int cell_key;
    int axis;
    int x;
    int y;
    int z;

    for (axis = 0; axis < 3; ++axis) {
        (&minimum.x)[axis] =
            static_cast<int>(((&bounds->minimum.x)[axis] - (&m_spatial.m_minimum.x)[axis]) /
                             m_spatial.m_node_extent);
    }
    for (axis = 0; axis < 3; ++axis) {
        (&maximum.x)[axis] =
            static_cast<int>(((&bounds->maximum.x)[axis] - (&m_spatial.m_minimum.x)[axis]) /
                             m_spatial.m_node_extent);
    }

    prop_key = PackOctreeObjectKey(W8_OCTREE_KIND_PROP, index + 1);
    for (x = minimum.x; x <= maximum.x; ++x) {
        for (y = minimum.y; y <= maximum.y; ++y) {
            for (z = minimum.z; z <= maximum.z; ++z) {
                cell_key = PackOctreeCellKey(x, y, z);
                int cell_value = static_cast<int>(cell_key);
                int prop_value = static_cast<int>(prop_key);
                by_prop = object_registry->by_object.get();
                RemoveIndexValue(*by_prop, prop_key, cell_value);
                InsertIndexValue(*by_prop, prop_key, cell_value);
                by_cell = object_registry->by_cell.get();
                RemoveIndexValue(*by_cell, cell_key, prop_value);
                InsertIndexValue(*by_cell, cell_key, prop_value);
            }
        }
    }
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
    const glm::ivec3 from_cell((from - m_spatial.m_minimum.glmValue()) * g_octree_cell_scale);
    const glm::ivec3 to_cell((to - m_spatial.m_minimum.glmValue()) * g_octree_cell_scale);
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
   GameData prop trace. The returned index lands in current_prop and, when
   non-negative, `to` is rewritten with the record's contact point. */
// FUNCTION: WIZ8 0x00436510
int W8Octree::TraceAgainstProps(const srVector3T<float>* from, srVector3T<float>* to, int skip_flag,
                                int gate)
{
    glm::ivec3 cell;
    glm::ivec3 end_cell;

    W8OctreeTrace trace(from, to);
    g_octree_state = m_aulGDObjs;
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    cell = cellAt(from->glmValue());
    end_cell = cellAt(to->glmValue());
    visitOctreeSegment(cell, end_cell,
        [&] { return cellWalk(from->glmValue(), to->glmValue()); },
        [&](const glm::ivec3& current, bool) {
            g_octree_state = m_aulGDObjs;
            CollectObjectsInCell(current, W8_OCTREE_KIND_PROP);
            return true;
        });
    current_prop = g_octree_game_data->TestPropSurfaces(m_gd_result_count, m_aulGDObjs, &trace,
                                                        skip_flag, gate);
    if (current_prop < 0) {
        return 0;
    }
    *to = trace.end;
    return current_prop + 1;
}

/* The cell probes share one lookup: bounds-check the cell against the grid,
   resolve its leaf index through the direct leaf grid when one exists else by
   descending the branch tree one octant per level, then drain the chosen leaf
   stream into m_aulGDObjs through the m_visited_object_bits dedupe set. ProbeCellForTrace
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
            m_gd_surface_index_stream + m_leaves[leaf_index].gd_polygon_offset;
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
        const w8_ulong* stream = m_polygon_index_stream + m_leaves[leaf_index].polygon_offset;
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
        const w8_ulong* stream = m_polygon_index_stream + m_leaves[leaf_index].polygon_offset;
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
    srVector3T<float> best_contact;

    for (unsigned int index = 0; index < m_gd_result_count; ++index) {
        unsigned int mesh_index = m_aulGDObjs[index] >> 0x10;
        if (mesh_index < m_meshCount && g_world->psrMeshes[mesh_index] != 0 &&
            !TestBit(m_alpha_bits, mesh_index)) {
            stMeshModel* model =
                static_cast<stMeshModel*>(g_world->psrMeshes[mesh_index]->getModel());
            const glm::vec4* planes = model->getPolyEq();
            unsigned int polygon = m_aulGDObjs[index] & 0xffff;
            const glm::vec4* plane = planes + polygon;
            const auto contact = planeContact(*plane,
                {trace->start.glmValue(), trace->end.glmValue(), trace->step.glmValue(),
                 trace->length, trace->hit_limit}, PlaneContactMode::Collision);
            if (contact) {
                const auto axis = dominantPlaneAxis(glm::vec3(*plane));
                const srVector3i* poly_vertex = model->getPolyVertex() + polygon;
                const glm::vec3* vertices = model->getVertexLoc();
                const RuntimeTriangle triangle{vertices[poly_vertex->x],
                                               vertices[poly_vertex->y],
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
w8_ulong* W8Octree::CollectPolygonsNearPoint(srVector3T<float>* center, float radius,
                                                  float height)
{
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_polygon_bits, false);
    std::ranges::fill(m_current_regions, false);

    const auto position = center->glmValue();
    const srBoundingBox box{position - glm::vec3(radius, height, radius),
                            position + glm::vec3(radius, 0, radius)};
    const auto start = cellAt(box.minimum);
    const auto end = cellAt(box.maximum);
    glm::ivec3 cell;
    for (cell.x = start.x; cell.x <= end.x; ++cell.x) {
        for (cell.y = start.y; cell.y <= end.y; ++cell.y) {
            for (cell.z = start.z; cell.z <= end.z; ++cell.z) {
                ProbeCellForBlockersAppend(cell);
            }
        }
    }

    unsigned int index;
    for (index = 0; index < m_gd_result_count; ++index) {
        unsigned int mesh_index = m_aulGDObjs[index] >> 0x10;
        stMeshModel* model = 0;
        const glm::vec4* plane = 0;
        if (mesh_index < m_meshCount && g_world->psrMeshes[mesh_index] != 0 &&
            !TestBit(m_alpha_bits, mesh_index)) {
            model = static_cast<stMeshModel*>(g_world->psrMeshes[mesh_index]->getModel());
            unsigned int polygon = m_aulGDObjs[index] & 0xffff;
            plane = model->getPolyEq() + polygon;
            if (plane->y < g_float_half) {
                m_aulGDObjs[index] = 0;
            }
        } else {
            m_aulGDObjs[index] = 0;
        }
        if (m_aulGDObjs[index] != 0) {
            const auto normal = glm::vec3(*plane);
            const srVector3i* poly_vertex = model->getPolyVertex() + (m_aulGDObjs[index] & 0xffff);
            const glm::vec3* vertices = model->getVertexLoc();
            const RuntimeTriangle triangle{vertices[poly_vertex->x],
                                           vertices[poly_vertex->y],
                                           vertices[poly_vertex->z]};
            if (!triangleIntersectsBox(box, triangle, normal)) {
                m_aulGDObjs[index] = 0;
            }
        }
    }

    unsigned int count = 0;
    for (index = 0; index < m_gd_result_count; ++index) {
        unsigned int key = m_aulGDObjs[index];
        if (key != 0) {
            m_aulGDObjs[count] = key;
            ++count;
        }
    }
    m_aulGDObjs[count] = 0;
    if (count != 0) {
        std::sort(m_aulGDObjs, m_aulGDObjs + count);
    }
    return m_aulGDObjs;
}

/* A location the octree cannot resolve, or one whose submesh has no live model
   instance, clears the monster's cached mesh rather than leaving a stale one. */
// FUNCTION: WIZ8 0x0042e540
void W8Octree::UpdateMonsterLocation(unsigned short location_id, const srVector3T<float>* position)
{
    int queue_id = location_id + 1;
    unsigned int monster_list_index;
    W8MonsterInfo* info;
    W8Monster* monster;
    int sector;
    srModelInstance* mesh;
    glm::ivec3 point;

    if (location_id == 0) {
        return;
    }
    monster_list_index = MonsterGetIndexByLocationID(0x4c4, OCTREE_CPP, location_id, true);
    info = MonsterGetScriptPartByLocationIndex(monster_list_index);
    if (info != 0 && info->p3D != 0) {
        monster = info->p3D;
        sector = GetSectorForPosition(position);
        if (sector == 0 || (mesh = g_world->psrMeshes[m_pSubmeshes[sector].mesh]) == 0) {
            monster->sector_mesh = 0;
        } else {
            monster->sector_mesh = mesh;
        }
    }
    point = cellAt(position->glmValue());
    object_registry->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, queue_id, point);
}

/* Store `path` with its extension stripped into m_owned_0c0; the sibling data
   files (.PNT/.LVL/.WGD/.RLK) are then derived from the stem. The constructor
   inlines the same sequence; this out-of-line emission serves the
   preprocessed-file builder. */
// FUNCTION: WIZ8 0x0042cf90
bool W8Octree::SetPathStem(const char* path)
{
    size_t name_length = strlen(path);
    if (name_length == 0) {
        return false;
    }
    if (m_owned_0c0 != 0) {
        free(m_owned_0c0);
    }
    m_owned_0c0 = static_cast<char*>(malloc(name_length + 1));
    if (m_owned_0c0 == 0) {
        return false;
    }
    strcpy(m_owned_0c0, path);
    char* extension = strrchr(m_owned_0c0, '.');
    if (extension != 0 && extension - m_owned_0c0 > static_cast<int>(name_length) - 6) {
        *extension = '\0';
    }
    return true;
}

/* Read one .oct file into a fresh octree.

   ReadOctFile is the recovered name. Counts are checked before allocation,
   and exact-read failures set the load error bit tested by LoadWorld. Members
   retain ownership of partial loads for the normal destructor cleanup.

   A null path builds an empty octree, and a level whose preprocessed files
   cannot be built runs on the LVL file alone with the same error bit set. */
// FUNCTION: WIZ8 0x0042bc10
W8Octree::W8Octree(const char* path, W8GameData** game_data)
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
        if (asset_status > 0 && !BuildPreprocessedFiles(path)) {
            ReportStartupMessage("Cannot find or build current preprocessed files.");
            ReportStartupMessage("Attempting to run with LVL file only -- SOME FEATURES DISABLED.");
            ReportStartupMessage({});
            throw std::runtime_error("Could not build octree assets.");
        }
        auto hOctFile = wiz8::open_file(path);
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
            grid_yz > static_cast<unsigned int>(std::numeric_limits<int>::max()) / header.m_grid_dims.x ||
            header.m_depth >= 31 || header.m_leaf_level > 8 ||
            header.m_prop_count > 0xffff || header.m_particle_count > 0xffff ||
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
            (header.m_region_count > 1 ? std::uint64_t{header.m_region_count} * sizeof(W8OctRegionVolume) : 0) +
            (header.m_submesh_count != 0 ? (std::uint64_t{header.m_submesh_count} + 1) * sizeof(W8OctSubmesh) : 0) +
            std::uint64_t{header.m_path_nodes} * sizeof(W8FilePathNode) +
            3 * sizeof(uiTerminator);
        if (required > remaining) {
            throw std::runtime_error("Octree tables exceed file size.");
        }
        for (const auto count : {std::uint64_t{header.m_polygon_count},
                                 std::uint64_t{header.m_surface_count},
                                 std::uint64_t{header.m_submesh_count} + 1}) {
            const auto whole_words = static_cast<std::uint64_t>(static_cast<float>(count) / 32.0f);
            if (whole_words * 32 > count || count - whole_words * 32 >= 32) {
                throw std::runtime_error("Octree count exceeds BitArray sizing precision.");
            }
        }
        if ((std::uint64_t{header.m_branch_count} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(W8OctPreTreeBranch) ||
            (std::uint64_t{header.m_leaf_count} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(W8OctPreTreeLeaf) ||
            (std::uint64_t{header.m_submesh_count} + 1) > std::numeric_limits<std::size_t>::max() / sizeof(W8OctSubmesh) ||
            (std::uint64_t{header.m_polygon_count} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_leaf_polygon_stream_len} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_gd_surface_stream_len} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(w8_ulong) ||
            (std::uint64_t{header.m_region_list_len} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(unsigned short) ||
            (std::uint64_t{header.m_trigger_count} + 2) > std::numeric_limits<std::size_t>::max() / sizeof(unsigned short)) {
            throw std::runtime_error("Octree allocation exceeds address space.");
        }

        Initialize(&header);
        if (!SetPathStem(path)) {
            throw std::runtime_error("Could not store octree path.");
        }
        m_branches = static_cast<W8OctPreTreeBranch*>(
            malloc((std::size_t{header.m_branch_count} + 2) * sizeof(W8OctPreTreeBranch)));
        m_leaves = static_cast<W8OctPreTreeLeaf*>(
            malloc((std::size_t{header.m_leaf_count} + 2) * sizeof(W8OctPreTreeLeaf)));
        m_polygon_index_stream = static_cast<w8_ulong*>(
            malloc((std::size_t{header.m_leaf_polygon_stream_len} + 2) * sizeof(w8_ulong)));
        m_aulPolyLookup = static_cast<w8_ulong*>(
            malloc((std::size_t{header.m_polygon_count} + 2) * sizeof(w8_ulong)));
        if (!m_branches || !m_leaves || !m_polygon_index_stream || !m_aulPolyLookup) {
            throw std::bad_alloc();
        }
        hOctFile->read_exact(m_branches, std::size_t{header.m_branch_count} * sizeof(W8OctPreTreeBranch));
        hOctFile->read_exact(m_leaves, std::size_t{header.m_leaf_count} * sizeof(W8OctPreTreeLeaf));
        hOctFile->read_exact(m_polygon_index_stream, std::size_t{header.m_leaf_polygon_stream_len} * sizeof(w8_ulong));
        if (grid_count < 250000) {
            m_leaf_lookup = static_cast<w8_ulong*>(malloc(grid_count * sizeof(w8_ulong)));
            if (!m_leaf_lookup) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_leaf_lookup, grid_count * sizeof(w8_ulong));
        }
        hOctFile->read_exact(m_aulPolyLookup, std::size_t{header.m_polygon_count} * sizeof(w8_ulong));
        if (header.m_region_list_len != 0) {
            m_region_index_stream = static_cast<unsigned short*>(
                malloc((std::size_t{header.m_region_list_len} + 2) * sizeof(unsigned short)));
            if (!m_region_index_stream) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_region_index_stream, std::size_t{header.m_region_list_len} * sizeof(unsigned short));
        }
        if (header.m_gd_surface_stream_len != 0) {
            m_gd_surface_index_stream = static_cast<w8_ulong*>(
                malloc((std::size_t{header.m_gd_surface_stream_len} + 2) * sizeof(w8_ulong)));
            if (!m_gd_surface_index_stream) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_gd_surface_index_stream, std::size_t{header.m_gd_surface_stream_len} * sizeof(w8_ulong));
        }
        if (header.m_trigger_count != 0) {
            m_trigger_indices = static_cast<unsigned short*>(
                malloc((std::size_t{header.m_trigger_count} + 2) * sizeof(unsigned short)));
            if (!m_trigger_indices) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_trigger_indices, std::size_t{header.m_trigger_count} * sizeof(unsigned short));
        }
        if (header.m_region_count > 1) {
            m_spatial.m_region_volumes = static_cast<W8OctRegionVolume*>(
                malloc((std::size_t{header.m_region_count} + 2) * sizeof(W8OctRegionVolume)));
            if (!m_spatial.m_region_volumes) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_spatial.m_region_volumes, std::size_t{header.m_region_count} * sizeof(W8OctRegionVolume));
        }
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff) {
            throw std::runtime_error("Invalid octree region terminator.");
        }
        if (header.m_submesh_count != 0) {
            m_pSubmeshes = static_cast<W8OctSubmesh*>(
                malloc((std::size_t{header.m_submesh_count} + 1) * sizeof(W8OctSubmesh)));
            if (!m_pSubmeshes) {
                throw std::bad_alloc();
            }
            hOctFile->read_exact(m_pSubmeshes, (std::size_t{header.m_submesh_count} + 1) * sizeof(W8OctSubmesh));
            unsigned int limit = 0;
            for (std::size_t index = 0; index <= header.m_submesh_count; ++index) {
                limit = std::max(limit, m_pSubmeshes[index].polygon_count);
            }
            if (limit >= 9999) {
                throw std::runtime_error("Octree submesh polygon count exceeds scratch capacity.");
            }
            ++limit;
            g_octree_storage_ = static_cast<unsigned int*>(malloc(std::size_t{limit} * sizeof(unsigned int)));
            if (!g_octree_storage_) {
                throw std::bad_alloc();
            }
            for (unsigned int index = 0; index < limit; ++index) {
                g_octree_storage_[index] = index;
            }
            if (m_meshCount != 0) {
                    if (!LoadPackedBits(hOctFile.get(), m_alpha_bits)) {
                    throw std::runtime_error("Could not read octree alpha bits.");
                }
            }
            if (m_ulNumParticles != 0) {
                m_pusMeshParticleLookup = static_cast<unsigned short*>(
                    malloc((std::size_t{m_meshCount} + 1) * sizeof(unsigned short)));
                m_pusMeshParticles = static_cast<unsigned short*>(
                    malloc(std::size_t{m_usMeshParticlesLen} * sizeof(unsigned short)));
                if (!m_pusMeshParticleLookup || (m_usMeshParticlesLen != 0 && !m_pusMeshParticles)) {
                    throw std::bad_alloc();
                }
                hOctFile->read_exact(m_pusMeshParticleLookup, (std::size_t{m_meshCount} + 1) * sizeof(unsigned short));
                hOctFile->read_exact(m_pusMeshParticles, std::size_t{m_usMeshParticlesLen} * sizeof(unsigned short));
            }
            if (m_ulNumProps != 0) {
                m_pusMeshPropLookup = static_cast<unsigned short*>(
                    malloc((std::size_t{m_meshCount} + 1) * sizeof(unsigned short)));
                m_pusMeshProps = static_cast<unsigned short*>(
                    malloc(std::size_t{m_usMeshPropsLen} * sizeof(unsigned short)));
                if (!m_pusMeshPropLookup || (m_usMeshPropsLen != 0 && !m_pusMeshProps)) {
                    throw std::bad_alloc();
                }
                hOctFile->read_exact(m_pusMeshPropLookup, (std::size_t{m_meshCount} + 1) * sizeof(unsigned short));
                hOctFile->read_exact(m_pusMeshProps, std::size_t{m_usMeshPropsLen} * sizeof(unsigned short));
            }
        }
        hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
        if (uiTerminator != 0xffffffff) {
            throw std::runtime_error("Invalid octree mesh terminator.");
        }
        if (header.m_path_nodes != 0) {
            pathing = new W8PathingService;
            pathing->ConfigureForLevel(header.m_path_nodes, header.m_region_cell,
                                      header.m_path_clearance, header.m_bounds, m_owned_0c0);
            if (!pathing->ReadPathNodes(hOctFile.get())) {
                throw std::runtime_error("Could not read octree path nodes.");
            }
        }
        if (m_ulNumProps != 0 && header.m_prop_sun_bits != 0) {
            if (!LoadPackedBits(hOctFile.get(), m_sunlit_props)) {
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
            *game_data = pGameData.release();
        }
        g_octree = this;
        m_region_links_ready = ReadRegionLinkFile(m_owned_0c0);
        LoadPointFiles(m_owned_0c0);
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

/* Answer whether this level's OCT/PVL/WGD/LVL set is present, current, and
   newer than the raw sources. Negative means skip the octree, zero means the
   set is ready, and positive means the preprocessed files should be rebuilt. */
// FUNCTION: WIZ8 0x0042ccc0
int CheckLevelAssetSet(const char* level_path)
{
    if (g_octree_disabled) {
        return -1;
    }
    std::string copy = level_path;
    TrimAndLowercaseString(copy.data());
    if (strstr(copy.c_str(), "sky") != 0) {
        return -1;
    }
    const auto cd = wiz8::file_status("CD.ROM");
    if (cd && cd->info.type == SDL_PATHTYPE_FILE) {
        return 0;
    }

    std::string stem = level_path;
    if (const auto extension = stem.find_last_of('.'); extension != std::string::npos) {
        stem.resize(extension);
    }
    const auto oct = wiz8::file_status(level_path);
    const auto pvl = wiz8::file_status(stem + ".PVL");
    int rebuild = 1;
    unsigned short version = 0;
    if (oct && oct->info.type == SDL_PATHTYPE_FILE &&
        pvl && pvl->info.type == SDL_PATHTYPE_FILE) {
        try {
            auto file = wiz8::open_file(level_path);
            file->read_exact(&version, sizeof(version));
            rebuild = version < W8OctFileHeader::VERSION;
        } catch (const std::exception&) {
            rebuild = 1;
        }
        if (version > W8OctFileHeader::VERSION) {
            ShutdownWithErrorBox(
                "EXE OUT OF DATE: Program is older than File version--There is a new "
                "executable available.");
        }
    }
    const auto lvl = wiz8::file_status(stem + ".LVL");
    if (!lvl || lvl->info.type != SDL_PATHTYPE_FILE) {
        return -rebuild;
    }
    const auto wgd = wiz8::file_status(stem + ".WGD");
    if (!wgd || wgd->info.type != SDL_PATHTYPE_FILE) {
        ReportStartupMessage(
            "Could not find WGD file. Cannot find or build current preprocessed files.");
        ReportStartupMessage("Attempting to run with LVL file only -- NO COLLISION DATA.");
        ReportStartupMessage({});
        return -rebuild;
    }

    const auto oct_time = oct ? wiz8::file_time_from_sdl(oct->info.modify_time).ticks() : 0;
    const auto pvl_time = pvl ? wiz8::file_time_from_sdl(pvl->info.modify_time).ticks() : 0;
    const auto wgd_time = wiz8::file_time_from_sdl(wgd->info.modify_time).ticks();
    const auto lvl_time = wiz8::file_time_from_sdl(lvl->info.modify_time).ticks();
    constexpr std::uint64_t tolerance = 10 * 10000000;
    if (oct && pvl && oct_time < pvl_time) {
        if (wgd_time <= oct_time || wgd_time - oct_time < tolerance) {
            return lvl_time > oct_time && lvl_time - oct_time >= tolerance ? 1 : rebuild;
        }
    } else if (pvl) {
        if ((wgd_time <= pvl_time || wgd_time - pvl_time < tolerance) &&
            (lvl_time <= pvl_time || lvl_time - pvl_time < tolerance)) {
            return rebuild;
        }
    } else {
        return rebuild;
    }
    return 1;
}

/* Reset the owned tables and query state before applying an OCT header.
   Retail leaves the region masks and positional flag untouched here. */
// FUNCTION: WIZ8 0x0042d040
void W8Octree::Reset()
{
    g_octree_storage_ = 0;
    g_octree_state = 0;
    m_spatial.Reset();
    m_branches = 0;
    m_leaves = 0;
    m_leaf_grid_dimensions.z = 0;
    m_leaf_grid_dimensions.y = 0;
    m_leaf_grid_dimensions.x = 0;
    m_leaf_lookup = 0;
    m_branch_count = 0;
    m_leaf_count = 0;
    m_vertex_count = 0;
    m_leaf_polygon_stream_len = 0;
    m_gd_surface_stream_len = 0;
    m_trigger_count = 0;
    m_region_list_len = 0;
    m_unknown_13c = 0;
    m_polygon_index_stream = 0;
    m_aulPolyLookup = 0;
    m_gd_surface_index_stream = 0;
    m_region_index_stream = 0;
    m_trigger_indices = 0;
    m_pfRegsVisited = 0;
    m_pSubmeshes = 0;
    object_registry = 0;
    m_pRegionLinks = 0;
    m_point_count = 0;
    m_sample_points = 0;
    m_owned_0c0 = 0;
    m_region_cell = 0;
    m_path_clearance = 0;
    pathing = 0;
    m_fAccumulating = false;
    m_pusMeshParticleLookup = 0;
    m_pusMeshParticles = 0;
    m_usMeshParticlesLen = 0;
    m_pusMeshPropLookup = 0;
    m_pusMeshProps = 0;
    m_usMeshPropsLen = 0;
    m_ulNumParticles = 0;
    m_papProps = 0;
    m_papParticles = 0;
    m_usNumPropsLoaded = 0;
    m_usNumParticlesLoaded = 0;
    m_trace_skip_flag = 0;
    m_root_mesh_count = 0;
    m_kind1_submesh_count = 0;
    m_alpha_polygon_count = 0;
    m_meshCount = 0;
    m_gd_result_count = 0;
    m_aulGDObjs = 0;

    memset(&view, 0, sizeof(view));
    /* 0x0042D1F1 then re-zeroes the four bounds inline, which
       Reset above already did through the callee. */
    m_spatial.m_clipped_minimum.SetZero();
    m_spatial.m_clipped_maximum.SetZero();
    m_spatial.m_working_minimum.SetZero();
    m_spatial.m_working_maximum.SetZero();
    m_unknown_27c[0] = 0;
    m_unknown_27c[1] = 0;
    m_unknown_27c[2] = 0;
    m_unknown_27c[3] = 0;
    m_unknown_27c[4] = 0;
    m_unknown_27c[5] = 0;
    m_visibility_suspended = false;
    m_reset_visibility = false;
    m_projected_regions_valid = false;
    m_region_links_ready = false;
    m_region_links_dirty = false;
    m_points_dirty = false;
    m_unknown_299 = 0;
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

        m_unknown_27c[0] = 0;
        m_unknown_27c[1] = 0;
        m_unknown_27c[2] = 0;
        m_unknown_27c[3] = 0;
        m_unknown_27c[4] = 0;
        m_unknown_27c[5] = 0;
        m_spatial.m_minimum = header->m_bounds[0];
        m_spatial.m_maximum = header->m_bounds[1];
        m_spatial.m_clipped_minimum = header->m_bounds[2];
        m_spatial.m_clipped_maximum = header->m_bounds[3];
        m_spatial.m_working_minimum = header->m_bounds[4];
        m_spatial.m_working_maximum = header->m_bounds[5];
        m_leaf_grid_dimensions = header->m_grid_dims;

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
        m_usMeshParticlesLen = header->m_particle_len;
        m_usMeshPropsLen = header->m_prop_len;
        m_ulNumParticles = header->m_particle_count;

        if (m_ulNumParticles != 0) {
            m_linked_particles.assign(m_ulNumParticles, false);
            m_visible_particles.assign(m_ulNumParticles, false);
            m_papParticles = static_cast<stParticle**>(malloc((m_ulNumParticles + 2) * sizeof(*m_papParticles)));
        }
        if (m_ulNumProps != 0) {
            m_linked_props.assign(m_ulNumProps, false);
            m_visible_props.assign(m_ulNumProps, false);
            m_papProps = static_cast<W8Prop**>(malloc((m_ulNumProps + 2) * sizeof(*m_papProps)));
        }

        m_visited_polygon_bits.assign(header->m_polygon_count, false);
        m_projected_regions.assign(header->m_submesh_count + 1, false);
        m_current_regions.assign(header->m_submesh_count + 1, false);
        m_previous_regions.assign(header->m_submesh_count + 1, false);
        m_visited_object_bits.assign(header->m_surface_count < 5000 ? 5000 : header->m_surface_count, false);
        m_accumulated_regions.assign(header->m_submesh_count + 1, false);

        object_registry = new W8OctreeObjectRegistry;
        m_pRegionLinks = new std::unordered_multimap<unsigned int, unsigned short>;
        m_pRegionLinks->clear();

        unsigned int visited_size = header->m_region_id_bound + 1;
        m_pfRegsVisited = static_cast<unsigned char*>(malloc(visited_size));
        if (m_pfRegsVisited == 0) {
            srAssertFail("m_pfRegsVisited", "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp",
                         0x36a, "InitOctree: Couldn't allocate m_pfRegsVisited.");
        }
        memset(m_pfRegsVisited, 0, visited_size);
        m_reset_visibility = true;
    }

    m_aulGDObjs = static_cast<w8_ulong*>(malloc(10000 * sizeof(*m_aulGDObjs)));
    if (m_aulGDObjs == 0) {
        srAssertFail("m_aulGDObjs", "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp", 0x372,
                     "InitOctree: Couldn't allocate m_aulGDObjs.");
    }
    m_spatial.m_level_kind = 3;
    m_fAccumulating = true;
    ToggleUpdateSuspension(0);
}

// FUNCTION: WIZ8 0x0042e440
void W8Octree::AddLoadedProp(W8Prop* prop)
{
    if (m_fAccumulating) {
        if (m_usNumPropsLoaded >= static_cast<unsigned short>(m_ulNumProps)) {
            srAssertFail("m_usNumPropsLoaded<(UINT16)m_ulNumProps",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp", 0x485,
                         "Too many props loaded for Octree");
        }
        m_papProps[m_usNumPropsLoaded] = prop;
        m_usNumPropsLoaded++;
        m_papProps[m_usNumPropsLoaded] = 0;
    }
}

// FUNCTION: WIZ8 0x0042e4c0
void W8Octree::AddLoadedParticle(stParticle* particle)
{
    if (m_fAccumulating) {
        if (m_usNumParticlesLoaded >= static_cast<unsigned short>(m_ulNumParticles)) {
            srAssertFail("m_usNumParticlesLoaded<(UINT16)m_ulNumParticles",
                         "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp", 0x49d,
                         "Too many particles loaded for Octree");
        }
        m_papParticles[m_usNumParticlesLoaded] = particle;
        m_usNumParticlesLoaded++;
        m_papParticles[m_usNumParticlesLoaded] = 0;
    }
}

/* Complete non-deleting teardown. LoadWorld and DestroyWorld both perform the
   matching operator delete only after this method returns. */
// FUNCTION: WIZ8 0x0042de60
W8Octree::~W8Octree()
{
    if (m_region_links_dirty) {
        SaveRegionLinks(m_owned_0c0);
    }
    if (m_points_dirty) {
        SavePoints(m_owned_0c0);
    }
    if (pathing != 0) {
        pathing->SaveWaypointSnapshot(false);
    }

    free(m_aulGDObjs);
    if (m_branches != 0) {
        free(m_branches);
    }
    if (m_leaves != 0) {
        free(m_leaves);
    }
    if (m_polygon_index_stream != 0) {
        free(m_polygon_index_stream);
    }
    if (m_aulPolyLookup != 0) {
        free(m_aulPolyLookup);
    }
    if (m_leaf_lookup != 0) {
        free(m_leaf_lookup);
    }
    if (m_gd_surface_index_stream != 0) {
        free(m_gd_surface_index_stream);
    }
    if (m_region_index_stream != 0) {
        free(m_region_index_stream);
    }
    if (m_trigger_indices != 0) {
        free(m_trigger_indices);
    }

    if (m_spatial.m_region_volumes != 0) {
        free(m_spatial.m_region_volumes);
    }

    if (m_pRegionLinks != 0) {
        m_pRegionLinks->clear();
        delete m_pRegionLinks;
        m_pRegionLinks = 0;
    }

    if (m_pfRegsVisited != 0) {
        free(m_pfRegsVisited);
    }

    if (m_pusMeshParticleLookup != 0) {
        free(m_pusMeshParticleLookup);
    }
    m_pusMeshParticleLookup = 0;
    if (m_pusMeshParticles != 0) {
        free(m_pusMeshParticles);
    }
    m_pusMeshParticles = 0;
    if (m_pusMeshPropLookup != 0) {
        free(m_pusMeshPropLookup);
    }
    m_pusMeshPropLookup = 0;
    if (m_pusMeshProps != 0) {
        free(m_pusMeshProps);
    }
    m_pusMeshProps = 0;
    if (m_papProps != 0) {
        free(m_papProps);
    }
    if (m_papParticles != 0) {
        free(m_papParticles);
    }

    m_point_count = 0;
    if (m_sample_points != 0) {
        delete[] m_sample_points;
    }
    if (m_owned_0c0 != 0) {
        free(m_owned_0c0);
    }

    delete object_registry;

    if (m_pSubmeshes != 0) {
        free(m_pSubmeshes);
    }
    if (g_octree_storage_ != 0) {
        free(g_octree_storage_);
    }
    delete pathing;
    pathing = 0;
    if (g_octree_trace_node != 0) {
        delete g_octree_trace_node;
        g_octree_trace_node = 0;
    }
    g_octree_storage_ = 0;
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
void W8Octree::VisitPointCopy(unsigned short location_id, srVector3T<float>* position)
{
    srVector3T<float> copy;

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

template<class Visit>
static void ForEachQueryCell(const glm::ivec3& start, const glm::ivec3& end,
                             const glm::ivec3& dimensions, Visit visit)
{
    const auto lower = glm::max(start, glm::ivec3(0));
    const auto upper = glm::min(end, dimensions - 1);
    glm::ivec3 cell;
    for (cell.x = lower.x; cell.x <= upper.x; ++cell.x)
        for (cell.y = lower.y; cell.y <= upper.y; ++cell.y)
            for (cell.z = lower.z; cell.z <= upper.z; ++cell.z)
                visit(cell);
}

/* Query the expanded segment box and own its ordered, deduplicated IDs. */
// FUNCTION: WIZ8 0x0042ed60
std::vector<w8_ulong> W8Octree::queryAlongSegment(const glm::vec3& origin,
                                                const glm::vec3& delta, float extent,
                                                unsigned short kind)
{
    const auto bounds = srSegmentMath::sweptBounds(origin, delta, extent);
    const auto lower = cellCoordinates(bounds.minimum).cell;
    const auto upper = cellCoordinates(bounds.maximum).cell;
    g_octree_state = m_aulGDObjs;
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    ForEachQueryCell(lower, upper, glm::ivec3(m_leaf_grid_dimensions.glmValue()),
        [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind); });
    g_octree_state = nullptr;
    return {m_aulGDObjs, m_aulGDObjs + m_gd_result_count};
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
unsigned char W8Octree::TestBoxOccupied(const srVector3T<float>* lower,
                                        const srVector3T<float>* upper)
{
    auto objects = queryObjects({lower->glmValue(), upper->glmValue()}, W8_OCTREE_KIND_SURFACE, -1);
    unsigned int count = objects.size();
    unsigned int index;
    for (index = 0; index < count; ++index) {
        W8GDSurface* surface = g_octree_game_data->m_pSurfaces.data() + objects[index];
        if (SurfaceIntersectsBox(*surface, g_octree_game_data->m_pVertices,
                                  {lower->glmValue(), upper->glmValue()})) {
            return 1;
        }
    }
    objects = queryObjects({lower->glmValue(), upper->glmValue()}, W8_OCTREE_KIND_LOCATION, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        unsigned int monster_index = MonsterGetIndexByLocationID(
            0x62d, "C:\\Projects\\Wizardry 8\\Engine Code\\Octree.cpp", objects[index], true);
        W8MonsterInfo* info = MonsterGetScriptPartByLocationIndex(monster_index);
        if (info != 0 && info->p3D != 0) {
            srVector3T<float> position = info->p3D->GetPosition();
            float radius = info->p3D->radius;
            if (lower->x - radius < position.x && position.x < radius + upper->x &&
                lower->y - radius < position.y && position.y < radius + upper->y &&
                lower->z - radius < position.z && position.z < radius + upper->z) {
                return 1;
            }
        }
    }
    objects = queryObjects({lower->glmValue(), upper->glmValue()}, W8_OCTREE_KIND_PROP, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        W8Prop* prop = g_world->collidable_props[objects[index]];
        if (prop->GetActivationState() != 0 && prop->m_gd_prop != 0) {
            GDProp* gd_prop = prop->m_gd_prop;
            for (int surface_index = 0; surface_index < gd_prop->m_surface_count; ++surface_index) {
                W8GDSurface* surface = gd_prop->m_pGDSurfaces + surface_index;
                if (SurfaceIntersectsBox(*surface,
                        {gd_prop->m_pVertices, static_cast<std::size_t>(gd_prop->m_vertex_count)},
                        {lower->glmValue(), upper->glmValue()})) {
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
std::vector<w8_ulong> W8Octree::queryObjects(const srBoundingBox& bounds,
                                           unsigned short kind, int excluded)
{
    const auto start = cellAt(bounds.minimum);
    const auto end = cellAt(bounds.maximum);
    g_octree_state = m_aulGDObjs;
    m_gd_result_count = 0;
    std::ranges::fill(m_visited_object_bits, false);
    if (excluded >= 0) SetBit(m_visited_object_bits, excluded);
    ForEachQueryCell(start, end, glm::ivec3(m_leaf_grid_dimensions.glmValue()),
        [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind); });
    g_octree_state = nullptr;
    return {m_aulGDObjs, m_aulGDObjs + m_gd_result_count};
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
        if (m_leaf_lookup == 0) {
            leaf_index = FindLeaf(cell);
        } else {
            leaf_index = m_leaf_lookup[m_spatial.m_leaf_grid_stride_y * cell.y +
                                       m_spatial.m_leaf_grid_stride_x * cell.x + cell.z];
        }
        if (leaf_index != 0 && m_leaves[leaf_index].gd_polygon_offset != 0) {
            const w8_ulong* stream =
                m_gd_surface_index_stream + m_leaves[leaf_index].gd_polygon_offset;
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
        g_octree_query_slot = object_registry->by_cell->end();
        slot = NextIndexValue(*object_registry->by_cell, g_octree_query_cell, g_octree_query_slot);
        g_octree_query_slot = slot;
        while (slot != object_registry->by_cell->end() && m_gd_result_count < 10000) {
            unsigned int packed = static_cast<unsigned int>(slot->second);
            g_octree_query_id = static_cast<unsigned short>(packed);
            const unsigned short entry_kind = OctreeKeyKind(packed);
            const bool matches = kind > W8_OCTREE_KIND_LOCATION
                ? (entry_kind == W8_OCTREE_KIND_LOCATION || entry_kind == W8_OCTREE_KIND_NAVIGATOR)
                : entry_kind == kind;
            if (matches && !SetBit(m_visited_object_bits, OctreeKeyId(packed) - 1)) {
                g_octree_state[m_gd_result_count] = g_octree_query_id - 1;
                ++m_gd_result_count;
            }
            slot = NextIndexValue(*object_registry->by_cell, g_octree_query_cell, g_octree_query_slot);
            g_octree_query_slot = slot;
        }
        break;
    }
    return found;
}

/* Register the navigator's world position in the spatial cell registry.
   Navigator IDs are stored with a one-based bias. */
// FUNCTION: WIZ8 0x0042e810
void W8Octree::RegisterNavigatorCell(int id, const srVector3T<float>* position)
{
    glm::ivec3 point;

    point = cellAt(position->glmValue());
    object_registry->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, id + 1, point);
}

/* Quantize the world point relative to the spatial minimum. This preserves
   truncation toward zero, including points below the minimum. */
// FUNCTION: WIZ8 0x00431440
glm::ivec3 W8Octree::cellAt(const glm::vec3& position) const
{
    return glm::ivec3((position - m_spatial.m_minimum.glmValue()) / m_spatial.m_node_extent);
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
    srVector3T<float> vecDir;
    srVector3T<float> vecPos;
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
    distance = glm::length(((vecDir.xz())));
    step = g_game_time_accumulator->GetFrameDelta() * movement->movement_scale * g_rate *
           g_world_scale;
    if (step >= distance) {
        step = distance;
    } else {
        reached = false;
    }
    if ((vecDir.x * vecDir.x + vecDir.z * vecDir.z) != g_double_zero) {
        vecDir.SetLength(step);
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
void W8Octree::AdjustPortalDestination(srVector3T<float>* destination,
                                       const srVector3T<float>* source)
{
    if (!pathing || (!pathing->m_waypoint_editing && pathing->m_ulNumWayPoints != 0))
        return;
    const auto source_point = source->glmValue();
    const auto local_destination = settleToGround(destination->glmValue(), true, 500).position;
    const auto local_source = settleToGround(source_point, true, 500).position;
    const srVector3 destination_record(local_destination), source_record(local_source);
    pathing->EditTeleportalLink(&destination_record, &source_record);
}

/* Release the location-variable names and empty their parallel value and
   level vectors. Trigger.cpp creates the names as copied character arrays. */

// FUNCTION: WIZ8 0x004372E0
bool WriteVector4Array(wiz8::File* file, const glm::vec4* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->write(values, static_cast<std::size_t>(count) * sizeof(glm::vec4));
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00437390
bool WriteVector3Array(wiz8::File* file, const srVector3T<float>* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->write(values, static_cast<std::size_t>(count) * sizeof(srVector3T<float>));
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00437430
bool WriteVector2Array(wiz8::File* file, const glm::vec2* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->write(values, static_cast<std::size_t>(count) * sizeof(glm::vec2));
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004374C0
bool ReadVector4Array(wiz8::File* file, glm::vec4* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->read_exact(values, static_cast<std::size_t>(count) * sizeof(glm::vec4));
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x004374E0
bool ReadVector3Array(wiz8::File* file, void* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->read_exact(values, static_cast<std::size_t>(count) * sizeof(srVector3T<float>));
    return true;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x00437510
bool ReadVector2Array(wiz8::File* file, glm::vec2* values, int count)
try
{
    if (count < 0) {
        return false;
    }
    file->read_exact(values, static_cast<std::size_t>(count) * sizeof(glm::vec2));
    return true;
}
catch (const std::exception&) { return false; }

/* Grow `minimum`/`maximum` to include `point`, returning whether any bound
   moved. Retail's second comparison tests point->z where point->y is meant
   (0x0043790F FCOMPs [ECX+8]) - the proven quirk stays. */
// FUNCTION: WIZ8 0x004378f0
char GrowBoundsByPoint(const srVector3T<float>* point, srVector3T<float>* minimum,
                       srVector3T<float>* maximum)
{
    bool changed = false;
    if (minimum->x > point->x) {
        minimum->x = point->x;
        changed = true;
    }
    if (minimum->y > point->z) {
        minimum->y = point->y;
        changed = true;
    }
    if (minimum->z > point->z) {
        minimum->z = point->z;
        changed = true;
    }
    if (maximum->x < point->x) {
        maximum->x = point->x;
        changed = true;
    }
    if (maximum->y < point->y) {
        maximum->y = point->y;
        changed = true;
    }
    if (maximum->z < point->z) {
        maximum->z = point->z;
        return 1;
    }
    return changed;
}

std::vector<w8_ulong> W8Octree::nearbyLocations(const glm::vec3& position, float spacing)
{
    const float expand = spacing * g_monster_proximity_radius_scale;
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
        const auto delta = monster->GetPosition().glmValue() - point;
        const float clearance =
            (alternate_radius ? monster->movement.alternate_radius : monster->radius) + reach;
        if (glm::dot(delta, delta) < clearance * clearance) return true;
    }
    return false;
}

// FUNCTION: WIZ8 0x00437980
unsigned int W8Octree::FindScatterPositions(const srVector3T<float>* position, float yaw,
                                            float spacing, unsigned int count,
                                            srVector3T<float>* positions, bool proximity_check,
                                            bool flatten_y)
{
    float source_y = position->y;
    unsigned int found = 0;
    std::vector<w8_ulong> candidates;
    unsigned int columns = 3;
    if (count > 10) {
        columns = 5;
    }
    float angle = NormalizeAngle(yaw + g_monster_rotation_offset);
    double cos_angle = cos(angle);
    srVector3T<float> source;
    source = *position;
    double sin_angle = sin(angle);
    float cos_step = spacing * cos_angle;
    float sin_step = spacing * sin_angle;
    float neg_cos_step = -cos_step;
    if (proximity_check) {
        candidates = nearbyLocations(position->glmValue(), spacing);
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
                    jitter_column = g_float_zero;
                } else {
                    jitter_ring = Random(1000) * g_scatter_outer_ring_jitter_scale -
                                  g_generator_jitter_fraction;
                    jitter_column = Random(1000) * g_scatter_outer_ring_jitter_scale -
                                    g_generator_jitter_fraction;
                }
                srVector3T<float> candidate;
                candidate.x = sin_step * (ring + jitter_ring) + source.x +
                              neg_cos_step * (jitter_column + *column_offset);
                candidate.y = source.y + g_world_scale;
                candidate.z = (ring + jitter_ring) * cos_step + source.z +
                              (jitter_column + *column_offset) * sin_step;
                float ground = probeGround(candidate.glmValue()).position.y;
                float height = candidate.y - ground;
                if (g_double_one <= fabsf(height) &&
                    fabsf(height) <= g_navigator_vertical_snap_limit &&
                    (flatten_y || fabsf(height) <= g_double_twenty_five_hundred)) {
                    candidate.y = ground;
                    if (!proximity_check) {
                        if (pathing->SnapWaypointPosition(&candidate, false)) {
                            goto accept;
                        }
                    } else {
                        float reach = spacing * g_float_half;
                        if (pathing->TestPathCellClearance(&candidate, reach, false)) {
                            srVector3T<float> navigator = g_startup_world->GetPosition();
                            const auto delta = navigator.glmValue() - candidate.glmValue();
                            float separation = g_startup_world->radius + reach;
                            if (separation * separation <= glm::dot(delta, delta)) {
                                if (OverlapsNearbyMonster(candidate.glmValue(), reach, candidates, false))
                                    goto next_cell;
                                goto accept;
                            }
                        }
                    }
                    goto next_cell;
                accept:
                    if (found == 0) {
                        positions[0] = candidate;
                        source = candidate;
                        found = 1;
                    } else if (pathing == 0 ||
                               pathing->TestWaypointSpan(&candidate, &source, false, false)) {
                        positions[found] = candidate;
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
    return found;
}

// FUNCTION: WIZ8 0x00437f30
unsigned int W8Octree::FindNavigatorPosition(srVector3T<float>* source, float yaw, float radius,
                                             unsigned int count, srVector3T<float>* positions,
                                             bool first_only, bool settle_any_height,
                                             bool avoid_triggers, int mode,
                                             bool require_waypoint_span)
{
    unsigned int found = 0;
    int found_i = 9999;
    int found_j = 9999;
    float source_y = source->y;
    std::vector<w8_ulong> candidates;
    float camera_radius = g_startup_world->movement.alternate_radius;
    bool placed = false;
    float separation = (CalcRangeDistance(W8_RANGE_TOUCH) + radius) * g_float_half + camera_radius;
    float angle = NormalizeAngle(yaw + g_monster_rotation_offset);
    double cos_angle = cos(angle);
    double sin_angle = sin(angle);
    float cos_radius = radius * cos_angle;
    float sin_radius = radius * sin_angle;
    float neg_cos_radius = -cos_radius;
    source->y += g_world_scale;
    float ground = probeGround(source->glmValue()).position.y;
    float height = source->y - ground;
    if (g_double_one <= fabsf(height) && fabsf(height) <= g_navigator_vertical_snap_limit &&
        (settle_any_height || fabsf(height) <= g_double_twenty_five_hundred)) {
        source->y = ground;
    }
    if (first_only) {
        candidates = nearbyLocations(source->glmValue(), radius);
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
                                jitter_j = g_float_zero;
                            } else {
                                jitter_i = Random(1000) * g_scatter_inner_ring_jitter_scale -
                                           g_float_one_tenth;
                                jitter_j = Random(1000) * g_scatter_inner_ring_jitter_scale -
                                           g_float_one_tenth;
                            }
                            srVector3T<float> candidate;
                            candidate.x = sin_radius * (i + jitter_i) + source->x +
                                          neg_cos_radius * (jitter_j + j);
                            candidate.y = source->y + g_world_scale;
                            candidate.z = (i + jitter_i) * cos_radius + source->z +
                                          (jitter_j + j) * sin_radius;
                            ground = probeGround(candidate.glmValue()).position.y;
                            height = candidate.y - ground;
                            if (g_double_one <= fabsf(height) &&
                                fabsf(height) <= g_navigator_vertical_snap_limit &&
                                (settle_any_height ||
                                 fabsf(height) <= g_double_twenty_five_hundred)) {
                                candidate.y = ground;
                                unsigned char clear;
                                if (!first_only) {
                                    clear = pathing->SnapWaypointPosition(&candidate, false);
                                } else {
                                    clear = pathing->TestPathCellClearance(
                                        &candidate, radius * g_float_half, false);
                                }
                                if (clear != 0 && (!avoid_triggers ||
                                                   !InsideDestinationTrigger(
                                                       candidate.x, candidate.y, candidate.z))) {
                                    if (first_only) {
                                        srVector3T<float> camera = g_startup_world->GetPosition();
                                        const auto delta = camera.glmValue() - candidate.glmValue();
                                        if (glm::dot(delta, delta) < separation * separation) {
                                            goto next_cell;
                                        }
                                        if (OverlapsNearbyMonster(candidate.glmValue(), radius * g_float_half,
                                                                  candidates, true))
                                            goto next_cell;
                                    }
                                    if (!require_waypoint_span || pathing == 0 ||
                                        pathing->TestWaypointSpan(&candidate, source, false,
                                                                  false)) {
                                        if (found == 0) {
                                            placed = true;
                                            found_i = i;
                                            found_j = j;
                                        }
                                        positions[found] = candidate;
                                        ++found;
                                    } else if (found != 0) {
                                        unsigned int k = 0;
                                        srVector3T<float>* existing = positions;
                                        do {
                                            if (pathing->TestWaypointSpan(&candidate, existing,
                                                                          false, false)) {
                                                positions[found] = candidate;
                                                ++found;
                                                break;
                                            }
                                            ++k;
                                            ++existing;
                                        } while (k < found);
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
    if (settle_any_height && 0 < static_cast<int>(found)) {
        srVector3T<float>* out = positions;
        unsigned int remaining = found;
        do {
            out->y = source_y;
            ++out;
            --remaining;
        } while (remaining != 0);
    }
    return found;
}
