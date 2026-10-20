#include "wiz8/spatial/level_spatial_index.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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

#include <glm/common.hpp> // NOLINT(misc-include-cleaner)
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/vector_relational.hpp> // NOLINT(misc-include-cleaner)

#include "asset_paths.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_int3_sized.hpp"
#include "ground_probe.h"
#include "index_algorithms.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/geometry.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/math.h"
#include "wiz8/monster_generators.h"
#include "wiz8/octree_walk.h"
#include "wiz8/packed_bits.h"
#include "wiz8/placement_math.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compiled_mesh.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/plane_math.h"
#include "wiz8/render/quality.h"
#include "wiz8/render/segment_math.h"
#include "wiz8/render/swept_bounds.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/retail_geometry_records.h"
#include "wiz8/retail_geometry_types.h"
#include "wiz8/retail_text_records.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/mesh_links.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/spatial/octree_geometry.h"
#include "wiz8/spatial/surface_index.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

constexpr float kMonsterProximityRadiusScale = 15.0f;
constexpr float kRegionAxisAlignmentThreshold = 0.9998999834060669f;
constexpr float kScatterInnerRingJitterScale = 0.00020000000949949026f;
constexpr float kScatterOuterRingJitterScale = 0.00040000001899898052f;

glm::vec3 LevelSpatialIndex::pathSurfaceNormal(const glm::vec3& position) const
{
    return pathing ? pathing->pathSurfaceNormal(position) : glm::vec3(0.0f, 1.0f, 0.0f);
}

static const float NAVIGATOR_MINIMUM_HORIZONTAL_DISTANCE = 50.0f;

void W8OctreeView::captureCamera(const Camera3D& camera)
{
    camera_location = glm::vec3(camera.transform.getLocation());
    camera_dof = glm::vec3(camera.transform.forward());
    const auto rotation = camera.transform.getRotation<glm::mat3>();
    rotation_column0 = rotation[0];
    rotation_column1 = rotation[1];
    horizontal_fov = static_cast<float>(camera.getHorizontalFOV());
    vertical_fov = static_cast<float>(camera.getVerticalFOV());
    horizontal_fov_cosine = static_cast<float>(cos(horizontal_fov));
}

MeshInstance* LevelSpatialIndex::meshForRegion(std::size_t region) const
{
    if (!owner_world || region >= m_pSubmeshes.size()) return nullptr;
    const auto found = registry.region_meshes.find(region);
    const auto index = found != registry.region_meshes.end()
                           ? found->second
                           : static_cast<std::size_t>(m_pSubmeshes[region].mesh);
    return index < owner_world->psrMeshes.size() ? owner_world->psrMeshes[index] : nullptr;
}

void LevelSpatialIndex::UpdateCameraVisibility()
{
    World* world = owner_world;
    if (registry.visibility_suspended) {
        return;
    }

    registry.view.far_clip = static_cast<float>(WorldGetFarClip(world));
    registry.view.captureCamera(*world->camera);
    std::ranges::fill(registry.projected_regions, false);
    UpdateVisibility();
}

void LevelSpatialIndex::UpdateVisibility()
{
    unsigned short mesh_index;
    unsigned int index;
    if (registry.reset_visibility) {
        registry.reset_visibility = false;
        for (mesh_index = 0; mesh_index < m_meshCount; ++mesh_index) {
            if (owner_world->psrMeshes[mesh_index] != 0) {
                registry.region_meshes[owner_world->psrMeshes[mesh_index]->mesh_index + 1] =
                    mesh_index;
                MeshInstance* mesh = static_cast<MeshInstance*>(owner_world->psrMeshes[mesh_index]);
                if (mesh != 0) {
                    mesh->visible = false;
                    mesh->draw_parts = false;
                }
            }
        }
        for (index = 0; index < registry.props.size(); ++index) {
            registry.props[index]->SetActivationState(false);
        }
        for (const auto id : m_prop_links.alwaysVisible())
            registry.props[id - 1]->SetActivationState(true);
        for (index = 0; index < registry.particles.size(); ++index) {
            registry.particles[index]->SetTraversalEnabled(false);
        }
        for (const auto id : m_particle_links.alwaysVisible())
            registry.particles[id - 1]->SetTraversalEnabled(true);
        std::ranges::fill(registry.previous_regions, false);
        std::ranges::fill(registry.accumulated_regions, false);
        if (m_ulNumParticles != 0) {
            std::ranges::fill(registry.visible_particles, false);
        }
        if (m_ulNumProps != 0) {
            std::ranges::fill(registry.visible_props, false);
        }
    }

    std::ranges::fill(registry.current_regions, false);
    std::ranges::fill(registry.projected_regions, false);
    if (m_ulNumParticles != 0) {
        std::ranges::fill(registry.linked_particles, false);
    }
    if (m_ulNumProps != 0) {
        std::ranges::fill(registry.linked_props, false);
    }
    registry.projected_regions_valid = false;
    CollectVisibleCells();
    for (std::size_t region = 0; region < registry.current_regions.size(); ++region) {
        if (registry.projected_regions_valid && !registry.projected_regions[region]) {
            registry.current_regions[region] = false;
        }
        if (registry.current_regions[region]) {
            registry.accumulated_regions[region] = true;
        }
    }
    for (std::size_t region = 1; region < registry.previous_regions.size(); ++region) {
        if (registry.previous_regions[region] && !registry.current_regions[region]) {
            MeshInstance* mesh = meshForRegion(region);
            if (mesh != 0) {
                mesh->visible = false;
                mesh->draw_parts = false;
            }
        }
    }
    for (std::size_t region = 0; region < registry.current_regions.size(); ++region) {
        if (!registry.current_regions[region]) {
            continue;
        }
        if (!registry.previous_regions[region] && region != 0) {
            MeshInstance* mesh = meshForRegion(region);
            if (mesh != 0) {
                mesh->visible = true;
                mesh->draw_parts = true;
            }
        }
        MarkMeshLinksVisible(static_cast<unsigned int>(region));
    }
    if (m_ulNumParticles != 0) {
        for (const auto id : m_particle_links.alwaysVisible())
            SetBit(registry.linked_particles, id - 1);
        for (std::size_t particle = 0; particle < registry.linked_particles.size(); ++particle) {
            if (registry.linked_particles[particle] && !registry.visible_particles[particle]) {
                registry.particles[particle]->SetTraversalEnabled(true);
            } else if (!registry.linked_particles[particle] &&
                       registry.visible_particles[particle]) {
                registry.particles[particle]->SetTraversalEnabled(false);
            }
        }
        registry.visible_particles = registry.linked_particles;
    }
    if (m_ulNumProps != 0) {
        for (const auto id : m_prop_links.alwaysVisible()) SetBit(registry.linked_props, id - 1);
        for (std::size_t prop = 0; prop < registry.linked_props.size(); ++prop) {
            if (registry.linked_props[prop] && !registry.visible_props[prop]) {
                registry.props[prop]->SetActivationState(true);
            } else if (!registry.linked_props[prop] && registry.visible_props[prop]) {
                registry.props[prop]->SetActivationState(false);
            }
        }
        registry.visible_props = registry.linked_props;
    }
    registry.previous_regions = registry.current_regions;
}

/* Collect mesh and prop instances whose world-space bounds intersect the query sphere.
   The grid and pre-generated frusta supply candidate regions. */
std::vector<MeshInstance*> LevelSpatialIndex::modelsNearPoint(const glm::vec3& center,
                                                              float radius) const
{
    std::vector<MeshInstance*> out;
    if (!(radius >= 0) || glm::any(glm::isnan(center)) || glm::any(glm::isinf(center)) ||
        m_spatial.m_region_cells_per_axis == 0 || !(m_spatial.m_region_grid_cell > 0))
        return out;
    const auto relative = (center - m_spatial.m_bounds.minimum) / m_spatial.m_region_grid_cell;
    const float reach = radius / m_spatial.m_region_grid_cell;
    const glm::vec3 limit(m_spatial.m_region_cells_per_axis - 1);
    const glm::ivec3 first(glm::clamp(glm::floor(relative - reach) - 1.0f, glm::vec3(0), limit));
    const glm::ivec3 last(glm::clamp(glm::floor(relative + reach) + 1.0f, glm::vec3(0), limit));
    const auto intersects = [&](MeshInstance* instance) {
        if (!instance || !instance->getModel()) return false;
        const auto bounds = instance->getModel()->getBounds().box;
        const auto world = instance->transform.matrix<glm::mat4>();
        return bounds.transformed(world).intersectsSphere(center, radius);
    };
    const unsigned int level_mask = m_region_mask;
    std::vector<bool> regions(m_pSubmeshes.size(), false);
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
                SetBit(regions, region);
            }
        }
    });

    unsigned int region;
    for (region = 1; region < m_spatial.m_region_count; ++region) {
        if (SphereInsideFrustum(center, radius, m_spatial.m_region_volumes[region].m_planes) &&
            m_spatial.m_region_volumes[region].m_region_bit != 0) {
            SetBit(regions, m_spatial.m_region_volumes[region].m_region_bit);
        }
    }

    for (unsigned int submesh = 0; submesh < regions.size(); ++submesh) {
        if (regions[submesh] && submesh != 0) {
            MeshInstance* instance = meshForRegion(submesh);
            if (intersects(instance)) out.push_back(instance);
        }
    }

    return out;
}

/* Add every particle and prop linked to one mesh to the live visibility sets.

   The lookup gives a 1-based start into a packed link table; each run is
   terminated by a zero. A linked entry whose slot is empty is skipped. */
void LevelSpatialIndex::MarkMeshLinksVisible(unsigned int mesh)
{
    if (mesh > m_meshCount) return;
    m_particle_links.markVisible(mesh, std::span<ParticleSystem* const>(registry.particles),
                                 registry.linked_particles);
    m_prop_links.markVisible(mesh, std::span<Prop* const>(registry.props), registry.linked_props);
}

/* Expand the camera box over the spatial levels and collect the regions the
   camera can occupy.

   The camera cell is quantized twice, once against the node extent and once
   against the leaf cell size. The walk keeps the camera inside an expanding
   box while the node extent shrinks per level; the deepest level looks the
   camera cell up in the region-link table and adds every linked region to the
   projected set. The leaf the walk ends on contributes its region list through
   ProjectLinkedRegionsForLocation. */
short LevelSpatialIndex::ProjectLinkedRegionsForLocation(glm::vec3* location,
                                                         unsigned short* region_list)
{
    if (!m_region_links_ready) {
        return 0;
    }

    short match_count = 0;

    if (region_list != 0 && region_list[0] != 0) {
        for (unsigned short* cursor = region_list; *cursor != 0; ++cursor) {
            unsigned short region_index = *cursor;

            W8OctRegionVolume* volume = &m_spatial.m_region_volumes[region_index];
            if (!PointInsideFrustum((*location), volume->m_planes)) {
                continue;
            }
            if (match_count == 0) {
                std::ranges::fill(registry.projected_regions, false);
                registry.projected_regions_valid = false;
            }
            ++match_count;
            unsigned int key = region_index;
            for (auto slot = m_region_links.equal_range(key).first; slot != m_region_links.end();
                 slot = NextIndexValue(m_region_links, key, slot)) {
                SetBit(registry.projected_regions, slot->second);
                registry.projected_regions_valid = true;
            }
        }
        if (match_count != 0) {
            return match_count;
        }
    }

    if (m_spatial.m_region_count < 2) {
        return 0;
    }

    for (unsigned short region_index = 1; region_index < m_spatial.m_region_count; ++region_index) {
        if (match_count != 0) {
            break;
        }
        W8OctRegionVolume* volume = &m_spatial.m_region_volumes[region_index];
        if (!PointInsideFrustum((*location), volume->m_planes)) {
            continue;
        }

        std::ranges::fill(registry.projected_regions, false);
        registry.projected_regions_valid = false;
        match_count = 1;
        unsigned int key = region_index;
        for (auto slot = m_region_links.equal_range(key).first; slot != m_region_links.end();
             slot = NextIndexValue(m_region_links, key, slot)) {
            SetBit(registry.projected_regions, slot->second);
            registry.projected_regions_valid = true;
        }
    }

    if (match_count == 0) {
        return 0;
    }
    return match_count;
}

unsigned int LevelSpatialIndex::GetSectorForPosition(const glm::vec3* position) const
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

OctreeCellCoordinates LevelSpatialIndex::cellCoordinates(const glm::vec3& location) const
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

OctreeCellCoordinates LevelSpatialIndex::projectRegionsAt(const glm::vec3& position)
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
            auto slot = m_region_links.equal_range(key).first;
            while (slot != m_region_links.end()) {
                SetBit(registry.projected_regions, slot->second);
                slot = NextIndexValue(m_region_links, key, slot);
                registry.projected_regions_valid = true;
            }
        }
        if (node != 0) {
            node = static_cast<int>(m_branches[node].children[child_index]);
        }
        span *= 0.5f;
    }
    if (node != 0) {
        std::uint32_t region_offset = m_leaves[node].region_offset;
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
void LevelSpatialIndex::MarkVisibleRegions()
{
    float radius = registry.view.far_clip;
    float radius_squared = radius * radius;

    for (int index = 1; index < m_spatial.m_region_count; ++index) {
        W8OctRegionVolume* volume = &m_spatial.m_region_volumes[index];

        if (registry.projected_regions_valid &&
            !TestBit(registry.projected_regions, volume->m_region_bit)) {
            continue;
        }
        const auto delta = registry.view.camera_location - volume->m_points[0];

        if (glm::dot(delta, delta) >= radius_squared) {
            continue;
        }
        bool visible =
            planeMath::containsSphere(registry.view.frustum_planes, volume->m_points[0], 0);

        for (int point = 1; !visible && point < 9; ++point) {
            visible =
                planeMath::containsSphere(registry.view.frustum_planes, volume->m_points[point], 0);
        }
        if (visible && volume->m_region_bit != 0) {
            SetBit(registry.current_regions, volume->m_region_bit);
        }
    }
}

/* Build the four side frustum planes from the camera basis and far clip, and
   accumulate the two far-plane offsets the region projection reads. */
void LevelSpatialIndex::BuildFrustumPlanes()
{
    float fov = registry.view.horizontal_fov * 0.5f;
    float extent = m_spatial.m_max_region_radius;
    float far_clip = registry.view.far_clip;
    float backoff = extent / sin(fov);
    float ratio = extent / cos(fov);
    double tangent = tan(fov);
    double tangent_vertical = tan(registry.view.vertical_fov * 0.5f);
    const auto location = registry.view.camera_location;
    const auto dof = registry.view.camera_dof;
    const auto width =
        glm::vec3((tangent * far_clip + ratio) * glm::dvec3(registry.view.rotation_column0));
    const auto height = glm::vec3((tangent_vertical * far_clip + ratio) *
                                  glm::dvec3(registry.view.rotation_column1));
    const auto forward = far_clip * dof;
    const auto apex = location - backoff * dof;
    const std::array<glm::vec3, 4> far_corners{
        {(forward - width) + height + location, (forward + height) + width + location,
         (forward - width) - height + location, (forward + width) - height + location}};
    registry.view.frustum_planes[4] = {dof, -glm::dot(dof, apex)};
    registry.view.frustum_planes[5] = {-dof, glm::dot(dof, far_corners[0])};
    constexpr std::array<std::array<unsigned, 2>, 4> sides{{{1, 0}, {0, 2}, {3, 1}, {2, 3}}};
    for (unsigned side = 0; side < sides.size(); ++side)
        registry.view.frustum_planes[side] =
            planeMath::triangle(apex, far_corners[sides[side][0]], far_corners[sides[side][1]]);
    for (int index = 0; index < 6; ++index) {
        registry.view.frustum_planes[index].w += m_spatial.m_region_grid_cell;
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
void LevelSpatialIndex::CollectVisibleCells()
{
    BuildFrustumPlanes();
    MarkVisibleRegions();
    short radius = static_cast<short>(
        (static_cast<int>((registry.view.far_clip / m_spatial.m_region_grid_cell)) + 1));
    glm::i16vec3 center(0);

    for (int axis = 0; axis < 3; ++axis) {
        center[axis] = static_cast<short>(static_cast<int>(
            ((registry.view.camera_location[axis] - m_spatial.m_bounds.minimum[axis]) /
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
                    if (!planeMath::containsSphere(registry.view.frustum_planes, point, 0)) {
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
                        SetBit(registry.current_regions, region);
                    }
                }
            }
        }
    }
}

bool LevelSpatialIndex::ReadRegionLinkFile(std::string_view level_name)
try {
    std::string name(level_name);
    if (const auto extension = name.find_last_of('.'); extension != std::string::npos) {
        name.resize(extension);
    }
    name += ".rlk";
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
    for (unsigned int index = 0; index < count; ++index) {
        RemoveIndexValue(m_region_links, keys[index], values[index]);
        InsertIndexValue(m_region_links, keys[index], values[index]);
    }
    m_region_links_ready = true;
    return true;
} catch (const std::exception&) {
    return false;
}

std::uint32_t* PackColourToLong(std::uint32_t* color, double alpha, double red, double green,
                                double blue)
{
    unsigned char* bytes =
        reinterpret_cast<unsigned char*>(color); // reinterpret-ok: packed colour storage
    bytes[3] = static_cast<unsigned char>(roundedInt32(alpha * kColorByteScale));
    bytes[2] = static_cast<unsigned char>(roundedInt32(red * kColorByteScale));
    bytes[1] = static_cast<unsigned char>(roundedInt32(green * kColorByteScale));
    bytes[0] = static_cast<unsigned char>(roundedInt32(blue * kColorByteScale));
    return color;
}

std::optional<MeasuredPath>
LevelSpatialIndex::measureNoisePath(const glm::vec3& from, const glm::vec3& to, float budget) const
{
    PathSearchScratch path_search;
    return pathing->MeasureAttachmentPath(path_search, from, to, budget);
}

unsigned char LevelSpatialIndex::PrepareNavigatorTarget(NavigatorMovement* movement, float radius,
                                                        float separation)
{
    PathSearchScratch path_search;
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
            if (!pathing->tracePathSpan(movement->position, target, false, false).reachable) {
                movement->attachment->InitializeSegment(&movement->position, &target);
                movement->attachment->separation = separation;
                result = pathing->BuildAttachmentPath(path_search, movement->attachment.get(),
                                                      movement->navigation_filter);
                if (result != 0) {
                    NavigatorRoute* attachment = movement->attachment.get();
                    attachment->path[attachment->path_position_index].position =
                        movement->target_position;
                    attachment->path_destination =
                        attachment->path[attachment->path_position_index].position;
                    pathing->AdvanceAttachmentWaypoint(&movement->position, attachment);
                    movement->target_position = movement->attachment->GetNextPosition();
                    return result;
                }
                result = pathing->ProbeAttachmentPath(path_search, movement->attachment.get());
                if (result != 0) {
                    NavigatorRoute* attachment = movement->attachment.get();
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
    return pathing->PlanMovement(path_search, movement, radius, separation) != 0;
}

bool IsNavigatorAtTarget(NavigatorMovement* movement)
{
    glm::vec3 target;
    if (movement->attachment != 0) {
        NavigatorRoute* attachment = movement->attachment.get();
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

unsigned char LevelSpatialIndex::PrepareNavigatorPatrol(NavigatorMovement* movement, float minimum,
                                                        float maximum)
{
    PathSearchScratch path_search;
    unsigned char result = 0;
    if (pathing != 0) {
        glm::vec3 velocity = movement->velocity;
        movement->attachment->InitializeSegment(&movement->position, &movement->target_position);
        result = pathing->BuildPatrolPath(path_search, movement->attachment.get(),
                                          movement->navigation_filter, &movement->target_position,
                                          minimum, &velocity, maximum);
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

unsigned char LevelSpatialIndex::LinkNavigatorTarget(NavigatorMovement* movement,
                                                     const glm::vec3* target, float separation)
{
    PathSearchScratch path_search;
    if (pathing != 0) {
        return pathing->LinkAttachmentTarget(path_search, movement->attachment.get(),
                                             movement->navigation_filter, target, separation);
    }
    return 0;
}

/* The cell-walk probes and the trace helpers the two line-of-sight bodies use.
   None of their bodies are recovered, so they keep address-qualified names. */

const float g_octree_cell_scale = 100.0f;
/* Follow one child bit per axis and level through the compact 9-word branch
   records.  Zero is the missing-child sentinel; live leaves start at one. */
std::uint32_t LevelSpatialIndex::FindLeaf(const glm::ivec3& point) const
{
    int level = m_spatial.m_depth;
    int mask = 1 << m_spatial.m_depth;
    std::uint32_t node = 1;

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
int LevelSpatialIndex::DescendByMask(const unsigned int* masked_cell) const
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
unsigned int LevelSpatialIndex::LeafIndexForCell(const glm::ivec3& cell) const
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
   testing the cell's props first and always testing the walkable level
   surfaces. A prop hit shortens the cell's surface trace, so a level surface
   reported in the same cell lies nearer and takes the contact. The point's y
   drops to the contact on a hit and keeps its input value on a miss, after
   clamping to the ceiling. Props and level surfaces share the scratch object
   set, as retail does. */
GroundProbe LevelSpatialIndex::settleToGround(const glm::vec3& input, bool test_props,
                                              float limit) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    glm::vec3 position = input;
    if (m_spatial.m_clipped_bounds.maximum.y < position.y) {
        position.y = m_spatial.m_clipped_bounds.maximum.y;
    }
    glm::vec3 end = position;
    glm::vec3 start = position;
    start.y = position.y + limit;
    glm::ivec3 cell = cellAt(start);
    end.y = ((cell.y - 1)) * m_spatial.m_node_extent + m_spatial.m_bounds.minimum.y;
    auto trace = planeSegment(start, end);
    while (-1 < cell.y) {
        std::optional<PropTraceHit> prop;
        if (test_props) {
            scratch.keys.clear();
            CollectObjectsInCell(cell, W8_OCTREE_KIND_PROP, scratch);
            prop = tracePropList(scratch.keys, trace, {});
            if (prop) trace = planeSegment(trace.start, prop->position);
        }
        if (ProbeCellForTrace(cell, scratch) != 0) {
            if (const auto contact = traceGeometry(trace, {.walkable_only = true}, scratch.keys)) {
                position.y = contact->point.y;
                return {position, GroundSurfaceContact{contact->surface_index}};
            }
        }
        if (prop) {
            position.y = trace.end.y;
            return {position, GroundPropContact{prop->prop_id}};
        }
        const float level = static_cast<float>(cell.y);
        --cell.y;
        start.y = level * m_spatial.m_node_extent + m_spatial.m_bounds.minimum.y;
        end.y -= m_spatial.m_node_extent;
        trace = planeSegment(start, end);
    }
    return {position, std::nullopt};
}

bool LevelSpatialIndex::hasLineOfSight(const glm::vec3& from, const glm::vec3& to) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    const auto segment = planeSegment(from, to);
    bool blocked = false;
    visitOctreeSegment(
        cellAt(from), cellAt(to), [&] { return cellWalk(from, to); },
        [&](const glm::ivec3& current, bool adjacent) {
            if (ProbeCellForBlockers(current, scratch) != 0 || adjacent)
                blocked = meshPolygonsBlock(segment, scratch.keys);
            return !blocked;
        });
    return !blocked && !traceProps(from, to, {.skip_filtered = true, .attached_only = true});
}

/* The walk stops in the first cell whose level surfaces meet the segment;
   props are then traced only up to that contact, so a nearer prop wins. */
std::optional<SightHit> LevelSpatialIndex::traceSight(const glm::vec3& from, const glm::vec3& to,
                                                      SightQuery query) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    const auto segment = planeSegment(from, to);
    std::optional<SurfaceRayHit> surface;
    visitOctreeSegment(
        cellAt(from), cellAt(to), [&] { return cellWalk(from, to); },
        [&](const glm::ivec3& current, bool adjacent) {
            if (ProbeCellForTrace(current, scratch) != 0 || adjacent) {
                surface =
                    !scratch.keys.empty() ? traceGeometry(segment, {}, scratch.keys) : std::nullopt;
            }
            return !surface;
        });
    const glm::vec3 end = surface ? surface->point : to;
    if (query.props) {
        if (const auto prop = traceProps(from, end, {}))
            return SightHit{prop->position, SightBlocker::Obstacle};
    }
    if (surface) return SightHit{end, SightBlocker::Obstacle};
    if (query.locations) {
        const auto& locations = *query.locations;
        if (const auto hit = traceSphereHit(from, to, locations.source, locations.target,
                                            locations.target, 0, locations.noise_adjust))
            return SightHit{hit->position, SightBlocker::Location};
    }
    return std::nullopt;
}

/* Trace location spheres and then the camera. The winning location id
   retains the historical last-collider-offset behavior. */
std::optional<LevelSpatialIndex::SphereTraceHit>
LevelSpatialIndex::traceSphereHit(const glm::vec3& from, const glm::vec3& to, int excluded,
                                  int target, int excluded_location, unsigned int flags,
                                  bool noise_adjust) const
{
    PathSearchScratch path_search;
    unsigned int index = 0;
    std::vector<std::uint32_t> ids;
    unsigned int best_index = 0;
    double best = -1.0;
    float inflate;
    Navigator* navigator;
    float radius;
    unsigned int probe_set = 0;
    glm::vec3 offset;
    const float segment_length = noise_adjust ? glm::distance(from, to) : 0;
    const auto sphereDistance = [&](const glm::vec3& center) {
        const auto projection = segmentMath::project(center, from, to);
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
    if (registry.maximum_navigation_radius > inflate) {
        inflate = registry.maximum_navigation_radius;
    }
    const auto reversed = glm::greaterThan(from, to);
    const glm::vec3 low(glm::mix(from, to, reversed) - inflate);
    const glm::vec3 high(glm::mix(to, from, reversed) + inflate);
    if (target == -1) {
        radius = PartyNavigator()->movement.alternate_radius;
        navigator = PartyNavigator();
    } else {
        if (target < 1) {
            goto no_probes;
        }
        W8MonsterInfo* info =
            (owner_world ? owner_world->population->findMonster(target) : nullptr);
        if (info == 0 || info->p3D == 0) {
            goto no_probes;
        }
        navigator = &(info->p3D)->nav;
        radius = navigator->movement.alternate_radius;
    }
    if (navigator != 0 && pathing != 0) {
        probe_set = pathing->CollectPathProbes(path_search, &navigator->movement, radius);
    }
no_probes:;
    ids = queryObjects({low, high}, W8_OCTREE_KIND_LOCATION, -1);
    auto count = ids.size(); /* c-style-cast-ok:
            the shared query count field is stored unsigned */
    if (count != 0) {
        do {
            int id = ids[index];
            if (((excluded < 0) || (excluded != id)) &&
                ((target < 0) || (excluded_location != id)) &&
                (probe_set == 0 || !pathing->MatchesPathProbe(path_search, id, 0, 0))) {
                W8MonsterInfo* info =
                    (owner_world ? owner_world->population->findMonster(id) : nullptr);
                if (info != 0) {
                    Monster* monster = info->p3D;
                    if (monster != 0 && monster->nav.active &&
                        (monster->nav.trace_mask & flags) == 0) {
                        const glm::vec3 center(monster->nav.movement.position.x,
                                               monster->nav.movement.position.y +
                                                   monster->nav.movement.height_offset,
                                               monster->nav.movement.position.z);
                        const float distance = sphereDistance(center);
                        float monster_radius = monster->nav.radius;
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
        const float distance = sphereDistance(cameraLocation(*owner_world));
        const float camera_radius = PartyNavigator()->radius * g_camera_sphere_radius_scale;
        if (distance < camera_radius)
            return SphereTraceHit{sphereOffset(camera_radius, distance) + from, 0};
    }
    return std::nullopt;
}

/* Second Remove emission (0x00438C90 above is the other), serving the
   keyboard/automap tables' callers. */

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
void LevelSpatialIndex::AddCollidablePropBounds(int index, const BoundingBox& bounds)
{
    const auto minimum = cellAt(bounds.minimum);
    const auto maximum = cellAt(bounds.maximum);
    const auto prop_key = PackOctreeObjectKey(W8_OCTREE_KIND_PROP, index + 1);
    visitOctreeCellBox(minimum, maximum, [&](const glm::ivec3& cell) {
        const auto cell_key = PackOctreeCellKey(cell.x, cell.y, cell.z);
        const int cell_value = static_cast<int>(cell_key);
        const int prop_value = static_cast<int>(prop_key);
        auto& by_prop = registry.cells->by_object;
        RemoveIndexValue(by_prop, prop_key, cell_value);
        InsertIndexValue(by_prop, prop_key, cell_value);
        auto& by_cell = registry.cells->by_cell;
        RemoveIndexValue(by_cell, cell_key, prop_value);
        InsertIndexValue(by_cell, cell_key, prop_value);
    });
}

/* Build the cell walk between two world points.

   The three axis deltas are taken in cells; the longest of them drives, and the
   other two each get an error triple seeded from where inside its cell the line
   starts. The step count covers the driving axis plus the partial cell at each
   end, which is why the remainder decides between one and two extra. */
W8OctreeWalk LevelSpatialIndex::cellWalk(const glm::vec3& from, const glm::vec3& to) const
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
   entry, then trace the collected props in collection order. */
std::optional<LevelSpatialIndex::PropTraceHit>
LevelSpatialIndex::traceProps(const glm::vec3& from, const glm::vec3& to,
                              PropTraceFilter filter) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    visitOctreeSegment(
        cellAt(from), cellAt(to), [&] { return cellWalk(from, to); },
        [&](const glm::ivec3& current, bool) {
            CollectObjectsInCell(current, W8_OCTREE_KIND_PROP, scratch);
            return true;
        });
    return tracePropList(scratch.keys, planeSegment(from, to), filter);
}

/* The cell probes share one lookup: bounds-check the cell against the grid,
   resolve its leaf index through the direct leaf grid when one exists else by
   descending the branch tree one octant per level, then drain the chosen leaf
   stream into scratch.keys.data() through the scratch.visited_objects dedupe set. ProbeCellForTrace
   reads the leaf's gd_polygon_offset into the m_gd_surface_index_stream surface-id stream;
   ProbeCellForBlockers reads polygon_offset into the m_polygon_index_stream index stream
   and maps each entry through m_aulPolyLookup to a (mesh<<16)|polygon key. The Append variant skips the result-count
   reset so successive cells accumulate. */
/* Cell probes share LeafIndexForCell; their retail bodies expand this lookup. */
std::size_t LevelSpatialIndex::ProbeCellForTrace(const glm::ivec3& cell,
                                                 OctreeScratch& scratch) const
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    scratch.keys.clear();
    if (leaf_index != 0 && m_leaves[leaf_index].gd_polygon_offset != 0) {
        const std::uint32_t* stream =
            m_gd_surface_index_stream.data() + m_leaves[leaf_index].gd_polygon_offset;
        int remaining = *stream;
        while (remaining != 0) {
            ++stream;
            if (!SetBit(scratch.visited_objects, *stream)) {
                scratch.keys.push_back(*stream);
            }
            --remaining;
        }
    }
    return scratch.keys.size();
}

std::size_t LevelSpatialIndex::ProbeCellForBlockers(const glm::ivec3& cell,
                                                    OctreeScratch& scratch) const
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    scratch.keys.clear();
    if (leaf_index != 0 && m_leaves[leaf_index].polygon_offset != 0) {
        const std::uint32_t* stream =
            m_polygon_index_stream.data() + m_leaves[leaf_index].polygon_offset;
        AppendBlockerStream(stream, scratch);
    }
    return scratch.keys.size();
}

void LevelSpatialIndex::AppendBlockerStream(const std::uint32_t* stream,
                                            OctreeScratch& scratch) const
{
    for (int remaining = *stream; remaining != 0; --remaining) {
        ++stream;
        if (!SetBit(scratch.visited_polygons, *stream)) {
            unsigned int key = m_aulPolyLookup[*stream];
            scratch.keys.push_back(key);
        }
    }
}

std::size_t LevelSpatialIndex::ProbeCellForBlockersAppend(const glm::ivec3& cell,
                                                          OctreeScratch& scratch) const
{
    unsigned int leaf_index = LeafIndexForCell(cell);
    if (leaf_index != 0 && m_leaves[leaf_index].polygon_offset != 0) {
        const std::uint32_t* stream =
            m_polygon_index_stream.data() + m_leaves[leaf_index].polygon_offset;
        AppendBlockerStream(stream, scratch);
    }
    return scratch.keys.size();
}

/* Ray-test each live, opaque mesh instance's triangle for the buffered
   (mesh<<16)|polygon keys against the segment. */
bool LevelSpatialIndex::meshPolygonsBlock(const PlaneSegment& segment,
                                          std::span<const std::uint32_t> keys) const
{
    return std::ranges::any_of(keys, [&](std::uint32_t key) {
        const unsigned int mesh_index = key >> 0x10;
        if (mesh_index >= m_meshCount || owner_world->psrMeshes[mesh_index] == 0 ||
            TestBit(m_alpha_bits, mesh_index))
            return false;
        Mesh* model = static_cast<Mesh*>(owner_world->psrMeshes[mesh_index]->getModel());
        const unsigned int polygon = key & 0xffff;
        const glm::vec4& plane = model->getPolyEq()[polygon];
        const auto contact = planeContact(plane, segment, PlaneContactMode::Collision);
        if (!contact) return false;
        const glm::ivec3& poly_vertex = model->getPolyVertex()[polygon];
        const glm::vec3* vertices = model->getVertexLoc();
        const RuntimeTriangle triangle{vertices[poly_vertex.x], vertices[poly_vertex.y],
                                       vertices[poly_vertex.z]};
        return PointInsideTriangle(triangle, dominantPlaneAxis(glm::vec3(plane)), contact->point) &&
               contact->distance < segment.limit;
    });
}

/* Collect the (mesh<<16)|polygon keys whose triangles touch the box
   (x±radius, y-height..y, z±radius) around `center`: every cell in the box is
   probed through ProbeCellForBlockersAppend, each key is then dropped when the
   mesh is gone, alpha-flagged or facing away, and when its triangle fails the
   spatial bounds test. The survivors are compacted, sorted for ordered
   consumption and zero-terminated; the shared buffer is returned. */
std::vector<std::uint32_t> LevelSpatialIndex::polygonsNearPoint(const glm::vec3& position,
                                                                float radius, float height) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    scratch.keys.clear();
    std::ranges::fill(scratch.visited_polygons, false);

    const BoundingBox box{position - glm::vec3(radius, height, radius),
                          position + glm::vec3(radius, 0, radius)};
    const auto start = cellAt(box.minimum);
    const auto end = cellAt(box.maximum);
    visitOctreeCellBox(start, end,
                       [&](const glm::ivec3& cell) { ProbeCellForBlockersAppend(cell, scratch); });

    std::vector<std::uint32_t> polygons;
    polygons.reserve(scratch.keys.size());
    for (const auto key : scratch.keys) {
        const auto mesh_index = key >> 16;
        if (key == 0 || mesh_index >= m_meshCount || !owner_world->psrMeshes[mesh_index] ||
            TestBit(m_alpha_bits, mesh_index))
            continue;
        auto& model = *static_cast<Mesh*>(owner_world->psrMeshes[mesh_index]->getModel());
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
void LevelSpatialIndex::UpdateMonsterLocation(unsigned short location_id, const glm::vec3& position)
{
    int queue_id = location_id + 1;
    W8MonsterInfo* info;
    Monster* monster;
    int sector;
    MeshInstance* mesh;
    glm::ivec3 point;

    if (location_id == 0) {
        return;
    }
    info = (owner_world ? owner_world->population->findMonster(location_id) : nullptr);
    if (info != 0 && info->p3D != 0) {
        monster = info->p3D;
        sector = GetSectorForPosition(&position);
        if (sector == 0 || (mesh = meshForRegion(sector)) == 0) {
            monster->sector_mesh = 0;
        } else {
            monster->sector_mesh = mesh;
        }
    }
    point = cellAt(position);
    registry.cells->RegisterObjectCell(W8_OCTREE_KIND_LOCATION, queue_id, point);
}

bool LevelSpatialIndex::SetPathStem(std::string_view path)
{
    if (path.empty()) {
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
LevelSpatialIndex::LevelSpatialIndex(std::string_view path, World* owner) : owner_world(owner)
{
    initializeCollision();
    Reset();
    try {
        if (path.empty()) {
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
            header.m_submesh_count >= std::numeric_limits<unsigned int>::max()) {
            throw std::runtime_error("Octree dimensions or counts exceed runtime limits.");
        }
        const std::size_t grid_count = grid_yz * header.m_grid_dims.x;
        const auto required =
            std::uint64_t{header.m_branch_count} * sizeof(W8OctPreTreeBranch) +
            std::uint64_t{header.m_leaf_count} * sizeof(W8OctPreTreeLeaf) +
            std::uint64_t{header.m_leaf_polygon_stream_len} * sizeof(std::uint32_t) +
            (grid_count < 250000 ? grid_count * sizeof(std::uint32_t) : 0) +
            std::uint64_t{header.m_polygon_count} * sizeof(std::uint32_t) +
            std::uint64_t{header.m_region_list_len} * sizeof(unsigned short) +
            std::uint64_t{header.m_gd_surface_stream_len} * sizeof(std::uint32_t) +
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
                std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
            (std::uint64_t{header.m_leaf_polygon_stream_len} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
            (std::uint64_t{header.m_gd_surface_stream_len} + 2) >
                std::numeric_limits<std::size_t>::max() / sizeof(std::uint32_t) ||
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
                             std::size_t{header.m_leaf_polygon_stream_len} * sizeof(std::uint32_t));
        if (grid_count < 250000) {
            m_leaf_lookup.resize(grid_count);
            hOctFile->read_exact(m_leaf_lookup.data(), grid_count * sizeof(std::uint32_t));
        }
        hOctFile->read_exact(m_aulPolyLookup.data(),
                             std::size_t{header.m_polygon_count} * sizeof(std::uint32_t));
        if (header.m_region_list_len != 0) {
            m_region_index_stream.resize((std::size_t{header.m_region_list_len} + 2));
            hOctFile->read_exact(m_region_index_stream.data(),
                                 std::size_t{header.m_region_list_len} * sizeof(unsigned short));
        }
        if (header.m_gd_surface_stream_len != 0) {
            m_gd_surface_index_stream.resize((std::size_t{header.m_gd_surface_stream_len} + 2));
            hOctFile->read_exact(m_gd_surface_index_stream.data(),
                                 std::size_t{header.m_gd_surface_stream_len} *
                                     sizeof(std::uint32_t));
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
            pathing = std::make_unique<NavigationMesh>(this);
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
        if (header.m_gd_surface_stream_len != 0) {
            ReadProcessedCollision(hOctFile.get());
            hOctFile->read_exact(&uiTerminator, sizeof(uiTerminator));
            if (uiTerminator != 0xffffffff) {
                throw std::runtime_error("Invalid octree GameData terminator.");
            }
        }

        m_region_links_ready = ReadRegionLinkFile(m_path_stem);
        if (pathing != 0) {
            pathing->ReadWaypointFile(*this);
        }
        return;
    } catch (const std::exception&) {

        m_spatial.flags |= 0x80000000;
    }
}

// Installed assets are already prepared. Missing OCT/PVL falls back to LVL.
int CheckLevelAssetSet(std::string_view level_path)
{
    if (level_path.find("Sky") != std::string_view::npos ||
        level_path.find("sky") != std::string_view::npos)
        return -1;
    auto pvl = wiz8::asset_path(level_path);
    pvl.replace_extension(".pvl");
    return wiz8::file_status(wiz8::asset_path(level_path)) && wiz8::file_status(pvl) ? 0 : -1;
}

/* Reset the owned tables and query state before applying an OCT header.
   Retail leaves the region masks and positional flag untouched here. */
void LevelSpatialIndex::Reset()
{
    m_spatial = {};
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
    registry.cells.reset();
    registry.region_meshes.clear();
    m_region_links.clear();
    m_path_stem.clear();
    m_path_clearance = 0;
    pathing = 0;
    registry.collecting_links = false;
    m_particle_links = {};
    m_prop_links = {};
    m_ulNumParticles = 0;
    registry.props.clear();
    registry.particles.clear();
    m_root_mesh_count = 0;
    m_kind1_submesh_count = 0;
    m_alpha_polygon_count = 0;
    m_meshCount = 0;

    registry.view = {};
    registry.visibility_suspended = false;
    registry.reset_visibility = false;
    registry.projected_regions_valid = false;
    m_region_links_ready = false;
    m_sun_count = 0;
    prop_sun_base = 0;
    m_ulNumProps = 0;
}

/* ReadOctFile calls this after Reset. The 0xf5-byte packed record has one
   canonical source model shared with WriteOctFile. */
void LevelSpatialIndex::Initialize(const W8OctFileHeader* header)
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
        m_leaf_grid_dimensions = {header->m_grid_dims.x, header->m_grid_dims.y,
                                  header->m_grid_dims.z};

        m_spatial.m_leaf_grid_stride_x = m_leaf_grid_dimensions.y * m_leaf_grid_dimensions.z;
        m_spatial.m_leaf_grid_stride_y = m_leaf_grid_dimensions.z;
        m_spatial.m_depth = header->m_depth;

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

        m_vertex_count = header->m_vertex_count;

        m_leaf_polygon_stream_len = header->m_leaf_polygon_stream_len;
        m_gd_surface_stream_len = header->m_gd_surface_stream_len;
        m_trigger_count = header->m_trigger_count;
        m_region_list_len = header->m_region_list_len;
        m_spatial.m_region_grid_cell = header->m_region_grid_cell;
        m_path_clearance = header->m_path_clearance;
        m_spatial.m_max_region_radius = header->m_max_region_radius;
        m_ulNumProps = header->m_prop_count;
        m_particle_links.records.resize(header->m_particle_len);
        m_prop_links.records.resize(header->m_prop_len);
        m_ulNumParticles = header->m_particle_count;

        if (m_ulNumParticles != 0) {
            registry.linked_particles.assign(m_ulNumParticles, false);
            registry.visible_particles.assign(m_ulNumParticles, false);
            registry.particles.reserve(m_ulNumParticles);
        }
        if (m_ulNumProps != 0) {
            registry.linked_props.assign(m_ulNumProps, false);
            registry.visible_props.assign(m_ulNumProps, false);
            registry.props.reserve(m_ulNumProps);
        }

        m_polygon_bit_count = header->m_polygon_count;
        registry.projected_regions.assign(header->m_submesh_count + 1, false);
        registry.current_regions.assign(header->m_submesh_count + 1, false);
        registry.previous_regions.assign(header->m_submesh_count + 1, false);
        m_object_bit_count = header->m_surface_count < 5000 ? 5000 : header->m_surface_count;
        registry.accumulated_regions.assign(header->m_submesh_count + 1, false);

        registry.cells = std::make_unique<W8OctreeObjectRegistry>();
        m_region_links.clear();

        registry.reset_visibility = true;
    }

    registry.collecting_links = true;
}

void LevelSpatialIndex::AddLoadedProp(Prop* prop)
{
    if (!registry.collecting_links) {
        return;
    }
    if (registry.props.size() >= m_ulNumProps) {
        throw std::runtime_error("Too many props loaded for octree.");
    }
    registry.props.push_back(prop);
}

void LevelSpatialIndex::AddLoadedParticles(std::span<ParticleSystem* const> particles)
{
    if (!registry.collecting_links) {
        return;
    }
    if (registry.particles.size() > m_ulNumParticles ||
        particles.size() > m_ulNumParticles - registry.particles.size()) {
        throw std::runtime_error("Too many particles loaded for octree.");
    }
    registry.particles.insert(registry.particles.end(), particles.begin(), particles.end());
}

LevelSpatialIndex::~LevelSpatialIndex() = default;

/* Running index into the prop-sunlight bit stream, shared across the octree
   so consecutive levels pick up where the previous one's props ended. */

/* Test whether prop `offset` past the octree's base index has its sunlight
   bit set; the tested index also becomes the shared running index. A
   negative offset instead checkpoints the shared index into the base, which
   is how ReadWorldProps starts each level's props on a fresh range. With no
   array attached the answer is always no. */
bool LevelSpatialIndex::TestPropSunBit(int offset)
{
    if (offset < 0) {
        prop_sun_base = registry.sun_index;
        return false;
    }
    registry.sun_index = prop_sun_base + offset;
    if (TestBit(m_sunlit_props, registry.sun_index)) {
        return true;
    }
    return false;
}

/* Remove every kind-12 and kind-13 registry pairing owned by one location
   id; each by_object hit also drops the mirrored by_cell entry.
   MonsterManager/NPC Manager drive this during location teardown.
   Retail verified at 0x0042E650/0x00438D50: the just-removed slot is passed
   back as the FindNextEntry cursor even though RemoveAt has pushed that
   entry onto the free list; the walk then follows the overwritten
   next_index, which is the free head (-1 in practice), ending the loop. */
void LevelSpatialIndex::UnregisterLocationObjects(unsigned int location_id)
{
    UnregisterLocationObject(location_id, W8_OCTREE_KIND_LOCATION);
    UnregisterLocationObject(location_id, W8_OCTREE_KIND_NAVIGATOR);
}

/* Remove the registered pairings for one (location, kind) object key — used
   by Navigator teardown with kind 0xd.  The removed-slot cursor reuse is
   retail verified (the same sequence is inlined at 0x0042E880): FindNextEntry
   follows entries[slot].next_index after RemoveAt has overwritten it with
   the free-list head. */
void LevelSpatialIndex::UnregisterLocationObject(unsigned int location_id, W8OctreeObjectKind kind)
{
    registry.cells->UnregisterObject(kind, location_id + 1);
}

/* Query the expanded segment box and own its ordered, deduplicated IDs. */
std::vector<std::uint32_t> LevelSpatialIndex::queryAlongSegment(const glm::vec3& origin,
                                                                const glm::vec3& delta,
                                                                float extent,
                                                                unsigned short kind) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    const auto bounds = segmentMath::sweptBounds(origin, delta, extent);
    const auto lower = cellCoordinates(bounds.minimum).cell;
    const auto upper = cellCoordinates(bounds.maximum).cell;
    scratch.keys.clear();
    std::ranges::fill(scratch.visited_objects, false);
    visitClippedOctreeCellBox(
        lower, upper, glm::ivec3(m_leaf_grid_dimensions),
        [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind, scratch); });
    return std::move(scratch.keys);
}

/* The kind-12 convenience query: a zero exclusion maps to -1 (none). OctPath
   uses it to list the location ids near a mover. */

std::vector<std::uint32_t> LevelSpatialIndex::locationsInBox(const BoundingBox& bounds,
                                                             unsigned short exclusion) const
{
    return queryObjects(bounds, W8_OCTREE_KIND_LOCATION, exclusion == 0 ? -1 : exclusion);
}

/* AABB occupancy test: GD triangles, kind-12 location objects (each
   monster's navigator radius inflates the box) and collidable-prop surfaces
   all answer "occupied". */
bool LevelSpatialIndex::isBoxOccupied(const BoundingBox& box) const
{
    auto objects = queryObjects(box, W8_OCTREE_KIND_SURFACE, -1);
    auto count = objects.size();
    unsigned int index;
    for (index = 0; index < count; ++index) {
        const W8GDSurface* surface = this->m_pSurfaces.data() + objects[index];
        if (SurfaceIntersectsBox(*surface, this->m_pVertices, box)) {
            return true;
        }
    }
    objects = queryObjects(box, W8_OCTREE_KIND_LOCATION, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        W8MonsterInfo* info =
            (owner_world ? owner_world->population->findMonster(objects[index]) : nullptr);
        if (info != 0 && info->p3D != 0) {
            glm::vec3 position = info->p3D->nav.GetPosition();
            float radius = info->p3D->nav.radius;
            if (box.minimum.x - radius < position.x && position.x < radius + box.maximum.x &&
                box.minimum.y - radius < position.y && position.y < radius + box.maximum.y &&
                box.minimum.z - radius < position.z && position.z < radius + box.maximum.z) {
                return true;
            }
        }
    }
    objects = queryObjects(box, W8_OCTREE_KIND_PROP, -1);
    count = objects.size();
    for (index = 0; index < count; ++index) {
        Prop* prop = owner_world->collidable_props[objects[index]];
        if (prop->GetActivationState() != 0 && prop->collision_ready) {
            Prop* gd_prop = (prop->collision_ready ? prop : nullptr);
            for (int surface_index = 0;
                 surface_index < static_cast<int>(gd_prop->m_pGDSurfaces.size()); ++surface_index) {
                W8GDSurface* surface = gd_prop->m_pGDSurfaces.data() + surface_index;
                if (SurfaceIntersectsBox(*surface, gd_prop->m_pVertices, box)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Query the inclusive cell box and copy the deduplicated IDs out of the
   scratch buffer before any subsequent query can overwrite them. */
std::vector<std::uint32_t> LevelSpatialIndex::queryObjects(const BoundingBox& bounds,
                                                           unsigned short kind, int excluded) const
{
    OctreeScratch scratch(m_polygon_bit_count, m_object_bit_count);
    const auto start = cellAt(bounds.minimum);
    const auto end = cellAt(bounds.maximum);
    scratch.keys.clear();
    std::ranges::fill(scratch.visited_objects, false);
    if (excluded >= 0) SetBit(scratch.visited_objects, excluded);
    visitClippedOctreeCellBox(
        start, end, glm::ivec3(m_leaf_grid_dimensions),
        [&](const glm::ivec3& cell) { CollectObjectsInCell(cell, kind, scratch); });
    return std::move(scratch.keys);
}

/* Append the objects of `kind` inside one cell to the shared query buffer.
   Kind 3 walks the static tree (or the direct leaf grid when one exists) to a
   leaf whose gd-polygon stream lists surface ids. Kinds 8, 9 and 12 query the
   object registry's cell hash; kinds above 12 accept both the 12 and 13
   halfwords packed into each registry value. Every append deduplicates
   through scratch.visited_objects; the result vector grows with the query. */
unsigned int LevelSpatialIndex::CollectObjectsInCell(const glm::ivec3& cell, unsigned short kind,
                                                     OctreeScratch& scratch) const
{
    unsigned int found = 0;
    unsigned int leaf_index;

    switch (kind) {
    case W8_OCTREE_KIND_SURFACE:
        if (m_leaf_lookup.empty()) {
            leaf_index = FindLeaf(cell);
        } else {
            leaf_index = m_leaf_lookup[m_spatial.m_leaf_grid_stride_y * cell.y +
                                       m_spatial.m_leaf_grid_stride_x * cell.x + cell.z];
        }
        if (leaf_index != 0 && m_leaves[leaf_index].gd_polygon_offset != 0) {
            const std::uint32_t* stream =
                m_gd_surface_index_stream.data() + m_leaves[leaf_index].gd_polygon_offset;
            found = *stream;
            for (unsigned int index = 0; index < found; ++index) {
                ++stream;
                if (!SetBit(scratch.visited_objects, *stream)) {
                    scratch.keys.push_back(*stream);
                }
            }
        }
        break;
    default: {
        const unsigned int cell_key = PackOctreeCellKey(cell.x, cell.y, cell.z);
        auto slot =
            NextIndexValue(registry.cells->by_cell, cell_key, registry.cells->by_cell.end());
        while (slot != registry.cells->by_cell.end()) {
            unsigned int packed = static_cast<unsigned int>(slot->second);
            const auto query_id = static_cast<unsigned short>(packed);
            const unsigned short entry_kind = OctreeKeyKind(packed);
            const bool matches = kind > W8_OCTREE_KIND_LOCATION
                                     ? (entry_kind == W8_OCTREE_KIND_LOCATION ||
                                        entry_kind == W8_OCTREE_KIND_NAVIGATOR)
                                     : entry_kind == kind;
            if (matches && !SetBit(scratch.visited_objects, OctreeKeyId(packed) - 1)) {
                scratch.keys.push_back(query_id - 1);
            }
            slot = NextIndexValue(registry.cells->by_cell, cell_key, slot);
        }
        break;
    }
    }
    return found;
}

/* Register the navigator's world position in the spatial cell registry.
   Navigator IDs are stored with a one-based bias. */
void LevelSpatialIndex::RegisterNavigatorCell(int id, const glm::vec3& position)
{
    glm::ivec3 point;

    point = cellAt(position);
    registry.cells->RegisterObjectCell(W8_OCTREE_KIND_NAVIGATOR, id + 1, point);
}

/* Quantize the world point relative to the spatial minimum. This preserves
   truncation toward zero, including points below the minimum. */
glm::ivec3 LevelSpatialIndex::cellAt(const glm::vec3& position) const
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
unsigned int LevelSpatialIndex::AdvanceNavigator(NavigatorMovement* movement, float radius,
                                                 float separation)
{
    PathSearchScratch path_search;
    glm::vec3 vecDir;
    glm::vec3 vecPos;
    NavigatorRoute* attachment;
    float distance;
    float step;
    bool reached = true;

    if (pathing != 0) {
        if (g_navigator_link_mode == W8_NAVIGATOR_LINK_MODE_DEFAULT) {
            return pathing->StepAlongPath(path_search, movement, radius, separation);
        }
        return pathing->StepMonsterAlongPath(movement, radius, separation);
    }
    if (g_navigator_link_mode != W8_NAVIGATOR_LINK_MODE_DEFAULT) {
        return 1;
    }
    vecDir = movement->target_position - movement->position;
    vecDir.y = 0.0f;
    distance = glm::length(((glm::vec2(vecDir.x, vecDir.z))));
    step = GameTimeAccumulator().GetFrameDelta() * movement->movement_scale * g_rate * kWorldScale;
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

/* Release the location-variable names and empty their parallel value and
   level vectors. Trigger.cpp creates the names as copied character arrays. */

std::vector<std::uint32_t> LevelSpatialIndex::nearbyLocations(const glm::vec3& position,
                                                              float spacing)
{
    const float expand = spacing * kMonsterProximityRadiusScale;
    return queryObjects({position - expand, position + expand}, W8_OCTREE_KIND_LOCATION, -1);
}

/* Lateral column offsets for the scatter search below: the inner loop walks
   three columns (0, +1, -1) per ring, or all five when more than ten
   positions are wanted. */
static constexpr float g_scatter_column_offsets[5] = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f};

/* Ring-scatter search for up to `count` clear positions around `position`.
   Each ring steps `spacing` units out along the `yaw`-rotated frame and
   columns offset laterally through g_scatter_column_offsets; the
   first accepted candidate becomes the reference the remaining candidates
   must span to. With `proximity_check` set each candidate is also rejected
   inside the startup navigator's or any nearby monster's radius. `flatten_y`
   relaxes the tight vertical bound during search and rewrites every accepted
   y to the original source height afterwards. */
static bool OverlapsNearbyMonster(const World* world, const glm::vec3& point, float reach,
                                  std::span<const std::uint32_t> locations, bool alternate_radius)
{
    return std::ranges::any_of(locations, [&](std::uint32_t id) {
        auto* info = world && world->population ? world->population->findMonster(id) : nullptr;
        auto* monster = info ? info->p3D : nullptr;
        if (!monster) return false;
        const auto delta = monster->nav.GetPosition() - point;
        const float clearance =
            (alternate_radius ? monster->nav.movement.alternate_radius : monster->nav.radius) +
            reach;
        return glm::dot(delta, delta) < clearance * clearance;
    });
}

std::vector<glm::vec3> LevelSpatialIndex::scatterPositions(const glm::vec3& position, float yaw,
                                                           float spacing, unsigned int count,
                                                           bool proximity_check, bool flatten_y,
                                                           std::optional<BoundingSphere> player)
{
    float source_y = position.y;
    std::vector<glm::vec3> positions;
    unsigned int found = 0;
    std::vector<std::uint32_t> candidates;
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
                    jitter_ring = CurrentGame().rng.below(1000) * kScatterOuterRingJitterScale -
                                  g_generator_jitter_fraction;
                    jitter_column = CurrentGame().rng.below(1000) * kScatterOuterRingJitterScale -
                                    g_generator_jitter_fraction;
                }
                const auto probe =
                    grid.candidate(source, {ring + jitter_ring, jitter_column + *column_offset});
                if (const auto settled = wiz8::placement::grounded(
                        probe, settleToGround(probe, true, 500).position.y, flatten_y)) {
                    glm::vec3 candidate = *settled;
                    if (!proximity_check) {
                        if (pathing->SnapWaypointPosition(&candidate, false)) {
                            goto accept;
                        }
                    } else {
                        float reach = spacing * 0.5f;
                        if (pathing->TestPathCellClearance(&candidate, reach, false)) {
                            const auto delta = player ? player->center - candidate : glm::vec3(0);
                            const float separation = player ? player->radius + reach : 0;
                            if (!player || separation * separation <= glm::dot(delta, delta)) {
                                if (OverlapsNearbyMonster(owner_world, candidate, reach, candidates,
                                                          false))
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
                               pathing->tracePathSpan(candidate, source, false, false).reachable) {
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

NavigatorPlacement LevelSpatialIndex::navigatorPositions(const glm::vec3& input, float yaw,
                                                         float radius, unsigned int count,
                                                         bool first_only, bool settle_any_height,
                                                         bool avoid_triggers, int mode,
                                                         bool require_waypoint_span,
                                                         std::optional<BoundingSphere> player)
{
    unsigned int found = 0;
    glm::vec3 source = input;
    std::vector<glm::vec3> positions;
    int found_i = 9999;
    int found_j = 9999;
    float source_y = source.y;
    std::vector<std::uint32_t> candidates;
    float camera_radius = player ? player->radius : 0;
    bool placed = false;
    float separation = (CalcRangeDistance(W8_RANGE_TOUCH) + radius) * 0.5f + camera_radius;
    float angle = NormalizeAngle(yaw + g_monster_rotation_offset);
    const wiz8::placement::Grid grid(angle, radius);
    source.y += kWorldScale;
    if (const auto settled = wiz8::placement::grounded(
            source, settleToGround(source, true, 500).position.y, settle_any_height))
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
                                jitter_i =
                                    CurrentGame().rng.below(1000) * kScatterInnerRingJitterScale -
                                    0.1f;
                                jitter_j =
                                    CurrentGame().rng.below(1000) * kScatterInnerRingJitterScale -
                                    0.1f;
                            }
                            const auto probe = grid.candidate(source, {i + jitter_i, jitter_j + j});
                            if (const auto settled = wiz8::placement::grounded(
                                    probe, settleToGround(probe, true, 500).position.y,
                                    settle_any_height)) {
                                glm::vec3 candidate = *settled;
                                unsigned char clear;
                                if (!first_only) {
                                    clear = pathing->SnapWaypointPosition(&candidate, false);
                                } else {
                                    clear = pathing->TestPathCellClearance(&candidate,
                                                                           radius * 0.5f, false);
                                }
                                if (clear != 0 &&
                                    (!avoid_triggers ||
                                     !InsideDestinationTrigger(*owner_world, candidate.x,
                                                               candidate.y, candidate.z))) {
                                    if (first_only) {
                                        const auto delta =
                                            player ? player->center - candidate : glm::vec3(0);
                                        if (player &&
                                            glm::dot(delta, delta) < separation * separation) {
                                            goto next_cell;
                                        }
                                        if (OverlapsNearbyMonster(owner_world, candidate,
                                                                  radius * 0.5f, candidates, true))
                                            goto next_cell;
                                    }
                                    if (!require_waypoint_span || pathing == 0 ||
                                        pathing->tracePathSpan(candidate, source, false, false)
                                            .reachable) {
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
                                            const bool reachable =
                                                pathing
                                                    ->tracePathSpan(candidate, existing, false,
                                                                    false)
                                                    .reachable;
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

bool SurfaceIntersectsBox(const W8GDSurface& surface, std::span<const glm::vec3> vertices,
                          const BoundingBox& box)
{
    return triangleIntersectsBox(box, indexedTriangle(vertices, surface.vertex_indices),
                                 surface.plane.normal);
}

bool triangleIntersectsBox(const BoundingBox& box, std::span<const glm::vec3, 3> vertices,
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
