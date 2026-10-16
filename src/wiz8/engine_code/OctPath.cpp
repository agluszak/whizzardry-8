#include "wiz8/engine_code/OctPath.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <fmt/format.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <wiz8/filesystem.h>

#include "Font.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "geometry.h"
#include "glm/common.hpp" // NOLINT(misc-include-cleaner)
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "glm/geometric.hpp" // NOLINT(misc-include-cleaner)
#include "glm/vector_relational.hpp" // NOLINT(misc-include-cleaner)
#include "himage.h"
#include "index_algorithms.h"
#include "input.h"
#include "layouts/gameplay_databases.h"
#include "layouts/world.h"
#include "local_code/Factions.h"
#include "navigation_flags.h"
#include "packed_bits.h"
#include "random.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srMath.h"
#include "surrender/srMeshModel.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h" // IWYU pragma: keep - complete scene type for conversion to srNode.
#include "surrender/srShader.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GDFileIO.h"
#include "wiz8/engine_code/GDProp.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/OctBuildPreTree.h"
#include "wiz8/engine_code/OctMeshModel.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stMeshModel.h"
#include "wiz8/engine_code/stModelInstance.h"
#include "wiz8/float_constants.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/CombatRange.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/navigation_math.h"
#include "wiz8/sr_api.h"
#include "wiz8/startup_world.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/virtual_file.h"

class srMaterialIFace;
class srTextureIFace;

// GLOBAL: WIZ8 0x005ebc30
const double g_double_one = 1.0;
// GLOBAL: WIZ8 0x005ec020
const double g_path_overlap_tolerance = 250.0;
// GLOBAL: WIZ8 0x005ec368
const double g_waypoint_edge_offset = 25.00000037252903;
// GLOBAL: WIZ8 0x005ec378
const double g_waypoint_marker_scale = 4.0;
// GLOBAL: WIZ8 0x005ec38c
const float g_path_direct_alignment_threshold = 0.9847999811172485f;
// GLOBAL: WIZ8 0x005ec384
const float g_waypoint_fallback_query_half_extent = 37500.0f;
// GLOBAL: WIZ8 0x005ec388
const float g_path_surface_query_half_extent = 100000.0f;
// GLOBAL: WIZ8 0x005ec370
const float g_waypoint_marker_height = 550.0f;
// GLOBAL: WIZ8 0x005ec394
const float g_path_heuristic_scale = 1.2000000476837158f;
// GLOBAL: WIZ8 0x005ec398
const float g_path_link_cost_base = 1001.0f;
// GLOBAL: WIZ8 0x005ec39c
const float g_patrol_cost_limit = 250000.0f;
// GLOBAL: WIZ8 0x005ec3b8
const float g_float_one_and_a_half = 1.5f;
// GLOBAL: WIZ8 0x005ec3bc
const float g_path_gap_penalty_scale = 0.5099999904632568f;
// GLOBAL: WIZ8 0x005ec3c0
const float g_float_one_million = 1000000.0f;
// GLOBAL: WIZ8 0x005ec3c8
const double g_path_blocking_alignment_upper_bound = 0.9;
// GLOBAL: WIZ8 0x005ec3d0
const double g_path_blocking_alignment_lower_bound = -0.9;
// GLOBAL: WIZ8 0x005ed2e0
const double g_obstacle_slide_side_threshold = 0.2;
// GLOBAL: WIZ8 0x005ed2e8
const float g_obstacle_radius_average_scale = 0.3333333f;
// GLOBAL: WIZ8 0x005ec2e8
const double g_double_negative_one = -1.0;

// GLOBAL: WIZ8 0x00659c60
W8PathingService* g_pathing;

/* Engine Code\OctPath.cpp. The unit is named by its own assertions, which
   place every one of these bodies in OctPath.cpp rather than in Octree.cpp
   where the octree's own loader lives. */

// GLOBAL: WIZ8 0x0060827a
unsigned short g_path_reserve = 2000;
// GLOBAL: WIZ8 0x005ec340
const float g_fast_magic_recovery_scale = 1.25f;
// GLOBAL: WIZ8 0x005ec344
const float g_path_span_scale = 1.5259254723787308e-05f;

// GLOBAL: WIZ8 0x00659c5c
static bool g_path_continue_after_blocker;
// GLOBAL: WIZ8 0x00659c64
// GLOBAL: WIZ8 0x005ec3a8
const double g_path_initial_cost_slack = 1.1;
// GLOBAL: WIZ8 0x005ec3a0
const double g_path_segment_split_distance = 25000.0;
// GLOBAL: WIZ8 0x005ec3b0
const double g_double_one_tenth = 0.1;
// GLOBAL: WIZ8 0x005ec348
const float g_path_direction_threshold_0 = -0.9239000082015991f;
// GLOBAL: WIZ8 0x005ec34c
const float g_path_direction_threshold_1 = -0.38269999623298645f;
// GLOBAL: WIZ8 0x005ec350
const float g_path_direction_threshold_2 = 0.38269999623298645f;
// GLOBAL: WIZ8 0x005ec354
const float g_path_direction_threshold_3 = 0.9239000082015991f;
// GLOBAL: WIZ8 0x005ec358
const float g_path_cardinal_scale = 1.4149999618530273f;

// GLOBAL: WIZ8 0x005ec360
const float g_float_twenty_five_thousand = 25000.0f;
// GLOBAL: WIZ8 0x00659c6c
static unsigned int g_path_visualization_cell;
// GLOBAL: WIZ8 0x005ec380
const float g_path_search_visualization_limit = 15000.0f;

// GLOBAL: WIZ8 0x0060f9e8
static float g_path_acceleration_factor = 3.0f;
// GLOBAL: WIZ8 0x0060f9ec
static float g_path_angular_acceleration_factor = 2.0f;
// GLOBAL: WIZ8 0x0060f9f0
static float g_path_angular_deceleration_factor = 0.5f;
// GLOBAL: WIZ8 0x0060f9f4
static float g_path_prediction_time = 1.0f;
// GLOBAL: WIZ8 0x0060f9f8
static float g_path_approach_slow_time = 1.0f;
// GLOBAL: WIZ8 0x0060f9fc
static float g_path_approach_run_time = 8.0f;
// GLOBAL: WIZ8 0x0060fa00
static float g_path_approach_run_rate = 2.0f;
// GLOBAL: WIZ8 0x0060fa04
static float g_path_lookahead_time = 1.0f;
// GLOBAL: WIZ8 0x0060fa08
static float g_path_group_repulsion_factor = 1.0f;
// GLOBAL: WIZ8 0x0060fa0c
static float g_path_party_boundary_radius = 2.0f;
// GLOBAL: WIZ8 0x0060fa10
static float g_path_obstacle_steering_factor = 2.0f;
// GLOBAL: WIZ8 0x0060fa14
static float g_path_obstacle_braking_factor = 0.3f;

#define OCTPATH_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\OctPath.cpp"

bool LoadPathParameters();

/* Mark a waypoint, growing the set with slack when the index is past its end. */
static void SetGrowing(std::vector<bool>& bits, std::size_t bit)
{
    if (bit >= bits.size()) {
        bits.resize(bit + 100);
    }
    bits[bit] = true;
}

// FUNCTION: WIZ8 0x004577F0
void W8PathHeapHandle::DeleteRoot(W8PathSearchNode* node)
{
    if (!open.empty()) {
        open.pop();
    }
    root_node = open.empty() ? 0 : open.top().node;
    node->flags |= W8_PATH_SEARCH_CLOSED;
}

/* Write the path hash serialization and its five conditional tables. Counts
   smaller than the two sentinel entries are normalized to an empty set before
   the header is emitted, exactly as the read side treats them. */
// FUNCTION: WIZ8 0x00458ad0
bool W8PathingService::WritePathNodes(wiz8::File* handle)
try {
    if (path_node_count < 0 || m_ulNumCondPaths < 0) {
        return false;
    }
    handle->write(file_path_nodes,
                  static_cast<std::size_t>(path_node_count) * sizeof(W8FilePathNode));
    if (m_ulNumCondFrames < 2 || m_ulNumCondNodes < 2) {
        m_ulNumCondPaths = 0;
        m_ulNumCondFrames = 0;
        m_ulNumCondNodes = 0;
    }
    const W8ConditionalPathHeader header{static_cast<unsigned int>(m_ulNumCondPaths),
                                         static_cast<unsigned int>(m_ulNumCondFrames),
                                         static_cast<unsigned int>(m_ulNumCondNodes), 0};
    handle->write(&header, sizeof(header));
    handle->write(m_pCondPaths,
                  static_cast<std::size_t>(m_ulNumCondPaths) * sizeof(GDPropCondPaths));
    handle->write(m_pulCondLookup,
                  static_cast<std::size_t>(m_ulNumCondFrames) * sizeof(*m_pulCondLookup));
    handle->write(m_pusCondNodeFrames,
                  static_cast<std::size_t>(m_ulNumCondFrames) * sizeof(*m_pusCondNodeFrames));
    handle->write(m_pulCondNodeKeys,
                  static_cast<std::size_t>(m_ulNumCondNodes) * sizeof(*m_pulCondNodeKeys));
    handle->write(m_pulCondNodeValues,
                  static_cast<std::size_t>(m_ulNumCondNodes) * sizeof(*m_pulCondNodeValues));
    return true;
} catch (const std::exception&) {
    return false;
}

/* Snapshot live path surfaces into the compact waypoint-file representation,
   clearing runtime-only disabled and edge bits before writing the .WPT file.
   The cd-rom sentinel disables this editor-side write path. */
// FUNCTION: WIZ8 0x00459400
unsigned char W8PathingService::SaveWaypointSnapshot(bool force)
try {
    if (!waypoints_dirty && !force) {
        return 0;
    }
    const auto cd = wiz8::file_status("cd.rom");
    if (cd && cd->info.type == SDL_PATHTYPE_FILE) {
        return 0;
    }

    BuildWaypointFileData();
    std::unique_ptr<W8FileWaypoint, decltype(&free)> snapshot(
        static_cast<W8FileWaypoint*>(
            malloc(std::size_t{m_ulNumWayPoints} * sizeof(W8FileWaypoint))),
        free);
    if (m_ulNumWayPoints != 0 && !snapshot) {
        return false;
    }

    unsigned int index;
    for (index = 0; index < m_ulNumWayPoints; ++index) {
        snapshot.get()[index].flags = m_waypoints[index].flags & (0xffffu & ~W8_WAYPOINT_DISABLED);
        snapshot.get()[index].first_edge = m_waypoints[index].first_edge;
        snapshot.get()[index].position = m_waypoints[index].position;
    }
    for (index = 0; index < m_ulNumWayPtLinks; ++index) {
        m_pEdges[index].flags &= ~W8_PATH_EDGE_DISABLED;
    }

    free(m_pFileWayPoints);
    m_pFileWayPoints = snapshot.get();
    const auto result = WriteWaypointFile();
    m_pFileWayPoints = 0;
    return result;
} catch (const std::exception&) {
    return false;
}

/* Write the version-two waypoint snapshot. A throwing write or flush fails
   the operation; partial output is never reported as a successful snapshot. */
// FUNCTION: WIZ8 0x00459540
unsigned char W8PathingService::WriteWaypointFile()
try {
    if (m_ulNumWayPoints == 0 || m_ulNumWayPoints > 0xffff || m_ulNumWayPtLinks > 0xffff ||
        m_pFileWayPoints == 0) {
        return false;
    }
    auto handle = wiz8::open_file(std::string(level_name) + ".WPT", wiz8::OpenMode::replace);
    const unsigned int version = 2;
    handle->write(&version, sizeof(version));
    handle->write(&edge_node_count, sizeof(edge_node_count));
    handle->write(&m_ulNumWayPoints, sizeof(m_ulNumWayPoints));
    handle->write(&m_ulNumWayPtLinks, sizeof(m_ulNumWayPtLinks));
    handle->write(m_pFileWayPoints, std::size_t{m_ulNumWayPoints} * sizeof(W8FileWaypoint));
    handle->write(m_pEdges, std::size_t{m_ulNumWayPtLinks} * sizeof(W8PathEdge));
    handle->close();
    return true;
} catch (const std::exception&) {
    return false;
}

/* Read a versioned .WPT snapshot and rebuild its live graph representation.
   Version one stores the four persistent edge fields separately; later files
   contain the complete packed 0x0e-byte edge record. */
// FUNCTION: WIZ8 0x00459650
unsigned char W8PathingService::ReadWaypointFile()
try {
    if (path_node_count == 0) {
        return false;
    }
    auto handle = wiz8::open_file(std::string(level_name) + ".WPT");
    unsigned int version, edge_nodes, waypoint_count, link_count;
    handle->read_exact(&version, sizeof(version));
    handle->read_exact(&edge_nodes, sizeof(edge_nodes));
    handle->read_exact(&waypoint_count, sizeof(waypoint_count));
    handle->read_exact(&link_count, sizeof(link_count));
    if (version < 1 || waypoint_count == 0 || waypoint_count > 0xffff || link_count == 0 ||
        link_count > 0xffff) {
        return false;
    }
    const auto required = std::uint64_t{waypoint_count} * sizeof(W8FileWaypoint) +
                          std::uint64_t{link_count} * (version == 1 ? 12 : sizeof(W8PathEdge));
    if (required > static_cast<std::uint64_t>(handle->size() - handle->tell())) {
        return false;
    }
    const unsigned int surface_capacity = (waypoint_count / 100 + 1) * 100;
    const unsigned int edge_capacity = (link_count / 100 + 1) * 100;
    std::unique_ptr<W8FileWaypoint, decltype(&free)> file_waypoints(
        static_cast<W8FileWaypoint*>(malloc(std::size_t{waypoint_count} * sizeof(W8FileWaypoint))),
        free);
    std::unique_ptr<W8PathSurface, decltype(&free)> waypoints(
        static_cast<W8PathSurface*>(calloc(surface_capacity, sizeof(W8PathSurface))), free);
    std::unique_ptr<W8PathEdge, decltype(&free)> edges(
        static_cast<W8PathEdge*>(calloc(edge_capacity, sizeof(W8PathEdge))), free);
    std::vector<std::uint16_t> scratch(surface_capacity);
    if (!file_waypoints || !waypoints || !edges) {
        return false;
    }
    handle->read_exact(file_waypoints.get(), std::size_t{waypoint_count} * sizeof(W8FileWaypoint));
    if (version == 1) {
        for (unsigned int index = 0; index < link_count; ++index) {
            auto& item = edges.get()[index];
            handle->read_exact(&item.flags, sizeof(item.flags));
            handle->read_exact(&item.destination, sizeof(item.destination));
            handle->read_exact(&item.distance, sizeof(item.distance));
            handle->read_exact(&item.next, sizeof(item.next));
        }
    } else {
        handle->read_exact(edges.get(), std::size_t{link_count} * sizeof(W8PathEdge));
    }
    for (unsigned int index = 1; index < link_count; ++index) {
        const auto& edge = edges.get()[index];
        if (edge.destination >= waypoint_count || edge.source >= waypoint_count ||
            edge.next >= link_count) {
            return false;
        }
    }
    std::vector<bool> visited(link_count);
    for (unsigned int surface = 1; surface < waypoint_count; ++surface) {
        const auto& source = file_waypoints.get()[surface];
        auto& destination = waypoints.get()[surface];
        destination.flags = source.flags;
        destination.index = static_cast<unsigned short>(surface);
        destination.first_edge = source.first_edge;
        destination.position = source.position;
        if ((destination.flags & 0xf000) == 0) {
            destination.flags |= 0x2000;
        }
        for (unsigned int edge = source.first_edge; edge != 0; edge = edges.get()[edge].next) {
            if (edge >= link_count || visited[edge]) {
                return false;
            }
            visited[edge] = true;
        }
    }
    auto heap = std::make_unique<W8PathHeapHandle>();
    std::vector<bool> marked(surface_capacity);
    std::vector<bool> visited_nodes(surface_capacity);
    std::vector<bool> collected(surface_capacity);

    for (unsigned int surface = 1; surface < m_ulNumWayPoints; ++surface) {
        g_octree->object_registry->UnregisterObject(W8_OCTREE_KIND_WAYPOINT, surface + 1);
    }
    free(m_pFileWayPoints);
    free(m_waypoints);
    free(m_pEdges);
    delete path_heap;
    m_pFileWayPoints = file_waypoints.release();
    m_waypoints = waypoints.release();
    m_pEdges = edges.release();
    m_path_scratch = std::move(scratch);
    path_heap = heap.release();
    m_marked_path_nodes = std::move(marked);
    m_visited_path_nodes = std::move(visited_nodes);
    m_collected_path_nodes = std::move(collected);
    m_ulNumWayPoints = waypoint_count;
    m_ulNumWayPtLinks = link_count;
    edge_node_count = edge_nodes;
    for (unsigned int surface = 1; surface < waypoint_count; ++surface) {
        const auto point = g_octree->cellAt(m_waypoints[surface].position.glmValue());
        g_octree->object_registry->MoveObjectToCell(W8_OCTREE_KIND_WAYPOINT, surface + 1, point);
    }

    if (version <= 2) {
        unsigned int surface;
        for (surface = 1; surface < m_ulNumWayPoints; ++surface) {
            if ((ClassifyWaypoint(&m_waypoints[surface].position) & W8_PATH_CELL_CONDITIONAL) != 0)
                m_waypoints[surface].flags |= W8_WAYPOINT_CONDITIONAL;
        }
        for (surface = 1; surface < m_ulNumWayPoints; ++surface) {
            unsigned short edge_index = m_waypoints[surface].first_edge;
            while (edge_index != 0) {
                W8PathEdge* edge = &m_pEdges[edge_index];
                edge->source = static_cast<unsigned short>(surface);
                unsigned int destination_index = edge->destination;
                if ((m_waypoints[surface].flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
                    (m_waypoints[destination_index].flags & W8_WAYPOINT_CONDITIONAL) != 0) {
                    edge->flags |= W8_PATH_EDGE_CONDITIONAL;
                } else {
                    TestWaypointSpan(&m_waypoints[surface].position,
                                     &m_waypoints[destination_index].position, false, false);
                    if (span_blocked) edge->flags |= W8_PATH_EDGE_CONDITIONAL;
                }
                edge_index = edge->next;
            }
        }
    }
    waypoints_dirty = true;
    return true;
} catch (const std::exception&) {
    return false;
}

/* Compact the editable waypoint graph. Invalid and dead-end surfaces are
   unregistered and discarded; every surviving surface and edge is packed
   toward its sentinel and all indices are rewritten through temporary maps. */
// FUNCTION: WIZ8 0x0045e440
void W8PathingService::BuildWaypointFileData()
{
    unsigned short next_surface = 1;
    unsigned short next_edge = 1;
    std::vector<unsigned short> surface_map(std::size_t{m_ulNumWayPoints} + 1);
    std::vector<unsigned short> edge_map(std::size_t{m_ulNumWayPtLinks} + 1);

    unsigned int old_surface;
    for (old_surface = 1; old_surface < m_ulNumWayPoints; ++old_surface) {
        W8PathSurface* surface = &m_waypoints[old_surface];
        if (surface->index == 0 || surface->first_edge == 0) {
            g_octree->object_registry->UnregisterObject(W8_OCTREE_KIND_WAYPOINT, old_surface + 1);
            if (surface->first_edge == 0) {
                unsigned short removed = 0;
                unsigned int edge;
                for (edge = 1; edge < m_ulNumWayPtLinks; ++edge) {
                    if (m_pEdges[edge].destination == old_surface) {
                        /* Retail passes the discarded surface, not this incoming edge. */
                        RemoveWaypointLink(static_cast<unsigned short>(old_surface));
                        ++removed;
                    }
                }
                if (g_dev_mode) {
                    if (removed == 0) {
                        DebugMessage(
                            0, fmt::format("Deleting Isolated WayPt at ({:.1f}, {:.1f}, {:.1f})",
                                           surface->position.x, surface->position.y,
                                           surface->position.z));
                    } else {
                        DebugMessage(
                            0, fmt::format("Deleting Dead End WayPt at ({:.1f}, {:.1f}, {:.1f})",
                                           surface->position.x, surface->position.y,
                                           surface->position.z));
                    }
                }
            }
        } else {
            surface_map[old_surface] = next_surface;
            if (old_surface != next_surface) {
                m_waypoints[next_surface] = *surface;
                m_waypoints[next_surface].index = next_surface;
                g_octree->object_registry->UnregisterObject(W8_OCTREE_KIND_WAYPOINT,
                                                            old_surface + 1);
                const auto point = g_octree->cellAt(m_waypoints[next_surface].position.glmValue());
                g_octree->object_registry->MoveObjectToCell(W8_OCTREE_KIND_WAYPOINT,
                                                            next_surface + 1, point);
            }
            ++next_surface;
        }
    }

    unsigned int old_edge;
    for (old_edge = 1; old_edge < m_ulNumWayPtLinks; ++old_edge) {
        W8PathEdge* edge = &m_pEdges[old_edge];
        if (edge->destination != 0) {
            edge->destination = surface_map[edge->destination];
            edge->source = surface_map[edge->source];
            edge_map[old_edge] = next_edge;
            if (old_edge != next_edge) {
                m_pEdges[next_edge] = *edge;
            }
            ++next_edge;
        }
    }

    memset(&m_pEdges[next_edge], 0, (m_ulNumWayPtLinks - next_edge) * sizeof(W8PathEdge));
    memset(&m_waypoints[next_surface], 0,
           (m_ulNumWayPoints - next_surface) * sizeof(W8PathSurface));
    m_ulNumWayPoints = next_surface;
    m_ulNumWayPtLinks = next_edge;

    for (old_surface = 1; old_surface < m_ulNumWayPoints; ++old_surface) {
        m_waypoints[old_surface].first_edge = edge_map[m_waypoints[old_surface].first_edge];
    }
    for (old_edge = 1; old_edge < m_ulNumWayPtLinks; ++old_edge) {
        m_pEdges[old_edge].next = edge_map[m_pEdges[old_edge].next];
    }
    saved_surface = surface_map[saved_surface];
}

/* Read the path hash and conditional tables completely before replacing
   live state. Counts below the lookup/key sentinel pair denote empty tables. */
// FUNCTION: WIZ8 0x00458ce0
unsigned char W8PathingService::ReadPathNodes(wiz8::File* handle)
try {
    if (path_node_count < 0 ||
        std::uint64_t{static_cast<unsigned int>(path_node_count)} * sizeof(W8FilePathNode) +
                sizeof(W8ConditionalPathHeader) >
            static_cast<std::uint64_t>(handle->size() - handle->tell())) {
        return false;
    }
    std::vector<W8FilePathNode> nodes(path_node_count);
    handle->read_exact(nodes.data(), nodes.size() * sizeof(W8FilePathNode));
    W8ConditionalPathHeader header;
    handle->read_exact(&header, sizeof(header));
    if (header.frame_count < 2 || header.node_count < 2) {
        header.path_count = header.frame_count = header.node_count = 0;
    }
    const auto required =
        std::uint64_t{header.path_count} * sizeof(GDPropCondPaths) +
        std::uint64_t{header.frame_count} * (sizeof(unsigned int) + sizeof(unsigned short)) +
        std::uint64_t{header.node_count} * 2 * sizeof(unsigned int);
    if (required > static_cast<std::uint64_t>(handle->size() - handle->tell()) ||
        header.path_count > std::numeric_limits<int>::max() ||
        header.frame_count > std::numeric_limits<int>::max() ||
        header.node_count > std::numeric_limits<int>::max() ||
        std::uint64_t{header.path_count} >
            std::numeric_limits<std::size_t>::max() / sizeof(GDPropCondPaths) ||
        std::uint64_t{header.frame_count} >
            std::numeric_limits<std::size_t>::max() / sizeof(unsigned int) ||
        std::uint64_t{header.node_count} >
            std::numeric_limits<std::size_t>::max() / sizeof(unsigned int)) {
        return false;
    }
    std::unique_ptr<GDPropCondPaths, decltype(&free)> paths(
        static_cast<GDPropCondPaths*>(
            malloc(std::size_t{header.path_count} * sizeof(GDPropCondPaths))),
        free);
    std::unique_ptr<unsigned int, decltype(&free)> lookup(
        static_cast<unsigned int*>(malloc(std::size_t{header.frame_count} * sizeof(unsigned int))),
        free);
    std::unique_ptr<unsigned short, decltype(&free)> frames(
        static_cast<unsigned short*>(
            malloc(std::size_t{header.frame_count} * sizeof(unsigned short))),
        free);
    std::unique_ptr<unsigned int, decltype(&free)> keys(
        static_cast<unsigned int*>(malloc(std::size_t{header.node_count} * sizeof(unsigned int))),
        free);
    std::unique_ptr<unsigned int, decltype(&free)> values(
        static_cast<unsigned int*>(malloc(std::size_t{header.node_count} * sizeof(unsigned int))),
        free);
    if ((header.path_count != 0 && !paths) || (header.frame_count != 0 && (!lookup || !frames)) ||
        (header.node_count != 0 && (!keys || !values))) {
        return false;
    }
    handle->read_exact(paths.get(), std::size_t{header.path_count} * sizeof(GDPropCondPaths));
    handle->read_exact(lookup.get(), std::size_t{header.frame_count} * sizeof(unsigned int));
    handle->read_exact(frames.get(), std::size_t{header.frame_count} * sizeof(unsigned short));
    handle->read_exact(keys.get(), std::size_t{header.node_count} * sizeof(unsigned int));
    handle->read_exact(values.get(), std::size_t{header.node_count} * sizeof(unsigned int));
    for (unsigned int index = 0; index < header.path_count; ++index) {
        if (paths.get()[index].lookup_index >= header.frame_count ||
            memchr(paths.get()[index].name, 0, sizeof(paths.get()[index].name)) == 0) {
            return false;
        }
    }
    for (unsigned int index = 1; index < header.frame_count; ++index) {
        if (lookup.get()[index] >= header.node_count) {
            return false;
        }
    }
    if (header.frame_count != 0 &&
        (lookup.get()[header.frame_count - 1] != 0 || keys.get()[header.node_count - 1] != 0)) {
        return false;
    }

    auto path_values = std::make_unique<std::unordered_multimap<unsigned int, unsigned int>>();
    auto visited_cells = std::make_unique<std::unordered_map<unsigned int, int>>();
    for (const auto& node : nodes) {
        path_values->emplace(node.cell, node.level_flags);
    }
    delete m_pPathValues;
    delete m_pVisitedCells;
    free(m_pCondPaths);
    free(m_pulCondLookup);
    free(m_pusCondNodeFrames);
    free(m_pulCondNodeKeys);
    free(m_pulCondNodeValues);
    m_pPathValues = path_values.release();
    m_pVisitedCells = visited_cells.release();
    m_pCondPaths = paths.release();
    m_pulCondLookup = lookup.release();
    m_pusCondNodeFrames = frames.release();
    m_pulCondNodeKeys = keys.release();
    m_pulCondNodeValues = values.release();
    m_ulNumCondPaths = header.path_count;
    m_ulNumCondFrames = header.frame_count;
    m_ulNumCondNodes = header.node_count;
    navigation_filter = header.flags;
    return true;
} catch (const std::exception&) {
    return false;
}

// Convert X/Z once; truncation toward zero also applies below the bounds.
PathCellCoordinates W8PathingService::pathCell(const glm::vec3& position) const
{
    const glm::ivec2 cell((glm::vec2(position.x, position.z) -
                           glm::vec2(level_bounds.minimum.x, level_bounds.minimum.z)) /
                          grid_scale);
    return {cell, static_cast<unsigned>(cell.y) * 0x10000u + static_cast<unsigned>(cell.x)};
}

/* Offer every flagged surface to the path builder.

   Surfaces are 0x28 bytes apart and the walk starts at index one, so entry zero
   is never a real surface. The point handed on is the surface's own position
   converted to the graph's integer grid - x from the bounds floor, z from the
   third bound - and the two conversions happen in the order the point's fields
   do not. */
// FUNCTION: WIZ8 0x00460020
void W8PathingService::LinkSurfaces(GDProp* prop)
{
    unsigned int index = 1;
    W8PathSurface* surface;

    if (m_ulNumWayPoints <= index) {
        return;
    }
    do {
        surface = &m_waypoints[index];
        if ((surface->flags & W8_WAYPOINT_CONDITIONAL) != 0) {
            prop->RegisterPathSurface(index, pathCell(surface->position.glmValue()).coordinates);
        }
        ++index;
    } while (index < m_ulNumWayPoints);
}

/* The same for the edges, which are 0xe bytes apart, gated by a different flag,
   and which name two surfaces by index in their shorts at +4 and +6. Each of
   those surfaces contributes one converted point, so the builder receives the
   edge as a pair. */
// FUNCTION: WIZ8 0x004600b0
void W8PathingService::LinkEdges(GDProp* prop)
{
    unsigned int index = 1;
    W8PathEdge* edge;
    W8PathSurface* surface;

    if (m_ulNumWayPtLinks <= index) {
        return;
    }
    do {
        edge = &m_pEdges[index];
        if ((edge->flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
            surface = &m_waypoints[edge->source];
            const auto first = pathCell(surface->position.glmValue()).coordinates;

            surface = &m_waypoints[edge->destination];
            const auto second = pathCell(surface->position.glmValue()).coordinates;
            prop->RegisterPathVertex(index, first, second);
        }
        ++index;
    } while (index < m_ulNumWayPtLinks);
}

/* Apply one GD prop frame to every conditional path cell in its serialized
   lookup run. Bit 0x02000000 is the frame-local state, while 0x10000000 is the
   disabled state published through the live path hash.

   A frame of -1 disables the complete run. When the requested frame exists,
   non-selected frames publish the inverse of their local state and the
   selected frame publishes the state itself. The serialized values for the
   non-selected frames are updated alongside the hash, exactly as retail does.

   Retail's missing-frame path performs its inverse-state work in two loops and
   deliberately carries the last value found by the first loop into the second.
   Once that carried value is disabled, later clear-state keys are skipped. */
// FUNCTION: WIZ8 0x00457ea0
void W8PathingService::SetConditionalPathFrame(unsigned int path_handle, unsigned short frame)
{
    std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
    unsigned int lookup_index = path_handle;
    bool frame_missing = true;

    while (m_pulCondLookup[lookup_index] != 0 && frame_missing) {
        if (m_pusCondNodeFrames[lookup_index] == frame) {
            frame_missing = false;
        }
        ++lookup_index;
    }

    if (frame == 0xffff) {
        lookup_index = path_handle;
        while (m_pulCondLookup[lookup_index] != 0) {
            unsigned int key_index = m_pulCondLookup[lookup_index];
            while (m_pulCondNodeKeys[key_index] != 0) {
                unsigned int key = m_pulCondNodeKeys[key_index];
                unsigned int current_value =
                    FindConditionalPathValue(key, m_pulCondNodeValues[key_index]);
                if ((current_value & W8_PATH_CELL_INACTIVE) == 0) {
                    RemoveIndexValue(*index, key, current_value);
                    current_value |= W8_PATH_CELL_INACTIVE;
                    InsertIndexValue(*index, key, current_value);
                }
                ++key_index;
            }
            ++lookup_index;
        }
        return;
    }

    if (frame_missing) {
        unsigned int current_value;
        lookup_index = path_handle;
        while (m_pulCondLookup[lookup_index] != 0) {
            unsigned int key_index = m_pulCondLookup[lookup_index];
            while (m_pulCondNodeKeys[key_index] != 0) {
                unsigned int key = m_pulCondNodeKeys[key_index];
                current_value = FindConditionalPathValue(key, m_pulCondNodeValues[key_index]);
                if ((m_pulCondNodeValues[key_index] & W8_PATH_CELL_BLOCKING_FRAME) != 0 &&
                    (current_value & W8_PATH_CELL_INACTIVE) != 0) {
                    RemoveIndexValue(*index, key, current_value);
                    current_value &= 0xefffffff;
                    InsertIndexValue(*index, key, current_value);
                }
                ++key_index;
            }
            ++lookup_index;
        }

        lookup_index = path_handle;
        while (m_pulCondLookup[lookup_index] != 0) {
            unsigned int key_index = m_pulCondLookup[lookup_index];
            while (m_pulCondNodeKeys[key_index] != 0) {
                if ((m_pulCondNodeValues[key_index] & W8_PATH_CELL_BLOCKING_FRAME) == 0 &&
                    (current_value & W8_PATH_CELL_INACTIVE) == 0) {
                    unsigned int key = m_pulCondNodeKeys[key_index];
                    RemoveIndexValue(*index, key, current_value);
                    current_value |= W8_PATH_CELL_INACTIVE;
                    InsertIndexValue(*index, key, current_value);
                }
                ++key_index;
            }
            ++lookup_index;
        }
        return;
    }

    // Non-selected frames update the persisted flags first. The selected
    // frame changes only the live index, as in the original two passes.
    for (int pass = 0; pass < 2; ++pass) {
        const bool selected_frame = pass == 1;
        for (unsigned int lookup = path_handle; m_pulCondLookup[lookup] != 0; ++lookup) {
            if ((m_pusCondNodeFrames[lookup] == frame) != selected_frame) continue;

            for (unsigned int key_index = m_pulCondLookup[lookup];
                 m_pulCondNodeKeys[key_index] != 0; ++key_index) {
                const unsigned int key = m_pulCondNodeKeys[key_index];
                unsigned int& stored = m_pulCondNodeValues[key_index];
                unsigned int current = FindConditionalPathValue(key, stored);
                const bool blocking_frame = (stored & W8_PATH_CELL_BLOCKING_FRAME) != 0;
                const bool should_be_inactive = blocking_frame == selected_frame;
                const bool is_inactive = (current & W8_PATH_CELL_INACTIVE) != 0;
                if (should_be_inactive == is_inactive) continue;

                RemoveIndexValue(*index, key, current);
                if (should_be_inactive) {
                    current |= W8_PATH_CELL_INACTIVE;
                    if (!selected_frame) stored |= W8_PATH_CELL_INACTIVE;
                } else {
                    current &= ~W8_PATH_CELL_INACTIVE;
                    if (!selected_frame) stored &= ~W8_PATH_CELL_INACTIVE;
                }
                InsertIndexValue(*index, key, current);
            }
        }
    }
}

/* Find the value for one conditional path key whose persistent identity is
   the requested low word. Several state variants of the same path cell can
   occupy one hash bucket, so an ordinary key lookup is not sufficient. */
// FUNCTION: WIZ8 0x00458970
unsigned int W8PathingService::FindConditionalPathValue(unsigned int key, unsigned int value)
{
    std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
    auto slot = index->equal_range(key).first;
    unsigned int found = 0;
    bool searching = true;
    while (slot != index->end() && searching) {
        unsigned int candidate = slot->second;
        if (((candidate ^ value) & 0xffff) == 0) {
            searching = false;
            found = candidate;
        }
        slot = NextIndexValue(*index, key, slot);
    }
    return found;
}

/* Refresh the disabled bit for a conditional list of waypoints. */
// FUNCTION: WIZ8 0x004601b0
void W8PathingService::CheckConditionalWayPtStatus(std::span<const unsigned short> waypoints)
{
    for (const auto waypoint : waypoints) {
        if (waypoint >= static_cast<unsigned short>(m_ulNumWayPoints)) {
            srAssertFail("pusWayPts[i] < (UINT16)m_ulNumWayPoints", OCTPATH_CPP, 0x1a4e,
                         "Pathing::CheckConditionalWayPtStatus: WayPt Index out of range.");
        }

        W8PathSurface* surface = &m_waypoints[waypoint];
        if ((ClassifyWaypoint(&surface->position) & W8_PATH_CELL_INACTIVE) == 0) {
            surface->flags &= 0xffffu & ~W8_WAYPOINT_DISABLED;
        } else {
            surface->flags |= W8_WAYPOINT_DISABLED;
        }
    }
}

/* Refresh the disabled bit for conditional path edges. Only edges carrying
   the conditional-span flag participate. A disabled source always disables
   its edge; otherwise the current span test decides the bit. */
// FUNCTION: WIZ8 0x00460250
void W8PathingService::CheckConditionalLinkStatus(std::span<const unsigned short> edges)
{
    for (const auto edge_index : edges) {
        if (edge_index >= static_cast<unsigned short>(m_ulNumWayPtLinks)) {
            srAssertFail("pusLinks[i] < (UINT16)m_ulNumWayPtLinks", OCTPATH_CPP, 0x1a6c,
                         "Pathing::CheckConditionalLinkStatus: Link Index out of range.");
        }

        W8PathEdge* edge = &m_pEdges[edge_index];
        if ((edge->flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
            if ((m_waypoints[edge->source].flags & W8_WAYPOINT_DISABLED) != 0 ||
                !TestWaypointSpan(&m_waypoints[edge->source].position,
                                  &m_waypoints[edge->destination].position, false, false)) {
                edge->flags |= W8_PATH_EDGE_DISABLED;
            } else {
                edge->flags &= ~W8_PATH_EDGE_DISABLED;
            }
        }
    }
}

/* Resolve the navigator attachment's current directed edge and apply the
   transition encoded by its flags.

   Disabled ordinary edges stop movement. Teleportal edges consume the current
   pair, move both live and attachment positions to its destination, and toggle
   the attachment's transition mode. */
// FUNCTION: WIZ8 0x00460350
unsigned char W8PathingService::HandlePathEdgeTransition(W8NavigatorMovementState* movement)
{
    W8NavigatorAttachment* attachment = movement->attachment.get();
    unsigned short cursor = attachment->path_cursor;
    unsigned int flags = 0;
    unsigned short destination;

    if (cursor < attachment->path_position_index) {
        const auto& pairs = attachment->path;
        unsigned short source = pairs[cursor].value;
        destination = pairs[cursor + 1].value;
        unsigned short edge_index = m_waypoints[source].first_edge;

        while (edge_index != 0) {
            W8PathEdge* edge = &m_pEdges[edge_index];
            if (edge->destination == destination) {
                flags = edge->flags;
                break;
            }
            edge_index = edge->next;
        }
    }

    if ((flags & W8_PATH_EDGE_DISABLED) != 0 &&
        ((flags & W8_NAV_THROUGH_DOORS) == 0 ||
         (movement->navigation_filter & W8_NAV_THROUGH_DOORS) == 0)) {
        return 0;
    }
    if ((flags & W8_PATH_EDGE_TELEPORTAL) == 0) {
        return 1;
    }

    attachment->path_cursor += 2;
    movement->position = m_waypoints[destination].position;
    attachment->path_length_origin = m_waypoints[destination].position;
    if ((attachment->flags & W8_NAV_ATTACHMENT_TELEPORT_PHASE) == 0) {
        attachment->flags |= W8_NAV_ATTACHMENT_TELEPORT_PHASE;
    } else {
        attachment->flags &= ~W8_NAV_ATTACHMENT_TELEPORT_PHASE;
    }
    return 2;
}

/* Measure the route between two points for the noise line-of-sight query. A
   throwaway attachment is built over (from, to); a positive `range` budget is
   held in m_path_cost_limit while the path builds, and both
   exits restore the 1.0e10f limit. On success `range` receives the
   measured path length and `hops` the count of hop segments whose link is
   gated by a door. */
// FUNCTION: WIZ8 0x004604B0
bool W8PathingService::MeasureAttachmentPath(const srVector3T<float>* from, srVector3T<float>* to,
                                             float* range, int* hops)
{
    W8NavigatorAttachment attachment(from, to);
    if (*range > g_float_zero) {
        m_path_cost_limit = *range;
    }
    if (!BuildAttachmentPath(&attachment, 0)) {
        m_path_cost_limit = 1.0e10f;
        return false;
    }

    unsigned short cursor = attachment.path_cursor;
    if ((attachment.flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        --cursor;
    }
    attachment.position_cursor = cursor;
    ++attachment.position_cursor;

    srVector3T<float> next;
    if (attachment.position_cursor < attachment.path_position_index) {
        next = attachment.path[attachment.position_cursor].position;
        TestWaypointSpan(from, &next, false, false);
    } else {
        next = attachment.path_destination;
    }

    *range = attachment.MeasurePathLength();
    *hops = 0;
    while (attachment.path_cursor < attachment.path_position_index) {
        if (TestAttachmentHopDoor(&attachment)) {
            ++*hops;
        }
        ++attachment.path_cursor;
    }
    m_path_cost_limit = 1.0e10f;
    return true;
}

/* Whether the hop at the attachment's current index runs over a disabled
   conditional edge guarded by a closed door. The current path value pair names
   the two surfaces; when their link carries both the conditional and disabled
   bits, the props inside the segment's bounds box are queried and the one
   nearest the midpoint supplies the trigger. The hop counts when that
   trigger's action record is a type-10 door record whose flag bit is clear. */
// FUNCTION: WIZ8 0x00460680
bool W8PathingService::TestAttachmentHopDoor(W8NavigatorAttachment* attachment)
{
    unsigned short current = attachment->path_cursor;
    if (current < attachment->path_position_index) {
        const auto& pairs = attachment->path;
        unsigned short source = pairs[current].value;
        unsigned short destination = pairs[current + 1].value;
        W8PathSurface* source_surface = &m_waypoints[source];
        unsigned short edge_index = source_surface->first_edge;
        bool found = false;
        while (edge_index != 0) {
            if (found) {
                break;
            }
            if (m_pEdges[edge_index].destination == destination) {
                found = true;
            } else {
                edge_index = m_pEdges[edge_index].next;
            }
        }
        if (edge_index != 0 && (m_pEdges[edge_index].flags & W8_NAV_THROUGH_DOORS) != 0 &&
            (m_pEdges[edge_index].flags & W8_PATH_EDGE_DISABLED) != 0) {
            srBoundingBox bounds{source_surface->position.glmValue(),
                                 source_surface->position.glmValue()};
            bounds.include(m_waypoints[destination].position.glmValue());
            std::vector<w8_ulong> candidates;
            Trigger* selected = 0;
            candidates = g_octree->queryObjects(bounds, W8_OCTREE_KIND_PROP, -1);
            int count = candidates.size();
            if (count > 0) {
                if (count == 1) {
                    W8Prop* prop = g_world->collidable_props[candidates[0]];
                    selected = prop->GetGDPropOwnerTrigger();
                    if (selected == 0) {
                        return false;
                    }
                } else {
                    double nearest_distance = 1e32;
                    const glm::vec3 center = bounds.center();
                    for (int index = 0; index < count; ++index) {
                        W8Prop* prop = g_world->collidable_props[candidates[index]];
                        Trigger* trigger = prop->GetGDPropOwnerTrigger();
                        if (trigger != 0) {
                            const glm::vec3 position = prop->GetPosition().glmValue();
                            const auto offset = position - center;
                            // NOLINTNEXTLINE(misc-include-cleaner)
                            const float distance = glm::dot(offset, offset);
                            if (distance < nearest_distance) {
                                nearest_distance = distance;
                                selected = trigger;
                            }
                        }
                    }
                }
                W8TriggerActionData* action_data = selected->m_pActionData;
                if (action_data == 0 || action_data->type != W8_TRIGGER_PAYLOAD_DOOR) {
                    action_data = 0;
                }
                if (!static_cast<W8DoorTriggerActionData*>(action_data)->open) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* A* search over the waypoint graph from the attachment's start position to
   its end position. The shared bit arrays mark generated (visible) and closed
   (rendered) nodes; the path heap holds the open set ordered by the integer
   truncation of each node's estimated remaining cost. Edges are filtered by
   the surface/edge flags and the caller's `flags` masks. Returns the reached
   end waypoint index, or zero on failure. */
// FUNCTION: WIZ8 0x00460b80
unsigned short W8PathingService::FindPath(W8NavigatorAttachment* attachment, unsigned int flags)
{
    unsigned short usStartNode = FindWaypoint(attachment->segment_start.glmValue(), false);
    if (usStartNode == 0 && (usStartNode = start_waypoint) == 0) {
        return 0;
    }
    unsigned int start = usStartNode;
    unsigned short usEndNode = FindWaypoint(attachment->path_destination.glmValue(), false);
    if (m_ulNumWayPoints <= start) {
        srAssertFail("usStartNode < m_ulNumWayPoints", OCTPATH_CPP, 0x1bdb,
                     "Starting index out of range");
    }
    unsigned int end = usEndNode;
    if (m_ulNumWayPoints <= end) {
        srAssertFail("usEndNode < m_ulNumWayPoints", OCTPATH_CPP, 0x1bdc,
                     "Last index out of range");
    }
    if (usEndNode == 0) {
        return 0;
    }
    std::ranges::fill(m_visited_path_nodes, false);
    std::ranges::fill(m_marked_path_nodes, false);
    path_heap->open = {};
    SetBit(m_marked_path_nodes, start);
    m_waypoints[start].search_parent = 0;
    unsigned short usWayPt = usStartNode;
    if (usStartNode != usEndNode) {
        do {
            unsigned int current = usWayPt;
            if (m_ulNumWayPoints <= current) {
                srAssertFail("usWayPt < m_ulNumWayPoints", OCTPATH_CPP, 0x1bea,
                             "Waypoint index out of range (1)");
            }
            unsigned short usLink = m_waypoints[current].first_edge;
            unsigned int link = usLink;
            if (m_ulNumWayPtLinks <= link) {
                srAssertFail("usLink < m_ulNumWayPtLinks", OCTPATH_CPP, 0x1bec,
                             "Link index out of range (1)");
            }
            bool settled = true;
            if (usLink != 0) {
                do {
                    if (m_ulNumWayPtLinks <= link) {
                        srAssertFail("usLink < m_ulNumWayPtLinks", OCTPATH_CPP, 0x1bf0,
                                     "Link index out of range (2)");
                    }
                    unsigned int edge_flags = m_pEdges[link].flags;
                    unsigned int usNextNode = m_pEdges[link].destination;
                    if (((m_waypoints[usNextNode].flags & W8_WAYPOINT_DISABLED) != 0) ||
                        ((edge_flags & W8_PATH_EDGE_DISABLED) != 0 &&
                         ((edge_flags & W8_NAV_THROUGH_DOORS) == 0 ||
                          (flags & W8_NAV_THROUGH_DOORS) == 0)) ||
                        (flags != 0 &&
                         (((edge_flags & W8_NAV_GROUP_MASK) != W8_NAV_GROUP_MASK &&
                           (edge_flags & flags & W8_NAV_GROUP_MASK) == 0) ||
                          ((edge_flags & W8_NAV_MOVEMENT_MASK) != W8_NAV_MOVEMENT_MASK &&
                           (edge_flags & flags & W8_NAV_MOVEMENT_MASK) == 0) ||
                          ((edge_flags & W8_NAV_SIZE_FILTER_MASK) != W8_NAV_SIZE_FILTER_MASK &&
                           (edge_flags & flags & W8_NAV_SIZE_FILTER_MASK) == 0)))) {
                        usNextNode = 0;
                    }
                    unsigned short usNext = static_cast<unsigned short>(usNextNode);
                    unsigned short usCurrent = usWayPt;
                    if (m_ulNumWayPoints <= usNextNode) {
                        srAssertFail("usNextNode < m_ulNumWayPoints", OCTPATH_CPP, 0x1bf5,
                                     "Waypoint index out of range (2)");
                    }
                    if ((usNext != 0) && (usNext != usCurrent) &&
                        (!TestBit(m_visited_path_nodes, usNextNode))) {
                        if (!SetBit(m_marked_path_nodes, usNextNode)) {
                            m_waypoints[usNextNode].path_cost =
                                m_pEdges[link].distance + m_waypoints[current].path_cost;
                            W8PathSurface* surfaces = m_waypoints;
                            if (surfaces[usNextNode].path_cost < m_path_cost_limit) {
                                float dx =
                                    surfaces[end].position.x - surfaces[usNextNode].position.x;
                                settled = false;
                                float dy =
                                    surfaces[end].position.y - surfaces[usNextNode].position.y;
                                float dz =
                                    surfaces[end].position.z - surfaces[usNextNode].position.z;
                                surfaces[usNextNode].heuristic =
                                    sqrt(dx * dx + dy * dy + dz * dz) * g_path_heuristic_scale;
                                surfaces[usNextNode].search_parent = usCurrent;
                                surfaces = m_waypoints;
                                surfaces[current].estimated_total_cost =
                                    surfaces[usNextNode].heuristic * g_path_heuristic_scale +
                                    surfaces[usNextNode].path_cost;
                                m_waypoints[usNextNode].flags |= W8_WAYPOINT_IN_HEAP;
                                W8PathHeapHandle* handle = path_heap;
                                W8PathHeapEntry entry;
                                entry.node = usNextNode;
                                entry.priority = static_cast<unsigned int>(
                                    surfaces[current].estimated_total_cost);
                                handle->open.push(entry);
                                handle->root_node = handle->open.top().node;
                            }
                        } else {
                            float cost = m_pEdges[link].distance + m_waypoints[current].path_cost;
                            if ((cost + g_float_ten < m_waypoints[usNextNode].path_cost) &&
                                (cost < m_path_cost_limit)) {
                                float reduction = m_waypoints[usNextNode].path_cost - cost;
                                m_waypoints[usNextNode].search_parent = usCurrent;
                                W8PathSurface* surfaces = m_waypoints;
                                settled = false;
                                surfaces[current].estimated_total_cost =
                                    surfaces[usNextNode].heuristic * g_path_heuristic_scale +
                                    surfaces[usNextNode].path_cost;
                                if ((m_waypoints[current].flags & W8_WAYPOINT_IN_HEAP) == 0) {
                                    surfaces = m_waypoints + usNextNode;
                                    surfaces->flags |= W8_WAYPOINT_IN_HEAP;
                                    W8PathHeapHandle* handle = path_heap;
                                    W8PathHeapEntry entry;
                                    entry.node = usNextNode;
                                    entry.priority = static_cast<unsigned int>(
                                        m_waypoints[current].estimated_total_cost);
                                    handle->open.push(entry);
                                    handle->root_node = handle->open.top().node;
                                }
                                ReduceWaypointCosts(usNextNode, reduction);
                            }
                        }
                    }
                    usLink = m_pEdges[link].next;
                    link = usLink;
                    if (m_ulNumWayPtLinks <= link) {
                        srAssertFail("usLink < m_ulNumWayPtLinks", OCTPATH_CPP, 0x1c22,
                                     "Link index out of range (3)");
                    }
                } while (usLink != 0);
            }
            if (settled) {
                W8PathHeapHandle* handle = path_heap;
                W8PathOpenSet* heap = &handle->open;
                if (!heap->empty()) {
                    heap->pop();
                }
                handle->root_node = heap->empty() ? 0 : heap->top().node;
                m_waypoints[current].flags &= 0xffffu & ~W8_WAYPOINT_IN_HEAP;
                SetBit(m_visited_path_nodes, current);
            }
            usWayPt = path_heap->root_node;
            current = usWayPt;
            if (m_ulNumWayPoints <= current) {
                srAssertFail("usWayPt < m_ulNumWayPoints", OCTPATH_CPP, 0x1c2a,
                             "Waypoint index out of range (3)");
            }
            if (usWayPt == 0) {
                return 0;
            }
        } while (usWayPt != usEndNode);
    }
    m_waypoints[start].search_parent = 0;
    return usWayPt;
}

void W8PathingService::AppendScratchPath(W8NavigatorAttachment* attachment, unsigned int count)
{
    m_path_scratch[count] = 0;
    while (count != 0) {
        unsigned short surface_index = m_path_scratch[count - 1];
        srVector3T<float>* position = &m_waypoints[surface_index].position;
        attachment->AppendPathPosition(position, surface_index);
        --count;
    }
}

/* Build the attachment's stored route for the path found between its start
   and end positions. The parent chain left by FindPath is reversed through
   the shared scratch array into route entries, the end position
   is appended, and the final hop is trimmed back to the destination when the
   last waypoint already sees it. */
// FUNCTION: WIZ8 0x00460950
bool W8PathingService::BuildAttachmentPath(W8NavigatorAttachment* attachment, unsigned int flags)
{
    if (path_heap != 0 && m_waypoints != 0) {
        std::ranges::fill(m_visited_path_nodes, false);
        std::ranges::fill(m_marked_path_nodes, false);
        path_heap->open = {};
        unsigned short node = FindPath(attachment, flags);
        if (node != 0) {
            unsigned short count = 0;
            std::ranges::fill(m_marked_path_nodes, false);
            do {
                if (SetBit(m_marked_path_nodes, node)) {
                    break;
                }
                if (m_ulNumWayPoints <= count) {
                    srAssertFail("i < m_ulNumWayPoints", OCTPATH_CPP, 0x1ba2, 0);
                }
                m_path_scratch[count] = node;
                node = m_waypoints[node].search_parent;
                ++count;
            } while (node != 0);
            AppendScratchPath(attachment, count);
            srVector3T<float>* destination = &attachment->path_destination;
            srVector3T<float>* slot = &attachment->path[attachment->path_position_index].position;
            *slot = *destination;
            if (((attachment->path_cursor < attachment->path_position_index) ||
                 ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0)) &&
                (TestWaypointSpan(&attachment->path[attachment->path_position_index - 2].position,
                                  destination, false, false))) {
                --attachment->path_position_index;
                slot = &attachment->path[attachment->path_position_index].position;
                *slot = *destination;
            }
            attachment->flags |= 0x20000;
            return true;
        }
    }
    return false;
}

/* Both recursive path searches finish with the same attachment updates. */
static void FinalizeWaypointRoute(W8NavigatorAttachment* attachment, bool has_start_waypoint,
                                  const srVector3T<float>& waypoint)
{
    if (1 < attachment->path_position_index) {
        --attachment->path_position_index;
        attachment->path_destination = attachment->path[attachment->path_position_index].position;
    }
    if (has_start_waypoint) {
        attachment->start_waypoint = waypoint;
        attachment->flags |= W8_NAV_ATTACHMENT_START_WAYPOINT;
    }
    attachment->flags |= 0x20000;
}

/* Link the attachment's recorded route to `target`: seeds the search state
   from the attachment's start waypoint, runs the recursive link search, and
   when that fails falls back to the A* FindPath when the straight-line cost
   still exceeds `separation`. The reached chain is walked back through
   parent into the scratch array, reversed into the attachment's position
   and value arrays, trimmed at the `separation` sphere around the target, and
   the last stored position becomes path_destination. */
// FUNCTION: WIZ8 0x004612a0
bool W8PathingService::LinkAttachmentTarget(W8NavigatorAttachment* attachment, unsigned int flags,
                                            const srVector3T<float>* target, float separation)
{
    unsigned short node;
    unsigned short count;
    unsigned short current;
    unsigned int index;
    unsigned short start;

    m_patrol_min = separation;
    m_patrol_distance = separation + g_float_ten_thousand;
    m_patrol_start = *target;
    navigation_filter = flags;
    start_waypoint = 0;
    start = FindWaypoint(attachment->segment_start.glmValue(), true);
    if ((start == 0) && ((start = start_waypoint) == 0)) {
        return false;
    }
    std::ranges::fill(m_visited_path_nodes, false);
    std::ranges::fill(m_marked_path_nodes, false);
    path_heap->open = {};
    index = start;
    SetBit(m_marked_path_nodes, index);
    m_probe_cell_key = index;
    m_patrol_cost = (*target - attachment->segment_start).Length();
    m_waypoints[index].search_parent = 0;
    m_waypoints[index].path_cost = m_patrol_cost;
    node = RecurseTargetLinks(start);
    if (node == 0) {
        if (m_patrol_cost > separation) {
            attachment->path_destination = m_waypoints[m_probe_cell_key].position;
            node = FindPath(attachment, flags);
        }
        if (node == 0) {
            return false;
        }
    }
    attachment->path_destination = m_waypoints[node].position;
    count = 0;
    std::ranges::fill(m_marked_path_nodes, false);
    current = node;
    while (current != 0) {
        if (SetBit(m_marked_path_nodes, current)) {
            break;
        }
        if (m_ulNumWayPoints <= count) {
            srAssertFail("i < m_ulNumWayPoints", OCTPATH_CPP, 0x1c97, 0);
        }
        m_path_scratch[count] = current;
        current = m_waypoints[current].search_parent;
        ++count;
    }
    AppendScratchPath(attachment, count);
    attachment->TruncatePathAtRadius(target, separation);
    FinalizeWaypointRoute(attachment, start_waypoint != 0, m_probe_position);
    return true;
}

/* Recursive depth-first link search from `waypoint`. Collects the waypoint's
   admissible links (the same edge/surface flag filters as FindPath), extends
   each destination's accumulated link cost, keys the candidates by their
   distance to the stored target, sorts them, and either returns the first
   candidate priced beyond m_patrol_distance or recurses into each in sorted
   order. The farthest-measured candidate is parked in m_probe_cell_key for
   the fallback path in LinkAttachmentTarget. */
// FUNCTION: WIZ8 0x004615d0
unsigned short W8PathingService::RecurseTargetLinks(unsigned short waypoint)
{
    unsigned short edge;
    unsigned short destination;
    unsigned short candidates[20];
    w8_ulong keys[20];
    float distances[20];
    unsigned int count = 0;
    unsigned int index;
    unsigned int link_flags;
    float distance;
    W8PathEdge* link;

    edge = m_waypoints[waypoint].first_edge;
    if (edge != 0) {
        unsigned short* slot = candidates;
        do {
            link = m_pEdges + edge;
            link_flags = link->flags;
            if ((link_flags & W8_PATH_EDGE_TELEPORTAL) == 0) {
                destination = link->destination;
                if ((((m_waypoints[destination].flags & W8_WAYPOINT_DISABLED) == 0) &&
                     (((link_flags & W8_PATH_EDGE_DISABLED) == 0) ||
                      (((link_flags & W8_NAV_THROUGH_DOORS) != 0 &&
                        ((navigation_filter & W8_NAV_THROUGH_DOORS) != 0))))) &&
                    ((navigation_filter == 0) ||
                     ((((link_flags & W8_NAV_GROUP_MASK) == W8_NAV_GROUP_MASK ||
                        ((link_flags & navigation_filter & W8_NAV_GROUP_MASK) != 0)) &&
                       (((link_flags & W8_NAV_MOVEMENT_MASK) == W8_NAV_MOVEMENT_MASK ||
                         ((link_flags & navigation_filter & W8_NAV_MOVEMENT_MASK) != 0)))) &&
                      (((link_flags & W8_NAV_SIZE_FILTER_MASK) == W8_NAV_SIZE_FILTER_MASK ||
                        ((link_flags & navigation_filter & W8_NAV_SIZE_FILTER_MASK) != 0))))) &&
                    (destination != 0)) {
                    if (!TestBit(m_visited_path_nodes, destination)) {
                        m_waypoints[destination].path_cost =
                            link->distance + m_waypoints[waypoint].path_cost;
                        m_waypoints[destination].search_parent = waypoint;
                        srVector3T<float> to_target =
                            m_waypoints[destination].position - m_patrol_start;
                        distance = to_target.Length();
                        distances[count] = distance;
                        if (m_patrol_cost < distance) {
                            m_patrol_cost = distance;
                            m_probe_cell_key = destination;
                        }
                        srVector3T<float> link_direction =
                            m_waypoints[destination].position - m_waypoints[waypoint].position;
                        link_direction.Normalize();
                        *slot = edge;
                        keys[count] = static_cast<w8_ulong>(
                            g_path_link_cost_base -
                            glm::dot(link_direction.glmValue(), (to_target / distance).glmValue()) *
                                g_float_one_thousand);
                        ++count;
                        ++slot;
                    }
                }
            }
            edge = link->next;
        } while (edge != 0);
    }
    std::ranges::sort(std::views::zip(std::span(keys, count), std::span(candidates, count)), {},
                      [](const auto& entry) { return std::get<0>(entry); });
    SetBit(m_visited_path_nodes, waypoint);
    if (count != 0) {
        for (index = 0; index < count; ++index) {
            destination = m_pEdges[candidates[index]].destination;
            if (m_patrol_distance < distances[index]) {
                return destination;
            }
            destination = RecurseTargetLinks(destination);
            if (destination != 0) {
                return destination;
            }
        }
    }
    return 0;
}

/* Build a randomized patrol route into the attachment. When the destination
   sits within `maximum` of the attachment's start position, RecursePatrolLinks
   walks the waypoint graph for a node whose accumulated link cost clears a
   randomized target distance; farther out it falls back to the straight
   attachment path. The fallback nodes tracked in m_patrol_node and
   m_probe_cell_key supply the endpoint when no candidate qualifies.
   `velocity` is passed by callers but never read. */
// FUNCTION: WIZ8 0x00461960
bool W8PathingService::BuildPatrolPath(W8NavigatorAttachment* attachment, unsigned int flags,
                                       const srVector3T<float>* destination, float minimum,
                                       const srVector3T<float>* velocity, float maximum)
{
    if (minimum >= maximum) {
        return false;
    }
    m_patrol_max = maximum;
    m_patrol_min = minimum;
    m_patrol_cost = 0.0f;
    m_patrol_start = attachment->segment_start;
    m_patrol_destination = *destination;
    navigation_filter = flags;
    attachment->flags |= 0x200000;
    srVector3T<float> delta = m_patrol_start - m_patrol_destination;
    if (maximum < delta.Length()) {
        return BuildAttachmentPath(attachment, flags);
    }
    float roll = Random(900) + g_octree_cell_scale;
    start_waypoint = 0;
    m_patrol_distance = roll * maximum * g_float_one_thousandth + maximum;
    unsigned short usStartNode = FindWaypoint(attachment->segment_start.glmValue(), true);
    if ((usStartNode == 0) && ((usStartNode = start_waypoint) == 0)) {
        return false;
    }
    std::ranges::fill(m_visited_path_nodes, false);
    std::ranges::fill(m_marked_path_nodes, false);
    path_heap->open = {};
    m_probe_cell_key = 0;
    m_patrol_node = 0;
    unsigned int start = usStartNode;
    SetBit(m_marked_path_nodes, start);
    W8PathSurface* surfaces = m_waypoints;
    m_probe_limit = surfaces[start].visit_stamp;
    m_waypoints[start].path_cost = (surfaces[start].position - attachment->segment_start).Length();
    m_waypoints[start].search_parent = 0;
    unsigned int node = RecursePatrolLinks(usStartNode);
    if (static_cast<short>(node) == 0) {
        if (m_patrol_node == 0) {
            node = static_cast<unsigned short>(m_probe_cell_key);
        } else {
            node = m_patrol_node & 0xffff;
        }
        if (static_cast<short>(node) == 0) {
            return false;
        }
    }
    attachment->path_destination = m_waypoints[node & 0xffff].position;
    unsigned short count = 0;
    unsigned int current = node;
    unsigned short previous = 0;
    do {
        if (static_cast<unsigned short>(current) == 0) {
            break;
        }
        if (m_ulNumWayPoints <= count) {
            srAssertFail("i < m_ulNumWayPoints", OCTPATH_CPP, 0x1d87, 0);
        }
        previous = static_cast<unsigned short>(current);
        m_path_scratch[count] = previous;
        current = m_waypoints[current & 0xffff].search_parent;
        ++count;
    } while (previous != static_cast<unsigned short>(current));
    AppendScratchPath(attachment, count);
    FinalizeWaypointRoute(attachment, start_waypoint != 0, m_probe_position);
    return true;
}

/* Recursive depth-first patrol search from `waypoint`. Each pass collects the
   waypoint's admissible links (edge/surface flag filters matching FindPath),
   prices them by accumulated link cost, sorts them by the surface key, then
   accepts the first candidate whose cost clears the randomized
   patrol_distance - or the 250000 sanity bound - while the last-measured
   candidate distance still exceeds patrol_min. Otherwise it recurses into
   each candidate in sorted order. The argmin-key candidate is parked in
   m_patrol_node and the best-cost alternate in m_probe_cell_key for the
   caller's fallback. The visited set is m_visited_path_nodes. */
// FUNCTION: WIZ8 0x00461d10
unsigned short W8PathingService::RecursePatrolLinks(unsigned short waypoint)
{
    unsigned short links[20];
    w8_ulong keys[20];
    float costs[20];
    unsigned int count = 0;
    float distance;

    unsigned short link = m_waypoints[waypoint].first_edge;
    if (link != 0) {
        do {
            if (m_ulNumWayPtLinks <= static_cast<unsigned int>(link)) {
                srAssertFail("usLink < m_ulNumWayPtLinks", OCTPATH_CPP, 0x1dcf,
                             "Link index out of range");
            }
            unsigned int edge_flags = m_pEdges[link].flags;
            unsigned short next = m_pEdges[link].destination;
            if ((((edge_flags & W8_PATH_EDGE_TELEPORTAL) == 0) && (next != waypoint)) &&
                ((m_waypoints[next].flags & W8_WAYPOINT_DISABLED) == 0) &&
                (((edge_flags & W8_PATH_EDGE_DISABLED) == 0) ||
                 (((edge_flags & W8_NAV_THROUGH_DOORS) != 0) &&
                  ((navigation_filter & W8_NAV_THROUGH_DOORS) != 0))) &&
                ((navigation_filter == 0) ||
                 ((((edge_flags & W8_NAV_GROUP_MASK) == W8_NAV_GROUP_MASK) ||
                   ((edge_flags & navigation_filter & W8_NAV_GROUP_MASK) != 0)) &&
                  (((edge_flags & W8_NAV_MOVEMENT_MASK) == W8_NAV_MOVEMENT_MASK) ||
                   ((edge_flags & navigation_filter & W8_NAV_MOVEMENT_MASK) != 0)) &&
                  (((edge_flags & W8_NAV_SIZE_FILTER_MASK) == W8_NAV_SIZE_FILTER_MASK) ||
                   ((edge_flags & navigation_filter & W8_NAV_SIZE_FILTER_MASK) != 0)))) &&
                (next != 0)) {
                if (!TestBit(m_visited_path_nodes, next)) {
                    if (m_ulNumWayPoints <= static_cast<unsigned int>(next)) {
                        srAssertFail("usNextNode < m_ulNumWayPoints", OCTPATH_CPP, 0x1dd9,
                                     "Waypoint index out of range");
                    }
                    W8PathSurface* next_surface = &m_waypoints[next];
                    distance = (next_surface->position - m_patrol_destination).Length();
                    if (distance < m_patrol_max) {
                        next_surface->path_cost =
                            m_waypoints[waypoint].path_cost + m_pEdges[link].distance;
                        costs[count] = next_surface->path_cost;
                        next_surface->search_parent = waypoint;
                        links[count] = link;
                        unsigned int key = next_surface->visit_stamp;
                        keys[count] = key;
                        ++count;
                        if (key < m_probe_limit) {
                            m_probe_limit = key;
                            m_patrol_node = next;
                        } else {
                            if ((key != m_probe_limit) ||
                                (next_surface->path_cost <= m_patrol_cost)) {
                                if ((m_patrol_node == 0) &&
                                    (m_patrol_cost < next_surface->path_cost)) {
                                    m_patrol_cost = next_surface->path_cost;
                                    m_probe_cell_key = next;
                                }
                            } else {
                                m_patrol_cost = next_surface->path_cost;
                                m_patrol_node = next;
                                m_probe_cell_key = next;
                            }
                        }
                        if (0x13 < count) {
                            srAssertFail("ulLinkNum < 20", OCTPATH_CPP, 0x1e00,
                                         "Too many links for waypoint in RecursePatrolLinks");
                        }
                    }
                }
            }
            link = m_pEdges[link].next;
        } while (link != 0);
        if (1 < count) {
            std::ranges::sort(std::views::zip(std::span(keys, count), std::span(links, count)), {},
                              [](const auto& entry) { return std::get<0>(entry); });
        }
    }
    SetBit(m_visited_path_nodes, waypoint);
    for (unsigned int index = 0; index < count; ++index) {
        unsigned short next = m_pEdges[links[index]].destination;
        if (m_ulNumWayPoints <= static_cast<unsigned int>(next)) {
            srAssertFail("usNextNode < m_ulNumWayPoints", OCTPATH_CPP, 0x1e0f,
                         "Waypoint index out of range (2)");
        }
        if (((m_patrol_distance < costs[index]) || (g_patrol_cost_limit < costs[index])) &&
            (m_patrol_min < distance)) {
            return next;
        }
        unsigned short found = RecursePatrolLinks(next);
        if (found != 0) {
            return found;
        }
    }
    return 0;
}

/* Reduce both accumulated costs for one accepted waypoint and continue down
   the selected parent tree. A child participates only while it remains in the
   active waypoint bit set and names the current waypoint as its parent. The
   cost reaching zero is the retail recursion boundary. */
// FUNCTION: WIZ8 0x00462220
void W8PathingService::ReduceWaypointCosts(unsigned int waypoint, float amount)
{
    W8PathSurface* surface = &m_waypoints[waypoint];
    surface->path_cost -= amount;
    if (surface->path_cost >= g_float_zero) {
        surface->estimated_total_cost -= amount;

        unsigned short edge_index = surface->first_edge;
        while (edge_index != 0) {
            W8PathEdge* edge = &m_pEdges[edge_index];
            unsigned short child = edge->destination;
            if (child != 0 && TestBit(m_marked_path_nodes, child) &&
                m_waypoints[child].search_parent == waypoint) {
                ReduceWaypointCosts(child, amount);
            }
            edge_index = edge->next;
        }
    }
}

/* Move an integer path cell one compass step. Directions immediately outside
   the eight-value range wrap once; values still outside it leave the cell
   untouched. The jump-table order is north through north-west. */
// FUNCTION: WIZ8 0x004622d0
void __stdcall StepPathCell(int* x, int* z, int direction)
{
    if (direction < 0) {
        direction += 8;
    } else if (direction > 7) {
        direction -= 8;
    }

    switch (direction) {
    case 0:
        ++*z;
        break;
    case 1:
        ++*x;
        ++*z;
        break;
    case 2:
        ++*x;
        break;
    case 3:
        ++*x;
        --*z;
        break;
    case 4:
        --*z;
        break;
    case 5:
        --*x;
        --*z;
        break;
    case 6:
        --*x;
        break;
    case 7:
        --*x;
        ++*z;
        break;
    }
}

/* Advance the attachment's probe cursor and accept its next stored waypoint
   only when the live path grid permits the span from the supplied position.
   W8_NAV_ATTACHMENT_START_WAYPOINT makes the first probe repeat the current path
   index. */
// FUNCTION: WIZ8 0x00462de0
bool W8PathingService::AdvanceAttachmentWaypoint(const srVector3T<float>* source,
                                                 W8NavigatorAttachment* attachment)
{
    unsigned short cursor = attachment->path_cursor;
    if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        --cursor;
    }
    attachment->position_cursor = cursor;
    ++attachment->position_cursor;

    if (attachment->position_cursor < attachment->path_position_index) {
        srVector3T<float> destination = attachment->path[attachment->position_cursor].position;
        if (TestWaypointSpan(source, &destination, false, false)) {
            return true;
        }
    }
    return false;
}

/* Match a tag against the path probes collected for the current search. A
   tag-only query succeeds immediately. A spatial query must lie strictly
   farther from the probe center than the search origin was, while remaining
   strictly inside its collision radius plus the caller's own radius. */
// FUNCTION: WIZ8 0x00465970
bool W8PathingService::MatchesPathProbe(unsigned int tag, const float* radius,
                                        const srVector3T<float>* position)
{
    for (unsigned int index = 0; index < m_path_probe_count; ++index) {
        W8PathProbeVolume* probe = &m_path_probes[index];
        if (probe->tag == tag) {
            if (radius == 0) {
                return true;
            }

            srVector3T<float> delta = probe->center - *position;
            float distance = delta.Length();
            if (distance < probe->outer_radius + *radius && probe->inner_radius < distance) {
                return true;
            }
        }
    }
    return false;
}

/* Reserve the next fixed-width planner node. Storage grows by fifty records,
   is cleared in full, and retains every node through the newly issued index. */
// FUNCTION: WIZ8 0x00465A00
unsigned short W8PathingService::AllocateSearchNode()
{
    unsigned int node_index = ++m_search_node_count;

    if (m_search_nodes != 0 && node_index < m_search_node_capacity) {
        return static_cast<unsigned short>(node_index);
    }

    m_search_node_capacity += 50;
    W8PathSearchNode* new_nodes = new W8PathSearchNode[m_search_node_capacity];
    if (new_nodes == 0) {
        srAssertFail("pNewSearchNodes", OCTPATH_CPP, 0x2751, 0);
    }
    memset(new_nodes, 0, m_search_node_capacity * sizeof(W8PathSearchNode));
    if (m_search_nodes != 0) {
        memcpy(new_nodes, m_search_nodes, m_search_node_count * sizeof(W8PathSearchNode));
        delete[] m_search_nodes;
    }
    m_search_nodes = new_nodes;
    return static_cast<unsigned short>(node_index);
}

/* Walk grid-sized steps from a position toward a search node. Every crossed
   cell must resolve through the visited-node index to a live, unblocked node
   whose recorded clearance exceeds the caller's limit. */
// FUNCTION: WIZ8 0x00465AF0
bool W8PathingService::CanReachSearchNode(const srVector3T<float>* position,
                                          unsigned short target_node, float clearance)
{
    W8PathSearchNode* target = &m_search_nodes[target_node];
    srVector3T<float> delta = target->position - *position;
    float scale;
    if (static_cast<float>(fabs(delta.x)) > static_cast<float>(fabs(delta.z))) {
        scale = static_cast<float>(fabs(grid_scale / delta.x));
    } else {
        scale = static_cast<float>(fabs(grid_scale / delta.z));
    }

    srVector3T<float> probe = *position;
    float step_x = delta.x * scale;
    float step_z = delta.z * scale;
    std::unordered_map<unsigned int, int>* visited = m_pVisitedCells;
    bool blocked = false;

    while (1) {
        unsigned int key = pathCell(probe.glmValue()).key;
        unsigned int node_index = LookupIndexValue(*visited, key);

        if (static_cast<unsigned short>(node_index) == target_node || blocked) {
            return !blocked;
        }
        if (static_cast<unsigned short>(node_index) == 0 ||
            (m_search_nodes[node_index & 0xffff].flags & W8_PATH_SEARCH_BLOCKED) != 0 ||
            m_search_nodes[node_index & 0xffff].clearance <= clearance) {
            blocked = true;
        } else {
            probe.x += step_x;
            probe.z += step_z;
        }
    }
}

/* Pull the last planned point onto the requested contact shell when it only
   overshoots that shell by less than half a path cell. The adjusted point is
   also made the attachment's current point and republished to the octree. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored                                                                   \
    "-Wsometimes-uninitialized" // uninit-ok: retail uses an unset target radius/position when
// monster info or its model is absent; this can alter the published
// endpoint.
#pragma clang diagnostic ignored                                                                   \
    "-Wuninitialized" // uninit-ok: retail uses an unset target radius/position when monster info or
// its model is absent; this can alter the published endpoint.
// FUNCTION: WIZ8 0x00465D70
void W8PathingService::AdjustFinalPathEndpoint(W8NavigatorMovementState* movement, float radius,
                                               float separation)
{
    int target_location = movement->target_location_id;
    if (target_location < 0 || explicit_target) {
        return;
    }

    /* Retail leaves the target radius and position unset when the monster
       info or model is absent. */
    float target_radius;
    srVector3T<float> target_position;
    if (target_location <= 0) {
        target_radius = g_startup_world->movement.alternate_radius;
        target_position = g_startup_world->GetPosition();
    } else {
        W8MonsterInfo* info = FindMonster(target_location);
        if (info != 0 && info->p3D != 0) {
            target_radius = info->p3D->movement.alternate_radius;
            target_position = info->p3D->GetPosition();
        }
    }

    W8NavigatorAttachment* attachment = movement->attachment.get();
    srVector3T<float>* endpoint = &attachment->path[attachment->path_position_index].position;
    srVector3T<float> direction = *endpoint - target_position;
    float excess = direction.Length() - (target_radius + radius);
    if (separation < excess && excess - separation < grid_scale * g_float_half) {
        direction.SetLength((target_radius + radius + separation) * g_path_endpoint_scale);

        srVector3T<float> adjusted;
        adjusted = target_position + direction;
        *endpoint = adjusted;
        attachment->path_destination = *endpoint;

        if ((attachment->flags & 0x08000000) == 0) {
            attachment->RecordPosition(&adjusted);
            g_octree->RegisterNavigatorCell(movement->location_id, &adjusted);
        }
    }
}
#pragma clang diagnostic pop

/* Select one conditional frame for a GD prop's path cells. Entries belonging
   to every other frame first lose both prop-state bits. Entries belonging to
   the selected frame then gain the caller's state bits. The hash table may
   contain several values for one key, so the low word from the serialized
   conditional value is the identity used to find the exact pairing. */
// FUNCTION: WIZ8 0x00465fb0
void W8PathingService::UpdateConditionalPathFlags(unsigned int path_handle, unsigned short frame,
                                                  unsigned int flags)
{
    auto& index = *m_pPathValues;
    // First clear inactive frames, then apply the active frame's flags.
    for (int pass = 0; pass < 2; ++pass) {
        const bool active_frame = pass == 1;
        for (unsigned int lookup = path_handle; m_pulCondLookup[lookup] != 0; ++lookup) {
            if ((m_pusCondNodeFrames[lookup] == frame) != active_frame) continue;

            for (unsigned int key_index = m_pulCondLookup[lookup];
                 m_pulCondNodeKeys[key_index] != 0; ++key_index) {
                const unsigned int key = m_pulCondNodeKeys[key_index];
                const unsigned int wanted = m_pulCondNodeValues[key_index];
                const unsigned int current = FindConditionalPathValue(key, wanted);
                const bool update =
                    active_frame ? (flags & current) == 0 : (current & W8_PATH_CELL_DOOR) != 0;
                if (!update) continue;

                const unsigned int next =
                    active_frame ? current | flags
                                 : current & ~(W8_PATH_CELL_DOOR | W8_PATH_CELL_BLOCKED);
                RemoveIndexValue(index, key, current);
                InsertIndexValue(index, key, next);
            }
        }
    }
}

/* Notify every collidable prop overlapping the search cell. The planner's
   single-result form stops after the first notification and returns that
   prop's collection index; the ordinary form visits the complete query. */
// FUNCTION: WIZ8 0x004663D0
int W8PathingService::ProcessSearchNodeProps(unsigned short node_index, bool first_only)
{
    W8PathSearchNode* node = &m_search_nodes[node_index];
    float half_cell = grid_scale * g_float_half;

    srVector3T<float> lower;
    srVector3T<float> upper;
    srVector3T<float> lower_extent;
    srVector3T<float> upper_extent;
    float vertical_extent = grid_scale + grid_scale;
    lower_extent.Set(half_cell, 0.0f, half_cell);
    upper_extent.Set(half_cell, vertical_extent, half_cell);
    lower = node->position - lower_extent;
    upper = node->position + upper_extent;

    const auto candidates =
        g_octree->queryObjects({lower.glmValue(), upper.glmValue()}, W8_OCTREE_KIND_PROP, -1);
    unsigned int count = candidates.size();
    for (unsigned int index = 0; index < count; ++index) {
        W8Prop* prop = g_world->collidable_props[candidates[index]];
        prop->CanBeUsedFrom(node->cell_x, node->cell_z, true);
        if (first_only) {
            return candidates[index];
        }
    }
    return 0;
}

/* Collect the player and nearby active monsters whose collision volumes can
   overlap the movement search radius. The octree supplies location tags; the
   monster manager remains authoritative for resolving each live navigator.
   Retail caps the resulting probe table at five entries even though its fixed
   storage has room for ten. */
// FUNCTION: WIZ8 0x004656a0
unsigned int W8PathingService::CollectPathProbes(W8NavigatorMovementState* movement, float radius)
{
    m_path_probe_count = 0;

    float extent = g_runtime_world_scale + radius;
    srVector3T<float> lower;
    srVector3T<float> upper;
    srVector3T<float> half_extent;
    half_extent.Set(extent, extent, extent);
    lower = movement->position - half_extent;
    upper = movement->position + half_extent;

    m_path_candidates =
        g_octree->locationsInBox({lower.glmValue(), upper.glmValue()}, movement->location_id);

    srVector3T<float> player_position = g_startup_world->GetPosition();
    srVector3T<float> delta = player_position - movement->position;
    float distance = delta.Length();
    float player_radius = g_startup_world->movement.alternate_radius;
    float overlap_radius = radius;
    if (player_radius < radius) {
        overlap_radius = player_radius;
    }
    if (distance < (radius - overlap_radius * g_float_half) + player_radius) {
        W8PathProbeVolume* probe = &m_path_probes[m_path_probe_count];
        probe->tag = 0;
        probe->outer_radius = player_radius;
        probe->center = g_startup_world->GetPosition();
        ++m_path_probe_count;
    }

    for (unsigned int index = 0; index < m_path_candidates.size() && m_path_probe_count < 5;
         ++index) {
        int location_id = m_path_candidates[index];
        W8MonsterInfo* info = FindMonster(location_id, false);
        if (info != 0) {
            if (info != 0 && info->p3D != 0 && info->p3D->active) {
                W8Monster* monster = info->p3D;
                srVector3T<float> monster_position = monster->GetPosition();
                srVector3T<float> monster_delta = monster_position - movement->position;
                distance = monster_delta.Length();
                float monster_radius = monster->movement.alternate_radius;
                overlap_radius = radius;
                if (monster_radius < radius) {
                    overlap_radius = monster_radius;
                }
                if (distance < (radius - overlap_radius * g_float_half) + monster_radius) {
                    W8PathProbeVolume* probe = &m_path_probes[m_path_probe_count];
                    probe->tag = location_id;
                    probe->outer_radius = monster_radius;
                    probe->inner_radius = distance;
                    probe->center = monster->GetPosition();
                    ++m_path_probe_count;
                }
            }
        }
    }
    return m_path_probe_count;
}

srVector3T<float> W8PathingService::GetSearchTraceOffset(float bearing)
{
    float target_yaw = NormalizeAngle(m_trace_target_yaw);
    glm::mat3 rotation;
    rotation = glm::mat3(1);
    float angle = bearing - target_yaw;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
    return srVector3T<float>{rotation * trace_offset.glmValue()};
}

short W8PathingService::TraceSearchNodeToTarget(unsigned short node,
                                                W8NavigatorMovementState* movement)
{
    srVector3T<float> trace_target = movement->target_position;
    trace_target.y += trace_height_offset;
    float bearing = NormalizeAngle(GetHeadingAngle(&m_search_nodes[node].position, &trace_target));
    srVector3T<float> transformed = GetSearchTraceOffset(bearing);
    srVector3T<float> trace_source = m_search_nodes[node].position + transformed;
    return g_octree->TraceLineOfSight(&trace_source, &trace_target, true, -3, -3, true, 0);
}

/* Build a bounded grid route from the navigator's current position to its
   active attachment target. The open-chain index owns one search node per
   cell, the fixed-capacity minimum heap chooses the next node to expand, and
   the selected parent chain is collapsed into the attachment's route array. */
// FUNCTION: WIZ8 0x00463460
unsigned short W8PathingService::PlanMovement(W8NavigatorMovementState* movement, float radius,
                                              float separation)
{
    float diagonal_step = grid_scale * g_path_cardinal_scale;
    unsigned short result = 0;
    bool stop_search = false;

    if (!m_trace_configured) {
        trace_offset.Set(0.0f, 500.0f, 0.0f);
        m_trace_mode = 0;
        trace_height_offset = 500.0f;
    }

    m_search_node_count = 0;
    planner_location = movement->location_id;
    memset(m_search_nodes, 0, m_search_node_capacity * sizeof(W8PathSearchNode));
    m_probe_cell_key = 0;
    m_probe_limit = static_cast<unsigned int>(-1);
    path_heap->open = {};

    W8NavigatorAttachment* attachment = movement->attachment.get();
    attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
    std::unordered_map<unsigned int, int>* visited = m_pVisitedCells;
    visited->clear();
    attachment->flags &= ~W8_NAV_ATTACHMENT_POSITION_RECORDED;

    bool allow_dynamic = (movement->navigation_filter & W8_NAV_THROUGH_DOORS) != 0;
    srVector3T<float> start = movement->position;
    srVector3T<float> target;
    if (explicit_target) {
        target = movement->target_position;
    } else if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        target = attachment->start_waypoint;
    } else if (attachment->path_cursor < attachment->path_position_index) {
        target = attachment->path[attachment->path_cursor].position;
    } else {
        target = attachment->path_destination;
    }

    srVector3T<float> target_delta = target - start;
    float target_distance = target_delta.Length();
    float remaining_callback = movement->callback_threshold - movement->callback_progress;

    if ((attachment->flags & W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION) == 0) {
        float search_extent = target_distance;
        if (search_extent < remaining_callback) {
            search_extent = remaining_callback;
        }
        search_extent += g_runtime_world_scale + radius;
        srVector3T<float> lower;
        srVector3T<float> upper;
        srVector3T<float> half_extent;
        half_extent.Set(search_extent, search_extent, search_extent);
        lower = start - half_extent;
        upper = start + half_extent;
        CollectPathProbes(movement, radius);
        m_path_candidates =
            g_octree->locationsInBox({lower.glmValue(), upper.glmValue()}, movement->location_id);

    } else {
        m_path_probe_count = 0;
        if (movement->target_location_id < 1) {
            m_path_candidates.clear();
        } else {
            m_path_candidates = {static_cast<w8_ulong>(movement->target_location_id)};
        }
    }

    srVector2i root_cell;
    const auto converted_cell = pathCell(start.glmValue());
    root_cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int root_key = converted_cell.key;
    unsigned short root_index = AllocateSearchNode();
    W8PathSearchNode* root = &m_search_nodes[root_index];
    root->flags = 0;
    root->node_index = root_index;
    root->cell_x = static_cast<unsigned short>(root_cell.x);
    root->cell_z = static_cast<unsigned short>(root_cell.y);
    root->path_height = static_cast<unsigned short>(
        static_cast<int>((start.y - level_bounds.minimum.y) / span) + 1);
    root->parent_node = 0;
    root->base_score = 0.0f;
    root->path_cost = 0.0f;
    root->distance = target_distance;
    root->position = start;

    int root_value = root_index;
    visited->emplace(root_key, root_value);

    unsigned int root_height = root->path_height;
    float root_clearance = radius;
    float root_vertical = 0.0f;
    bool root_dynamic = false;
    ResolvePathCell(root_key, true, &root_height, &root_clearance, &root_vertical, &root_dynamic);
    UpdateSearchNodeScore(root_index, &target, root_clearance, radius);
    unsigned int root_score = static_cast<unsigned int>(root->score);
    if (root_score < m_probe_limit) {
        m_probe_limit = root_score;
        m_probe_cell_key = root_index;
    }
    m_probe_cell_key = 0;

    W8PathOpenSet* heap = &path_heap->open;
    W8PathHeapEntry entry;
    entry.node = root_index;
    entry.priority = static_cast<unsigned int>(root->score);
    heap->push(entry);
    path_heap->root_node = heap->top().node;

    unsigned int best_node = path_heap->root_node;
    while (best_node != 0 && !stop_search && m_search_node_count < g_path_reserve) {
        W8PathSearchNode* current = &m_search_nodes[best_node];
        unsigned short current_x = current->cell_x;
        unsigned short current_z = current->cell_z;
        unsigned int current_height = current->path_height;

        for (int direction = 0; direction < 8; ++direction) {
            current = &m_search_nodes[best_node];
            unsigned int neighbor_x = current_x;
            unsigned int neighbor_z = current_z;
            if (direction >= 1 && direction <= 3) {
                ++neighbor_x;
            } else if (direction > 4) {
                --neighbor_x;
            }
            if (direction < 2 || direction > 6) {
                ++neighbor_z;
            } else if (direction > 2 && direction < 6) {
                --neighbor_z;
            }

            unsigned int key = neighbor_z * 0x10000 + neighbor_x;
            float step = (direction & 1) == 0 ? grid_scale : diagonal_step;
            float path_cost = current->path_cost + step;
            float base_score = current->base_score + step;

            unsigned int existing_index = LookupIndexValue(*visited, key);

            if (existing_index != 0) {
                W8PathSearchNode* existing = &m_search_nodes[existing_index & 0xffff];
                if ((existing->flags & W8_PATH_SEARCH_BLOCKED) != 0) {
                    continue;
                }
                int height_delta = static_cast<int>(current->path_height) -
                                   static_cast<int>(existing->path_height);
                if (height_delta < 0) {
                    height_delta = -height_delta;
                }
                base_score += height_delta * span * g_float_one_and_a_half;
                if (existing->base_score <= base_score) {
                    continue;
                }
                existing->base_score = base_score;
                existing->path_cost = path_cost;
                existing->parent_node = static_cast<unsigned short>(best_node);
                UpdateSearchNodeScore(existing_index, &target, existing->clearance, radius);
                if ((existing->flags & W8_PATH_SEARCH_CLOSED) != 0) {
                    existing->flags &= ~W8_PATH_SEARCH_CLOSED;
                    entry.node = existing->node_index;
                    entry.priority = static_cast<unsigned int>(existing->score);
                    heap->push(entry);
                    path_heap->root_node = heap->top().node;
                }
                continue;
            }

            unsigned int height = current_height;
            float clearance = radius;
            float vertical = base_score;
            bool dynamic = false;
            if (!ResolvePathCell(key, allow_dynamic, &height, &clearance, &vertical, &dynamic)) {
                continue;
            }

            unsigned short node_index = AllocateSearchNode();
            int node_value = node_index;
            visited->emplace(key, node_value);

            W8PathSearchNode* node = &m_search_nodes[node_index];
            node->flags = dynamic ? W8_PATH_SEARCH_INACTIVE_CELL : 0;
            node->node_index = node_index;
            node->cell_x = static_cast<unsigned short>(neighbor_x);
            node->cell_z = static_cast<unsigned short>(neighbor_z);
            node->path_height = static_cast<unsigned short>(height);
            node->parent_node = static_cast<unsigned short>(best_node);
            node->base_score = vertical;
            node->path_cost = path_cost;
            node->clearance = clearance;
            node->position.Set((node->cell_x + g_float_half) * grid_scale + level_bounds.minimum.x,
                               (node->path_height - 1) * span + level_bounds.minimum.y,
                               (node->cell_z + g_float_half) * grid_scale + level_bounds.minimum.z);

            unsigned short collision =
                ResolveSearchNodeCollisions(movement, node_index, radius, separation);
            UpdateSearchNodeScore(node_index, &target, clearance, radius);
            if (collision == 1) {
                node->flags |= W8_PATH_SEARCH_BLOCKED;
                continue;
            }
            if (collision == 3 && (m_search_nodes[best_node].flags & W8_PATH_SEARCH_BLOCKED) == 0) {
                stop_search = true;
                result = 1;
                m_probe_cell_key = node_index;
                continue;
            }
            if (collision == 2) {
                node->flags |= W8_PATH_SEARCH_BLOCKED;
            }

            srVector3T<float> node_delta = target - node->position;
            float distance = node_delta.Length();
            if (!explicit_target) {
                if (distance < diagonal_step || distance < separation) {
                    stop_search = true;
                    result = 1;
                }
            } else if (separation < distance) {
                stop_search = true;
                result = 1;
            }

            if (collision == 0) {
                unsigned int score = static_cast<unsigned int>(node->score);
                if (score < m_probe_limit) {
                    m_probe_limit = score;
                    m_probe_cell_key = node_index;
                }
            }
            entry.node = node->node_index;
            entry.priority = static_cast<unsigned int>(node->score);
            heap->push(entry);
            path_heap->root_node = heap->top().node;
        }

        path_heap->DeleteRoot(&m_search_nodes[best_node]);
        best_node = path_heap->root_node;
        if (best_node > m_search_node_count) {
            char message[80];
            sprintf(message, "A*, Invalid node index %d from Queue", best_node);
            srAssertFail("(ulBestNode <= m_ulSearchNodesUsed)", OCTPATH_CPP, 0x22ad, message);
        }
    }

    if ((attachment->flags & W8_NAV_ATTACHMENT_SEARCH_RESULT_ONLY) != 0) {
        if (!stop_search) {
            result = 3;
        }
        attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
        attachment->flags |= result;
        return result;
    }

    bool direct_path = false;
    unsigned short direct_visibility_node = 0;
    if (m_probe_cell_key != 0) {
        unsigned short walk = static_cast<unsigned short>(m_probe_cell_key);
        unsigned short walk_parent = m_search_nodes[walk].parent_node;
        while (walk_parent != 0) {
            if (!explicit_target && direct_visibility_node == 0) {
                short trace = TraceSearchNodeToTarget(walk, movement);
                if (trace != 0) {
                    m_search_nodes[walk].flags |= W8_PATH_SEARCH_EXPANDED;
                } else {
                    direct_visibility_node = walk;
                }
            }
            walk = walk_parent;
            walk_parent = m_search_nodes[walk].parent_node;
        }
        if (!explicit_target && direct_visibility_node == 0) {
            short trace = TraceSearchNodeToTarget(walk, movement);
            if (trace == 0) {
                direct_path = true;
            } else {
                m_search_nodes[walk].flags |= W8_PATH_SEARCH_EXPANDED;
            }
        }
    }

    if ((m_probe_cell_key == 0 && result == 0) || direct_path) {
        attachment->RecordPosition(&movement->position);
        attachment->path[attachment->path_position_index].position = movement->position;
        attachment->path_destination = attachment->path[attachment->path_position_index].position;
        g_octree->RegisterNavigatorCell(movement->location_id, &movement->position);
        g_startup_world->radius = g_startup_world->movement.alternate_radius;
        attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
        if (search_visualization && m_path_visualization != 0) {
            BuildSearchVisualization();
        }
        return 0;
    }

    if (m_search_node_count >= g_path_reserve || best_node == 0) {
        result = 3;
    }

    unsigned short selected = static_cast<unsigned short>(m_probe_cell_key);
    m_search_nodes[selected].flags |= W8_PATH_SEARCH_ROUTE;
    unsigned short previous = selected;
    unsigned short parent = m_search_nodes[selected].parent_node;
    while (parent != 0) {
        W8PathSearchNode* parent_node = &m_search_nodes[parent];
        parent_node->flags |= W8_PATH_SEARCH_ROUTE;
        unsigned short next_parent = parent_node->parent_node;
        parent_node->parent_node = previous;
        previous = parent;
        parent = next_parent;
    }
    m_search_nodes[selected].parent_node = 0;

    if (search_visualization && m_path_visualization != 0) {
        BuildSearchVisualization();
    }

    unsigned short route_node = previous;
    attachment->path_position_index = 1;
    unsigned int prop_count = 0;
    unsigned short anchor_node = previous;
    unsigned short route_parent = m_search_nodes[route_node].parent_node;
    while (route_parent != 0) {
        W8PathSearchNode* node = &m_search_nodes[route_parent];
        if ((node->flags & W8_PATH_SEARCH_INACTIVE_CELL) != 0) {
            if ((attachment->flags & 0x08000000) == 0) {
                int prop = ProcessSearchNodeProps(route_parent, false);
                if (prop != 0) {
                    attachment->path[prop_count++].value = static_cast<unsigned short>(prop);
                }
            } else {
                ProcessSearchNodeProps(route_parent, true);
            }
        }

        if (!CanReachSearchNode(&m_search_nodes[anchor_node].position, route_parent, radius)) {
            attachment->AppendPathPosition(&m_search_nodes[route_node].position, 0);
            anchor_node = route_node;
        }

        unsigned short next = node->parent_node;
        if (m_trace_configured) {
            if ((node->flags & W8_PATH_SEARCH_EXPANDED) != 0 ||
                !TestSearchPositionVisibility(&node->position, movement)) {
                node->flags |= 8;
            } else {
                next = 0;
                result = 1;
                m_probe_cell_key = route_parent;
            }
        }

        route_node = route_parent;
        route_parent = next;
        if (direct_visibility_node != 0 && route_node == direct_visibility_node) {
            m_probe_cell_key = route_node;
            break;
        }
        if (route_node == 0 || m_search_nodes[route_node].path_cost > remaining_callback) {
            continue;
        }
        m_probe_cell_key = route_node;
        break;
    }

    attachment->AppendPathPosition(&m_search_nodes[anchor_node].position, 0);
    attachment->path[prop_count].value = 0;

    if (attachment->path_position_index > 1) {
        --attachment->path_position_index;
        attachment->path_destination = attachment->path[attachment->path_position_index].position;
    }
    if ((attachment->flags & 0x08000000) == 0) {
        attachment->RecordPosition(&m_search_nodes[m_probe_cell_key].position);
        g_octree->RegisterNavigatorCell(movement->location_id,
                                        &m_search_nodes[m_probe_cell_key].position);
    }
    if (movement->target_location_id >= 0 && !explicit_target) {
        AdjustFinalPathEndpoint(movement, radius, separation);
    }
    attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
    attachment->flags |= result;
    g_startup_world->radius = g_startup_world->movement.alternate_radius;
    return result;
}

/* Plan with an explicit target position. The service flag suppresses the core
   planner's ordinary post-search callback for exactly this nested call, while
   the planner's status is passed straight back to the navigator caller. */
// FUNCTION: WIZ8 0x00464ab0
unsigned short W8PathingService::PlanMovementToPosition(W8NavigatorMovementState* movement,
                                                        const srVector3T<float>* target,
                                                        float radius, float separation)
{
    explicit_target = true;
    movement->target_position = *target;
    unsigned short result = PlanMovement(movement, radius, separation);
    explicit_target = false;
    return result;
}

/* Refresh one planner node's distance and accumulated score. Explicit-target
   mode scores from the shared ceiling; ordinary mode starts from the node's
   base score and adds a range penalty only when the adjusted gap is positive.
   Flag 0x2000 applies the final fixed penalty in either mode. */
// FUNCTION: WIZ8 0x00464ff0
float W8PathingService::UpdateSearchNodeScore(unsigned short node_index,
                                              const srVector3T<float>* position, float minimum,
                                              float maximum)
{
    W8PathSearchNode* node = &m_search_nodes[node_index];
    float distance = (*position - node->position).Length();
    node->distance = distance;

    if (!explicit_target) {
        node->score = distance * g_float_one_and_a_half + node->base_score;
    } else {
        node->score = g_float_one_million - distance;
    }

    float gap = maximum - minimum;
    if (!explicit_target) {
        float adjusted_gap = gap;
        if (distance <= gap) {
            adjusted_gap = (gap - distance) * g_float_nine_tenths;
        }
        if (adjusted_gap > g_float_zero) {
            node->score += gap * g_path_gap_penalty_scale;
        }
    } else if (gap > g_float_zero) {
        node->score += g_float_one_million;
    }

    if ((node->flags & W8_PATH_SEARCH_PROBE_OVERLAP) != 0) {
        node->score += g_float_one_million;
    }
    return node->score;
}

/* Resolve dynamic navigator overlap for one candidate search node.

   The player is tag zero; a target navigator receives the caller's separation
   allowance, while every other live monster uses only the two radii. Probe
   volumes can mark the node as hard-blocked before the live object lookup.
   Shallow overlaps move the node outward and set flag 0x200; deeper or
   directionally conflicting overlaps return the retail collision state. */
// FUNCTION: WIZ8 0x00465130
unsigned short W8PathingService::ResolveSearchNodeCollisions(W8NavigatorMovementState* movement,
                                                             unsigned short node_index,
                                                             float radius, float separation)
{
    unsigned short result = 0;
    if (explicit_target) {
        separation = 0.0f;
    }

    W8PathSearchNode* node = &m_search_nodes[node_index];
    srVector3T<float> blocking_direction;
    srVector3T<float> player_position = g_startup_world->GetPosition();
    srVector3T<float> player_delta = player_position - node->position;
    float distance = player_delta.Length();
    float threshold = g_startup_world->movement.alternate_radius + radius;
    if (movement->target_location_id == 0) {
        threshold += separation;
    }

    if (distance < threshold) {
        if (movement->target_location_id != 0 || explicit_target) {
            return 1;
        }
        blocking_direction.Set(player_delta.x, player_delta.y, player_delta.z);
        blocking_direction.Normalize();
        result = 3;
    }

    for (unsigned int candidate = 0; candidate < m_path_candidates.size(); ++candidate) {
        int location_id = m_path_candidates[candidate];
        W8MonsterInfo* info = FindMonster(location_id, false);
        if (info == 0) {
            continue;
        }

        unsigned int probe_index;
        for (probe_index = 0; probe_index < m_path_probe_count; ++probe_index) {
            W8PathProbeVolume* probe = &m_path_probes[probe_index];
            if (probe->tag == static_cast<unsigned int>(location_id)) {
                float probe_distance = (probe->center - node->position).Length();
                if (probe_distance < radius + probe->outer_radius &&
                    probe->inner_radius < probe_distance) {
                    node->flags |= W8_PATH_SEARCH_PROBE_OVERLAP;
                    break;
                }
            }
        }
        if (probe_index < m_path_probe_count) {
            continue;
        }
        if (info->p3D == 0 || !info->p3D->active) {
            continue;
        }

        W8Monster* monster = info->p3D;
        srVector3T<float> monster_position = monster->GetPosition();
        srVector3T<float> delta = node->position - monster_position;
        float distance = delta.Length();
        threshold = monster->movement.alternate_radius + radius;
        if (location_id == movement->target_location_id && !explicit_target) {
            threshold += separation;
        }

        if (distance < threshold) {
            if (location_id == movement->target_location_id && !explicit_target) {
                blocking_direction.Set(delta.x, delta.y, delta.z);
                blocking_direction.Normalize();
                result = 3;
                continue;
            }

            bool adjust = false;
            if ((node->flags & W8_PATH_SEARCH_DISPLACED) == 0 &&
                threshold <= distance + g_path_overlap_tolerance) {
                if (result == 3) {
                    srVector3T<float> direction = delta;
                    direction.Normalize();
                    float dot = glm::dot(direction.glmValue(), blocking_direction.glmValue());
                    if (dot <= g_path_blocking_alignment_lower_bound ||
                        g_path_blocking_alignment_upper_bound <= dot) {
                        adjust = true;
                    }
                } else if (result != 1) {
                    adjust = true;
                }
            }

            if (adjust) {
                delta.SetLength(threshold - distance);
                node->position += delta;
                node->flags |= W8_PATH_SEARCH_DISPLACED;
                if (result != 1) {
                    continue;
                }
            }

            if (!g_path_continue_after_blocker) {
                return 1;
            }
            result = 2;
        }
    }
    return result;
}

/* Test whether a candidate search position has the configured range and line
   of sight to the movement target. The trace origin is an offset rotated from
   the candidate-to-target bearing into the configured target yaw; the trace
   endpoint is the movement target with its configured vertical adjustment. */
// FUNCTION: WIZ8 0x00464cc0
bool W8PathingService::TestSearchPositionVisibility(const srVector3T<float>* position,
                                                    W8NavigatorMovementState* movement)
{
    W8Monster* monster = GetMonsterByLocationID(static_cast<unsigned int>(movement->location_id));
    float distance;
    if (m_trace_target_location == -1) {
        distance = monster->GetPointDistanceToPlayer(*position);
    } else {
        W8Monster* target = GetMonsterByLocationID(m_trace_target_location);
        distance = monster->GetPointDistanceToMonster(target, *position);
    }
    if (m_trace_max_distance < distance) {
        return false;
    }

    srVector3T<float> movement_target = movement->target_position;
    float bearing = NormalizeAngle(GetHeadingAngle(position, &movement_target));
    srVector3T<float> transformed = GetSearchTraceOffset(bearing);

    srVector3T<float> trace_source = *position + transformed;
    srVector3T<float> trace_target = movement->target_position;
    trace_target.y += trace_height_offset;

    unsigned char range_mode = 0;
    if (CalcRangeDistance(W8_RANGE_TOUCH) < distance && m_trace_mode == 1) {
        range_mode = 1;
    }
    short trace =
        g_octree->TraceLineOfSight(&trace_source, &trace_target, true, movement->location_id,
                                   m_trace_target_location, true, range_mode);
    if (trace != 1 && (trace != -1 || TraceModeRejectsNoHit(m_trace_mode) != 0)) {
        return true;
    }
    return false;
}

/* Configure and run one movement search. An attachment already in path mode
   first gets a direct current-to-target segment; a nearby visible target can
   collapse that segment to the current position and finish without planning.
   The optional probe runs the same core planner under its two temporary flags
   before the ordinary authoritative call. */
// FUNCTION: WIZ8 0x00464b00
unsigned short W8PathingService::ConfigureMovementSearch(
    W8NavigatorMovementState* movement, int target_location, float radius, float separation,
    float maximum_distance, srVector3T<float> trace_offset, int requested_trace_mode,
    float target_height_offset, float target_yaw, unsigned char* probe_result)
{
    m_trace_max_distance = maximum_distance;
    this->trace_offset = trace_offset;
    m_trace_mode = requested_trace_mode;
    m_trace_configured = true;
    trace_height_offset = target_height_offset;
    m_trace_target_yaw = target_yaw;
    if (target_location == 0) {
        m_trace_target_location = -1;
    } else {
        m_trace_target_location = target_location;
    }

    movement->target_position =
        g_octree->settleToGround(movement->target_position.glmValue(), true, 500).position;

    unsigned short result = 0;
    W8NavigatorAttachment* attachment = movement->attachment.get();
    if ((attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) != 0) {
        srVector3T<float> delta = movement->target_position - movement->position;
        // NOLINTNEXTLINE(misc-include-cleaner)
        float horizontal_clearance = glm::length(((delta.xz()))) - radius;
        float target_radius;
        if (target_location == 0) {
            target_radius = g_startup_world->movement.alternate_radius;
        } else {
            W8Monster* target = GetMonsterByLocationID(target_location);
            target_radius = target->movement.alternate_radius;
        }

        if (horizontal_clearance - target_radius <= separation &&
            TestSearchPositionVisibility(&movement->position, movement)) {
            attachment->InitializeSegment(&movement->position, &movement->position);
            m_trace_configured = false;
            if (probe_result != 0) {
                *probe_result = 0;
            }
            return 0;
        }

        attachment->InitializeSegment(&movement->position, &movement->target_position);
        attachment->separation = separation;
        if (probe_result != 0) {
            attachment->flags |=
                W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION | W8_NAV_ATTACHMENT_SEARCH_RESULT_ONLY;
            result = PlanMovement(movement, radius, separation);
            attachment->flags &= ~(W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION |
                                   W8_NAV_ATTACHMENT_SEARCH_RESULT_ONLY);
            *probe_result = result == 1;
            attachment->InitializeSegment(&movement->position, &movement->target_position);
        }
        result = PlanMovement(movement, radius, separation);
    }

    m_trace_configured = false;
    return result;
}

/* Resolve one packed path-cell entry from the open-chained index. Compatible
   height entries must be unblocked; dynamic entries additionally require the
   caller's permission and their own enabled bit. The selected packed value
   updates height, vertical offset, compass direction, and the dynamic byte. */
// FUNCTION: WIZ8 0x004648d0
bool W8PathingService::ResolvePathCell(unsigned int key, bool allow_dynamic, unsigned int* height,
                                       float* direction, float* vertical, bool* dynamic)
{
    std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
    auto slot = index->equal_range(key).first;
    bool found = false;

    while (slot != index->end() && !found) {
        unsigned int value = slot->second;
        int difference = (value & W8_PATH_CELL_HEIGHT_MASK) - *height;
        if (-cell_count < difference && difference < cell_count &&
            (value & W8_PATH_CELL_BLOCKED) == 0 &&
            ((value & W8_PATH_CELL_INACTIVE) == 0 ||
             (allow_dynamic && (value & W8_PATH_CELL_DOOR) != 0))) {
            int magnitude = difference;
            if (magnitude < 0) {
                magnitude = -magnitude;
            }
            *vertical += magnitude * span * g_float_one_and_a_half;
            *height = value & W8_PATH_CELL_HEIGHT_MASK;
            if ((value & W8_PATH_CELL_HAS_DIRECTIONS) == 0) {
                *direction = static_cast<float>(value >> 16 & 0xff);
            } else {
                *direction = 0.0f;
            }
            *direction = (*direction + g_float_half) * g_world_scale;
            *dynamic = (value & W8_PATH_CELL_INACTIVE) != 0;
            found = true;
        }

        slot = NextIndexValue(*index, key, slot);
    }
    return found;
}

/* Resolve the path cell under `position` through the path-value hash, pick the
   entry whose height is within two cell spans, and compute the slide
   direction for `delta` from its neighbor mask. */
// FUNCTION: WIZ8 0x00466600
std::optional<glm::vec3> W8PathingService::neighborSlideDirection(const glm::vec3& position,
                                                                  const glm::vec3& delta)
{
    std::unordered_multimap<unsigned int, unsigned int>* index;
    int range;
    int height;
    srVector2i cell;
    unsigned int key;
    unsigned int value;

    bool in_range;

    range = cell_count * 2;
    in_range = false;
    height = static_cast<int>((position.y - level_bounds.minimum.y) / span) + 1;
    const auto converted_cell = pathCell(position);
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    key = converted_cell.key;
    index = m_pPathValues;
    value = 0;
    auto slot = index->equal_range(key).first;
    while (slot != index->end()) {
        if (in_range) {
            unsigned int mask = ComputeWaypointNeighborMask(&cell, value);
            return slideDirection(mask, delta);
        }
        value = slot->second;
        int difference = height - static_cast<int>(value & W8_PATH_CELL_HEIGHT_MASK);
        if (-range < difference && difference < range) {
            in_range = true;
        } else {
            slot = NextIndexValue(*index, key, slot);
        }
    }
    return std::nullopt;
}

// FUNCTION: WIZ8 0x00466990
std::optional<glm::vec3> W8PathingService::obstacleDirection(const glm::vec3& delta)
{
    return slideDirection(m_waypoint_neighbor_mask, delta);
}

/* One movement step for a navigator walking an attachment path. The 0x10000
   mode follows the recorded route directly; otherwise the steering context
   advances the position and the waypoint cursor, falls through to the
   edge-transition handler, and relinks to a linked navigator's route when
   the end is reached but still out of contact. Returns whether the step
   completed the path. */
// FUNCTION: WIZ8 0x004669b0
unsigned int W8PathingService::StepAlongPath(W8NavigatorMovementState* movement, float radius,
                                             float separation)
{
    g_navigator_position_changed = true;
    W8NavigatorAttachment* attachment = movement->attachment.get();
    unsigned int flags = attachment->flags;
    bool arrived;
    if ((flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) != 0) {
        arrived = true;
        if ((flags & 0x8000000) == 0) {
            srVector3T<float>* position = &movement->position;
            srVector3T<float> advanced = *position;
            bool on_path = attachment->AdvanceAlongPathPositions(
                g_game_time_accumulator->GetFrameDelta() * movement->movement_speed *
                    movement->movement_scale * g_rate * g_world_scale,
                &advanced);
            arrived = !on_path;
            if (arrived) {
                attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
            }
            srVector3T<float> delta = advanced - *position;
            if (((delta.x != g_float_zero) || (delta.y != g_float_zero)) ||
                (delta.z != g_float_zero)) {
                srVector3T<float> target = delta + advanced;
                *position = advanced;
                attachment->path[0].position = advanced;
                attachment->path_length_origin = attachment->path[0].position;
                attachment->segment_start = attachment->path[0].position;
                movement->target_position = target;
                g_octree->UpdateMonsterLocation(movement->location_id, &advanced);
            }
        }
        return arrived;
    }
    arrived = false;
    ResolveLinkedNavigatorAttachment(movement, (flags & W8_NAV_HUGE) == 0);
    if ((attachment->path_cursor == 1) || (attachment->path[attachment->path_cursor].value == 0)) {
        bool ignore_linked_navigator =
            (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) != 0;
        m_steering_context->SteerFromPathStart(movement, ignore_linked_navigator);
        unsigned int destination_flag = attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT;
        srVector3T<float>* waypoint;
        if (destination_flag == 0) {
            if (attachment->path_cursor < attachment->path_position_index) {
                waypoint = &attachment->path[attachment->path_cursor].position;
            } else {
                waypoint = &attachment->path_destination;
            }
        } else {
            waypoint = &attachment->start_waypoint;
        }
        srVector3T<float> target = *waypoint;
        float distance = (target - movement->position).Length();
        if ((destination_flag != 0) &&
            (distance < static_cast<float>(g_monster_poster_max_distance))) {
            attachment->flags &= ~W8_NAV_ATTACHMENT_START_WAYPOINT;
        }
        unsigned short cursor = attachment->path_cursor;
        if (((cursor == 1) && (1 < attachment->path_position_index)) &&
            (attachment->path[2].value != 0)) {
            if (attachment->checkPositionHopHeight(movement->position.glmValue())) {
                ActivateMovementTrigger(movement, true);
                ++attachment->path_cursor;
            }
        } else if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0) {
            if (cursor == attachment->path_position_index) {
                bool in_range;
                if (movement->target_location_id < 0) {
                    float limit = g_startup_near_limit;
                    if ((attachment->flags & W8_NAV_ATTACHMENT_COLLISION_PREDICTED) != 0) {
                        limit = g_float_one_thousand;
                    }
                    in_range = limit <= distance;
                } else if (attachment->separation + separation <= distance) {
                    in_range = static_cast<float>(g_monster_poster_max_distance) <= distance;
                } else {
                    in_range = false;
                }
                if (in_range) {
                    ActivateMovementTrigger(movement, false);
                } else {
                    W8NavigatorAttachment* linked_attachment = m_linked_attachment;
                    if ((linked_attachment == 0) ||
                        (sqrt((attachment->path_destination.x -
                               linked_attachment->path_destination.x) *
                                  (attachment->path_destination.x -
                                   linked_attachment->path_destination.x) +
                              (attachment->path_destination.y -
                               linked_attachment->path_destination.y) *
                                  (attachment->path_destination.y -
                                   linked_attachment->path_destination.y) +
                              (attachment->path_destination.z -
                               linked_attachment->path_destination.z) *
                                  (attachment->path_destination.z -
                                   linked_attachment->path_destination.z)) <
                         static_cast<float>(g_double_one))) {
                        arrived = true;
                    } else {
                        PrepareLinkedNavigator(movement);
                    }
                }
            } else if (distance < static_cast<float>(g_monster_poster_max_distance)) {
                attachment->path_cursor = cursor + 1;
            }
        }
        ActivateMovementTrigger(movement, false);
        g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
        return arrived;
    }
    bool ignore_linked_navigator =
        (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) != 0;
    bool stepped = m_steering_context->SteerAlongPath(movement, ignore_linked_navigator);
    float distance = 0.0f;
    if (!stepped) {
        srVector3T<float> target;
        if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0) {
            if (attachment->path_cursor < attachment->path_position_index) {
                srVector3T<float>* waypoint = &attachment->path[attachment->path_cursor].position;
                target = *waypoint;
            } else {
                target = attachment->path_destination;
            }
        } else {
            target = attachment->start_waypoint;
        }
        float dx = target.x - movement->position.x;
        float dy = target.y - movement->position.y;
        target.z -= movement->position.z;
        distance = sqrt(dy * dy + target.z * target.z + dx * dx);
        float limit;
        if (attachment->path_cursor == attachment->path_position_index) {
            if (-1 < movement->target_location_id) {
                if (distance < attachment->separation + separation) {
                    goto transitioned;
                }
                limit = static_cast<float>(g_monster_poster_max_distance);
            } else {
                limit = g_startup_near_limit;
                if ((attachment->flags & W8_NAV_ATTACHMENT_COLLISION_PREDICTED) != 0) {
                    limit = g_float_one_thousand;
                }
            }
        } else {
            limit = static_cast<float>(g_monster_poster_max_distance);
        }
        if (limit <= distance) {
            g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
            return arrived;
        }
    }
transitioned: {
    unsigned char transition = HandlePathEdgeTransition(movement);
    if (transition == 1) {
        if (attachment->path_cursor != attachment->path_position_index) {
            ActivateMovementTrigger(movement, true);
            m_waypoints[attachment->path[attachment->path_cursor].value].visit_stamp =
                static_cast<unsigned int>(g_game_time_accumulator->GetElapsed());
            ++attachment->path_cursor;
            g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
            return arrived;
        }
        W8NavigatorAttachment* linked_attachment = m_linked_attachment;
        if ((linked_attachment != 0) &&
            (static_cast<float>(g_double_one) <=
             sqrt((attachment->path_destination.x - linked_attachment->path_destination.x) *
                      (attachment->path_destination.x - linked_attachment->path_destination.x) +
                  (attachment->path_destination.y - linked_attachment->path_destination.y) *
                      (attachment->path_destination.y - linked_attachment->path_destination.y) +
                  (attachment->path_destination.z - linked_attachment->path_destination.z) *
                      (attachment->path_destination.z - linked_attachment->path_destination.z)))) {
            PrepareLinkedNavigator(movement);
            g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
            return arrived;
        }
    } else if (transition != 0) {
        g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
        return arrived;
    }
}
    arrived = true;
    g_octree->UpdateMonsterLocation(movement->location_id, &movement->position);
    return arrived;
}

/* Find the moving monster's linked route, if this movement mode allows it. */
void W8PathingService::ResolveLinkedNavigatorAttachment(W8NavigatorMovementState* movement,
                                                        bool enabled)
{
    m_linked_attachment = nullptr;
    if (!enabled) return;

    W8Monster* monster = GetMonsterByLocationID(static_cast<unsigned int>(movement->location_id));
    W8Navigator* linked = monster->linked_navigator;
    if (linked) m_linked_attachment = linked->movement.attachment.get();
}

/* Rebuild the attachment route against the linked navigator's recorded path.
   The linked attachment is resolved through the moving monster's link (or
   cleared when the 0x800000 flag or a missing link says there is none), the
   route is rebuilt from the navigator's current position to the linked
   path's next live waypoint, and the linked route's remaining positions are
   appended verbatim. Returns the path-build result. */
// FUNCTION: WIZ8 0x00466fb0
bool W8PathingService::PrepareLinkedNavigator(W8NavigatorMovementState* movement)
{
    W8NavigatorAttachment* attachment = movement->attachment.get();
    ResolveLinkedNavigatorAttachment(
        movement, (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) == 0);
    W8NavigatorAttachment* linked_attachment = m_linked_attachment;
    if (linked_attachment == 0) {
        return false;
    }
    unsigned short index = linked_attachment->path_cursor;
    if (index < linked_attachment->path_position_index) {
        do {
            if (linked_attachment->path[index].value != 0) {
                break;
            }
            ++index;
        } while (index < linked_attachment->path_position_index);
    }
    attachment->InitializeSegment(&movement->position, &linked_attachment->path[index].position);
    bool built = BuildAttachmentPath(attachment, movement->navigation_filter);
    if (built) {
        ++index;
        attachment->follow_offset =
            attachment->path_position_index - linked_attachment->path_cursor;
        linked_attachment = m_linked_attachment;
        if (index <= linked_attachment->path_position_index) {
            do {
                unsigned short surface = linked_attachment->path[index].value;
                srVector3T<float>* source = &linked_attachment->path[index].position;
                attachment->AppendPathPosition(source, surface);
                linked_attachment = m_linked_attachment;
                ++index;
            } while (index <= linked_attachment->path_position_index);
        }
    }
    return built;
}

/* One movement step for a monster walking an attachment path. The scaled
   frame time is spent in 5.0-unit slices: each slice advances the position
   along the recorded route, stamps every waypoint the advance crossed with
   the accumulator's current time, and re-aims yaw at the step direction.
   Hostile groups run sight and RTAI between slices and bail when combat
   starts. `radius`/`separation` are passed by callers but never read.
   Returns whether the step completed the path. */
// FUNCTION: WIZ8 0x00467150
unsigned int W8PathingService::StepMonsterAlongPath(W8NavigatorMovementState* movement,
                                                    float radius, float separation)
{
    g_navigator_position_changed = true;
    bool arrived = false;
    W8NavigatorAttachment* attachment = movement->attachment.get();
    W8MonsterInfo* monster_info = FindMonster(movement->location_id);
    W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id);
    monster_info->p3D->group_linked = true;
    float remaining = g_rate * g_game_time_accumulator->GetFrameDelta();
    do {
        if (remaining <= g_float_zero) {
            break;
        }
        float step;
        if (remaining <= g_float_five) {
            step = remaining;
        } else {
            step = 5.0f;
        }
        unsigned short previous = attachment->path_cursor;
        srVector3T<float> direction;
        arrived = attachment->AdvancePositionWithDirection(
            &movement->position, step * movement->movement_scale * g_world_scale, &direction);
        unsigned short reached = attachment->path_cursor;
        if (previous < reached) {
            do {
                attachment->path_cursor = previous;
                m_waypoints[attachment->path[attachment->path_cursor].value].visit_stamp =
                    static_cast<unsigned int>(g_game_time_accumulator->GetElapsed());
                ++previous;
            } while (previous < reached);
            attachment->path_cursor = reached;
        }
        float angle = NormalizeAngle(static_cast<float>(atan2(direction.x, direction.z)));
        movement->target_yaw = angle;
        movement->yaw = angle;
        if (group->ubDisposition == W8_DISPOSITION_HOSTILE) {
            UpdateMonsterSight(monster_info, true, false);
            DoMonsterRTAI(monster_info, true);
            if (monster_info->fInCombat) {
                break;
            }
        }
        remaining -= step;
    } while (!arrived);
    return arrived;
}

/* Give everything the service owns back. The four malloc'd tables and the
   conditional path tables go back through free, the bit sets and the two
   hash indexes through their own teardown, and the global slot the
   constructor claimed is cleared last. */
// FUNCTION: WIZ8 0x00457b10
W8PathingService::~W8PathingService()
{
    free(file_path_nodes);
    free(m_waypoints);
    free(m_pEdges);
    free(m_pFileWayPoints);
    m_path_visualization.reset();
    delete m_pPathValues;
    delete m_pVisitedCells;
    delete path_heap;
    if (m_search_nodes != 0) {
        delete[] m_search_nodes;
    }
    if (m_steering_context != 0) {
        delete m_steering_context;
    }
    free(m_pCondPaths);
    free(m_pulCondLookup);
    free(m_pusCondNodeFrames);
    free(m_pulCondNodeKeys);
    free(m_pulCondNodeValues);
    g_pathing = 0;
}

/* Build the pathing service.

   Everything starts cleared except three hundred-bit sets, a reserve table
   sized from the shared bound, and one state object. The service registers
   itself in the global slot as it is built, which is what lets the rest of the
   engine reach it without the octree handing it over. */
// FUNCTION: WIZ8 0x004578e0
W8PathingService::W8PathingService()
{
    grid_scale = 0;
    span = 0;
    level_bounds.minimum.SetZero();
    level_bounds.maximum.SetZero();
    file_path_nodes = 0;
    path_node_count = 0;
    edge_node_count = 0;
    m_ulNumWayPoints = 0;
    m_ulNumWayPtLinks = 0;
    m_unknown_014 = 0;
    m_removed_edge_count = 0;
    m_waypoints = 0;
    m_pEdges = 0;
    m_pFileWayPoints = 0;
    m_path_visualization = 0;
    m_marked_path_nodes.assign(100, false);
    m_visited_path_nodes.assign(100, false);
    m_collected_path_nodes.assign(100, false);
    m_pPathValues = 0;
    m_pVisitedCells = 0;
    level_name = 0;
    m_probe_bounded = false;
    path_heap = 0;
    m_path_cost_limit = 1.0e10f;
    m_waypoint_editing = false;
    draw_waypoints = false;
    flag2 = false;
    search_visualization = false;
    waypoints_dirty = false;
    waypoint_editor_flags = 4;
    link_flags = 0;
    start_waypoint = 0;
    destination_waypoint = 0;
    saved_surface = 0;
    m_search_nodes = new W8PathSearchNode[g_path_reserve + 0x14];
    m_search_node_count = 0;
    m_search_node_capacity = 0;
    explicit_target = false;
    m_trace_configured = false;
    m_trace_target_location = 0;
    m_trace_max_distance = 0;
    trace_offset.SetZero();
    m_trace_mode = 0;
    trace_height_offset = 0;
    m_trace_target_yaw = 0;
    m_steering_context = new W8PathParameters();
    m_pCondPaths = 0;
    m_ulNumCondPaths = 0;
    m_ulNumCondFrames = 0;
    m_ulNumCondNodes = 0;
    m_pulCondLookup = 0;
    m_pusCondNodeFrames = 0;
    m_pulCondNodeKeys = 0;
    m_pulCondNodeValues = 0;
    g_pathing = this;
    g_runtime_world_scale = 500.0f;
}

/* Take the octree's own minimum/maximum bounds pair and level name. The span is the vertical extent
   of that box scaled down to one height unit; the cell count is how many of those units fit in
   1.25 grid cells, plus one, and bounds the vertical band the height lookups accept. */
// FUNCTION: WIZ8 0x00458a50
void W8PathingService::ConfigureForLevel(int size, float grid_scale, float path_clearance,
                                         const srBoundingBox& bounds, const char* name)
{
    path_node_count = size;
    this->grid_scale = grid_scale;
    this->path_clearance = path_clearance;
    level_bounds.minimum = bounds.minimum;
    level_bounds.maximum = bounds.maximum;
    span = (level_bounds.maximum.y - level_bounds.minimum.y) * g_path_span_scale;
    cell_count = static_cast<short>(static_cast<int>(grid_scale * 1.25f / span) + 1);
    level_name = name;
}

/* Classify a waypoint from the path index cell beneath it.

   X and Z form the hash key. Entries with that key carry a one-based vertical
   cell in their low half; among candidates inside the service's vertical span,
   the closest height wins and its complete packed value is returned. */
// FUNCTION: WIZ8 0x00459c00
unsigned int W8PathingService::ClassifyWaypoint(const srVector3T<float>* position)
{
    unsigned int key = pathCell(position->glmValue()).key;
    unsigned int result = 0;

    if (key != 0) {
        std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
        auto slot = index->equal_range(key).first;
        int height = static_cast<int>((position->y - level_bounds.minimum.y) / span) + 1;
        int nearest = 0x0fffffff;

        while (slot != index->end()) {
            unsigned int value = slot->second;
            int delta = (value & W8_PATH_CELL_HEIGHT_MASK) - height;

            if (delta < 0) {
                delta = -delta;
            }
            if (delta < cell_count && delta < nearest) {
                nearest = delta;
                result = value;
            }
            slot = NextIndexValue(*index, key, slot);
        }
    }
    return result;
}

// FUNCTION: WIZ8 0x00459d60
unsigned int W8PathingService::FindPathCell(srVector3T<float>* position, glm::uvec2* cell,
                                            bool adjust)
{
    int path_height = static_cast<int>((position->y - level_bounds.minimum.y) / span) + 1;
    srVector2i source_cell;
    const auto converted_cell = pathCell(position->glmValue());
    source_cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int key = converted_cell.key;
    unsigned int source_x = source_cell.x;
    unsigned int source_z = source_cell.y;
    unsigned int selected_x = source_x;
    unsigned int selected_z = source_z;
    unsigned int selected_key = 0;
    /* A compatible neighbor marks success before the nearest-distance test;
       retail can leave this height unset when that test rejects every match. */
    unsigned int selected_height;
    bool found = false;
    float closest_distance = 10000000.0f;
    auto slot = m_pPathValues->equal_range(key).first;

    while (slot != m_pPathValues->end()) {
        unsigned int value = slot->second;
        unsigned int height = value & W8_PATH_CELL_HEIGHT_MASK;
        int difference = path_height - height;

        if ((value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < difference &&
            difference < cell_count) {
            selected_key = key;
            selected_height = height;
            found = true;
            break;
        }
        slot = NextIndexValue(*m_pPathValues, key, slot);
    }

    if (!found) {
        int direction;

        for (direction = 0; direction < 8; ++direction) {
            unsigned int candidate_x;
            unsigned int candidate_z = source_z;

            if (direction < 1 || direction > 3) {
                candidate_x = source_x;
                if (direction > 4) {
                    --candidate_x;
                }
            } else {
                candidate_x = source_x + 1;
            }
            if (direction < 2 || direction > 6) {
                ++candidate_z;
            } else if (direction > 2 && direction < 6) {
                --candidate_z;
            }

            key = candidate_z * 0x10000 + candidate_x;
            slot = m_pPathValues->equal_range(key).first;
            while (slot != m_pPathValues->end()) {
                unsigned int value = slot->second;
                unsigned int height = value & W8_PATH_CELL_HEIGHT_MASK;
                int difference = path_height - height;

                if ((value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < difference &&
                    difference < cell_count) {
                    found = true;
                    float x = (candidate_x + g_float_half) * grid_scale -
                              (position->x - level_bounds.minimum.x);
                    float z = (candidate_z + g_float_half) * grid_scale -
                              (position->z - level_bounds.minimum.z);
                    float distance = x * x + z * z;

                    if (distance < closest_distance) {
                        selected_key = key;
                        selected_height = height;
                        selected_x = candidate_x;
                        selected_z = candidate_z;
                        closest_distance = distance;
                    }
                    break;
                }
                slot = NextIndexValue(*m_pPathValues, key, slot);
            }
        }
    }

    if (adjust && found) {
        position->Set((selected_x + g_float_half) * grid_scale + level_bounds.minimum.x,
                      (selected_height - 1) * span + level_bounds.minimum.y,
                      (selected_z + g_float_half) * grid_scale + level_bounds.minimum.z);
    }
    if (cell != 0) {
        cell->x = selected_x;
        cell->y = selected_z;
    }
    return selected_key;
}

/* The two near-cell operations share the height window and optional
   snap. Clearance additionally requires an active cell and its packed flags. */
bool W8PathingService::FindNearbyPathCell(srVector3T<float>* position, bool snap_to_cell,
                                          bool require_active, unsigned int* packed_out)
{
    const int vertical_window = cell_count * 2;
    const unsigned int height =
        static_cast<unsigned int>(static_cast<int>((position->y - level_bounds.minimum.y) / span)) +
        1;
    srVector2i cell;
    const auto converted_cell = pathCell(position->glmValue());
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    const unsigned int key = converted_cell.key;
    auto slot = m_pPathValues->equal_range(key).first;

    while (slot != m_pPathValues->end()) {
        const unsigned int packed = slot->second;
        const unsigned int matched_height = packed & W8_PATH_CELL_HEIGHT_MASK;
        const int delta = static_cast<int>(height - matched_height);
        if ((!require_active || (packed & W8_PATH_CELL_INACTIVE) == 0) &&
            -vertical_window < delta && delta < vertical_window) {
            if (snap_to_cell) {
                position->Set((cell.x + g_float_half) * grid_scale + level_bounds.minimum.x,
                              (matched_height - 1) * span + level_bounds.minimum.y,
                              (cell.y + g_float_half) * grid_scale + level_bounds.minimum.z);
            }
            if (packed_out) *packed_out = packed;
            return true;
        }
        slot = NextIndexValue(*m_pPathValues, key, slot);
    }
    return false;
}

/* Test whether a position lies in the vertical neighborhood represented by its
   X/Z path-index cell, optionally snapping it onto that indexed cell.

   The accepted vertical window is twice the service's cell count in either
   direction. X and Z snap to the horizontal cell centers; Y snaps to the exact
   one-based height carried by the matching packed index value. */
// FUNCTION: WIZ8 0x00462e60
bool W8PathingService::SnapWaypointPosition(srVector3T<float>* position, bool snap_to_cell)
{
    return FindNearbyPathCell(position, snap_to_cell, false, nullptr);
}

/* Test the first static path-index value in the position's vertical
   neighborhood. The packed direction byte becomes a world-space clearance;
   the special direction flag selects the global fallback instead. A successful
   lookup may also move the position onto the indexed cell before testing that
   clearance. */
// FUNCTION: WIZ8 0x00463040
bool W8PathingService::TestPathCellClearance(srVector3T<float>* position, float clearance,
                                             bool snap_to_cell)
{
    unsigned int packed;
    if (!FindNearbyPathCell(position, snap_to_cell, true, &packed)) return false;

    float direction = g_float_zero;
    if ((packed & W8_PATH_CELL_HAS_DIRECTIONS) == 0)
        direction = static_cast<float>((packed >> 16) & 0xff);
    return clearance < direction * g_world_scale + grid_scale * g_float_half;
}

/* Snap a position to the closest eligible indexed height no higher than its
   own. Directional entries are ignored unless the caller explicitly permits
   them; X and Z always move to the chosen cell's center. */
// FUNCTION: WIZ8 0x00463290
bool W8PathingService::SnapToLowerPathCell(srVector3T<float>* position, bool allow_directional)
{
    bool found = false;
    int nearest = 10000000;
    unsigned int height = static_cast<unsigned int>(
                              static_cast<int>(((position->y - level_bounds.minimum.y) / span))) +
                          1;
    srVector2i cell;
    const auto converted_cell = pathCell(position->glmValue());
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int key = converted_cell.key;
    unsigned int matched_height = 0;
    std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
    auto slot = index->equal_range(key).first;

    while (slot != index->end()) {
        const auto value = slot->second;

        if (allow_directional || (value & W8_PATH_CELL_NEIGHBOR_OR_DEPTH_MASK) == 0) {
            unsigned int candidate_height = value & 0xffff;
            int delta = static_cast<int>(height - candidate_height);

            if (delta >= 0 && delta < nearest) {
                found = true;
                nearest = delta;
                matched_height = candidate_height;
            }
        }
        slot = NextIndexValue(*index, key, slot);
    }

    if (found) {
        position->Set((cell.x + g_float_half) * grid_scale + level_bounds.minimum.x,
                      (matched_height - 1) * span + level_bounds.minimum.y,
                      (cell.y + g_float_half) * grid_scale + level_bounds.minimum.z);
    }
    return found;
}

/* Search the short arc between an attachment's two endpoint positions. The
   temporary index is rebuilt before the paired directed probes, so both walks
   share only the path position they discover. */
// FUNCTION: WIZ8 0x00462360
bool W8PathingService::ProbeAttachmentPath(W8NavigatorAttachment* attachment)
{
    float distance = (attachment->segment_start - attachment->path_destination).Length();

    if (distance > g_path_segment_split_distance) {
        return false;
    }

    m_probe_bounded = false;
    std::unordered_map<unsigned int, int>* visited = m_pVisitedCells;
    visited->clear();

    m_probe_cell_key = 0;
    m_probe_limit = 0;
    ProbeWaypointArc(&attachment->segment_start, &attachment->path_destination);
    m_probe_bounded = false;
    m_probe_limit = 0xffffffff;
    ProbeWaypointArc(&attachment->path_destination, &attachment->segment_start);

    if (m_probe_cell_key == 0) {
        return false;
    }
    attachment->start_waypoint = m_probe_position;
    attachment->flags |= W8_NAV_ATTACHMENT_START_WAYPOINT;
    return true;
}

/* Sweep probes around the arc defined by a pair of waypoint positions.

   The accumulator starts perpendicular to the pair's horizontal direction.
   Every iteration probes that offset, advances by one grid-scale tangent step,
   and renormalizes to the pair's original radius. The dot product identifies
   when the sweep has passed its forward threshold and the walk stops after it
   subsequently crosses behind the starting direction. */
// FUNCTION: WIZ8 0x00462570
void W8PathingService::ProbeWaypointArc(const srVector3T<float>* from, const srVector3T<float>* to)
{
    srVector3T<float> direction;
    srVector3T<float> arc;
    float radius;
    bool passed_forward = false;
    unsigned int iteration;

    direction = *to - *from;
    radius = direction.Length();
    arc.Set(-direction.z, 0.0f, direction.x);

    for (iteration = 0; iteration < 50000; ++iteration) {
        srVector3T<float> probe;
        srVector3T<float> step;
        float dot;

        probe = *from + arc;
        ProbeWaypointSegment(from, &probe);

        step.Set(arc.z, arc.y, -arc.x);
        step.SetLength(grid_scale);
        arc += step;

        arc.SetLength(radius);

        dot = glm::dot(direction.glmValue(), arc.glmValue());
        if (dot > g_float_nine_tenths) {
            passed_forward = true;
        } else if (!passed_forward) {
            continue;
        }
        if (dot < g_float_zero) {
            return;
        }
    }
}

/* Convert the signed steps on the walk's driving and secondary axes into the
   four horizontal direction codes consumed by the segment probe. */
// FUNCTION: WIZ8 0x0045aee0
void W8PathingService::GetPathGridStepDirections(const W8PathGridWalk* walk, int* directions)
{
    if (walk->major_axis != 0) {
        if (walk->step.y < 1) {
            directions[0] = 4;
            if (walk->step.x > 0) {
                directions[1] = 2;
                return;
            }
        } else {
            directions[0] = 0;
            if (walk->step.x > 0) {
                directions[1] = 2;
                return;
            }
        }
        directions[1] = 6;
        return;
    }

    if (walk->step.x < 1) {
        directions[0] = 6;
    } else {
        directions[0] = 2;
    }
    if (walk->step.y > 0) {
        directions[1] = 0;
    } else {
        directions[1] = 4;
    }
}

/* Build the two-dimensional Bresenham record used to walk path-index cells.

   Coordinates are first converted to integer distances from the level origin.
   The larger absolute delta drives the walk; the start-cell remainder fixes
   how far each axis is from its next boundary and therefore the initial error. */
// FUNCTION: WIZ8 0x0045af60
void W8PathingService::BuildPathGridWalk(const glm::vec2* from, const glm::vec2* to,
                                         const glm::vec2* origin, W8PathGridWalk* walk)
{
    int cell_size = static_cast<int>(grid_scale);
    srVector2i coordinate;
    srVector2i destination;
    srVector2i step;
    srVector2i absolute_delta;
    glm::vec2 boundary_offset;
    glm::vec2 signed_delta;
    int major_axis = 0;
    int largest_delta = 0;
    int axis;

    for (axis = 0; axis < 2; ++axis) {
        (&coordinate.x)[axis] = static_cast<int>((&from->x)[axis] - (&origin->x)[axis]);
        (&destination.x)[axis] = static_cast<int>((&to->x)[axis] - (&origin->x)[axis]);

        int delta = (&destination.x)[axis] - (&coordinate.x)[axis];
        (&boundary_offset.x)[axis] = (&coordinate.x)[axis] % cell_size / grid_scale;
        (&signed_delta.x)[axis] = delta;

        if (delta < 0) {
            (&step.x)[axis] = -1;
            delta = -delta;
        } else {
            (&step.x)[axis] = 1;
            (&boundary_offset.x)[axis] = g_float_one - (&boundary_offset.x)[axis];
        }
        if (largest_delta < delta) {
            largest_delta = delta;
            major_axis = axis;
        }
        (&absolute_delta.x)[axis] = delta;
    }

    int minor_axis = (major_axis + 1) % 2;
    float ratio = (&signed_delta.x)[minor_axis] / (&signed_delta.x)[major_axis];
    int error_delta = static_cast<int>((ratio < 0.0f ? -ratio : ratio) * grid_scale);
    int error = static_cast<int>(cell_size * (&boundary_offset.x)[minor_axis] -
                                 error_delta * (&boundary_offset.x)[major_axis]);
    int count;

    if (largest_delta % cell_size == 0) {
        count = largest_delta / cell_size;
    } else {
        count = largest_delta / cell_size + 1;
    }

    walk->major_axis = major_axis;
    walk->minor_axis0 = minor_axis;
    walk->error_reset0 = cell_size;
    walk->count = count;
    walk->cell.Set(destination.x / cell_size, destination.y / cell_size, 0);
    walk->step.Set(step.x, step.y, 0);
    walk->error_delta0 = error_delta;
    walk->error0 = error;
    walk->minor_axis1 = 0;
    walk->error_delta1 = 0;
    walk->error1 = 0;
    walk->error_reset1 = 0;
}

/* Walk every horizontal path cell crossed by a short waypoint segment.

   Each cell chooses the first vertically compatible path record. The secondary
   index carries the accumulated low-half cost and the most recent high-half
   step cost; when a bounded probe is active, the cheapest reached cell and its
   world-space center are retained on the service. Direction bits on the chosen
   path record can terminate the walk after the corresponding grid step. */
// FUNCTION: WIZ8 0x00462750
bool W8PathingService::ProbeWaypointSegment(const srVector3T<float>* from,
                                            const srVector3T<float>* to)
{
    float distance = (*to - *from).Length();

    if (grid_scale + grid_scale > distance) {
        return true;
    }

    srVector2i cell;
    glm::vec2 walk_from;
    glm::vec2 walk_to;
    glm::vec2 origin;
    W8PathGridWalk walk;
    int directions[2];

    const auto converted_cell = pathCell(from->glmValue());
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    walk_from = from->xz();
    walk_to = to->xz();
    origin = level_bounds.minimum.xz();
    BuildPathGridWalk(&walk_from, &walk_to, &origin, &walk);
    GetPathGridStepDirections(&walk, directions);

    int error = walk.error0;
    bool bounded_probe = m_probe_bounded && m_probe_limit != 0;
    unsigned int height =
        static_cast<unsigned int>(static_cast<int>(((from->y - level_bounds.minimum.y) / span))) +
        1;
    bool blocked = false;
    int iteration = 0;

    while (iteration < walk.count && !blocked) {
        unsigned int cell_key = cell.y * 0x10000 + cell.x;
        std::unordered_map<unsigned int, int>* visited_index = m_pVisitedCells;
        unsigned int visited = LookupIndexValue(*visited_index, cell_key);

        if (bounded_probe && (visited & 0xffff) != 0xffff) {
            bounded_probe = false;
        }

        unsigned int direction_mask = 0;
        unsigned int path_value = 0;
        bool found = false;

        if (iteration == 0 || (visited & 0xffff0000) != 0xffff0000 || bounded_probe) {
            std::unordered_multimap<unsigned int, unsigned int>* path_index = m_pPathValues;
            auto slot = path_index->equal_range(cell_key).first;

            while (slot != path_index->end() && !found) {
                path_value = slot->second;
                int height_delta = (path_value & W8_PATH_CELL_HEIGHT_MASK) - height;

                if ((path_value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < height_delta &&
                    height_delta < cell_count) {
                    if ((path_value & W8_PATH_CELL_HAS_DIRECTIONS) != 0) {
                        direction_mask = path_value >> 16 & 0xff;
                    }
                    found = true;
                    height = path_value & W8_PATH_CELL_HEIGHT_MASK;
                }
                slot = NextIndexValue(*path_index, cell_key, slot);
            }

            if (!found) {
                blocked = true;
            } else if (!bounded_probe && ((m_probe_limit == 0 && visited == 0) ||
                                          (m_probe_limit != 0 && (visited & 0xffff) != 0 &&
                                           (visited & 0xffff0000) == 0))) {
                srVector3T<float> position;
                unsigned int initial_cost;

                position.Set((cell.x + g_float_half) * grid_scale + level_bounds.minimum.x,
                             (height - 1) * span + level_bounds.minimum.y,
                             (cell.y + g_float_half) * grid_scale + level_bounds.minimum.z);

                initial_cost = static_cast<unsigned int>(
                    static_cast<int>(((position - *to).Length() * g_double_one_tenth)));

                if (m_probe_limit == 0) {
                    int stored_cost = initial_cost;
                    visited_index->emplace(cell_key, stored_cost);
                } else {
                    unsigned int step_cost = static_cast<unsigned int>(
                        static_cast<int>(initial_cost * g_path_initial_cost_slack));
                    unsigned int total_cost = (visited & 0xffff) + step_cost;

                    if (total_cost < m_probe_limit) {
                        m_probe_limit = total_cost;
                        m_probe_cell_key = cell_key;
                        m_probe_position = position;
                    }

                    visited_index->erase(cell_key);
                    int stored_cost = step_cost << 16 | visited;
                    visited_index->emplace(cell_key, stored_cost);
                }
            }
        } else {
            blocked = true;
        }

        unsigned int direction;
        if (error >= 0 || blocked) {
            direction = directions[0];
            (&cell.x)[walk.major_axis] += (&walk.step.x)[walk.major_axis];
            error -= walk.error_delta0;
        } else {
            direction = directions[1];
            --iteration;
            (&cell.x)[walk.minor_axis0] += (&walk.step.x)[walk.minor_axis0];
            error += walk.error_reset0;
        }
        if (direction_mask != 0 && (direction_mask & 1 << (direction & 0x1f)) == 0) {
            blocked = true;
        }
        ++iteration;
    }

    return !blocked;
}

/* Find the directions from one path cell that lead to vertically compatible
   neighboring cells. A source record carrying an explicit direction mask only
   permits those directions to be tested. As in retail, an entirely open set
   of eight neighbors is represented by zero rather than 0xff. */
// FUNCTION: WIZ8 0x004667a0
unsigned int W8PathingService::ComputeWaypointNeighborMask(const srVector2i* cell,
                                                           unsigned int path_value)
{
    unsigned int source_directions = 0;
    if ((path_value & W8_PATH_CELL_HAS_DIRECTIONS) != 0) {
        source_directions = path_value >> 16 & 0xff;
    }

    unsigned int result = 0;
    int direction;
    for (direction = 0; direction < 8; ++direction) {
        if ((path_value & W8_PATH_CELL_HAS_DIRECTIONS) == 0 ||
            (source_directions & 1 << (direction & 0x1f)) != 0) {
            srVector2i neighbor;
            neighbor.x = cell->x;
            neighbor.y = cell->y;

            if (direction >= 1 && direction <= 3) {
                ++neighbor.x;
            } else if (direction > 4) {
                --neighbor.x;
            }
            if (direction < 2 || direction > 6) {
                ++neighbor.y;
            } else if (direction > 2 && direction < 6) {
                --neighbor.y;
            }

            unsigned int key = neighbor.y * 0x10000 + neighbor.x;
            std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
            auto slot = index->equal_range(key).first;
            bool found = false;

            while (slot != index->end()) {
                unsigned int candidate = slot->second;
                int height_delta = (path_value & W8_PATH_CELL_HEIGHT_MASK) -
                                   (candidate & W8_PATH_CELL_HEIGHT_MASK);
                if ((candidate & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < height_delta &&
                    height_delta < cell_count) {
                    found = true;
                    break;
                }
                slot = NextIndexValue(*index, key, slot);
            }
            if (found) {
                result |= 1 << (direction & 0x1f);
            }
        }
    }

    if (static_cast<unsigned char>(result) == 0xff) {
        result = 0;
    }
    return result;
}

/* Test a waypoint span through the indexed path cells.

   The ordinary mode rejects endpoints outside the level and verifies the
   destination height. Adjustment mode instead snaps a failed destination to
   the last accepted cell; its alternate stepping mode combines simultaneous
   major/minor moves into the corresponding diagonal direction. Packed path
   records contribute their direction masks and two obstruction flag bits. */
// FUNCTION: WIZ8 0x0045a1b0
bool W8PathingService::TestWaypointSpan(const srVector3T<float>* source,
                                        srVector3T<float>* destination, bool adjust_destination,
                                        bool diagonal_steps)
{
    bool blocked = false;

    if (!adjust_destination &&
        (source->x < level_bounds.minimum.x || source->y < level_bounds.minimum.y ||
         source->z < level_bounds.minimum.z || level_bounds.maximum.x < source->x ||
         level_bounds.maximum.y < source->y || level_bounds.maximum.z < source->z ||
         destination->x < level_bounds.minimum.x || destination->y < level_bounds.minimum.y ||
         destination->z < level_bounds.minimum.z || level_bounds.maximum.x < destination->x ||
         level_bounds.maximum.y < destination->y || level_bounds.maximum.z < destination->z)) {
        return false;
    }

    span_blocked = false;
    srVector2i cell;
    const auto converted_cell = pathCell(source->glmValue());
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int cell_key = converted_cell.key;
    unsigned int destination_key = pathCell(destination->glmValue()).key;
    m_waypoint_neighbor_mask = 0;

    std::unordered_multimap<unsigned int, unsigned int>* path_index = m_pPathValues;

    if (cell_key == destination_key) {
        const auto findCellAtHeight = [&](const srVector3T<float>& position, unsigned int& result) {
            const unsigned int height = static_cast<unsigned int>(static_cast<int>(
                                            (position.y - level_bounds.minimum.y) / span)) +
                                        1;
            auto slot = path_index->equal_range(cell_key).first;
            bool found = false;
            while (slot != path_index->end() && !found) {
                const unsigned int value = slot->second;
                const int difference = (value & W8_PATH_CELL_HEIGHT_MASK) - height;
                if ((value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < difference &&
                    difference < cell_count) {
                    result = value;
                    found = true;
                }
                slot = NextIndexValue(*path_index, cell_key, slot);
            }
            return found;
        };

        unsigned int source_value = 0;
        if (!findCellAtHeight(*source, source_value)) return false;
        m_waypoint_neighbor_mask = ComputeWaypointNeighborMask(&cell, source_value);
        unsigned int destination_value;
        return findCellAtHeight(*destination, destination_value) &&
               destination_value == source_value;
    }

    glm::vec2 walk_source;
    glm::vec2 walk_destination;
    glm::vec2 origin;
    W8PathGridWalk walk;
    int directions[2];
    walk_source = source->xz();
    walk_destination = destination->xz();
    origin = level_bounds.minimum.xz();
    BuildPathGridWalk(&walk_source, &walk_destination, &origin, &walk);
    GetPathGridStepDirections(&walk, directions);

    int error = walk.error0;
    unsigned int height =
        static_cast<unsigned int>(static_cast<int>(((source->y - level_bounds.minimum.y) / span))) +
        1;
    unsigned int previous_key = 0;
    unsigned int previous_value;
    int iteration = 0;
    const auto updateNeighborMask = [&](unsigned int current_key, unsigned int current_value) {
        const unsigned int key = previous_key == 0 ? current_key : previous_key;
        const unsigned int value = previous_key == 0 ? current_value : previous_value;
        if (previous_key != 0 || current_key != 0) {
            srVector2i cell;
            cell.x = key & 0xffff;
            cell.y = key >> 16;
            m_waypoint_neighbor_mask = ComputeWaypointNeighborMask(&cell, value);
        }
    };

    while (iteration < walk.count && !blocked) {
        auto slot = path_index->equal_range(cell_key).first;
        unsigned int direction_mask = 0;
        unsigned int path_value;
        bool found = false;

        while (slot != path_index->end() && !found) {
            path_value = slot->second;
            int difference = (path_value & W8_PATH_CELL_HEIGHT_MASK) - height;

            if (-cell_count < difference && difference < cell_count) {
                if ((path_value & W8_PATH_CELL_INACTIVE) == 0) {
                    if ((path_value & W8_PATH_CELL_HAS_DIRECTIONS) != 0) {
                        direction_mask = path_value >> 16 & 0xff;
                    }
                    found = true;
                    height = path_value & W8_PATH_CELL_HEIGHT_MASK;
                    previous_key = cell_key;
                    previous_value = path_value;
                } else {
                    span_blocked = true;
                }
            }
            slot = NextIndexValue(*path_index, cell_key, slot);
        }

        if (!found) {
            updateNeighborMask(cell_key, path_value);
            blocked = true;
        }

        if ((previous_value & W8_PATH_CELL_CONDITIONAL) != 0) {
            span_blocked = true;
        }

        unsigned int direction = directions[0];
        if (!diagonal_steps) {
            if (error >= 0 || blocked) {
                (&cell.x)[walk.major_axis] += (&walk.step.x)[walk.major_axis];
                error -= walk.error_delta0;
            } else {
                (&cell.x)[walk.minor_axis0] += (&walk.step.x)[walk.minor_axis0];
                direction = directions[1];
                --iteration;
                error += walk.error_reset0;
            }
        } else if (error < 0 && !blocked) {
            (&cell.x)[walk.minor_axis0] += (&walk.step.x)[walk.minor_axis0];
            error += walk.error_reset0;
            if (static_cast<unsigned int>(cell.y * 0x10000 + cell.x) == destination_key) {
                --iteration;
                direction = directions[1];
            } else {
                if ((directions[0] == 0 && directions[1] == 6) ||
                    (directions[0] == 6 && directions[1] == 0)) {
                    direction = 7;
                } else {
                    direction = (directions[0] + directions[1]) / 2;
                }
                (&cell.x)[walk.major_axis] += (&walk.step.x)[walk.major_axis];
                error -= walk.error_delta0;
            }
        } else {
            (&cell.x)[walk.major_axis] += (&walk.step.x)[walk.major_axis];
            error -= walk.error_delta0;
        }

        if (cell_key == destination_key) {
            iteration = walk.count;
        } else if (direction_mask != 0 && (direction_mask & 1 << (direction & 0x1f)) == 0) {
            updateNeighborMask(cell_key, path_value);
            blocked = true;
        }

        cell_key = cell.y * 0x10000 + cell.x;
        ++iteration;
    }

    if (!adjust_destination) {
        if (!blocked) {
            int destination_height =
                static_cast<int>((destination->y - level_bounds.minimum.y) / span) + 1;
            int difference = destination_height - height;
            if (difference < -cell_count || cell_count < difference) {
                blocked = true;
            }
        }
    } else if (previous_key == 0) {
        *destination = *source;
    } else {
        if (blocked) {
            destination->x =
                ((previous_key & 0xffff) + g_float_half) * grid_scale + level_bounds.minimum.x;
            destination->z =
                ((previous_key >> 16) + g_float_half) * grid_scale + level_bounds.minimum.z;
        }
        destination->y = (height - 1) * span + level_bounds.minimum.y;
    }

    return !blocked;
}

/* Compare clearance along the two compass rays bracketing a horizontal
   direction. The normalized Z component selects the pair; the sign of X
   selects which half of the compass owns the middle bands. */
// FUNCTION: WIZ8 0x0045aac0
float W8PathingService::CompareDirectionalClearance(const srVector3T<float>* position,
                                                    const srVector3T<float>* direction,
                                                    float distance)
{
    glm::vec2 horizontal = direction->xz();
    horizontal = normalizedOrZero(horizontal);
    float normalized_x = horizontal.x;
    float normalized_z = horizontal.y;

    int first_direction;
    int second_direction;
    if (normalized_x <= g_float_zero) {
        if (g_path_direction_threshold_3 < normalized_z) {
            first_direction = 7;
            second_direction = 1;
        } else if (g_path_direction_threshold_2 < normalized_z) {
            first_direction = 6;
            second_direction = 0;
        } else if (normalized_z <= g_path_direction_threshold_1) {
            if (normalized_z <= g_path_direction_threshold_0) {
                first_direction = 3;
                second_direction = 5;
            } else {
                first_direction = 4;
                second_direction = 6;
            }
        } else {
            first_direction = 5;
            second_direction = 7;
        }
    } else {
        if (g_path_direction_threshold_3 < normalized_z) {
            first_direction = 7;
            second_direction = 1;
        } else if (g_path_direction_threshold_2 < normalized_z) {
            first_direction = 0;
            second_direction = 2;
        } else if (g_path_direction_threshold_1 < normalized_z) {
            first_direction = 1;
            second_direction = 3;
        } else if (g_path_direction_threshold_0 < normalized_z) {
            first_direction = 2;
            second_direction = 4;
        } else {
            first_direction = 3;
            second_direction = 5;
        }
    }

    srVector2i cell;
    const auto converted_cell = pathCell(position->glmValue());
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int height = static_cast<unsigned int>(
                              static_cast<int>(((position->y - level_bounds.minimum.y) / span))) +
                          1;
    float first = MeasureDirectionalPath(&cell, first_direction, height, distance);
    float second = MeasureDirectionalPath(&cell, second_direction, height, distance);
    return second - first;
}

/* Measure how much of a requested run remains traversable in one compass
   direction. Cardinal runs use the retail diagonal-to-axis scale before cell
   stepping; every crossed cell must carry a vertically compatible record whose
   explicit direction mask, when present, permits the same direction. */
// FUNCTION: WIZ8 0x0045ac70
float W8PathingService::MeasureDirectionalPath(const srVector2i* cell, int direction,
                                               unsigned int height, float distance)
{
    int step_x;
    int step_z;
    float remaining = distance;

    switch (direction) {
    case 0:
        step_x = 0;
        step_z = 1;
        remaining *= g_path_cardinal_scale;
        break;
    case 1:
        step_x = 1;
        step_z = 1;
        break;
    case 2:
        step_x = 1;
        step_z = 0;
        remaining *= g_path_cardinal_scale;
        break;
    case 3:
        step_x = 1;
        step_z = -1;
        break;
    case 4:
        step_x = 0;
        step_z = -1;
        remaining *= g_path_cardinal_scale;
        break;
    case 5:
        step_x = -1;
        step_z = -1;
        break;
    case 6:
        step_x = -1;
        step_z = 0;
        remaining *= g_path_cardinal_scale;
        break;
    case 7:
        step_x = -1;
        step_z = 1;
        break;
    }

    int cell_x = cell->x;
    int cell_z = cell->y;
    bool stopped = false;

    while (grid_scale < remaining) {
        cell_x += step_x;
        cell_z += step_z;
        unsigned int key = cell_z * 0x10000 + cell_x;
        std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
        auto slot = index->equal_range(key).first;
        unsigned int path_value;
        bool found = false;

        while (slot != index->end()) {
            path_value = slot->second;
            int difference = (path_value & W8_PATH_CELL_HEIGHT_MASK) - height;
            if ((path_value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < difference &&
                difference < cell_count) {
                found = true;
                height = path_value & W8_PATH_CELL_HEIGHT_MASK;
                break;
            }
            slot = NextIndexValue(*index, key, slot);
        }

        if (found) {
            remaining -= grid_scale;
        }
        if (!found || ((path_value & W8_PATH_CELL_HAS_DIRECTIONS) != 0 &&
                       (path_value & 1 << ((direction + 16) & 0x1f)) == 0)) {
            stopped = true;
            break;
        }
    }

    if (!stopped) {
        return distance;
    }
    return distance - remaining;
}

/* Find a waypoint surface near a world position.

   Nearby kind-nine octree objects are ordered by integer three-dimensional
   distance. A very close horizontal match wins immediately; otherwise the
   first candidate connected by the ordinary span test is selected. Exhaustive
   mode retries the ordered candidates with paired arc probes, rebuilding the
   temporary visitation index for every attempt. */
// FUNCTION: WIZ8 0x0045b120
unsigned short W8PathingService::FindWaypoint(const glm::vec3& position, bool exhaustive)
{
    srVector3T<float> query = position;
    unsigned short result = 0;
    start_waypoint = 0;

    if (!SnapWaypointPosition(&query, false)) {
        return 0;
    }

    srVector3T<float> lower;
    srVector3T<float> upper;
    srVector3T<float> half_extent;
    half_extent.Set(g_float_twenty_five_thousand, g_float_twelve_thousand_five_hundred,
                    g_float_twenty_five_thousand);
    lower = query - half_extent;
    upper = query + half_extent;

    auto candidates =
        g_octree->queryObjects({lower.glmValue(), upper.glmValue()}, W8_OCTREE_KIND_WAYPOINT, -1);
    int count = candidates.size();
    if (count == 0) {
        return result;
    }
    if (static_cast<unsigned int>(count) >= 200) {
        srAssertFail("ulCount<200", OCTPATH_CPP, 0xe0c, "Too many nodes in list");
    }

    unsigned int distances[199];
    int index;
    if (count > 1) {
        for (index = 0; index < count; ++index) {
            const srVector3T<float>* candidate = &m_waypoints[candidates[index]].position;
            srVector3T<float> delta = query - *candidate;
            distances[index] = static_cast<unsigned int>(static_cast<int>(delta.Length()));

            if (distances[index] < g_float_one_thousand &&
                glm::length(((delta.xz()))) < g_double_five_hundred) {
                result = static_cast<unsigned short>(candidates[index]);
            }
        }

        if (result == 0) {
            for (index = 1; index < count; ++index) {
                unsigned int distance = distances[index];
                int candidate = candidates[index];
                int insertion = index;

                while (insertion > 0 && distance < distances[insertion - 1]) {
                    distances[insertion] = distances[insertion - 1];
                    candidates[insertion] = candidates[insertion - 1];
                    --insertion;
                }
                distances[insertion] = distance;
                candidates[insertion] = candidate;
            }
        }
    }

    for (index = 0; index < count; ++index) {
        if (result != 0) {
            return result;
        }
        if (TestWaypointSpan(&query, &m_waypoints[candidates[index]].position, false, false)) {
            result = static_cast<unsigned short>(candidates[index]);
        }
    }

    if (result == 0 && exhaustive) {
        m_probe_position.SetZero();

        std::unordered_map<unsigned int, int>* visited = m_pVisitedCells;
        visited->clear();
        start_waypoint = 0;
        m_probe_bounded = false;

        for (index = 0; index < count; ++index) {
            if (start_waypoint != 0) {
                return result;
            }

            bool saved_flag = m_probe_bounded;
            visited = m_pVisitedCells;
            m_probe_bounded = false;
            visited->clear();

            m_probe_cell_key = 0;
            m_probe_limit = 0;
            srVector3T<float>* candidate = &m_waypoints[candidates[index]].position;
            ProbeWaypointArc(&query, candidate);
            m_probe_bounded = saved_flag;
            m_probe_limit = 0xffffffff;
            ProbeWaypointArc(candidate, &query);
            if (m_probe_cell_key != 0) {
                start_waypoint = static_cast<unsigned short>(candidates[index]);
            }
        }
    }

    return result;
}

/* Snap only the vertical component of a position to the first path-index
   record in the same horizontal cell and inside the service's vertical band.

   Unlike SnapWaypointPosition, this operation leaves X and Z exactly
   as supplied. The one-based height stored in the index is converted back to
   the level's world-space Y coordinate. */
// FUNCTION: WIZ8 0x0045b5a0
void W8PathingService::SnapPathHeight(srVector3T<float>* position)
{
    unsigned int key = pathCell(position->glmValue()).key;

    if (key == 0) {
        return;
    }

    std::unordered_multimap<unsigned int, unsigned int>* index = m_pPathValues;
    auto slot = index->equal_range(key).first;
    int height = static_cast<int>((position->y - level_bounds.minimum.y) / span) + 1;
    unsigned int matched_height;
    bool found = false;

    while (slot != index->end() && !found) {
        unsigned int candidate_height = slot->second & W8_PATH_CELL_HEIGHT_MASK;
        int difference = static_cast<int>(candidate_height) - height;
        if (-cell_count < difference && difference < cell_count) {
            matched_height = candidate_height;
            found = true;
        }
        slot = NextIndexValue(*index, key, slot);
    }
    if (found) {
        position->y = (matched_height - 1) * span + level_bounds.minimum.y;
    }
}

/* Build the path surface normal from height-snapped samples at the current
   point, one X cell over, and one Z cell over. The Z-edge crossed with the
   X-edge gives the upward normal on flat ground, matching the retail order. */
// FUNCTION: WIZ8 0x0045b730
void W8PathingService::GetPathSurfaceNormal(const srVector3T<float>* position,
                                            srVector3T<float>* normal)
{
    srVector3T<float> origin = *position;
    srVector3T<float> x_sample = *position;
    srVector3T<float> z_sample = *position;

    SnapPathHeight(&origin);
    x_sample.x += grid_scale;
    SnapPathHeight(&x_sample);
    z_sample.z += grid_scale;
    SnapPathHeight(&z_sample);

    *normal =
        srVector3T{glm::cross((z_sample - origin).glmValue(),
                              (x_sample - origin).glmValue())}; // NOLINT(misc-include-cleaner)
    normal->Normalize();
}

/* Activate the eligible trigger prop intersecting a navigator's next path
   segment.

   Ordinary movement supplies a box from the current position to twice the
   velocity. Edge mode instead resolves the attachment's current waypoint pair
   and accepts only an edge carrying both dynamic bits. Kind-eight octree hits
   are filtered to active props whose trigger owner permits this activation;
   when several remain, the owner nearest the segment midpoint wins. */
// FUNCTION: WIZ8 0x0045b880
void W8PathingService::ActivateMovementTrigger(W8NavigatorMovementState* movement,
                                               bool use_path_edge)
{
    if ((movement->navigation_filter & W8_NAV_THROUGH_DOORS) == 0) {
        return;
    }

    srVector3T<float> lower;
    srVector3T<float> upper;

    if (!use_path_edge) {
        if (IsZeroVector(&movement->velocity) != 0) {
            return;
        }
        lower = movement->position;
        upper = movement->position + movement->velocity * 2.0;
    } else {
        W8NavigatorAttachment* attachment = movement->attachment.get();

        /* Retail reaches the shared query with the local bounds untouched
           when this cursor is exhausted. Keep that source-level fallthrough;
           callers normally enter edge mode only while a pair remains. */
        if (attachment->path_cursor < attachment->path_position_index) {
            const auto& pairs = attachment->path;
            unsigned short source = pairs[attachment->path_cursor].value;
            unsigned short destination = pairs[attachment->path_cursor + 1].value;
            W8PathSurface* source_surface = &m_waypoints[source];
            unsigned short edge_index = source_surface->first_edge;

            if (edge_index == 0) {
                return;
            }
            while (m_pEdges[edge_index].destination != destination) {
                edge_index = m_pEdges[edge_index].next;
                if (edge_index == 0) {
                    return;
                }
            }

            unsigned int flags = m_pEdges[edge_index].flags;
            if ((flags & W8_NAV_THROUGH_DOORS) == 0 || (flags & W8_PATH_EDGE_DISABLED) == 0) {
                return;
            }
            lower = source_surface->position;
            upper = m_waypoints[destination].position;
        }
    }

    if (upper.x < lower.x) {
        float temporary = lower.x;
        lower.x = upper.x;
        upper.x = temporary;
    }
    if (upper.y < lower.y) {
        float temporary = lower.y;
        lower.y = upper.y;
        upper.y = temporary;
    }
    if (upper.z < lower.z) {
        float temporary = lower.z;
        lower.z = upper.z;
        upper.z = temporary;
    }

    const auto candidates =
        g_octree->queryObjects({lower.glmValue(), upper.glmValue()}, W8_OCTREE_KIND_PROP, -1);
    int count = candidates.size();
    if (count <= 0) {
        return;
    }

    Trigger* selected = 0;
    if (count == 1) {
        W8Prop* prop = g_world->collidable_props[candidates[0]];
        Trigger* trigger = prop->GetGDPropOwnerTrigger();
        if (prop->GetActivationState() == 0 || trigger == 0 ||
            (trigger->flags & W8_TRIGGER_ENABLED) == 0) {
            return;
        }
        selected = trigger;
    } else {
        srVector3T<float> midpoint;
        midpoint = (upper - lower) * g_double_half + lower;
        double nearest_distance = 1e32;

        for (int index = 0; index < count; ++index) {
            W8Prop* prop = g_world->collidable_props[candidates[index]];
            Trigger* trigger = prop->GetGDPropOwnerTrigger();
            if (prop->GetActivationState() != 0 && trigger != 0 &&
                (trigger->flags & W8_TRIGGER_ENABLED) != 0) {
                srVector3T<float> center;
                center = prop->GetPosition();
                srVector3T<float> difference = center - midpoint;
                double distance = difference.LengthSquared();
                if (distance < nearest_distance) {
                    nearest_distance = distance;
                    selected = trigger;
                }
            }
        }
    }

    if (selected != 0) {
        selected->Activate();
    }
}

/* Drive the path editor's owned scene node from the service's mode flags.

   The ordinary mode draws one adjusted position or hides the existing node.
   Active path mode prepares the source/destination pair and rebuilds the
   visualization when the collector reports content. The alternate editor
   mode lazily creates and attaches its node before drawing the adjusted point.
   Visibility flag order follows the retail exits exactly. */
// FUNCTION: WIZ8 0x0045bc40
void W8PathingService::UpdatePathVisualization(const glm::vec3& source,
                                               const glm::vec3& destination)
{
    W8World* world = GetWorld();
    srNode* node = m_path_visualization.get();

    if (m_waypoint_editing) {
        auto adjusted = g_octree->settleToGround(source, true, 500).position;
        PreparePathVisualization(adjusted, destination);

        if (CollectPathVisualization(adjusted) != 0) {
            if (m_path_visualization != 0) {
                BuildPathVisualization();
                m_path_visualization->clearFlag(srNode::FLAG_DISABLE);
                return;
            }

            BuildPathVisualization();
            node = m_path_visualization.get();
            if (node != 0) {
                node->setParent(world->static_scene, 1);
                node->clearFlag(srNode::FLAG_DISABLE);
                return;
            }
            node->clearFlag(srNode::FLAG_DISABLE);
            return;
        }

        node = m_path_visualization.get();
        if (node != 0) {
            node->setFlag(srNode::FLAG_DISABLE);
            node->setFlag(srNode::FLAG_TERMINATE);
        }
        return;
    }

    if (!draw_waypoints && !search_visualization) {
        DrawPathPosition(source, 0);
        node = m_path_visualization.get();
        if (node != 0) {
            node->setFlag(srNode::FLAG_DISABLE);
            node->setFlag(srNode::FLAG_TERMINATE);
        }
        return;
    }

    if (search_visualization) {
        if (m_path_visualization == 0) {
            EnsurePathVisualization();
            node = m_path_visualization.get();
            node->setParent(world->static_scene, 1);
            node->setFlag(srNode::FLAG_TERMINATE);
            if (m_path_visualization == 0) {
                node->clearFlag(srNode::FLAG_DISABLE);
                return;
            }
        }

        auto adjusted = g_octree->settleToGround(source, true, 500).position;
        DrawPathPosition(adjusted, 1);
    }

    m_path_visualization->clearFlag(srNode::FLAG_DISABLE);
}

/* Populate the editor mesh from the currently visible waypoint set. Marker
   geometry occupies the first hundred six-polygon groups; directed links use
   the following groups and are emitted once when a visible reverse edge
   exists. */
// FUNCTION: WIZ8 0x0045BE30
stModelInstance* W8PathingService::BuildPathVisualization()
{
    if (m_path_visualization == nullptr) EnsurePathVisualization();
    const std::array<glm::vec3, 5> marker_offsets{
        glm::vec3(0, 0.5f, 0) * static_cast<float>(g_double_five_hundred),
        glm::vec3(-0.25f, 0, -0.25f) * static_cast<float>(g_double_five_hundred),
        glm::vec3(-0.25f, 0, 0.25f) * static_cast<float>(g_double_five_hundred),
        glm::vec3(0.25f, 0, 0.25f) * static_cast<float>(g_double_five_hundred),
        glm::vec3(0.25f, 0, -0.25f) * static_cast<float>(g_double_five_hundred)};
    int index;

    stMeshModel* model = static_cast<stMeshModel*>(m_path_visualization->getModel());
    glm::vec3* colors = model->getVertexDIG(0, 1);
    glm::vec3* vertices = model->getVertexLoc();
    glm::ivec3* polygons = model->getPolyVertex();
    const auto write_marker = [&](unsigned short waypoint, int base, unsigned appearance_flags) {
        const auto& surface = m_waypoints[waypoint];
        const glm::vec3 scale((surface.flags >> 12) * g_waypoint_marker_scale,
                              (surface.flags >> 12) * 0.5,
                              (surface.flags >> 12) * g_waypoint_marker_scale);
        const auto color = waypointVisualizationColor(waypoint);
        for (unsigned corner = 0; corner < marker_offsets.size(); ++corner) {
            auto offset = scale * marker_offsets[corner];
            offset.y = static_cast<float>(offset.y + g_double_five_hundred);
            vertices[base + corner] = offset + surface.position.glmValue();
            colors[base + corner] = color;
        }
        if ((appearance_flags & 2) != 0) {
            // NOLINTNEXTLINE(misc-include-cleaner)
            colors[base] = glm::mix(
                glm::vec3(0), glm::vec3(1),
                glm::lessThanEqual(color, glm::vec3(g_float_zero))); // NOLINT(misc-include-cleaner)
        } else if ((appearance_flags & W8_WAYPOINT_CONDITIONAL) != 0) {
            colors[base] = glm::vec3(0);
        }
    };
    int marker_count = 0;
    int link_count = 0;
    std::ranges::fill(m_visited_path_nodes, false);
    for (std::size_t marked = 0; marked < m_marked_path_nodes.size() && marker_count < 99;
         ++marked) {
        if (!m_marked_path_nodes[marked]) {
            continue;
        }
        unsigned short source_index = static_cast<unsigned short>(marked);
        W8PathSurface* source = &m_waypoints[source_index];
        SetGrowing(m_visited_path_nodes, source_index);
        write_marker(source_index, marker_count * 5, source->flags);

        unsigned short edge_index = source->first_edge;
        while (edge_index != 0 && link_count * 6 + 504 <= 0x30d1) {
            W8PathEdge* edge = &m_pEdges[edge_index];
            unsigned short destination_index = edge->destination;
            W8PathSurface* destination = &m_waypoints[destination_index];
            unsigned short reverse_index = destination->first_edge;
            bool reverse_found = false;

            while (reverse_index != 0 && !reverse_found) {
                if (m_pEdges[reverse_index].destination == source_index) {
                    reverse_found = true;
                } else {
                    reverse_index = m_pEdges[reverse_index].next;
                }
            }

            if (!TestBit(m_visited_path_nodes, destination_index) &&
                !TestBit(m_marked_path_nodes, destination_index)) {
                // Neighbor markers inherit the source's highlight state.
                write_marker(destination_index, (marker_count + 1) * 5, source->flags);
                SetGrowing(m_visited_path_nodes, destination_index);
                ++marker_count;
            }

            if (source_index < destination_index ||
                !TestBit(m_marked_path_nodes, destination_index) || !reverse_found) {
                int base_vertex = 500 + link_count * 6;
                int base_polygon = 600 + link_count * 6;
                glm::vec2 perpendicular(-(destination->position.z - source->position.z),
                                        destination->position.x - source->position.x);
                perpendicular = normalizedOrZero(perpendicular) * float(g_waypoint_edge_offset);
                float perpendicular_x = perpendicular.x;
                float perpendicular_z = perpendicular.y;

                polygons[base_polygon].x = base_vertex;
                polygons[base_polygon].y = base_vertex + 1;
                polygons[base_polygon].z = base_vertex + 3;
                polygons[base_polygon + 1].x = base_vertex + 1;
                polygons[base_polygon + 1].y = base_vertex + 2;
                polygons[base_polygon + 1].z = base_vertex + 4;
                polygons[base_polygon + 2].x = base_vertex + 2;
                polygons[base_polygon + 2].y = base_vertex;
                polygons[base_polygon + 2].z = base_vertex + 5;
                polygons[base_polygon + 3].x = base_vertex + 4;
                polygons[base_polygon + 3].y = base_vertex + 3;
                polygons[base_polygon + 3].z = base_vertex + 1;
                polygons[base_polygon + 4].x = base_vertex + 5;
                polygons[base_polygon + 4].y = base_vertex + 4;
                polygons[base_polygon + 4].z = base_vertex + 2;
                polygons[base_polygon + 5].x = base_vertex + 3;
                polygons[base_polygon + 5].y = base_vertex + 5;
                polygons[base_polygon + 5].z = base_vertex;

                vertices[base_vertex] = source->position.glmValue();
                vertices[base_vertex].y += g_waypoint_marker_height;
                vertices[base_vertex + 3] = destination->position.glmValue();
                vertices[base_vertex + 3].y += g_waypoint_marker_height;
                srVector3T<float> offset;
                offset.Set(perpendicular_x, g_world_scale, perpendicular_z);
                vertices[base_vertex + 1] = (source->position + offset).glmValue();
                vertices[base_vertex + 2] = (source->position - offset).glmValue();
                vertices[base_vertex + 4] = (destination->position + offset).glmValue();
                vertices[base_vertex + 5] = (destination->position - offset).glmValue();

                for (index = 0; index < 6; ++index) {
                    colors[base_vertex + index] = glm::vec3(0);
                }
                if ((edge->flags & W8_PATH_EDGE_DISABLED) == 0) {
                    for (index = 0; index < 3; ++index) {
                        colors[base_vertex + index].z = 1.0f;
                        if ((edge->flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
                            colors[base_vertex + index].y = 1.0f;
                        }
                    }
                }
                if (reverse_found && (m_pEdges[reverse_index].flags & W8_PATH_EDGE_DISABLED) == 0) {
                    for (index = 3; index < 6; ++index) {
                        colors[base_vertex + index].z = 1.0f;
                    }
                }
                if (!reverse_found ||
                    (m_pEdges[reverse_index].flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
                    for (index = 3; index < 6; ++index) {
                        colors[base_vertex + index].y = 1.0f;
                    }
                }
                ++link_count;
            }
            edge_index = edge->next;
        }

        ++marker_count;
    }

    w8_ulong* active_polygons = model->getActivePolygonTable(1);
    w8_ulong active_count = 0;
    for (index = 0; index < marker_count * 6; ++index) {
        active_polygons[active_count++] = index;
    }
    for (index = 0; index < link_count * 6; ++index) {
        active_polygons[active_count++] = 600 + index;
    }
    model->setActivePolygonCount(active_count);
    model->setDirtyAll();
    model->flags &= ~W8_MESH_VERTEX_LIGHTING_DIRTY;
    return m_path_visualization.get();
}

/* Rebuild the editor's bounded grid search when the cursor enters a new path
   cell. Each reachable vertical span is inserted once into the visited hash;
   the minimum heap expands the nearest pending position first, and the final
   node set is handed to the search-trace renderer. */
// FUNCTION: WIZ8 0x0045C9A0
void W8PathingService::DrawPathPosition(const glm::vec3& position, unsigned char mode)
{
    if (mode == 0 || g_combat_inactive == 0) {
        g_path_visualization_cell = 0;
        return;
    }

    srVector2i root_cell;
    const auto converted_cell = pathCell(position);
    root_cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int root_key = converted_cell.key;
    if (root_key == g_path_visualization_cell) {
        return;
    }
    g_path_visualization_cell = root_key;

    std::unordered_map<unsigned int, int>* visited = m_pVisitedCells;
    visited->clear();

    m_search_node_count = 0;
    path_heap->open = {};
    unsigned short root_index = AllocateSearchNode();
    W8PathSearchNode* root = &m_search_nodes[root_index];
    root->flags = 0;
    root->node_index = root_index;
    root->cell_x = static_cast<unsigned short>(root_cell.x);
    root->cell_z = static_cast<unsigned short>(root_cell.y);
    root->path_height = static_cast<unsigned short>(
        static_cast<int>((position.y - level_bounds.minimum.y) / span) + 1);
    root->parent_node = 0;
    root->base_score = 0.0f;
    root->position = position;

    W8PathOpenSet* heap = &path_heap->open;
    W8PathHeapEntry root_entry;
    root_entry.node = root_index;
    root_entry.priority = static_cast<unsigned int>(root->score);
    heap->push(root_entry);
    path_heap->root_node = heap->top().node;

    unsigned int best_node = root_index;
    while (best_node != 0 && m_search_node_count < g_path_reserve) {
        W8PathSearchNode* current = &m_search_nodes[best_node];
        unsigned short current_x = current->cell_x;
        unsigned short current_z = current->cell_z;
        unsigned short current_height = current->path_height;

        for (int direction = 0; direction < 8; ++direction) {
            unsigned int neighbor_x;
            unsigned int neighbor_z;
            if (direction >= 1 && direction <= 3) {
                neighbor_x = current_x + 1;
            } else if (direction > 4) {
                neighbor_x = current_x - 1;
            } else {
                neighbor_x = current_x;
            }
            if (direction < 2 || direction > 6) {
                neighbor_z = current_z + 1;
            } else if (direction > 2 && direction < 6) {
                neighbor_z = current_z - 1;
            } else {
                neighbor_z = current_z;
            }

            unsigned int key = neighbor_z * 0x10000 + neighbor_x;
            if (LookupIndexValue(*visited, key) != 0) {
                continue;
            }

            std::unordered_multimap<unsigned int, unsigned int>* paths = m_pPathValues;
            auto path_slot = paths->equal_range(key).first;
            while (path_slot != paths->end()) {
                unsigned int path_value = path_slot->second;
                int height_delta =
                    static_cast<int>(path_value & W8_PATH_CELL_HEIGHT_MASK) - current_height;
                if (height_delta > -static_cast<int>(cell_count) &&
                    height_delta < static_cast<int>(cell_count)) {
                    unsigned short node_index = AllocateSearchNode();
                    int node_value = node_index;
                    visited->emplace(key, node_value);

                    W8PathSearchNode* node = &m_search_nodes[node_index];
                    node->flags = 0;
                    if ((path_value & W8_PATH_CELL_INACTIVE) != 0) {
                        node->flags = W8_PATH_SEARCH_INACTIVE_CELL;
                    }
                    if ((path_value & W8_PATH_CELL_CONDITIONAL) != 0) {
                        node->flags |= W8_PATH_SEARCH_CONDITIONAL_CELL;
                    }
                    node->node_index = node_index;
                    node->cell_x = static_cast<unsigned short>(neighbor_x);
                    node->cell_z = static_cast<unsigned short>(neighbor_z);
                    node->path_height =
                        static_cast<unsigned short>(path_value & W8_PATH_CELL_HEIGHT_MASK);
                    node->parent_node = static_cast<unsigned short>(best_node);
                    node->position.Set(
                        (node->cell_x + g_float_half) * grid_scale + level_bounds.minimum.x,
                        (node->path_height - 1) * span + level_bounds.minimum.y,
                        (node->cell_z + g_float_half) * grid_scale + level_bounds.minimum.z);
                    // NOLINTNEXTLINE(misc-include-cleaner)
                    node->score = glm::length(node->position.glmValue() - position);
                    if (node->score < g_path_search_visualization_limit) {
                        W8PathHeapEntry pending;
                        pending.node = node->node_index;
                        pending.priority = static_cast<unsigned int>(node->score);
                        heap->push(pending);
                        path_heap->root_node = heap->top().node;
                    } else {
                        --m_search_node_count;
                    }
                }
                path_slot = NextIndexValue(*paths, key, path_slot);
            }
        }

        path_heap->DeleteRoot(current);
        best_node = path_heap->root_node;
        if (best_node > m_search_node_count) {
            char message[80];
            sprintf(message, "A*, Invalid node index %d from Queue", best_node);
            srAssertFail("(ulBestNode <= m_ulSearchNodesUsed)", OCTPATH_CPP, 0x1110, message);
        }
    }
    BuildSearchVisualization();
}

/* Draw the bounded path-search trace in the editor mesh. Search node zero is
   the root, so every later node contributes one raised square at its stored
   position. Its flags select the diagnostic color used for all five vertices
   in that marker. */
// FUNCTION: WIZ8 0x0045CFD0
void W8PathingService::BuildSearchVisualization()
{
    const srVector3T<float> marker_offsets[5] = {
        srVector3T<float>(0.0f, 125.0f, 0.0f), srVector3T<float>(-62.5f, 0.0f, -62.5f),
        srVector3T<float>(-62.5f, 0.0f, 62.5f), srVector3T<float>(62.5f, 0.0f, 62.5f),
        srVector3T<float>(62.5f, 0.0f, -62.5f)};
    stMeshModel* model = static_cast<stMeshModel*>(m_path_visualization->getModel());
    glm::vec3* colors = model->getVertexDIG(0, 1);
    glm::vec3* vertices = model->getVertexLoc();
    model->getActivePolygonTable(1);
    glm::ivec3* polygons = model->getPolyVertex();
    /* 0x0045D3BD tests this bound signed even though m_search_node_count is
       stored unsigned. */
    int node_count = m_search_node_count;

    if (node_count > 2000) {
        node_count = 2000;
    }
    /* 0x0045D3AD decrements the bound and 0x0045D3BF tests it signed. */
    for (int node_index = 1; node_index < node_count; ++node_index) {
        W8PathSearchNode* node = &m_search_nodes[node_index];
        unsigned int vertex_index = 500 + (node_index - 1) * 5;
        unsigned int polygon_index = 600 + (node_index - 1) * 4;

        polygons[polygon_index].x = vertex_index;
        polygons[polygon_index].y = vertex_index + 1;
        polygons[polygon_index].z = vertex_index + 2;
        polygons[polygon_index + 1].x = vertex_index;
        polygons[polygon_index + 1].y = vertex_index + 2;
        polygons[polygon_index + 1].z = vertex_index + 3;
        polygons[polygon_index + 2].x = vertex_index;
        polygons[polygon_index + 2].y = vertex_index + 3;
        polygons[polygon_index + 2].z = vertex_index + 4;
        polygons[polygon_index + 3].x = vertex_index;
        polygons[polygon_index + 3].y = vertex_index + 4;
        polygons[polygon_index + 3].z = vertex_index + 1;

        srVector3T<float> color;
        if ((node->flags & W8_PATH_SEARCH_INACTIVE_CELL) != 0) {
            color = srVector3T<float>(0.0f, 0.0f, 0.0f);
        } else if ((node->flags & W8_PATH_SEARCH_EXPANDED) != 0) {
            color = srVector3T<float>(1.0f, 0.0f, 1.0f);
        } else if ((node->flags & W8_PATH_SEARCH_ROUTE) != 0) {
            color = srVector3T<float>(1.0f, 1.0f, 1.0f);
        } else if ((node->flags & W8_PATH_SEARCH_BLOCKED) != 0) {
            color = srVector3T<float>(1.0f, 0.0f, 0.0f);
        } else if ((node->flags & W8_PATH_SEARCH_PROBE_OVERLAP) != 0) {
            color = srVector3T<float>(1.0f, 1.0f, 0.0f);
        } else if ((node->flags & 0x8000) != 0) {
            color = srVector3T<float>(0.0f, 1.0f, 1.0f);
        } else if ((node->flags & W8_PATH_SEARCH_CONDITIONAL_CELL) != 0) {
            color = srVector3T<float>(0.0f, 0.0f, 1.0f);
        } else {
            color = srVector3T<float>(0.0f, 1.0f, 0.0f);
        }

        for (int offset_index = 0; offset_index < 5; ++offset_index) {
            vertices[vertex_index + offset_index] =
                (marker_offsets[offset_index] + node->position).glmValue();
            colors[vertex_index + offset_index] = color.glmValue();
        }
    }

    w8_ulong* active_polygons = model->getActivePolygonTable(1);
    /* 0x0045D3BF tests this bound with JLE after computing it as
       4 * (node_count - 1), so it is a signed count and not an unsigned one. */
    w8_long active_count = node_count > 1 ? (node_count - 1) * 4 : 0;
    for (w8_long index = 0; index < active_count; ++index) {
        active_polygons[index] = index + 600;
    }
    model->setActivePolygonCount(active_count);
    model->setDirtyAll();
    model->flags &= ~W8_MESH_VERTEX_LIGHTING_DIRTY;
}

/* Select the editor color for one waypoint. Disabled surfaces are black; the
   two current selection slots take yellow and either green or red; every other
   surface is blue. */
// FUNCTION: WIZ8 0x0045d490
glm::vec3 W8PathingService::waypointVisualizationColor(unsigned short waypoint) const
{
    if ((m_waypoints[waypoint].flags & W8_WAYPOINT_DISABLED) != 0) return glm::vec3(0);
    if (waypoint == start_waypoint) return {1, 1, 0};
    if (waypoint != destination_waypoint) return {0, 0, 1};
    return path_direction_valid ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
}

/* Create the fixed-capacity editor mesh shared by waypoint and edge drawing.
   The first hundred six-triangle groups describe waypoint markers; the next
   two thousand describe edge segments. Every polygon and vertex receives the
   path editor's shared texture/material state before the concrete model
   instance takes ownership of the mesh. */
// FUNCTION: WIZ8 0x0045D530
stModelInstance* W8PathingService::EnsurePathVisualization()
{
    const int polygon_count = 0x3138;
    const int vertex_count = 0x30d4;
    auto model = std::make_shared<stMeshModel>(polygon_count, vertex_count);
    int index;
    model->flags &= ~W8_MESH_SORTED_RENDERING;
    model->setShader(g_oct_mesh_default.render_flags, 0);
    model->setName("WayPoint Mesh");

    glm::ivec3* polygons = model->getPolyVertex();
    std::shared_ptr<srTextureIFace>* textures = model->getPolyTexture(0, 0, 1);
    glm::vec2* texture_coordinates = model->getVertexTexCoords(0, 0, 1);
    std::shared_ptr<srMaterialIFace>* materials =
        model->getVertexMaterial(0, srMeshModel::SIDE_FRONT, 1);
    w8_ulong* shade_indices = model->getVertexShadeIndex(1);

    for (index = 0; index < polygon_count; ++index) {
        textures[index] = g_oct_mesh_default.texture;
    }
    for (index = 0; index < vertex_count; ++index) {
        texture_coordinates[index] = glm::vec2(0);
        materials[index] = g_oct_mesh_default.material;
        shade_indices[index] = index;
    }

    int polygon_index = 0;
    int vertex_index = 1;
    do {
        polygons[polygon_index].x = vertex_index - 1;
        polygons[polygon_index].y = vertex_index;
        polygons[polygon_index].z = vertex_index + 1;
        polygons[polygon_index + 1].x = vertex_index - 1;
        polygons[polygon_index + 1].y = vertex_index + 1;
        polygons[polygon_index + 1].z = vertex_index + 2;
        polygons[polygon_index + 2].x = vertex_index - 1;
        polygons[polygon_index + 2].y = vertex_index + 2;
        polygons[polygon_index + 2].z = vertex_index + 3;
        polygons[polygon_index + 3].x = vertex_index - 1;
        polygons[polygon_index + 3].y = vertex_index + 3;
        polygons[polygon_index + 3].z = vertex_index;
        polygons[polygon_index + 4].x = vertex_index;
        polygons[polygon_index + 4].y = vertex_index + 3;
        polygons[polygon_index + 4].z = vertex_index + 2;
        polygons[polygon_index + 5].x = vertex_index;
        polygons[polygon_index + 5].y = vertex_index + 2;
        polygons[polygon_index + 5].z = vertex_index + 1;
        vertex_index += 5;
        polygon_index += 6;
    } while (vertex_index < 0x1f5);

    vertex_index = 0x1f8;
    do {
        polygons[polygon_index].x = vertex_index - 4;
        polygons[polygon_index].y = vertex_index - 3;
        polygons[polygon_index].z = vertex_index - 1;
        polygons[polygon_index + 1].x = vertex_index - 3;
        polygons[polygon_index + 1].y = vertex_index - 2;
        polygons[polygon_index + 1].z = vertex_index;
        polygons[polygon_index + 2].x = vertex_index - 2;
        polygons[polygon_index + 2].y = vertex_index - 4;
        polygons[polygon_index + 2].z = vertex_index + 1;
        polygons[polygon_index + 3].x = vertex_index;
        polygons[polygon_index + 3].y = vertex_index - 1;
        polygons[polygon_index + 3].z = vertex_index - 3;
        polygons[polygon_index + 4].x = vertex_index + 1;
        polygons[polygon_index + 4].y = vertex_index;
        polygons[polygon_index + 4].z = vertex_index - 2;
        polygons[polygon_index + 5].x = vertex_index - 1;
        polygons[polygon_index + 5].y = vertex_index + 1;
        polygons[polygon_index + 5].z = vertex_index - 4;
        vertex_index += 6;
        polygon_index += 6;
    } while (vertex_index < 0x30d8);

    glm::vec3* colors = model->getVertexDIG(0, 1);
    for (index = 0; index < 5; ++index) {
        colors[index] = glm::vec3(1.0f, 0.0f, 0.0f);
    }
    for (index = 5; index < 500; ++index) {
        colors[index] = glm::vec3(0.0, 0.0, 1.0);
    }

    auto instance = CreateModelInstance(model);
    if (!instance) {
        srAssertFail("m_path_visualization", OCTPATH_CPP, 0x1226,
                     "CreateWayPointMesh -- Could not create pstModelInstance.\n");
    }
    instance->setName("WayPoint Mesh");
    instance->setExclusionMask(3);
    instance->setFlag(srNode::FLAG_TERMINATE);
    instance->setParent(GetWorld()->static_scene, 0);
    m_path_visualization = std::move(instance);
    return m_path_visualization.get();
}

/* Collect nearby waypoint surfaces and mark the subset directly visible from
   the editor position. The near query also admits every outgoing neighbor;
   the wider query contributes only its own surfaces. Candidates are deduped,
   sorted by integer distance, and span-tested nearest first. */
// FUNCTION: WIZ8 0x0045D880
short W8PathingService::CollectPathVisualization(const glm::vec3& position)
{
    unsigned short waypoints[500];
    w8_ulong distances[500];
    unsigned short waypoint_count = 0;
    std::vector<w8_ulong> query_results;
    int query_count;
    int index;

    if (start_waypoint == 0) {
        return 0;
    }

    std::ranges::fill(m_marked_path_nodes, false);
    std::ranges::fill(m_collected_path_nodes, false);
    if (destination_waypoint != 0 && !path_direction_valid) {
        SetBit(m_marked_path_nodes, destination_waypoint);
        SetBit(m_collected_path_nodes, destination_waypoint);
    }

    const auto queryWaypoints = [&](const glm::vec3& half_extent) {
        return g_octree->queryObjects({position - half_extent, position + half_extent},
                                      W8_OCTREE_KIND_WAYPOINT, -1);
    };
    query_results = queryWaypoints({g_float_twelve_thousand_five_hundred, g_float_five_thousand,
                                    g_float_twelve_thousand_five_hundred});
    query_count = query_results.size();

    for (index = 0; index < query_count; ++index) {
        unsigned short waypoint = static_cast<unsigned short>(query_results[index]);
        unsigned short edge_index;

        if (!SetBit(m_collected_path_nodes, waypoint)) {
            waypoints[waypoint_count++] = waypoint;
        }
        edge_index = m_waypoints[waypoint].first_edge;
        while (edge_index != 0) {
            W8PathEdge* edge = &m_pEdges[edge_index];
            unsigned short neighbor = edge->destination;

            if (!SetBit(m_collected_path_nodes, neighbor)) {
                waypoints[waypoint_count++] = neighbor;
            }
            edge_index = edge->next;
        }
    }

    query_results =
        queryWaypoints({g_waypoint_fallback_query_half_extent, g_float_twelve_thousand_five_hundred,
                        g_waypoint_fallback_query_half_extent});
    query_count = query_results.size();

    for (index = 0; index < query_count; ++index) {
        unsigned short waypoint = static_cast<unsigned short>(query_results[index]);
        if (!SetBit(m_collected_path_nodes, waypoint)) {
            waypoints[waypoint_count++] = waypoint;
        }
    }

    if (waypoint_count > 1) {
        unsigned int sort_index;

        for (sort_index = 0; sort_index < waypoint_count; ++sort_index) {
            const srVector3T<float>* candidate = &m_waypoints[waypoints[sort_index]].position;
            distances[sort_index] = static_cast<unsigned int>(
                static_cast<int>(glm::length(position - candidate->glmValue())));
        }

        std::ranges::sort(std::views::zip(std::span(distances, waypoint_count),
                                          std::span(waypoints, waypoint_count)),
                          {}, [](const auto& entry) { return std::get<0>(entry); });
    }

    const srVector3 source = position;
    for (unsigned int visible_index = 0; visible_index < waypoint_count; ++visible_index) {
        unsigned short waypoint = waypoints[visible_index];
        if (TestWaypointSpan(&source, &m_waypoints[waypoint].position, false, false)) {
            SetBit(m_marked_path_nodes, waypoint);
        }
    }
    return start_waypoint;
}

/* Append one waypoint surface and keep every capacity-coupled side table sized
   to the same hundred-record block.

   Surface zero is reserved on the first allocation. Growth replaces the three
   bit sets rather than preserving their bits, and recreates the shared
   unsigned-short scratch run. The new surface receives its index and position,
   is classified for the path-surface flag, and is registered in the octree's
   spatial object index as kind nine. */
// FUNCTION: WIZ8 0x0045ddb0
void W8PathingService::AddWaypoint(const srVector3T<float>* position)
{
    if (m_ulNumWayPoints % 100 == 0) {
        unsigned int capacity = (m_ulNumWayPoints / 100 + 1) * 100;
        W8PathSurface* new_surfaces =
            static_cast<W8PathSurface*>(malloc(capacity * sizeof(W8PathSurface)));

        if (new_surfaces == 0) {
            srAssertFail("pNewWayPoints", OCTPATH_CPP, 0x12ec, 0);
        }
        memset(new_surfaces, 0, capacity * sizeof(W8PathSurface));
        if (m_ulNumWayPoints == 0) {
            m_ulNumWayPoints = 1;
        } else {
            memcpy(new_surfaces, m_waypoints, m_ulNumWayPoints * sizeof(W8PathSurface));
            free(m_waypoints);
        }
        m_waypoints = new_surfaces;

        m_marked_path_nodes.assign(capacity, false);
        m_visited_path_nodes.assign(capacity, false);
        m_collected_path_nodes.assign(capacity, false);

        m_path_scratch.resize(capacity);
    }

    W8PathSurface* surface = &m_waypoints[m_ulNumWayPoints];

    surface->flags = 0x2000;
    surface->index = static_cast<unsigned short>(m_ulNumWayPoints);
    surface->position = *position;
    if ((ClassifyWaypoint(&surface->position) & W8_PATH_CELL_CONDITIONAL) != 0) {
        surface->flags |= W8_WAYPOINT_CONDITIONAL;
    }
    const auto point = g_octree->cellAt(position->glmValue());
    g_octree->object_registry->MoveObjectToCell(
        W8_OCTREE_KIND_WAYPOINT, static_cast<unsigned short>(m_ulNumWayPoints + 1), point);
    ++m_ulNumWayPoints;
}

/* Interactive link builder for one waypoint: asks the editor for a direction
   mask and link flags, queries the waypoint octree kind inside a 100000/50000
   box around the waypoint, span-tests each candidate, sorts them nearest-first
   by truncated distance, then adds the links the accepted direction mask
   allows. The first unreachable candidate ends the walk (they sort last). */
// FUNCTION: WIZ8 0x0045e030
void W8PathingService::SetWaypointLinkFlags(unsigned short waypoint, unsigned int direction)
{
    std::vector<w8_ulong> objects;
    unsigned int link_flags[2];
    unsigned int index;
    unsigned int count;
    unsigned int accepted;
    w8_ulong* distances;
    W8PathSurface* surface;
    W8PathSurface* candidate;
    srVector3T<float> lower;
    srVector3T<float> upper;
    float distance;

    if (1 < m_ulNumWayPoints) {
        link_flags[0] = 0;
        link_flags[1] = 0;
        accepted = EditWaypointLinkFlags("SET FLAGS FOR ALL LINKS FOR THIS WAYPOINT", link_flags,
                                         direction);
        if (accepted != 0) {
            surface = m_waypoints + waypoint;
            lower.Set(surface->position.x - g_path_surface_query_half_extent,
                      surface->position.y - g_float_fifty_thousand,
                      surface->position.z - g_path_surface_query_half_extent);
            upper.Set(surface->position.x + g_path_surface_query_half_extent,
                      surface->position.y + g_float_fifty_thousand,
                      surface->position.z + g_path_surface_query_half_extent);
            objects = g_octree->queryObjects({lower.glmValue(), upper.glmValue()},
                                             W8_OCTREE_KIND_WAYPOINT, waypoint);
            count = objects.size();
            if (count != 0) {
                distances = static_cast<w8_ulong*>(malloc(count * sizeof(w8_ulong)));
                for (index = 0; index < count; ++index) {
                    candidate = m_waypoints + objects[index];
                    if ((candidate->flags & 2) == 0) {
                        distance = (candidate->position - surface->position).Length();
                        if (TestWaypointSpan(&surface->position, &candidate->position, false,
                                             false)) {
                            distances[index] = static_cast<w8_ulong>(distance);
                        } else {
                            distances[index] = 0xffffffff;
                        }
                    } else {
                        distances[index] = 0xffffffff;
                    }
                }
                std::ranges::sort(
                    std::views::zip(std::span(distances, count), std::span(objects).first(count)),
                    {}, [](const auto& entry) { return std::get<0>(entry); });
                for (index = 0; index < count; ++index) {
                    if (distances[index] == 0xffffffff) {
                        return;
                    }
                    if (!HasDirectionalWaypointLink(waypoint,
                                                    static_cast<unsigned short>(objects[index]))) {
                        if ((accepted & 1) != 0) {
                            AddWaypointLink(waypoint, static_cast<unsigned short>(objects[index]),
                                            link_flags[0]);
                        }
                        if ((accepted & 2) != 0) {
                            AddWaypointLink(static_cast<unsigned short>(objects[index]), waypoint,
                                            link_flags[1]);
                        }
                    }
                }
            }
        }
    }
}

/* Unlink and clear one edge record.

   The owning surface or predecessor edge is redirected to the removed edge's
   successor. The retail scans stop after the first owner is found, then clear
   the packed record and increment the service's free-record count. */
// FUNCTION: WIZ8 0x0045e360
void W8PathingService::RemoveWaypointLink(unsigned short edge_index)
{
    if (m_ulNumWayPoints > 2) {
        bool found = false;
        unsigned int index;
        for (index = 1; index < m_ulNumWayPoints && !found; ++index) {
            if (m_waypoints[index].first_edge == edge_index) {
                m_waypoints[index].first_edge = m_pEdges[edge_index].next;
                found = true;
            }
        }

        for (index = 1; index < m_ulNumWayPtLinks && !found; ++index) {
            if (m_pEdges[index].next == edge_index) {
                m_pEdges[index].next = m_pEdges[edge_index].next;
                found = true;
            }
        }

        memset(&m_pEdges[edge_index], 0, sizeof(W8PathEdge));
        ++m_removed_edge_count;
        MarkRendererReady();
        waypoints_dirty = true;
    }
}

/* Choose the waypoint that best continues from the surface nearest source in
   the requested direction. Existing graph edges take precedence. When none
   are sufficiently aligned, probe twenty-five fixed steps forward and retain
   the best distinct surface found there. The second selection is marked valid
   only when it is an existing edge or the direct span test accepts it. */
// FUNCTION: WIZ8 0x0045e840
bool W8PathingService::PreparePathVisualization(const glm::vec3& source, const glm::vec3& direction)
{
    float best_alignment = -1.0f;
    unsigned short best_waypoint = 0;
    unsigned short source_waypoint;
    W8PathSurface* source_surface;

    destination_waypoint = 0;
    path_direction_valid = false;
    source_waypoint = FindWaypoint(source, false);
    source_surface = &m_waypoints[source_waypoint];

    const auto considerAlignedWaypoint = [&](unsigned short index) {
        const auto heading = normalizedOrZero(m_waypoints[index].position.glmValue() -
                                              source_surface->position.glmValue());
        const float alignment = glm::dot(heading, direction);
        if (alignment > best_alignment) {
            best_alignment = alignment;
            best_waypoint = index;
        }
    };
    if (glm::length(source_surface->position.glmValue() - source) <= g_double_twenty_five_hundred) {
        unsigned short edge_index = source_surface->first_edge;

        while (edge_index != 0) {
            W8PathEdge* edge = &m_pEdges[edge_index];
            unsigned short neighbor_index = edge->destination;
            considerAlignedWaypoint(neighbor_index);
            edge_index = edge->next;
        }

        if (best_alignment > g_path_direct_alignment_threshold) {
            destination_waypoint = best_waypoint;
            path_direction_valid = true;
            start_waypoint = source_waypoint;
            return true;
        }

        best_alignment = -1.0f;
        best_waypoint = 0;
        float distance = 0.0f;
        int probe_count;
        for (probe_count = 0; probe_count < 25; ++probe_count) {
            unsigned short probe_waypoint;

            distance += g_float_one_thousand;
            const auto probe = source_surface->position.glmValue() + direction * distance;
            probe_waypoint = FindWaypoint(probe, false);
            if (probe_waypoint != 0 && probe_waypoint != source_waypoint) {
                considerAlignedWaypoint(probe_waypoint);
            }
        }

        if (best_alignment > g_path_direct_alignment_threshold) {
            destination_waypoint = best_waypoint;
            if (TestWaypointSpan(&source_surface->position, &m_waypoints[best_waypoint].position,
                                 false, false)) {
                path_direction_valid = true;
            }
            start_waypoint = source_waypoint;
            return true;
        }
    }

    start_waypoint = source_waypoint;
    return false;
}

/* Add one directed edge to the waypoint graph, or update the matching edge
   when the source already owns it.

   Edge zero is the list sentinel and storage grows in hundred-record blocks.
   The cached length is computed before growth, the new record is appended to
   the source surface's chain, and the geometry-derived flag follows the same
   endpoint/span tests as an updated edge. */
// FUNCTION: WIZ8 0x0045ec30
void W8PathingService::AddWaypointLink(unsigned short source, unsigned short destination,
                                       unsigned int flags)
{
    W8PathSurface* source_surface;
    W8PathSurface* destination_surface;
    float distance;
    W8PathEdge* edge;

    if (source == 0 || destination == 0 || source == destination) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("Cannot Link: Tried to link WayPt {:d} to WayPt {:d}. ", source,
                               destination));
        return;
    }

    source_surface = &m_waypoints[source];
    destination_surface = &m_waypoints[destination];
    if ((IsZeroVector(&source_surface->position) != 0) ||
        (IsZeroVector(&destination_surface->position) != 0)) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("Cannot Link: WayPt {:d} is at (0, 0, 0). ", source));
        return;
    }

    if (UpdateWaypointLink(source, destination, flags)) {
        return;
    }

    distance = (source_surface->position - destination_surface->position).Length();

    if (m_ulNumWayPtLinks % 100 == 0) {
        unsigned int capacity = m_ulNumWayPtLinks / 100 + 1;
        W8PathEdge* new_edges =
            static_cast<W8PathEdge*>(malloc(capacity * 100 * sizeof(W8PathEdge)));

        if (new_edges == 0) {
            srAssertFail("pNewWayPtLinks", OCTPATH_CPP, 0x1526, 0);
        }
        memset(new_edges, 0, capacity * 100 * sizeof(W8PathEdge));
        if (m_ulNumWayPtLinks == 0) {
            m_ulNumWayPtLinks = 1;
        } else {
            memcpy(new_edges, m_pEdges, m_ulNumWayPtLinks * sizeof(W8PathEdge));
            free(m_pEdges);
        }
        m_pEdges = new_edges;
    }

    edge = &m_pEdges[m_ulNumWayPtLinks];
    edge->flags = flags;
    edge->destination = destination;
    edge->source = source;
    edge->distance = distance;
    edge->next = 0;

    if (source_surface->first_edge == 0) {
        source_surface->first_edge = static_cast<unsigned short>(m_ulNumWayPtLinks);
    } else {
        unsigned short previous = source_surface->first_edge;

        while (m_pEdges[previous].next != 0) {
            previous = m_pEdges[previous].next;
        }
        m_pEdges[previous].next = static_cast<unsigned short>(m_ulNumWayPtLinks);
    }

    if ((source_surface->flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
        (destination_surface->flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
        (TestWaypointSpan(&source_surface->position, &destination_surface->position, false, false),
         span_blocked)) {
        edge->flags |= W8_PATH_EDGE_CONDITIONAL;
    }
    ++m_ulNumWayPtLinks;
}

/* Decide whether a new directed edge would duplicate the graph already leading
   from source toward destination.

   A direct edge is an immediate hit. Otherwise only nearer first-hop neighbors
   matter: their horizontal direction is normalized and compared with the
   destination direction. A neighbor that already links to the destination uses
   the tighter alignment threshold; every nearer neighbor also receives the
   looser threshold test. Vertical displacement participates in the distance
   ordering but not in either direction comparison. */
// FUNCTION: WIZ8 0x0045ef90
bool W8PathingService::HasDirectionalWaypointLink(unsigned short source, unsigned short destination)
{
    W8PathSurface* source_surface = &m_waypoints[source];
    const W8PathSurface* destination_surface = &m_waypoints[destination];
    srVector3T<float> destination_direction;
    float destination_distance;
    unsigned short edge_index;

    destination_direction = destination_surface->position - source_surface->position;
    destination_distance = destination_direction.Length();
    glm::vec2 destination_horizontal = destination_direction.xz();
    destination_horizontal = normalizedOrZero(destination_horizontal);
    destination_direction.Set(destination_horizontal.x, 0.0f, destination_horizontal.y);

    edge_index = source_surface->first_edge;
    while (edge_index != 0) {
        W8PathEdge* edge = &m_pEdges[edge_index];
        unsigned short neighbor_index = edge->destination;
        W8PathSurface* neighbor = &m_waypoints[neighbor_index];
        srVector3T<float> neighbor_direction;
        float neighbor_distance;

        if (neighbor_index == destination) {
            return true;
        }

        neighbor_direction = neighbor->position - source_surface->position;
        neighbor_distance = neighbor_direction.Length();
        if (neighbor_distance < destination_distance) {
            unsigned short second_edge_index;
            glm::vec2 neighbor_horizontal = neighbor_direction.xz();

            neighbor_horizontal = normalizedOrZero(neighbor_horizontal);
            neighbor_direction.Set(neighbor_horizontal.x, 0.0f, neighbor_horizontal.y);

            second_edge_index = neighbor->first_edge;
            while (second_edge_index != 0) {
                if (m_pEdges[second_edge_index].destination == destination) {
                    break;
                }
                second_edge_index = m_pEdges[second_edge_index].next;
            }
            if (second_edge_index != 0 &&
                glm::dot(neighbor_direction.glmValue(), destination_direction.glmValue()) >
                    g_float_nine_tenths) {
                return true;
            }
            if (glm::dot(neighbor_direction.glmValue(), destination_direction.glmValue()) >
                g_path_direct_alignment_threshold) {
                return true;
            }
        }
        edge_index = edge->next;
    }
    return false;
}

/* Update an already-linked directed edge.

   The source surface owns the chain. A match receives the caller's new flags;
   the geometry-derived flag is also forced when either endpoint is a registered
   path surface or when the service's span test leaves its shared mode enabled.
   The image applies that derived bit through the next-edge slot rather than the
   matched index, so this preserves that observable retail behavior. */
// FUNCTION: WIZ8 0x0045f200
bool W8PathingService::UpdateWaypointLink(unsigned short source, unsigned short destination,
                                          unsigned int flags)
{
    unsigned short edge_index = m_waypoints[source].first_edge;

    while (edge_index != 0) {
        W8PathEdge* edge = &m_pEdges[edge_index];

        if (edge->destination == destination) {
            edge->flags = flags;
            if ((m_waypoints[source].flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
                (m_waypoints[destination].flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
                (TestWaypointSpan(&m_waypoints[source].position, &m_waypoints[destination].position,
                                  false, false),
                 span_blocked)) {
                m_pEdges[m_ulNumWayPtLinks].flags |= W8_PATH_EDGE_CONDITIONAL;
            }
            return true;
        }
        edge_index = edge->next;
    }
    return false;
}

/* Edit the directed path edge joining a teleportal's two settled endpoints.

   An endpoint only counts as an existing teleportal waypoint when the lookup
   lands on a flagged surface within the shared snap distance. Missing ends are
   inserted with their respective inbound/outbound defaults. When both ends
   already existed, the first end's edge chain is searched so the dialog edits
   the current flags rather than starting from zero. The resulting edge is
   forced dynamic, its cached distance is invalidated, and both the renderer
   and path-edit state are marked dirty. */
// FUNCTION: WIZ8 0x0045f2d0
void W8PathingService::EditTeleportalLink(const srVector3T<float>* destination,
                                          const srVector3T<float>* source)
{
    char title[80];
    unsigned int link_flags[2];
    unsigned short destination_index;
    unsigned short source_index;
    unsigned short edge_index;
    bool both_existing = true;

    if (!m_waypoint_editing) {
        return;
    }

    destination_index = FindWaypoint(destination->glmValue(), false);
    if ((m_waypoints[destination_index].flags & 2) == 0 ||
        (m_waypoints[destination_index].position - *destination).Length() > g_double_five_hundred) {
        destination_index = 0;
    }

    source_index = FindWaypoint(source->glmValue(), false);
    if ((m_waypoints[source_index].flags & 2) == 0 ||
        (m_waypoints[source_index].position - *source).Length() > g_double_five_hundred) {
        source_index = 0;
    }

    if (destination_index == 0) {
        destination_index = static_cast<unsigned short>(m_ulNumWayPoints);
        AddWaypoint(destination);
        m_waypoints[destination_index].flags = 2;
        SetWaypointLinkFlags(destination_index, 6);
        both_existing = false;
    }
    if (source_index == 0) {
        source_index = static_cast<unsigned short>(m_ulNumWayPoints);
        AddWaypoint(source);
        m_waypoints[source_index].flags = 2;
        SetWaypointLinkFlags(source_index, 5);
        both_existing = false;
    }

    edge_index = 0;
    link_flags[0] = 0;
    link_flags[1] = 0;
    if (both_existing) {
        edge_index = m_waypoints[destination_index].first_edge;
        while (edge_index != 0) {
            if (m_pEdges[edge_index].destination == source_index) {
                link_flags[0] = m_pEdges[edge_index].flags;
                break;
            }
            edge_index = m_pEdges[edge_index].next;
        }
    }

    if (edge_index != 0) {
        sprintf(title, "EDIT FLAGS FOR EXISTING LINK BETWEEN TELEPORTAL WAYPOINTS: ");
    } else {
        sprintf(title, "EDIT FLAGS FOR NEW LINK BETWEEN TELEPORTAL WAYPOINTS: ");
    }
    EditWaypointLinkFlags(title, link_flags, 5);

    if (edge_index == 0) {
        edge_index = static_cast<unsigned short>(m_ulNumWayPtLinks);
        AddWaypointLink(destination_index, source_index, link_flags[0]);
    } else {
        m_pEdges[edge_index].flags = link_flags[0];
    }
    m_pEdges[edge_index].flags |= W8_PATH_EDGE_TELEPORTAL;
    m_pEdges[edge_index].distance = 0.0f;
    MarkRendererReady();
    waypoints_dirty = true;
}

struct FlagName {
    unsigned int bit;
    const char* name;
};

static const FlagName kMovementNames[] = {
    {W8_NAV_WALK, "Walk "},
    {W8_NAV_FLY, "Fly "},
    {W8_NAV_SWIM, "Swim "},
};

static const FlagName kSizeNames[] = {
    {W8_NAV_TINY, "Tiny "},   {W8_NAV_SMALL, "Small "}, {W8_NAV_MEDIUM, "Medium "},
    {W8_NAV_LARGE, "Large "}, {W8_NAV_HUGE, "Huge "},
};

/* Appends each name whose bit is set in value, in table order. */
template<std::size_t Count>
static void appendFlagNames(char* line, unsigned int value, const FlagName (&names)[Count])
{
    for (const auto& entry : names) {
        if ((value & entry.bit) != 0) strcat(line, entry.name);
    }
}

/* Modal waypoint-link flag editor: renders the link's direction, movement,
   size, nav-group, collision and door bits over nine heap-allocated text/wide
   line buffers and services a keyboard loop. 'C' and 'T' toggle the collide
   and through-door bits, 'D'/'G'/'M'/'S' arm a pending second-key prompt for
   the direction/group/movement/size bits, ENTER commits and ESC cancels. Only
   the pending-'D' direction keys actually change state; the 'G' second key
   computes a bit the retail code drops, and the 'M'/'S' second keys are dead.
   Returns the accepted direction mask, zero on cancel. */
// FUNCTION: WIZ8 0x0045f530
unsigned int W8PathingService::EditWaypointLinkFlags(const char* title, unsigned int* flags,
                                                     unsigned int direction)
{
    glm::vec3 colour_saved(0);
    glm::vec3 colour_backup(0);
    char* lines[9];
    char* wide[9];
    char groups[32];
    unsigned int current;
    unsigned int link_flags;
    unsigned int index;
    unsigned int line;
    int letter;
    int key;
    char pending = 0;
    short length;

    GetWorldColour(&colour_backup);
    PublishLightDirection(&colour_saved);
    g_smfnt_font.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
    const TextOptions text_options{
        .colors = {.shadow = Get16BPPColor(SDL_Color{0, 0, 0, SDL_ALPHA_OPAQUE})}};
    if (direction == 0 || (waypoint_editor_flags & 2) != 0) {
        direction = 3;
    }
    if (flags[0] == 0) {
        flags[0] = W8_NAV_GROUP_MASK | W8_NAV_MOVEMENT_MASK | W8_NAV_SIZE_FILTER_MASK;
    }
    if (flags[1] == 0) {
        flags[1] = W8_NAV_GROUP_MASK | W8_NAV_MOVEMENT_MASK | W8_NAV_SIZE_FILTER_MASK;
    }
    link_flags = (((direction & 3) == 2) ? flags[1] : flags[0]) &
                 (W8_NAV_THROUGH_DOORS | W8_NAV_NO_GEOMETRY_COLLISION);
    for (index = 0; index < 9; ++index) {
        lines[index] = new char[0x100];
        memset(lines[index], 0, 0x100 * sizeof(char));
        wide[index] = new char[0x100];
        memset(wide[index], 0, 0x100);
        groups[index] = 0;
    }
    if (title != 0) {
        strcpy(lines[0], title);
    } else {
        sprintf(lines[0], "EDIT NEW WAYPOINT LINK FLAGS:");
    }
    sprintf(lines[8], "ESC to Cancel, ENTER to Accept");
    for (;;) {
        current = ((direction & 1) != 0) ? flags[0] : flags[1];
        sprintf(lines[2], " (D)irections: ");
        if ((direction & 3) == 3) {
            strcat(lines[2], "2 way link             ");
        } else if ((direction & 3) == 1) {
            strcat(lines[2], "1 way link outward     ");
        } else {
            strcat(lines[2], "1 way link inward      ");
        }
        if ((direction & 4) != 0) {
            strcat(lines[2], "(FIXED)");
        }
        sprintf(lines[3], " (M)ovement: ");
        if ((current & W8_NAV_MOVEMENT_MASK) == W8_NAV_MOVEMENT_MASK) {
            strcat(lines[3], "All types              ");
        } else {
            appendFlagNames(lines[3], current, kMovementNames);
        }
        sprintf(lines[4], " (S)ize of Monster: ");
        if ((current & W8_NAV_SIZE_FILTER_MASK) == W8_NAV_SIZE_FILTER_MASK) {
            strcat(lines[4], "All sizes");
        } else {
            appendFlagNames(lines[4], current, kSizeNames);
        }
        sprintf(lines[5], " (G)roup: ");
        if ((current & W8_NAV_GROUP_MASK) == W8_NAV_GROUP_MASK) {
            strcat(lines[5], "All Nav Groups");
        } else {
            sprintf(groups, "[                   ] (A to P)");
            letter = 0;
            for (index = 0; index < 16; ++index) {
                if ((index % 4) == 0) {
                    ++letter;
                }
                if ((current & (1 << index)) != 0) {
                    groups[letter] = 'A' + index;
                }
                ++letter;
            }
            strcat(lines[5], groups);
        }
        sprintf(lines[6], " (C)ollide with Geometry: ");
        strcat(lines[6], (current & W8_NAV_NO_GEOMETRY_COLLISION) != 0 ? "No" : "Yes");
        sprintf(lines[7], " (T)hrough Door: ");
        strcat(lines[7], (current & W8_NAV_THROUGH_DOORS) != 0 ? "Yes" : "No");
        ClearSurfaceRect(0x1e, 0x64, 0x262, 0xcc);
        for (line = 0; line < 9; ++line) {
            for (length = 0; lines[line][length] != '\0' && length < 0x50; ++length) {
                wide[line][length] = lines[line][length];
            }
            while (length < 0x50) {
                wide[line][length] = 0x20;
                ++length;
            }
            wide[line][length] = 0;
            DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                                  {0x1f, 0x65 + static_cast<int>(line) * 0xd}, wide[line],
                                  text_options);
        }
        InvalidateRegion(0x1e, 0x64, 0x262, 0xcc);
        auto next_input = PollInputEvent();
        while (gfProgramIsRunning && !next_input) {
            RenderFrame();
            RenderFrame();
            PumpGameEvents(true);
            next_input = PollInputEvent();
        }
        if (!gfProgramIsRunning) {
            direction = 0;
            break;
        }
        const InputEvent atom = *next_input;
        if (atom.type != InputEventType::KeyDown) {
            continue;
        }
        if (atom.key == SDLK_ESCAPE) {
            if (pending == 0) {
                direction = 0;
                break;
            }
            sprintf(lines[1], "                                             ");
            pending = 0;
            continue;
        }
        if (atom.key == SDLK_RETURN) {
            if (pending == 0) {
                break;
            }
            pending = 0;
            continue;
        }
        if (pending != 0) {
            key = (atom.key <= 0x7f ? toupper(static_cast<unsigned char>(atom.key)) : atom.key);
            switch (pending) {
            case 'D':
                if (key == 'B') {
                    direction |= 3;
                } else if (key == 'O') {
                    direction = direction & ~2;
                } else if (key == 'I') {
                    direction = direction & ~1;
                }
                break;
            case 'M':
            case 'S':
                break;
            case 'G':
                if ('A' <= key && key <= 'P') {
                    /* Retail computes 1 << (pending - 'A') here and drops the
                       result; nothing consumes it. */
                }
                break;
            case 'C':
                if (key != 'Y') {
                    pending = 0;
                }
                break;
            }
        } else {
            switch ((atom.key <= 0x7f ? toupper(static_cast<unsigned char>(atom.key)) : atom.key)) {
            case 'D':
                if ((waypoint_editor_flags & 2) != 0) {
                    sprintf(lines[1], " Can only set flags in both directions in this link mode");
                } else if ((direction & 4) == 0) {
                    sprintf(lines[1], " (B)oth, (O)utward, or (I)nward");
                    pending = 'D';
                }
                break;
            case 'M':
                sprintf(lines[1], " Toggle (W)alking, (F)lying, or (S)wimming");
                pending = 'M';
                break;
            case 'S':
                sprintf(lines[1], " (A)ll (T)iny (S)mall (M)edium (L)arge (H)uge");
                pending = 'S';
                break;
            case 'G':
                sprintf(lines[1], " Key Toggle: Cap is On, Lowercase is Off");
                pending = 'G';
                break;
            case 'C':
                link_flags ^= W8_NAV_NO_GEOMETRY_COLLISION;
                break;
            case 'T':
                link_flags ^= W8_NAV_THROUGH_DOORS;
                break;
            default:
                lines[1][0] = 0;
                break;
            }
        }
    }
    ClearSurfaceRect(0x1e, 0x64, 0x262, 0xd9);
    for (line = 0; line < 9; ++line) {
        for (length = 0; length < 0x50; ++length) {
            wide[line][length] = 0x20;
        }
        wide[line][0x50] = 0;
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                              {0x1f, 0x65 + static_cast<int>(line) * 0xd}, wide[line],
                              text_options);
    }
    InvalidateRegion(0x1e, 0x64, 0x262, 0xd9);
    RenderFrame();
    RenderFrame();
    for (index = 0; index < 9; ++index) {
        delete[] lines[index];
        delete[] wide[index];
    }
    PublishLightDirection(&colour_backup);
    link_flags = (current & 0xedffffff) | link_flags;
    direction &= 3;
    if ((direction & 1) != 0) {
        flags[0] = link_flags;
    }
    if ((direction & 2) != 0) {
        flags[1] = link_flags;
    }
    waypoint_editor_flags |= 0x100;
    this->link_flags = link_flags;
    return direction;
}

/* Look one named path up, and report the region and height range it spans.

   The table holds fixed 0x44-byte entries whose name is the entry itself and
   whose handle sits at +0x40. A match walks the path's two chained tables to
   take the extent of every node it touches: the region ids straight out of the
   low and high halves of each entry, and the height from the entry's low half
   scaled by the service's own span and lifted by the bounds floor. */
// FUNCTION: WIZ8 0x00457cf0
unsigned int W8PathingService::FindPathHandle(const char* path_name, W8PathGridBounds* path_bounds,
                                              W8PathVerticalRange* path_range)
{
    unsigned int index;
    unsigned int lookup_index;
    unsigned short value;
    float height;

    if (path_name == 0 || *path_name == 0 || m_pCondPaths == 0 || m_ulNumCondPaths == 0) {
        return 0;
    }
    for (index = 0; index < static_cast<unsigned int>(m_ulNumCondPaths); ++index) {
        if (strcmp(path_name, m_pCondPaths[index].name) == 0) {
            path_bounds->min_z = 0xffff;
            path_bounds->min_x = 0xffff;
            path_bounds->max_z = 0;
            path_bounds->max_x = 0;
            path_range->minimum = 1e+08f;
            path_range->maximum = -1e+08f;
            lookup_index = m_pCondPaths[index].lookup_index;
            while (m_pulCondLookup[lookup_index] != 0) {
                unsigned int key_index = m_pulCondLookup[lookup_index];
                while (m_pulCondNodeKeys[key_index] != 0) {
                    value = static_cast<unsigned short>(m_pulCondNodeKeys[key_index]);
                    if (value < path_bounds->min_x) {
                        path_bounds->min_x = value;
                    }
                    if (path_bounds->max_x < value) {
                        path_bounds->max_x = value;
                    }
                    value = static_cast<unsigned short>(m_pulCondNodeKeys[key_index] >> 0x10);
                    if (value < path_bounds->min_z) {
                        path_bounds->min_z = value;
                    }
                    if (path_bounds->max_z < value) {
                        path_bounds->max_z = value;
                    }
                    height = (m_pulCondNodeValues[key_index] & W8_PATH_CELL_HEIGHT_MASK) * span +
                             level_bounds.minimum.y;
                    if (height < path_range->minimum) {
                        path_range->minimum = height;
                    }
                    if (path_range->maximum < height) {
                        path_range->maximum = height;
                    }
                    ++key_index;
                }
                ++lookup_index;
            }
            return m_pCondPaths[index].lookup_index;
        }
    }
    return 0;
}

struct W8PathParameter {
    const char* name;
    float* value;
};

// GLOBAL: WIZ8 0x0060FA18
static W8PathParameter g_path_parameters[] = {
    {"ACCELERATION_FACTOR", &g_path_acceleration_factor},
    {"ANGULAR_ACCEL_FACTOR", &g_path_angular_acceleration_factor},
    {"ANGULAR_DECEL_FACTOR", &g_path_angular_deceleration_factor},
    {"PREDICTION_TIME", &g_path_prediction_time},
    {"APPROACH_SLOW_TIME", &g_path_approach_slow_time},
    {"APPROACH_RUN_TIME", &g_path_approach_run_time},
    {"APPROACH_RUN_RATE", &g_path_approach_run_rate},
    {"PATH_PREDICTION_TIME", &g_path_lookahead_time},
    {"GROUP_REPULSION_FACTOR", &g_path_group_repulsion_factor},
    {"PARTY_BOUNDARY_RADIUS", &g_path_party_boundary_radius},
    {"OBSTACLE_STEER_FACTOR", &g_path_obstacle_steering_factor},
    {"OBSTACLE_BRAKE_FACTOR", &g_path_obstacle_braking_factor},
    {"MIN_ANIMATION_RATE", &g_navigator_minimum_speed},
    {"FLY_SWIM_MIN_ANIM_RATE", &g_navigator_minimum_speed_mode23},
    {0, 0}};

// FUNCTION: WIZ8 0x004cae40
W8PathParameters::W8PathParameters()
{
    LoadPathParameters();
}

// FUNCTION: WIZ8 0x004cae50
void W8PathParameters::InitializeSteeringContext(W8NavigatorMovementState* movement)
{
    W8Navigator* linked;

    this->movement = movement;
    monster = GetMonsterByLocationID(movement->location_id);
    radius = monster->radius;
    velocity_length = movement->velocity.Length();
    linked = monster->linked_navigator;
    if (linked == 0) {
        speed_limit = movement->movement_scale * g_world_scale;
    } else {
        speed_limit = linked->movement.movement_scale * g_world_scale;
    }
    acceleration = g_path_acceleration_factor * speed_limit;
    if (IsZeroVector(&movement->velocity) != 0) {
        direction.Set(0.0, 0.0, 1.0);
        direction.RotateAboutY(sin(movement->yaw), cos(movement->yaw));
    } else {
        direction = movement->velocity;
        direction.Normalize();
    }
    perpendicular.Set(direction.z, 0.0, -direction.x);
    force.SetZero();
    blocked = false;
    nearby_queried = false;
}

// FUNCTION: WIZ8 0x004cafc0
bool W8PathParameters::QueryNearbyNavigators()
{
    srVector3T<float> lower;
    srVector3T<float> upper;
    float extent;

    if (nearby_queried) {
        return !nearby_locations.empty();
    }
    extent = radius * g_float_five;
    nearby_queried = true;
    lower.Set(movement->position.x - extent, movement->position.y - extent,
              movement->position.z - extent);
    upper.Set(extent + movement->position.x, extent + movement->position.y,
              extent + movement->position.z);
    nearby_locations =
        g_octree->locationsInBox({lower.glmValue(), upper.glmValue()}, movement->location_id);

    return !nearby_locations.empty();
}

// FUNCTION: WIZ8 0x004cb090
void W8PathParameters::IntegrateSteering()
{
    float step;
    srVector3T<float> velocity;
    srVector3T<float> position;
    srVector3T<float> delta;
    srVector3T<float> slide;
    float scale;
    bool snapped;

    step = g_rate * g_game_time_accumulator->GetFrameDelta();
    if (!blocked) {
        force.y = 0.0f;
        if (speed_limit <= g_float_zero) {
            velocity.SetZero();
            velocity_length = 0.0f;
            movement->target_yaw = NormalizeAngle(static_cast<float>(atan2(force.x, force.z)));
        } else {
            if (acceleration < force.Length()) {
                force.SetLength(acceleration);
            }
            velocity = movement->velocity + force * step;
            velocity_length = velocity.Length();
            if (speed_limit < velocity_length) {
                velocity.SetLength(speed_limit);
                velocity_length = speed_limit;
            }
            movement->target_yaw =
                NormalizeAngle(static_cast<float>(atan2(velocity.x, velocity.z)));
        }
        if (movement->target_yaw != movement->yaw) {
            UpdateYawSteering(step, true);
            movement->yaw = movement->target_yaw;
            velocity.Set(0.0, 0.0, velocity_length);
            velocity.RotateAboutY(sin(movement->target_yaw), cos(movement->target_yaw));
        }
        position = movement->position + velocity * step;
        delta = position;
        snapped = g_octree->pathing->SnapWaypointPosition(&position, false);
        if (!snapped) {
            delta = position - movement->position;
            const auto slide_direction = g_octree->pathing->neighborSlideDirection(
                movement->position.glmValue(), delta.glmValue());
            if (!slide_direction) {
                blocked = true;
            } else {
                slide = *slide_direction;
                scale = glm::dot(*slide_direction, delta.glmValue());
                delta += slide * scale;
                position = movement->position + delta;
                snapped = g_octree->pathing->SnapWaypointPosition(&position, false);
                if (!snapped) {
                    movement->target_yaw =
                        NormalizeAngle(static_cast<float>(atan2(slide.x, slide.z)));
                    position = movement->position;
                    UpdateYawSteering(step, false);
                    movement->yaw = movement->target_yaw;
                }
            }
        }
        if (!blocked) {
            movement->velocity = velocity;
            movement->position = position;
            return;
        }
    }
    velocity.SetZero();
    position = movement->position;
    g_octree->pathing->FindPathCell(&position, 0, true);
    movement->velocity = velocity;
    movement->position = position;
}

// FUNCTION: WIZ8 0x004cb520
void W8PathParameters::UpdateYawSteering(float time_step, bool use_turn_rate)
{
    float remaining;
    float rate;
    float direction;
    float angular_velocity;

    remaining = NormalizeAngle(movement->yaw - movement->target_yaw);
    rate = NormalizeAngle(movement->target_yaw - movement->yaw);
    direction = g_negative_one;
    if (rate < remaining) {
        remaining = rate;
        direction = g_float_one;
    }
    if (!use_turn_rate) {
        rate = movement->turn_rate;
        angular_velocity = 0.0f;
    } else {
        angular_velocity =
            g_path_angular_acceleration_factor * movement->turn_rate * direction * time_step +
            movement->yaw_velocity;
        rate = static_cast<float>(fabs(angular_velocity));
    }
    if (remaining <= rate * time_step) {
        movement->yaw_velocity = g_path_angular_deceleration_factor * angular_velocity;
        return;
    }
    movement->target_yaw = NormalizeAngle(rate * time_step * direction + movement->yaw);
    movement->yaw_velocity = angular_velocity;
}

// FUNCTION: WIZ8 0x004cb620
bool W8PathParameters::PredictNavigatorCollision()
{
    float lookahead;
    float nearest;
    float lateral;
    bool found;
    unsigned int index;
    W8Monster* monster;
    W8Navigator* navigator;
    srVector3T<float> position;
    srVector3T<float> other_velocity;
    srVector3T<float> relative;
    srVector3T<float> delta;
    float approach;
    float combined;
    float steer;
    float brake;
    float scale;

    found = false;
    if (!this->monster->active) {
        return false;
    }
    if (velocity_length == g_double_zero) {
        return false;
    }
    lookahead = g_path_prediction_time * velocity_length + radius;
    nearest = lookahead + g_camera_snap_epsilon;
    if (QueryNearbyNavigators()) {
        for (index = 0; index < nearby_locations.size(); ++index) {
            monster = GetMonsterByLocationID(nearby_locations[index]);
            if (monster == 0) {
                navigator = 0;
            } else {
                navigator = monster;
            }
            if (this->monster->IsLinkedToNavigator(navigator)) {
                continue;
            }
            navigator = monster;
            position = navigator->GetPosition();
            delta = position - movement->position;
            if (g_float_zero < glm::dot(delta.glmValue(), direction.glmValue())) {
                other_velocity = navigator->GetVelocity();
                relative = movement->velocity - other_velocity;
                relative.Normalize();
                approach = glm::dot(delta.glmValue(), relative.glmValue());
                approach = approach - (monster->radius * approach) / delta.Length();
                if (g_float_zero < approach && approach < nearest) {
                    lateral = relative.z * delta.x + -relative.x * delta.z;
                    combined = monster->radius + radius;
                    if (static_cast<float>(fabs(lateral)) < combined) {
                        scale = lateral / combined;
                        found = true;
                        nearest = approach;
                    }
                }
            }
        }
    }
    if (movement->boundary_enabled) {
        position = g_startup_world->GetPosition();
        delta = position - movement->position;
        combined = g_path_party_boundary_radius * g_world_scale;
        approach = radius * g_float_five + combined;
        if (approach * approach > delta.LengthSquared()) {
            approach = glm::dot(delta.glmValue(), direction.glmValue());
            if (approach > g_float_zero) {
                relative = movement->velocity.glmValue() - g_level_data->camera_motion_velocity;
                relative.Normalize();
                approach = glm::dot(delta.glmValue(), relative.glmValue());
                approach = approach - (combined * approach) / delta.Length();
                if (approach > g_float_zero && approach < nearest) {
                    lateral = delta.x * relative.z + delta.z * -relative.x;
                    combined += radius;
                    if (static_cast<float>(fabs(lateral)) < combined) {
                        scale = lateral / combined;
                        found = true;
                        nearest = approach;
                    }
                }
            }
        }
    }
    if (!found) {
        return false;
    }
    if (scale >= g_float_zero) {
        steer = scale - g_float_one;
    } else {
        steer = scale + g_float_one;
    }
    steer = (g_float_one - nearest / lookahead) * acceleration * steer;
    brake = g_path_obstacle_braking_factor * steer;
    steer *= g_path_obstacle_steering_factor;
    force += perpendicular * steer;
    force += direction * -brake;
    return found;
}

// FUNCTION: WIZ8 0x004cbb70
bool W8PathParameters::HandleObstacleAhead()
{
    srVector3T<float> escape;
    srVector3T<float> waypoint;
    srVector3T<float> ahead;
    float reach;
    float distance;
    float clearance;
    float steer;
    float brake;
    float side;

    if (velocity_length == g_float_zero) {
        if (const auto slide = g_octree->pathing->neighborSlideDirection(
                movement->position.glmValue(), direction.glmValue())) {
            escape = *slide;
            waypoint = movement->attachment->GetNextPosition();
            waypoint -= movement->position;
            waypoint.y = 0.0f;
            if ((escape.z * waypoint.x + waypoint.z * -escape.x) *
                    (escape.z * direction.x + -escape.x * direction.z) <
                g_double_zero) {
                speed_limit = 0.0f;
                waypoint.SetLength(acceleration);
                force += waypoint;
                return true;
            }
        }
    } else {
        reach = g_path_prediction_time * velocity_length + radius;
        ahead = movement->position + direction * reach;
        if (!g_octree->pathing->TestWaypointSpan(&movement->position, &ahead, true, true)) {
            ahead -= movement->position;
            distance = ahead.Length();
            if (distance <= reach) {
                waypoint = movement->attachment->GetNextPosition();
                waypoint -= movement->position;
                waypoint.y = 0.0f;
                waypoint.SetLength(1.0);
                if (g_double_zero <= glm::dot(waypoint.glmValue(), direction.glmValue())) {
                    steer = (g_float_one - distance / reach) * acceleration;
                    brake = g_path_obstacle_braking_factor * steer;
                    steer = g_path_obstacle_steering_factor * steer;
                    if (const auto slide =
                            g_octree->pathing->obstacleDirection(direction.glmValue())) {
                        escape = *slide;
                        side = glm::dot(escape.glmValue(), perpendicular.glmValue());
                        if (g_obstacle_slide_side_threshold <= fabs(side)) {
                            if (side < g_float_zero) {
                                steer = -steer;
                            }
                        } else {
                            escape.Normalize();
                            if (glm::dot(escape.glmValue(), perpendicular.glmValue()) <
                                g_float_zero) {
                                steer = -steer;
                            }
                        }
                        force += perpendicular * steer;
                        force += direction * -brake;
                        return true;
                    }
                    blocked = true;
                    return true;
                }
                steer = acceleration;
                speed_limit = 0.0f;
                force += waypoint * steer;
                return true;
            }
        } else if (g_world_scale < radius) {
            clearance = g_octree->pathing->CompareDirectionalClearance(&movement->position,
                                                                       &direction, radius);
            if (g_float_one < static_cast<float>(fabs(clearance))) {
                force += perpendicular * clearance;
                return true;
            }
        }
    }
    return false;
}

// FUNCTION: WIZ8 0x004cc1a0
void W8PathParameters::AccumulateSeekForce()
{
    srVector3T<float> desired;
    srVector3T<float> seek;

    if (velocity_length == g_float_zero) {
        desired = target - movement->position;
        desired.y = 0.0f;
        desired.Normalize();
        if (glm::dot(desired.glmValue(), direction.glmValue()) < g_double_zero) {
            speed_limit = 0.0f;
            force += desired * acceleration;
            return;
        }
    }
    desired = target - movement->position;
    desired.y = 0.0f;
    desired.SetLength(speed_limit);
    seek = desired - movement->velocity;
    seek *= g_path_acceleration_factor;
    if (acceleration < seek.Length()) {
        seek.SetLength(acceleration);
    }
    force += seek;
}

// FUNCTION: WIZ8 0x004cc420
void W8PathParameters::SeekWithApproachSpeed()
{
    float distance;
    float scale;

    distance = (target - movement->position).Length();
    scale = distance / (g_path_approach_slow_time * speed_limit);
    if (g_float_one < scale) {
        distance /= g_path_approach_run_time * speed_limit;
        if (g_float_one < distance) {
            distance = g_float_one;
        }
        scale = (g_path_approach_run_rate - g_float_one) * distance + g_float_one;
    }
    speed_limit = scale * speed_limit;
    AccumulateSeekForce();
}

// FUNCTION: WIZ8 0x004cc4c0
void W8PathParameters::AccumulateGroupRepulsion()
{
    unsigned int index;
    W8Monster* monster;
    W8Navigator* navigator;
    srVector3T<float> position;
    srVector3T<float> delta;
    float distance;
    float combined;
    float falloff;

    if (QueryNearbyNavigators()) {
        for (index = 0; index < nearby_locations.size(); ++index) {
            monster = GetMonsterByLocationID(nearby_locations[index]);
            if (monster == 0) {
                navigator = 0;
            } else {
                navigator = monster;
            }
            if (!this->monster->IsLinkedToNavigator(navigator)) {
                continue;
            }
            position = monster->GetPosition();
            delta = position - movement->position;
            distance = delta.Length();
            combined = (radius + radius + monster->radius) * g_obstacle_radius_average_scale;
            if (distance < combined * g_float_three) {
                falloff = g_float_one;
                if (combined < distance) {
                    falloff = combined / distance;
                }
                delta = delta * static_cast<float>(g_double_negative_one);
                if (delta.LengthSquared() != g_double_zero) {
                    delta.SetLength(g_path_group_repulsion_factor * acceleration * falloff *
                                    falloff);
                }
                force += delta;
            }
        }
    }
}

// FUNCTION: WIZ8 0x004cc680
bool W8PathParameters::SteerAroundLeader(bool allow_path_fallback)
{
    W8Navigator* leader;
    srVector3T<float> heading;
    srVector3T<float> delta;
    srVector3T<float> offset;
    float leader_radius;
    float ahead;
    float lateral;
    float distance;
    double side;

    leader = monster->linked_navigator;
    leader_radius = leader->radius;
    if (IsZeroVector(&leader->movement.velocity) != 0) {
        heading.Set(0.0, 0.0, 1.0);
        heading.RotateAboutY(sin(leader->movement.yaw), cos(leader->movement.yaw));
    } else {
        heading = leader->movement.velocity;
        heading.Normalize();
    }
    if (leader->movement.velocity.x != g_float_zero ||
        leader->movement.velocity.y != g_float_zero ||
        leader->movement.velocity.z != g_float_zero) {
        delta.x = movement->position.x - leader->movement.position.x;
        delta.z = movement->position.z - leader->movement.position.z;
        ahead = delta.x * heading.x + delta.z * heading.z +
                (movement->position.y - leader->movement.position.y) * heading.y;
        if (g_float_zero < ahead &&
            ahead < leader->movement.velocity.Length() * g_path_prediction_time) {
            lateral = heading.z * delta.x - heading.x * delta.z;
            if (fabs(lateral) < static_cast<double>(leader_radius) + radius) {
                side = g_double_one;
                if (lateral < g_double_zero) {
                    side = g_double_negative_one;
                }
                offset.Set(heading.z * side * g_double_five_hundred, 0.0,
                           -heading.x * side * g_double_five_hundred);
                target = movement->position + offset;
                speed_limit = g_path_approach_run_rate * speed_limit;
                AccumulateSeekForce();
                return true;
            }
        }
    }
    if (allow_path_fallback && static_cast<int>(movement->attachment->path_cursor) <
                                   static_cast<int>(leader->movement.attachment->path_cursor) +
                                       static_cast<int>(movement->attachment->follow_offset)) {
        delta = leader->movement.position - movement->position;
        distance = delta.Length() - leader_radius;
        if (g_path_approach_slow_time * speed_limit < distance) {
            distance /= g_path_approach_run_time * speed_limit;
            if (g_float_one < distance) {
                distance = g_float_one;
            }
            speed_limit =
                ((g_path_approach_run_rate - g_float_one) * distance + g_float_one) * speed_limit;
        }
        return false;
    }
    offset = heading * (leader_radius * static_cast<float>(g_double_negative_one));
    target = leader->movement.position + offset;
    SeekWithApproachSpeed();
    return true;
}

// FUNCTION: WIZ8 0x004ccad0
void W8PathParameters::SteerFromPathStart(W8NavigatorMovementState* movement, bool alternate)
{
    InitializeSteeringContext(movement);
    movement->attachment->flags &= ~W8_NAV_ATTACHMENT_COLLISION_PREDICTED;
    if (!HandleObstacleAhead()) {
        if (PredictNavigatorCollision()) {
            movement->attachment->flags |= W8_NAV_ATTACHMENT_COLLISION_PREDICTED;
        } else if (!alternate && SteerAroundLeader(true)) {
            AccumulateGroupRepulsion();
            IntegrateSteering();
            return;
        } else {
            target = movement->attachment->GetNextPosition();
            AccumulateSeekForce();
        }
    }
    if (!alternate) {
        AccumulateGroupRepulsion();
    }
    IntegrateSteering();
}

// FUNCTION: WIZ8 0x004ccb60
bool W8PathParameters::SteerAlongPath(W8NavigatorMovementState* movement, bool alternate)
{
    srVector3T<float> ahead;
    bool advanced;
    float reach;

    advanced = false;
    InitializeSteeringContext(movement);
    movement->attachment->flags &= ~W8_NAV_ATTACHMENT_COLLISION_PREDICTED;
    if (!HandleObstacleAhead()) {
        if (g_float_zero < velocity_length) {
            reach = g_path_prediction_time * velocity_length;
            ahead = this->movement->position + direction * reach;
            if (!movement->attachment->checkPredictedHopHeight(ahead.glmValue())) {
                target = this->movement->position;
                advanced = movement->attachment->AdvancePositionTowardWaypoint(&target, reach);
                AccumulateSeekForce();
                goto steered;
            }
        }
        if (PredictNavigatorCollision()) {
            movement->attachment->flags |= W8_NAV_ATTACHMENT_COLLISION_PREDICTED;
            goto steered;
        }
        if (!alternate && SteerAroundLeader(true)) {
            AccumulateGroupRepulsion();
            IntegrateSteering();
            return false;
        }
        target = movement->attachment->GetNextPosition();
        AccumulateSeekForce();
    }
steered:
    if (!alternate) {
        AccumulateGroupRepulsion();
    }
    IntegrateSteering();
    return advanced;
}

// FUNCTION: WIZ8 0x004cccb0
bool LoadPathParameters()
try {
    auto handle = wiz8::open_file("Data\\Monsters\\pathparms.txt");
    bool more = true;
    char line[128];
    while (more != 0) {
        ReadTextLine(handle.get(), line, sizeof(line), more);
        if (line[0] == '#') {
            continue;
        }
        for (int index = 0; g_path_parameters[index].name != 0; ++index) {
            if (strncmp(line, g_path_parameters[index].name,
                        strlen(g_path_parameters[index].name)) == 0) {
                sscanf(line, "%*s %f", g_path_parameters[index].value);
                break;
            }
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Second emission of the float RotateAboutY; the primary template lives in
   srMath.h and surrender_math.cpp holds the 0x438F90 copy. */
