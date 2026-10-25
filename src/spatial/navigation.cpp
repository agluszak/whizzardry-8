#include "wiz8/spatial/navigation.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/common.hpp> // NOLINT(misc-include-cleaner)
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/ext/vector_double3.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)
#include <glm/gtc/quaternion.hpp>
#include <glm/vector_relational.hpp> // NOLINT(misc-include-cleaner)
#include <wiz8/filesystem.h>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "index_algorithms.h"
#include "local_code/Factions.h"
#include "navigation_flags.h"
#include "packed_bits.h"
#include "wiz8/asset_paths.h"
#include "wiz8/binary_reader.h"
#include "wiz8/combat/range.h"
#include "wiz8/data/records.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/math.h"
#include "wiz8/navigation_math.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/compiled_mesh.h"
#include "wiz8/render/picking.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation_settings.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {
TimerClockView navigationClocks(const NavigationMesh& navigation)
{
    const auto* index = navigation.owner_octree;
    return index && index->owner_world ? index->owner_world->clocks : TimerClockView{};
}

/* Path cells key Z in the high half and X in the low half, modulo 2^32 like
   retail, so out-of-grid coordinates still produce a (missing) key. */
unsigned int pathCellKey(int x, int z)
{
    return static_cast<unsigned int>(z) * 0x10000u + static_cast<unsigned int>(x);
}

class SteeringStep {
public:
    explicit SteeringStep(const NavigationMesh& navigation)
        : navigation(navigation), settings(navigation.settings)
    {
    }
    const NavigationMesh& navigation;
    const NavigationSettings& settings;

    /* Seeds the context from `movement`: the owning monster, its radius, the
       speed limit (the linked navigator's movement scale when linked), the
       normalized velocity or a yaw-derived default direction, the right-hand
       perpendicular, and cleared force/query state. */
    bool InitializeSteeringContext(NavigatorMovement* movement); /* 0x004CAE50 */
    /* Lazily fills nearby_locations with the location ids
       inside a radius-scaled box around the movement position; the cached
       result is returned on repeat calls. */
    bool QueryNearbyNavigators(); /* 0x004CAFC0 */
    /* Applies force for one time step: clamps it to acceleration,
       integrates velocity toward speed_limit, resolves the heading, snaps
       the new position against the path mesh and falls back to sliding or
       stopping when the snap fails. */
    void IntegrateSteering(); /* 0x004CB090 */
    /* Advances movement->target_yaw toward movement->yaw by the shorter
       arc, accelerating or decelerating the angular velocity in
       movement->yaw_velocity. */
    void UpdateYawSteering(float time_step, bool use_turn_rate); /* 0x004CB520 */
    /* Predicts a collision with another navigator or the party inside the
       prediction window; returns nonzero when one is found ahead. */
    bool PredictNavigatorCollision(); /* 0x004CB620 */
    /* Steers around geometry: brakes and deflects force perpendicular to a
       clipped span, pushes toward the next waypoint when it falls behind the
       heading, or uses directional clearance for oversized radii. */
    bool HandleObstacleAhead(); /* 0x004CBB70 */
    /* Adds the seek force toward target scaled by
       g_path_acceleration_factor into force, clamped to
       acceleration; with a stopped movement it instead pushes along the
       2-D target direction and zeroes speed_limit. */
    void AccumulateSeekForce(); /* 0x004CC1A0 */
    /* Scales speed_limit down by target distance through the approach
       profile, then accumulates the seek force. */
    void SeekWithApproachSpeed(); /* 0x004CC420 */
    /* Adds a repulsion force from every linked navigator inside the combined
       radius into force. */
    void AccumulateGroupRepulsion(); /* 0x004CC4C0 */
    /* Steers around the linked leader: lateral pass targets, following
       distance, or a blocked-path fallback that reseeds target. */
    bool SteerAroundLeader(bool allow_path_fallback); /* 0x004CC680 */
    /* The full per-step driver for a navigator at the start of its route:
       obstacle, collision, leader and waypoint steering, group repulsion,
       then integration. */
    void SteerFromPathStart(NavigatorMovement* movement, bool alternate); /* 0x004CCAD0 */
    /* The mid-route driver: additionally predicts the hop height along the
       path and advances the target past waypoints the step covers, returning
       the advance result. */
    bool SteerAlongPath(NavigatorMovement* movement, bool alternate); /* 0x004CCB60 */

private:
    NavigatorMovement* movement;
    float speed_limit;
    float acceleration;
    float velocity_length;
    glm::vec3 target;
    glm::vec3 direction;
    glm::vec3 perpendicular;
    glm::vec3 force;
    float radius;
    bool nearby_queried;
    bool blocked;
    std::vector<std::uint32_t> nearby_locations;
    Monster* monster;
};

}

constexpr double kObstacleSlideSideThreshold = 0.2;
constexpr double kPathBlockingAlignmentLowerBound = -0.9;
constexpr double kPathBlockingAlignmentUpperBound = 0.9;
constexpr float kPathGapPenaltyScale = 0.5099999904632568f;
constexpr float kPathHeuristicScale = 1.2000000476837158f;
constexpr double kPathOverlapTolerance = 250.0;

class Material;
class Texture;

const float g_path_link_cost_base = 1001.0f;
const float g_patrol_cost_limit = 250000.0f;
const float g_obstacle_radius_average_scale = 0.3333333f;

/* Engine Code\OctPath.cpp. The unit is named by its own assertions, which
   place every one of these bodies in OctPath.cpp rather than in Octree.cpp
   where the octree's own loader lives. */

unsigned short g_path_reserve = 2000;
const float g_path_span_scale = 1.5259254723787308e-05f;

const double g_path_initial_cost_slack = 1.1;
const double g_path_segment_split_distance = 25000.0;
const float g_path_direction_threshold_0 = -0.9239000082015991f;
const float g_path_direction_threshold_1 = -0.38269999623298645f;
const float g_path_direction_threshold_2 = 0.38269999623298645f;
const float g_path_direction_threshold_3 = 0.9239000082015991f;
const float g_path_cardinal_scale = 1.4149999618530273f;

bool LoadPathParameters(NavigationSettings& settings);

/* Mark a waypoint, growing the set with slack when the index is past its end. */

void W8PathHeapHandle::DeleteRoot(W8PathSearchNode* node)
{
    if (!open.empty()) {
        open.pop();
    }
    root_node = open.empty() ? 0 : open.top().node;
    node->flags |= W8_PATH_SEARCH_CLOSED;
}

namespace {
/* A waypoint as a .WPT file stores it: the persistent flags, the first edge and the
   world position. */
struct WaypointRecord {
    std::uint16_t flags = 0;
    std::uint16_t first_edge = 0;
    glm::vec3 position{0, 0, 0};
};

WaypointRecord ReadWaypointRecord(wiz8::BinaryReader& reader)
{
    WaypointRecord record;
    record.flags = reader.record<std::uint16_t>();
    record.first_edge = reader.record<std::uint16_t>();
    for (int axis = 0; axis < 3; ++axis) record.position[axis] = reader.record<float>();
    return record;
}

/* An edge as a .WPT file stores it, in little-endian fields: flags (4 bytes), the
   source waypoint (2, absent in version one, which derives it from the chains), the
   destination (2), the distance (4) and the next edge (2). */
W8PathEdge ReadEdgeRecord(wiz8::BinaryReader& reader, unsigned int version)
{
    W8PathEdge edge;
    edge.flags = reader.record<std::uint32_t>();
    if (version != 1) edge.source = reader.record<std::uint16_t>();
    edge.destination = reader.record<std::uint16_t>();
    edge.distance = reader.record<float>();
    edge.next = reader.record<std::uint16_t>();
    return edge;
}
} // namespace

/* Read a versioned .WPT snapshot and rebuild its live graph representation.
   Version one stores the edges without their source waypoint. */
unsigned char NavigationMesh::ReadWaypointFile(LevelSpatialIndex& octree)
try {
    if (path_node_count == 0) {
        return false;
    }
    auto handle = wiz8::open_file(wiz8::asset_path(std::string(level_name) + ".WPT"));
    wiz8::BinaryReader reader(*handle);
    const auto version = reader.record<std::uint32_t>();
    reader.record<std::uint32_t>(); // the edge node count, which nothing reads
    const auto waypoint_count = reader.record<std::uint32_t>();
    const auto link_count = reader.record<std::uint32_t>();
    if (version < 1 || waypoint_count == 0 || waypoint_count > 0xffff || link_count == 0 ||
        link_count > 0xffff) {
        return false;
    }
    std::vector<WaypointRecord> file_waypoints(waypoint_count);
    for (auto& waypoint : file_waypoints) waypoint = ReadWaypointRecord(reader);
    std::vector<W8PathSurface> waypoints(waypoint_count);
    std::vector<W8PathEdge> edges(link_count);
    for (auto& edge : edges) edge = ReadEdgeRecord(reader, version);
    for (unsigned int index = 1; index < link_count; ++index) {
        const auto& edge = edges[index];
        if (edge.destination >= waypoint_count || edge.source >= waypoint_count ||
            edge.next >= link_count) {
            return false;
        }
    }
    std::vector<bool> visited(link_count);
    for (unsigned int surface = 1; surface < waypoint_count; ++surface) {
        const auto& source = file_waypoints[surface];
        auto& destination = waypoints[surface];
        destination.flags = source.flags;
        destination.index = static_cast<unsigned short>(surface);
        destination.first_edge = source.first_edge;
        destination.position = source.position;
        if ((destination.flags & 0xf000) == 0) {
            destination.flags |= 0x2000;
        }
        for (unsigned int edge = source.first_edge; edge != 0; edge = edges[edge].next) {
            if (edge >= link_count || visited[edge]) {
                return false;
            }
            visited[edge] = true;
        }
    }

    for (unsigned int surface = 1; surface < m_waypoints.size(); ++surface) {
        octree.registry.cells->UnregisterObject(W8_OCTREE_KIND_WAYPOINT, surface + 1);
    }
    m_waypoints = std::move(waypoints);
    m_pEdges = std::move(edges);
    for (unsigned int surface = 1; surface < waypoint_count; ++surface) {
        const auto point = octree.cellAt(m_waypoints[surface].position);
        octree.registry.cells->MoveObjectToCell(W8_OCTREE_KIND_WAYPOINT, surface + 1, point);
    }

    if (version <= 2) {
        unsigned int surface;
        for (surface = 1; surface < m_waypoints.size(); ++surface) {
            if ((ClassifyWaypoint(&m_waypoints[surface].position) & W8_PATH_CELL_CONDITIONAL) != 0)
                m_waypoints[surface].flags |= W8_WAYPOINT_CONDITIONAL;
        }
        for (surface = 1; surface < m_waypoints.size(); ++surface) {
            unsigned short edge_index = m_waypoints[surface].first_edge;
            while (edge_index != 0) {
                W8PathEdge* edge = &m_pEdges[edge_index];
                edge->source = static_cast<unsigned short>(surface);
                unsigned int destination_index = edge->destination;
                if ((m_waypoints[surface].flags & W8_WAYPOINT_CONDITIONAL) != 0 ||
                    (m_waypoints[destination_index].flags & W8_WAYPOINT_CONDITIONAL) != 0) {
                    edge->flags |= W8_PATH_EDGE_CONDITIONAL;
                } else {
                    const auto span_result =
                        tracePathSpan(m_waypoints[surface].position,
                                      m_waypoints[destination_index].position, false, false);
                    if (span_result.conditional) edge->flags |= W8_PATH_EDGE_CONDITIONAL;
                }
                edge_index = edge->next;
            }
        }
    }
    return true;
} catch (const std::exception&) {
    return false;
}

/* Read the path hash and conditional tables completely before replacing
   live state. Counts below the lookup/key sentinel pair denote empty tables. */
unsigned char NavigationMesh::ReadPathNodes(wiz8::File* handle)
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
    std::vector<GDPropCondPaths> paths(header.path_count);
    std::vector<unsigned int> lookup(header.frame_count);
    std::vector<unsigned short> frames(header.frame_count);
    std::vector<unsigned int> keys(header.node_count);
    std::vector<unsigned int> values(header.node_count);
    handle->read_exact(paths.data(), std::size_t{header.path_count} * sizeof(GDPropCondPaths));
    handle->read_exact(lookup.data(), std::size_t{header.frame_count} * sizeof(unsigned int));
    handle->read_exact(frames.data(), std::size_t{header.frame_count} * sizeof(unsigned short));
    handle->read_exact(keys.data(), std::size_t{header.node_count} * sizeof(unsigned int));
    handle->read_exact(values.data(), std::size_t{header.node_count} * sizeof(unsigned int));
    for (unsigned int index = 0; index < header.path_count; ++index) {
        if (paths[index].lookup_index >= header.frame_count ||
            memchr(paths[index].name, 0, sizeof(paths[index].name)) == 0) {
            return false;
        }
    }
    for (unsigned int index = 1; index < header.frame_count; ++index) {
        if (lookup[index] >= header.node_count) {
            return false;
        }
    }
    if (header.frame_count != 0 &&
        (lookup[header.frame_count - 1] != 0 || keys[header.node_count - 1] != 0)) {
        return false;
    }

    std::unordered_multimap<unsigned int, unsigned int> path_values;
    for (const auto& node : nodes) {
        path_values.emplace(node.cell, node.level_flags);
    }
    cells = std::move(path_values);
    m_pCondPaths = std::move(paths);
    m_pulCondLookup = std::move(lookup);
    m_pusCondNodeFrames = std::move(frames);
    m_pulCondNodeKeys = std::move(keys);
    m_pulCondNodeValues = std::move(values);
    return true;
} catch (const std::exception&) {
    return false;
}

// Convert X/Z once; truncation toward zero also applies below the bounds.
Monster* NavigationMesh::findMonster(int location) const
{
    return owner_octree && owner_octree->owner_world
               ? owner_octree->owner_world->population->findEntity(location)
               : nullptr;
}

Navigator* NavigationMesh::partyNavigator() const
{
    return owner_octree && owner_octree->owner_world ? &owner_octree->owner_world->party.navigator
                                                     : nullptr;
}

PathCellCoordinates NavigationMesh::pathCell(const glm::vec3& position) const
{
    const glm::ivec2 cell((glm::vec2(position.x, position.z) -
                           glm::vec2(level_bounds.minimum.x, level_bounds.minimum.z)) /
                          grid_scale);
    return {cell, pathCellKey(cell.x, cell.y)};
}

/* Offer every flagged surface to the path builder.

   Surfaces are 0x28 bytes apart and the walk starts at index one, so entry zero
   is never a real surface. The point handed on is the surface's own position
   converted to the graph's integer grid - x from the bounds floor, z from the
   third bound - and the two conversions happen in the order the point's fields
   do not. */
void NavigationMesh::LinkSurfaces(Prop* prop)
{
    unsigned int index = 1;
    W8PathSurface* surface;

    if (m_waypoints.size() <= index) {
        return;
    }
    do {
        surface = &m_waypoints[index];
        if ((surface->flags & W8_WAYPOINT_CONDITIONAL) != 0) {
            prop->RegisterPathSurface(index, pathCell(surface->position).coordinates);
        }
        ++index;
    } while (index < m_waypoints.size());
}

/* The same for the edges, which are 0xe bytes apart, gated by a different flag,
   and which name two surfaces by index in their shorts at +4 and +6. Each of
   those surfaces contributes one converted point, so the builder receives the
   edge as a pair. */
void NavigationMesh::LinkEdges(Prop* prop)
{
    unsigned int index = 1;
    W8PathEdge* edge;
    W8PathSurface* surface;

    if (m_pEdges.size() <= index) {
        return;
    }
    do {
        edge = &m_pEdges[index];
        if ((edge->flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
            surface = &m_waypoints[edge->source];
            const auto first = pathCell(surface->position).coordinates;

            surface = &m_waypoints[edge->destination];
            const auto second = pathCell(surface->position).coordinates;
            prop->RegisterPathVertex(index, first, second);
        }
        ++index;
    } while (index < m_pEdges.size());
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
void NavigationMesh::SetConditionalPathFrame(unsigned int path_handle, unsigned short frame)
{
    auto* index = &cells;
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
        lookup_index = path_handle;
        while (m_pulCondLookup[lookup_index] != 0) {
            unsigned int key_index = m_pulCondLookup[lookup_index];
            while (m_pulCondNodeKeys[key_index] != 0) {
                unsigned int key = m_pulCondNodeKeys[key_index];
                unsigned int current_value =
                    FindConditionalPathValue(key, m_pulCondNodeValues[key_index]);
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
                unsigned int key = m_pulCondNodeKeys[key_index];
                unsigned int current_value =
                    FindConditionalPathValue(key, m_pulCondNodeValues[key_index]);
                if ((m_pulCondNodeValues[key_index] & W8_PATH_CELL_BLOCKING_FRAME) == 0 &&
                    (current_value & W8_PATH_CELL_INACTIVE) == 0) {
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
unsigned int NavigationMesh::FindConditionalPathValue(unsigned int key, unsigned int value)
{
    auto* index = &cells;
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
void NavigationMesh::CheckConditionalWayPtStatus(std::span<const unsigned short> waypoints)
{
    for (const auto waypoint : waypoints) {
        assert(waypoint < m_waypoints.size());

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
void NavigationMesh::CheckConditionalLinkStatus(std::span<const unsigned short> edges)
{
    for (const auto edge_index : edges) {
        assert(edge_index < m_pEdges.size());

        W8PathEdge* edge = &m_pEdges[edge_index];
        if ((edge->flags & W8_PATH_EDGE_CONDITIONAL) != 0) {
            if ((m_waypoints[edge->source].flags & W8_WAYPOINT_DISABLED) != 0 ||
                !tracePathSpan(m_waypoints[edge->source].position,
                               m_waypoints[edge->destination].position, false, false)
                     .reachable) {
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
unsigned char NavigationMesh::HandlePathEdgeTransition(NavigatorMovement* movement)
{
    NavigatorRoute* attachment = movement->attachment.get();
    std::size_t cursor = attachment->path_cursor;
    unsigned int flags = 0;
    unsigned short destination;

    if (cursor < attachment->lastWaypoint()) {
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
   throwaway attachment is built over (from, to); a positive `budget` is held
   in path_search.cost_limit while the path builds, and both exits restore the
   1.0e10f limit. The result carries the measured path length and the count of
   hop segments whose link is gated by a door. */
std::optional<MeasuredPath> NavigationMesh::MeasureAttachmentPath(PathSearchScratch& path_search,
                                                                  const glm::vec3& from,
                                                                  const glm::vec3& to,
                                                                  float budget) const
{
    NavigatorRoute attachment(&from, &to);
    if (budget > 0.0f) {
        path_search.cost_limit = budget;
    }
    if (!BuildAttachmentPath(path_search, &attachment, 0)) {
        path_search.cost_limit = 1.0e10f;
        return std::nullopt;
    }

    MeasuredPath result{attachment.MeasurePathLength(), 0};
    while (attachment.path_cursor < attachment.lastWaypoint()) {
        if (TestAttachmentHopDoor(&attachment)) {
            ++result.door_hops;
        }
        ++attachment.path_cursor;
    }
    path_search.cost_limit = 1.0e10f;
    return result;
}

/* Whether the hop at the attachment's current index runs over a disabled
   conditional edge guarded by a closed door. The current path value pair names
   the two surfaces; when their link carries both the conditional and disabled
   bits, the props inside the segment's bounds box are queried and the one
   nearest the midpoint supplies the trigger. The hop counts when that
   trigger's action record is a type-10 door record whose flag bit is clear. */
bool NavigationMesh::TestAttachmentHopDoor(NavigatorRoute* attachment) const
{
    std::size_t current = attachment->path_cursor;
    if (current < attachment->lastWaypoint()) {
        const auto& pairs = attachment->path;
        unsigned short source = pairs[current].value;
        unsigned short destination = pairs[current + 1].value;
        const W8PathSurface* source_surface = &m_waypoints[source];
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
            BoundingBox bounds{source_surface->position, source_surface->position};
            bounds.include(m_waypoints[destination].position);
            std::vector<std::uint32_t> candidates;
            Trigger* selected = 0;
            candidates = owner_octree->queryObjects(bounds, W8_OCTREE_KIND_PROP, -1);
            const auto count = std::ssize(candidates);
            if (count > 0) {
                if (count == 1) {
                    Prop* prop = owner_octree->owner_world->collidable_props[candidates[0]];
                    selected = prop->boundTrigger();
                    if (selected == 0) {
                        return false;
                    }
                } else {
                    double nearest_distance = 1e32;
                    const glm::vec3 center = bounds.center();
                    for (int index = 0; index < count; ++index) {
                        Prop* prop = owner_octree->owner_world->collidable_props[candidates[index]];
                        Trigger* trigger = prop->boundTrigger();
                        if (trigger != 0) {
                            const glm::vec3 position = prop->GetPosition();
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
                const auto* door = selected->doorData();
                if (door && !door->open) {
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
unsigned short NavigationMesh::FindPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                                        unsigned int flags) const
{
    path_search.graph.assign(m_waypoints.size(), {});
    path_search.route.resize(m_waypoints.size() + 1);
    W8PathHeapHandle path_heap;
    std::vector<bool> visited(std::max<std::size_t>(100, m_waypoints.size()), false);
    std::vector<bool> marked(std::max<std::size_t>(100, m_waypoints.size()), false);
    unsigned short usStartNode = FindWaypoint(path_search, attachment->segment_start, false);
    if (usStartNode == 0 && (usStartNode = path_search.start_waypoint) == 0) {
        return 0;
    }
    unsigned int start = usStartNode;
    unsigned short usEndNode = FindWaypoint(path_search, attachment->path_destination, false);
    assert(m_waypoints.size() > start);
    unsigned int end = usEndNode;
    assert(m_waypoints.size() > end);
    if (usEndNode == 0) {
        return 0;
    }
    path_heap.open = {};
    SetBit(marked, start);
    path_search.graph[start].search_parent = 0;
    unsigned short usWayPt = usStartNode;
    if (usStartNode != usEndNode) {
        do {
            unsigned int current = usWayPt;
            assert(m_waypoints.size() > current);
            unsigned short usLink = m_waypoints[current].first_edge;
            unsigned int link = usLink;
            assert(m_pEdges.size() > link);
            bool settled = true;
            if (usLink != 0) {
                do {
                    assert(m_pEdges.size() > link);
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
                    assert(m_waypoints.size() > usNextNode);
                    if ((usNext != 0) && (usNext != usCurrent) && (!TestBit(visited, usNextNode))) {
                        if (!SetBit(marked, usNextNode)) {
                            path_search.graph[usNextNode].path_cost =
                                m_pEdges[link].distance + path_search.graph[current].path_cost;
                            const W8PathSurface* surfaces = m_waypoints.data();
                            if (path_search.graph[usNextNode].path_cost < path_search.cost_limit) {
                                float dx =
                                    surfaces[end].position.x - surfaces[usNextNode].position.x;
                                settled = false;
                                float dy =
                                    surfaces[end].position.y - surfaces[usNextNode].position.y;
                                float dz =
                                    surfaces[end].position.z - surfaces[usNextNode].position.z;
                                path_search.graph[usNextNode].heuristic =
                                    sqrt(dx * dx + dy * dy + dz * dz) * kPathHeuristicScale;
                                path_search.graph[usNextNode].search_parent = usCurrent;
                                path_search.graph[current].estimated_total_cost =
                                    path_search.graph[usNextNode].heuristic * kPathHeuristicScale +
                                    path_search.graph[usNextNode].path_cost;
                                path_search.graph[usNextNode].in_heap = true;
                                W8PathHeapHandle* handle = &path_heap;
                                W8PathHeapEntry entry;
                                entry.node = usNextNode;
                                entry.priority = static_cast<unsigned int>(
                                    path_search.graph[current].estimated_total_cost);
                                handle->open.push(entry);
                                handle->root_node = handle->open.top().node;
                            }
                        } else {
                            float cost =
                                m_pEdges[link].distance + path_search.graph[current].path_cost;
                            if ((cost + 1e+01f < path_search.graph[usNextNode].path_cost) &&
                                (cost < path_search.cost_limit)) {
                                float reduction = path_search.graph[usNextNode].path_cost - cost;
                                path_search.graph[usNextNode].search_parent = usCurrent;
                                settled = false;
                                path_search.graph[current].estimated_total_cost =
                                    path_search.graph[usNextNode].heuristic * kPathHeuristicScale +
                                    path_search.graph[usNextNode].path_cost;
                                if (!path_search.graph[current].in_heap) {
                                    path_search.graph[usNextNode].in_heap = true;
                                    W8PathHeapHandle* handle = &path_heap;
                                    W8PathHeapEntry entry;
                                    entry.node = usNextNode;
                                    entry.priority = static_cast<unsigned int>(
                                        path_search.graph[current].estimated_total_cost);
                                    handle->open.push(entry);
                                    handle->root_node = handle->open.top().node;
                                }
                                ReduceWaypointCosts(path_search, usNextNode, reduction, marked);
                            }
                        }
                    }
                    usLink = m_pEdges[link].next;
                    link = usLink;
                    assert(m_pEdges.size() > link);
                } while (usLink != 0);
            }
            if (settled) {
                W8PathHeapHandle* handle = &path_heap;
                W8PathOpenSet* heap = &handle->open;
                if (!heap->empty()) {
                    heap->pop();
                }
                handle->root_node = heap->empty() ? 0 : heap->top().node;
                path_search.graph[current].in_heap = false;
                SetBit(visited, current);
            }
            usWayPt = path_heap.root_node;
            current = usWayPt;
            assert(m_waypoints.size() > current);
            if (usWayPt == 0) {
                return 0;
            }
        } while (usWayPt != usEndNode);
    }
    path_search.graph[start].search_parent = 0;
    return usWayPt;
}

void NavigationMesh::AppendScratchPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                                       unsigned int count) const
{
    path_search.graph.resize(m_waypoints.size());
    path_search.route.resize(m_waypoints.size() + 1);
    path_search.route[count] = 0;
    while (count != 0) {
        unsigned short surface_index = path_search.route[count - 1];
        const glm::vec3* position = &m_waypoints[surface_index].position;
        attachment->AppendPathPosition(position, surface_index);
        --count;
    }
}

/* Build the attachment's stored route for the path found between its start
   and end positions. The parent chain left by FindPath is reversed through
   the shared scratch array into route entries, the end position
   is appended, and the final hop is trimmed back to the destination when the
   last waypoint already sees it. */
bool NavigationMesh::BuildAttachmentPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                                         unsigned int flags) const
{
    path_search.graph.resize(m_waypoints.size());
    path_search.route.resize(m_waypoints.size() + 1);
    if (!m_waypoints.empty()) {
        unsigned short node = FindPath(path_search, attachment, flags);
        if (node != 0) {
            std::vector<bool> marked(std::max<std::size_t>(100, m_waypoints.size()), false);
            unsigned short count = 0;
            do {
                if (SetBit(marked, node)) {
                    break;
                }
                assert(m_waypoints.size() > count);
                path_search.route[count] = node;
                node = path_search.graph[node].search_parent;
                ++count;
            } while (node != 0);
            AppendScratchPath(path_search, attachment, count);
            glm::vec3* destination = &attachment->path_destination;
            glm::vec3* slot = &attachment->path[attachment->lastWaypoint()].position;
            *slot = *destination;
            if (attachment->lastWaypoint() >= 2 &&
                ((attachment->path_cursor < attachment->lastWaypoint()) ||
                 ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0)) &&
                (tracePathSpan(attachment->path[attachment->lastWaypoint() - 2].position,
                               *(destination), false, false)
                     .reachable)) {
                attachment->path.pop_back();
                slot = &attachment->path[attachment->lastWaypoint()].position;
                *slot = *destination;
            }
            attachment->flags |= 0x20000;
            return true;
        }
    }
    return false;
}

/* Both recursive path searches finish with the same attachment updates. */
static void FinalizeWaypointRoute(NavigatorRoute* attachment, bool has_start_waypoint,
                                  const glm::vec3& waypoint)
{
    if (1 < attachment->lastWaypoint()) {
        attachment->path.pop_back();
        attachment->path_destination = attachment->path[attachment->lastWaypoint()].position;
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
bool NavigationMesh::LinkAttachmentTarget(PathSearchScratch& path_search,
                                          NavigatorRoute* attachment, unsigned int flags,
                                          const glm::vec3* target, float separation) const
{
    path_search.graph.assign(m_waypoints.size(), {});
    path_search.route.resize(m_waypoints.size() + 1);
    W8PatrolSearch patrol;
    unsigned short node;
    unsigned short count;
    unsigned short current;
    unsigned int index;
    unsigned short start;

    patrol.min = separation;
    patrol.distance = separation + 1e+04f;
    patrol.start = *target;
    path_search.filter = flags;
    path_search.start_waypoint = 0;
    start = FindWaypoint(path_search, attachment->segment_start, true);
    if ((start == 0) && ((start = path_search.start_waypoint) == 0)) {
        return false;
    }
    std::vector<bool> visited(std::max<std::size_t>(100, m_waypoints.size()), false);
    std::vector<bool> marked(std::max<std::size_t>(100, m_waypoints.size()), false);
    index = start;
    SetBit(marked, index);
    path_search.best_key = index;
    patrol.cost = glm::length((*target - attachment->segment_start));
    path_search.graph[index].search_parent = 0;
    path_search.graph[index].path_cost = patrol.cost;
    node = RecurseTargetLinks(path_search, start, visited, patrol);
    if (node == 0) {
        if (patrol.cost > separation) {
            attachment->path_destination = m_waypoints[path_search.best_key].position;
            node = FindPath(path_search, attachment, flags);
        }
        if (node == 0) {
            return false;
        }
    }
    attachment->path_destination = m_waypoints[node].position;
    count = 0;
    std::ranges::fill(marked, false);
    current = node;
    while (current != 0) {
        if (SetBit(marked, current)) {
            break;
        }
        assert(m_waypoints.size() > count);
        path_search.route[count] = current;
        current = path_search.graph[current].search_parent;
        ++count;
    }
    AppendScratchPath(path_search, attachment, count);
    attachment->TruncatePathAtRadius(target, separation);
    FinalizeWaypointRoute(attachment, path_search.start_waypoint != 0, path_search.probe_position);
    return true;
}

/* Recursive depth-first link search from `waypoint`. Collects the waypoint's
   admissible links (the same edge/surface flag filters as FindPath), extends
   each destination's accumulated link cost, keys the candidates by their
   distance to the stored target, sorts them, and either returns the first
   candidate priced beyond patrol.distance or recurses into each in sorted
   order. The farthest-measured candidate is parked in path_search.best_key for
   the fallback path in LinkAttachmentTarget. */
unsigned short NavigationMesh::RecurseTargetLinks(PathSearchScratch& path_search,
                                                  unsigned short waypoint,
                                                  std::vector<bool>& visited,
                                                  W8PatrolSearch& patrol) const
{
    path_search.graph.resize(m_waypoints.size());
    path_search.route.resize(m_waypoints.size() + 1);
    unsigned short edge;
    unsigned short destination;
    unsigned short candidates[20];
    std::uint32_t keys[20];
    float distances[20];
    unsigned int count = 0;
    unsigned int index;
    unsigned int link_flags;
    float distance;
    const W8PathEdge* link;

    edge = m_waypoints[waypoint].first_edge;
    if (edge != 0) {
        unsigned short* slot = candidates;
        do {
            link = m_pEdges.data() + edge;
            link_flags = link->flags;
            if ((link_flags & W8_PATH_EDGE_TELEPORTAL) == 0) {
                destination = link->destination;
                if ((((m_waypoints[destination].flags & W8_WAYPOINT_DISABLED) == 0) &&
                     (((link_flags & W8_PATH_EDGE_DISABLED) == 0) ||
                      (((link_flags & W8_NAV_THROUGH_DOORS) != 0 &&
                        ((path_search.filter & W8_NAV_THROUGH_DOORS) != 0))))) &&
                    ((path_search.filter == 0) ||
                     ((((link_flags & W8_NAV_GROUP_MASK) == W8_NAV_GROUP_MASK ||
                        ((link_flags & path_search.filter & W8_NAV_GROUP_MASK) != 0)) &&
                       (((link_flags & W8_NAV_MOVEMENT_MASK) == W8_NAV_MOVEMENT_MASK ||
                         ((link_flags & path_search.filter & W8_NAV_MOVEMENT_MASK) != 0)))) &&
                      (((link_flags & W8_NAV_SIZE_FILTER_MASK) == W8_NAV_SIZE_FILTER_MASK ||
                        ((link_flags & path_search.filter & W8_NAV_SIZE_FILTER_MASK) != 0))))) &&
                    (destination != 0)) {
                    if (!TestBit(visited, destination)) {
                        path_search.graph[destination].path_cost =
                            link->distance + path_search.graph[waypoint].path_cost;
                        path_search.graph[destination].search_parent = waypoint;
                        glm::vec3 to_target = m_waypoints[destination].position - patrol.start;
                        distance = glm::length(to_target);
                        distances[count] = distance;
                        if (patrol.cost < distance) {
                            patrol.cost = distance;
                            path_search.best_key = destination;
                        }
                        glm::vec3 link_direction =
                            m_waypoints[destination].position - m_waypoints[waypoint].position;
                        link_direction = normalizedOrZero(link_direction);
                        *slot = edge;
                        keys[count] = static_cast<std::uint32_t>(
                            g_path_link_cost_base -
                            glm::dot(link_direction, (to_target / distance)) * 1e+03f);
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
    SetBit(visited, waypoint);
    if (count != 0) {
        for (index = 0; index < count; ++index) {
            destination = m_pEdges[candidates[index]].destination;
            if (patrol.distance < distances[index]) {
                return destination;
            }
            destination = RecurseTargetLinks(path_search, destination, visited, patrol);
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
   attachment path. The fallback nodes tracked in patrol.node and
   path_search.best_key supply the endpoint when no candidate qualifies.
   `velocity` is passed by callers but never read. */
bool NavigationMesh::BuildPatrolPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                                     unsigned int flags, const glm::vec3* destination,
                                     float minimum, const glm::vec3* velocity, float maximum) const
{
    path_search.graph.assign(m_waypoints.size(), {});
    path_search.route.resize(m_waypoints.size() + 1);
    W8PatrolSearch patrol;
    if (minimum >= maximum) {
        return false;
    }
    patrol.max = maximum;
    patrol.min = minimum;
    patrol.cost = 0.0f;
    patrol.start = attachment->segment_start;
    patrol.destination = *destination;
    path_search.filter = flags;
    attachment->flags |= 0x200000;
    glm::vec3 delta = patrol.start - patrol.destination;
    if (maximum < glm::length(delta)) {
        return BuildAttachmentPath(path_search, attachment, flags);
    }
    float roll = CurrentGame().rng.below(900) + g_octree_cell_scale;
    path_search.start_waypoint = 0;
    patrol.distance = roll * maximum * 0.001f + maximum;
    unsigned short usStartNode = FindWaypoint(path_search, attachment->segment_start, true);
    if ((usStartNode == 0) && ((usStartNode = path_search.start_waypoint) == 0)) {
        return false;
    }
    std::vector<bool> visited(std::max<std::size_t>(100, m_waypoints.size()), false);
    std::vector<bool> marked(std::max<std::size_t>(100, m_waypoints.size()), false);
    path_search.best_key = 0;
    patrol.node = 0;
    unsigned int start = usStartNode;
    SetBit(marked, start);
    const W8PathSurface* surfaces = m_waypoints.data();
    path_search.probe_limit = surfaces[start].visit_stamp;
    path_search.graph[start].path_cost =
        glm::length((surfaces[start].position - attachment->segment_start));
    path_search.graph[start].search_parent = 0;
    unsigned int node = RecursePatrolLinks(path_search, usStartNode, visited, patrol);
    if (static_cast<short>(node) == 0) {
        if (patrol.node == 0) {
            node = static_cast<unsigned short>(path_search.best_key);
        } else {
            node = patrol.node & 0xffff;
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
        assert(m_waypoints.size() > count);
        previous = static_cast<unsigned short>(current);
        path_search.route[count] = previous;
        current = path_search.graph[current & 0xffff].search_parent;
        ++count;
    } while (previous != static_cast<unsigned short>(current));
    AppendScratchPath(path_search, attachment, count);
    FinalizeWaypointRoute(attachment, path_search.start_waypoint != 0, path_search.probe_position);
    return true;
}

/* Recursive depth-first patrol search from `waypoint`. Each pass collects the
   waypoint's admissible links (edge/surface flag filters matching FindPath),
   prices them by accumulated link cost, sorts them by the surface key, then
   accepts the first candidate whose cost clears the randomized
   patrol_distance - or the 250000 sanity bound - while the last-measured
   candidate distance still exceeds patrol_min. Otherwise it recurses into
   each candidate in sorted order. The argmin-key candidate is parked in
   patrol.node and the best-cost alternate in path_search.best_key for the
   caller's fallback. The visited set is local to the search. */
unsigned short NavigationMesh::RecursePatrolLinks(PathSearchScratch& path_search,
                                                  unsigned short waypoint,
                                                  std::vector<bool>& visited,
                                                  W8PatrolSearch& patrol) const
{
    path_search.graph.resize(m_waypoints.size());
    path_search.route.resize(m_waypoints.size() + 1);
    unsigned short links[20];
    std::uint32_t keys[20];
    float costs[20];
    unsigned int count = 0;
    float distance;

    unsigned short link = m_waypoints[waypoint].first_edge;
    if (link != 0) {
        do {
            assert(link < m_pEdges.size());
            unsigned int edge_flags = m_pEdges[link].flags;
            unsigned short next = m_pEdges[link].destination;
            if ((((edge_flags & W8_PATH_EDGE_TELEPORTAL) == 0) && (next != waypoint)) &&
                ((m_waypoints[next].flags & W8_WAYPOINT_DISABLED) == 0) &&
                (((edge_flags & W8_PATH_EDGE_DISABLED) == 0) ||
                 (((edge_flags & W8_NAV_THROUGH_DOORS) != 0) &&
                  ((path_search.filter & W8_NAV_THROUGH_DOORS) != 0))) &&
                ((path_search.filter == 0) ||
                 ((((edge_flags & W8_NAV_GROUP_MASK) == W8_NAV_GROUP_MASK) ||
                   ((edge_flags & path_search.filter & W8_NAV_GROUP_MASK) != 0)) &&
                  (((edge_flags & W8_NAV_MOVEMENT_MASK) == W8_NAV_MOVEMENT_MASK) ||
                   ((edge_flags & path_search.filter & W8_NAV_MOVEMENT_MASK) != 0)) &&
                  (((edge_flags & W8_NAV_SIZE_FILTER_MASK) == W8_NAV_SIZE_FILTER_MASK) ||
                   ((edge_flags & path_search.filter & W8_NAV_SIZE_FILTER_MASK) != 0)))) &&
                (next != 0)) {
                if (!TestBit(visited, next)) {
                    assert(next < m_waypoints.size());
                    const W8PathSurface* next_surface = &m_waypoints[next];
                    distance = glm::length((next_surface->position - patrol.destination));
                    if (distance < patrol.max) {
                        path_search.graph[next].path_cost =
                            path_search.graph[waypoint].path_cost + m_pEdges[link].distance;
                        costs[count] = path_search.graph[next].path_cost;
                        path_search.graph[next].search_parent = waypoint;
                        links[count] = link;
                        unsigned int key = next_surface->visit_stamp;
                        keys[count] = key;
                        ++count;
                        if (key < path_search.probe_limit) {
                            path_search.probe_limit = key;
                            patrol.node = next;
                        } else {
                            if ((key != path_search.probe_limit) ||
                                (path_search.graph[next].path_cost <= patrol.cost)) {
                                if ((patrol.node == 0) &&
                                    (patrol.cost < path_search.graph[next].path_cost)) {
                                    patrol.cost = path_search.graph[next].path_cost;
                                    path_search.best_key = next;
                                }
                            } else {
                                patrol.cost = path_search.graph[next].path_cost;
                                patrol.node = next;
                                path_search.best_key = next;
                            }
                        }
                        if (count == std::size(links))
                            throw std::runtime_error("waypoint has too many patrol links");
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
    SetBit(visited, waypoint);
    for (unsigned int index = 0; index < count; ++index) {
        unsigned short next = m_pEdges[links[index]].destination;
        assert(next < m_waypoints.size());
        if (((patrol.distance < costs[index]) || (g_patrol_cost_limit < costs[index])) &&
            (patrol.min < distance)) {
            return next;
        }
        unsigned short found = RecursePatrolLinks(path_search, next, visited, patrol);
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
void NavigationMesh::ReduceWaypointCosts(PathSearchScratch& path_search, unsigned int waypoint,
                                         float amount, std::vector<bool>& marked) const
{
    path_search.graph.resize(m_waypoints.size());
    path_search.route.resize(m_waypoints.size() + 1);
    const W8PathSurface* surface = &m_waypoints[waypoint];
    path_search.graph[waypoint].path_cost -= amount;
    if (path_search.graph[waypoint].path_cost >= 0.0f) {
        path_search.graph[waypoint].estimated_total_cost -= amount;

        unsigned short edge_index = surface->first_edge;
        while (edge_index != 0) {
            const W8PathEdge* edge = &m_pEdges[edge_index];
            unsigned short child = edge->destination;
            if (child != 0 && TestBit(marked, child) &&
                path_search.graph[child].search_parent == waypoint) {
                ReduceWaypointCosts(path_search, child, amount, marked);
            }
            edge_index = edge->next;
        }
    }
}

/* Advance the attachment's probe cursor and accept its next stored waypoint
   only when the live path grid permits the span from the supplied position.
   W8_NAV_ATTACHMENT_START_WAYPOINT makes the first probe repeat the current path
   index. */
bool NavigationMesh::AdvanceAttachmentWaypoint(const glm::vec3* source, NavigatorRoute* attachment)
{
    std::size_t cursor = attachment->path_cursor;
    if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        --cursor;
    }
    const auto position_cursor = cursor + 1;

    if (position_cursor < attachment->lastWaypoint()) {
        glm::vec3 destination = attachment->path[position_cursor].position;
        if (tracePathSpan(*(source), destination, false, false).reachable) {
            return true;
        }
    }
    return false;
}

/* Match a tag against the path probes collected for the current search. A
   tag-only query succeeds immediately. A spatial query must lie strictly
   farther from the probe center than the search origin was, while remaining
   strictly inside its collision radius plus the caller's own radius. */
bool NavigationMesh::MatchesPathProbe(PathSearchScratch& path_search, unsigned int tag,
                                      const float* radius, const glm::vec3* position) const
{
    for (unsigned int index = 0; index < path_search.probe_count; ++index) {
        W8PathProbeVolume* probe = &path_search.probes[index];
        if (probe->tag == tag) {
            if (radius == 0) {
                return true;
            }

            glm::vec3 delta = probe->center - *position;
            float distance = glm::length(delta);
            if (distance < probe->outer_radius + *radius && probe->inner_radius < distance) {
                return true;
            }
        }
    }
    return false;
}

/* Append a cleared planner node; zero remains the sentinel. */
unsigned short NavigationMesh::AllocateSearchNode(std::vector<W8PathSearchNode>& nodes) const
{
    const auto index = nodes.size();
    nodes.emplace_back();
    return static_cast<unsigned short>(index);
}

/* Walk grid-sized steps from a position toward a search node. Every crossed
   cell must resolve through the visited-node index to a live, unblocked node
   whose recorded clearance exceeds the caller's limit. */
bool NavigationMesh::CanReachSearchNode(PathSearchScratch& path_search, const glm::vec3* position,
                                        unsigned short target_node, float clearance,
                                        std::vector<W8PathSearchNode>& nodes) const
{
    W8PathSearchNode* target = &nodes[target_node];
    glm::vec3 delta = target->position - *position;
    float scale;
    if (static_cast<float>(fabs(delta.x)) > static_cast<float>(fabs(delta.z))) {
        scale = static_cast<float>(fabs(grid_scale / delta.x));
    } else {
        scale = static_cast<float>(fabs(grid_scale / delta.z));
    }

    glm::vec3 probe = *position;
    float step_x = delta.x * scale;
    float step_z = delta.z * scale;
    std::unordered_map<unsigned int, int>* visited = &path_search.visited_cells;
    bool blocked = false;

    while (1) {
        unsigned int key = pathCell(probe).key;
        unsigned int node_index = LookupIndexValue(*visited, key);

        if (static_cast<unsigned short>(node_index) == target_node || blocked) {
            return !blocked;
        }
        if (static_cast<unsigned short>(node_index) == 0 ||
            (nodes[node_index & 0xffff].flags & W8_PATH_SEARCH_BLOCKED) != 0 ||
            nodes[node_index & 0xffff].clearance <= clearance) {
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
void NavigationMesh::AdjustFinalPathEndpoint(NavigatorMovement* movement, float radius,
                                             float separation) const
{
    int target_location = movement->target_location_id;
    if (target_location < 0) return;

    /* Retail read an unset target radius and position when the monster info
       or model is absent; without a target there is no shell to pull onto. */
    Navigator* target = partyNavigator();
    if (target_location > 0) {
        Monster* monster = findMonster(target_location);
        target = monster ? &monster->nav : nullptr;
    }
    if (!target) return;
    const float target_radius = target->movement.alternate_radius;
    const glm::vec3 target_position = target->GetPosition();

    NavigatorRoute* attachment = movement->attachment.get();
    glm::vec3* endpoint = &attachment->path[attachment->lastWaypoint()].position;
    glm::vec3 direction = *endpoint - target_position;
    float excess = glm::length(direction) - (target_radius + radius);
    if (separation < excess && excess - separation < grid_scale * 0.5f) {
        direction = glm::dot(direction, direction) == 0
                        ? direction
                        : glm::normalize(direction) *
                              static_cast<float>((target_radius + radius + separation) *
                                                 g_path_endpoint_scale);

        glm::vec3 adjusted;
        adjusted = target_position + direction;
        *endpoint = adjusted;
        attachment->path_destination = *endpoint;

        if ((attachment->flags & 0x08000000) == 0) {
            attachment->RecordPosition(&adjusted);
            owner_octree->RegisterNavigatorCell(movement->location_id, adjusted);
        }
    }
}

/* Select one conditional frame for a GD prop's path cells. Entries belonging
   to every other frame first lose both prop-state bits. Entries belonging to
   the selected frame then gain the caller's state bits. The hash table may
   contain several values for one key, so the low word from the serialized
   conditional value is the identity used to find the exact pairing. */
void NavigationMesh::UpdateConditionalPathFlags(unsigned int path_handle, unsigned short frame,
                                                unsigned int flags)
{
    auto& index = cells;
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
int NavigationMesh::ProcessSearchNodeProps(unsigned short node_index, bool first_only,
                                           std::vector<W8PathSearchNode>& nodes) const
{
    W8PathSearchNode* node = &nodes[node_index];
    float half_cell = grid_scale * 0.5f;

    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 lower_extent;
    glm::vec3 upper_extent;
    float vertical_extent = grid_scale + grid_scale;
    lower_extent = glm::vec3(half_cell, 0.0f, half_cell);
    upper_extent = glm::vec3(half_cell, vertical_extent, half_cell);
    lower = node->position - lower_extent;
    upper = node->position + upper_extent;

    const auto candidates = owner_octree->queryObjects({lower, upper}, W8_OCTREE_KIND_PROP, -1);
    const auto count = std::ssize(candidates);
    for (unsigned int index = 0; index < count; ++index) {
        Prop* prop = owner_octree->owner_world->collidable_props[candidates[index]];
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
unsigned int NavigationMesh::CollectPathProbes(PathSearchScratch& path_search,
                                               NavigatorMovement* movement, float radius) const
{
    path_search.probe_count = 0;

    float extent = owner_octree->registry.maximum_navigation_radius + radius;
    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 half_extent;
    half_extent = glm::vec3(extent, extent, extent);
    lower = movement->position - half_extent;
    upper = movement->position + half_extent;

    path_search.candidates = owner_octree->locationsInBox({lower, upper}, movement->location_id);

    if (const Navigator* party = partyNavigator()) {
        const glm::vec3 player_position = party->GetPosition();
        const float distance = glm::length(player_position - movement->position);
        const float player_radius = party->movement.alternate_radius;
        const float overlap_radius = std::min(player_radius, radius);
        if (distance < (radius - overlap_radius * 0.5f) + player_radius) {
            W8PathProbeVolume* probe = &path_search.probes[path_search.probe_count];
            probe->tag = 0;
            probe->outer_radius = player_radius;
            probe->center = player_position;
            ++path_search.probe_count;
        }
    }

    for (unsigned int index = 0;
         index < path_search.candidates.size() && path_search.probe_count < 5; ++index) {
        int location_id = path_search.candidates[index];
        Monster* monster = findMonster(location_id);
        if (monster && monster->nav.active) {
            const float distance = glm::length(monster->nav.GetPosition() - movement->position);
            const float monster_radius = monster->nav.movement.alternate_radius;
            const float overlap_radius = std::min(monster_radius, radius);
            if (distance < (radius - overlap_radius * 0.5f) + monster_radius) {
                W8PathProbeVolume* probe = &path_search.probes[path_search.probe_count];
                probe->tag = location_id;
                probe->outer_radius = monster_radius;
                probe->inner_radius = distance;
                probe->center = monster->nav.GetPosition();
                ++path_search.probe_count;
            }
        }
    }
    return path_search.probe_count;
}

static glm::vec3 GetSearchTraceOffset(float bearing, const W8MovementTrace& trace)
{
    float target_yaw = NormalizeAngle(trace.target_yaw);
    glm::mat3 rotation;
    rotation = glm::mat3(1);
    float angle = bearing - target_yaw;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 1, 0)));
    return glm::vec3{rotation * trace.offset};
}

bool NavigationMesh::SearchNodeSeesTarget(unsigned short node, NavigatorMovement* movement,
                                          std::vector<W8PathSearchNode>& nodes,
                                          const W8MovementTrace& trace) const
{
    glm::vec3 trace_target = movement->target_position;
    trace_target.y += trace.target_height_offset;
    float bearing = NormalizeAngle(GetHeadingAngle(&nodes[node].position, &trace_target));
    glm::vec3 transformed = GetSearchTraceOffset(bearing, trace);
    glm::vec3 trace_source = nodes[node].position + transformed;
    return !owner_octree->traceSight(trace_source, trace_target);
}

/* Build a bounded grid route from the navigator's current position to its
   active attachment target. The open-chain index owns one search node per
   cell, the fixed-capacity minimum heap chooses the next node to expand, and
   the selected parent chain is collapsed into the attachment's route array. */
unsigned short NavigationMesh::PlanMovement(PathSearchScratch& path_search,
                                            NavigatorMovement* movement, float radius,
                                            float separation, bool explicit_target,
                                            const W8MovementTrace* visibility) const
{
    W8PathHeapHandle path_heap;
    float diagonal_step = grid_scale * g_path_cardinal_scale;
    unsigned short result = 0;
    bool stop_search = false;

    const W8MovementTrace trace = visibility ? *visibility : W8MovementTrace{};

    std::vector<W8PathSearchNode> nodes(1);
    path_search.best_key = 0;
    path_search.probe_limit = static_cast<unsigned int>(-1);
    path_heap.open = {};

    NavigatorRoute* attachment = movement->attachment.get();
    attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
    std::unordered_map<unsigned int, int>* visited = &path_search.visited_cells;
    visited->clear();
    attachment->flags &= ~W8_NAV_ATTACHMENT_POSITION_RECORDED;

    bool allow_dynamic = (movement->navigation_filter & W8_NAV_THROUGH_DOORS) != 0;
    glm::vec3 start = movement->position;
    glm::vec3 target;
    if (explicit_target) {
        target = movement->target_position;
    } else if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        target = attachment->start_waypoint;
    } else if (attachment->path_cursor < attachment->lastWaypoint()) {
        target = attachment->path[attachment->path_cursor].position;
    } else {
        target = attachment->path_destination;
    }

    glm::vec3 target_delta = target - start;
    float target_distance = glm::length(target_delta);
    float remaining_callback = movement->callback_threshold - movement->callback_progress;

    if ((attachment->flags & W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION) == 0) {
        float search_extent = target_distance;
        if (search_extent < remaining_callback) {
            search_extent = remaining_callback;
        }
        search_extent += owner_octree->registry.maximum_navigation_radius + radius;
        glm::vec3 lower;
        glm::vec3 upper;
        glm::vec3 half_extent;
        half_extent = glm::vec3(search_extent, search_extent, search_extent);
        lower = start - half_extent;
        upper = start + half_extent;
        CollectPathProbes(path_search, movement, radius);
        path_search.candidates =
            owner_octree->locationsInBox({lower, upper}, movement->location_id);

    } else {
        path_search.probe_count = 0;
        if (movement->target_location_id < 1) {
            path_search.candidates.clear();
        } else {
            path_search.candidates = {static_cast<std::uint32_t>(movement->target_location_id)};
        }
    }

    glm::ivec2 root_cell;
    const auto converted_cell = pathCell(start);
    root_cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int root_key = converted_cell.key;
    unsigned short root_index = AllocateSearchNode(nodes);
    W8PathSearchNode* root = &nodes[root_index];
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
    UpdateSearchNodeScore(root_index, &target, root_clearance, radius, nodes, explicit_target);
    unsigned int root_score = static_cast<unsigned int>(root->score);
    if (root_score < path_search.probe_limit) {
        path_search.probe_limit = root_score;
        path_search.best_key = root_index;
    }
    path_search.best_key = 0;

    W8PathOpenSet* heap = &path_heap.open;
    W8PathHeapEntry entry;
    entry.node = root_index;
    entry.priority = static_cast<unsigned int>(root->score);
    heap->push(entry);
    path_heap.root_node = heap->top().node;

    unsigned int best_node = path_heap.root_node;
    while (best_node != 0 && !stop_search && (nodes.size() - 1) < g_path_reserve) {
        W8PathSearchNode* current = &nodes[best_node];
        unsigned short current_x = current->cell_x;
        unsigned short current_z = current->cell_z;
        unsigned int current_height = current->path_height;

        for (int direction = 0; direction < 8; ++direction) {
            current = &nodes[best_node];
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
                W8PathSearchNode* existing = &nodes[existing_index & 0xffff];
                if ((existing->flags & W8_PATH_SEARCH_BLOCKED) != 0) {
                    continue;
                }
                int height_delta = static_cast<int>(current->path_height) -
                                   static_cast<int>(existing->path_height);
                if (height_delta < 0) {
                    height_delta = -height_delta;
                }
                base_score += height_delta * span * 1.5f;
                if (existing->base_score <= base_score) {
                    continue;
                }
                existing->base_score = base_score;
                existing->path_cost = path_cost;
                existing->parent_node = static_cast<unsigned short>(best_node);
                UpdateSearchNodeScore(existing_index, &target, existing->clearance, radius, nodes,
                                      explicit_target);
                if ((existing->flags & W8_PATH_SEARCH_CLOSED) != 0) {
                    existing->flags &= ~W8_PATH_SEARCH_CLOSED;
                    entry.node = existing->node_index;
                    entry.priority = static_cast<unsigned int>(existing->score);
                    heap->push(entry);
                    path_heap.root_node = heap->top().node;
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

            unsigned short node_index = AllocateSearchNode(nodes);
            int node_value = node_index;
            visited->emplace(key, node_value);

            W8PathSearchNode* node = &nodes[node_index];
            node->flags = dynamic ? W8_PATH_SEARCH_INACTIVE_CELL : 0;
            node->node_index = node_index;
            node->cell_x = static_cast<unsigned short>(neighbor_x);
            node->cell_z = static_cast<unsigned short>(neighbor_z);
            node->path_height = static_cast<unsigned short>(height);
            node->parent_node = static_cast<unsigned short>(best_node);
            node->base_score = vertical;
            node->path_cost = path_cost;
            node->clearance = clearance;
            node->position = glm::vec3((node->cell_x + 0.5f) * grid_scale + level_bounds.minimum.x,
                                       (node->path_height - 1) * span + level_bounds.minimum.y,
                                       (node->cell_z + 0.5f) * grid_scale + level_bounds.minimum.z);

            unsigned short collision = ResolveSearchNodeCollisions(
                path_search, movement, node_index, radius, separation, nodes, explicit_target);
            UpdateSearchNodeScore(node_index, &target, clearance, radius, nodes, explicit_target);
            if (collision == 1) {
                node->flags |= W8_PATH_SEARCH_BLOCKED;
                continue;
            }
            if (collision == 3 && (nodes[best_node].flags & W8_PATH_SEARCH_BLOCKED) == 0) {
                stop_search = true;
                result = 1;
                path_search.best_key = node_index;
                continue;
            }
            if (collision == 2) {
                node->flags |= W8_PATH_SEARCH_BLOCKED;
            }

            glm::vec3 node_delta = target - node->position;
            float distance = glm::length(node_delta);
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
                if (score < path_search.probe_limit) {
                    path_search.probe_limit = score;
                    path_search.best_key = node_index;
                }
            }
            entry.node = node->node_index;
            entry.priority = static_cast<unsigned int>(node->score);
            heap->push(entry);
            path_heap.root_node = heap->top().node;
        }

        path_heap.DeleteRoot(&nodes[best_node]);
        best_node = path_heap.root_node;
        assert(best_node < nodes.size());
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
    if (path_search.best_key != 0) {
        unsigned short walk = static_cast<unsigned short>(path_search.best_key);
        unsigned short walk_parent = nodes[walk].parent_node;
        while (walk_parent != 0) {
            if (!explicit_target && direct_visibility_node == 0) {
                if (!SearchNodeSeesTarget(walk, movement, nodes, trace)) {
                    nodes[walk].flags |= W8_PATH_SEARCH_EXPANDED;
                } else {
                    direct_visibility_node = walk;
                }
            }
            walk = walk_parent;
            walk_parent = nodes[walk].parent_node;
        }
        if (!explicit_target && direct_visibility_node == 0) {
            if (SearchNodeSeesTarget(walk, movement, nodes, trace)) {
                direct_path = true;
            } else {
                nodes[walk].flags |= W8_PATH_SEARCH_EXPANDED;
            }
        }
    }

    if ((path_search.best_key == 0 && result == 0) || direct_path) {
        attachment->RecordPosition(&movement->position);
        attachment->path[attachment->lastWaypoint()].position = movement->position;
        attachment->path_destination = attachment->path[attachment->lastWaypoint()].position;
        owner_octree->RegisterNavigatorCell(movement->location_id, movement->position);
        if (Navigator* party = partyNavigator()) party->radius = party->movement.alternate_radius;
        attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
        return 0;
    }

    if ((nodes.size() - 1) >= g_path_reserve || best_node == 0) {
        result = 3;
    }

    unsigned short selected = static_cast<unsigned short>(path_search.best_key);
    nodes[selected].flags |= W8_PATH_SEARCH_ROUTE;
    unsigned short previous = selected;
    unsigned short parent = nodes[selected].parent_node;
    while (parent != 0) {
        W8PathSearchNode* parent_node = &nodes[parent];
        parent_node->flags |= W8_PATH_SEARCH_ROUTE;
        unsigned short next_parent = parent_node->parent_node;
        parent_node->parent_node = previous;
        previous = parent;
        parent = next_parent;
    }
    nodes[selected].parent_node = 0;

    unsigned short route_node = previous;
    // Direct-search prop annotations and waypoint appends can address the same slot.
    // Keep the planning writes in order without making spare slots part of the route.
    std::vector<unsigned short> values;
    unsigned int prop_count = 0;
    const auto annotate = [&](std::size_t index, unsigned short value) {
        if (values.size() <= index) values.resize(index + 1);
        values[index] = value;
    };
    const auto append = [&](const glm::vec3* point) {
        annotate(attachment->lastWaypoint(), 0);
        attachment->AppendPathPosition(point, 0);
    };
    attachment->path.resize(2);
    attachment->path_cursor = 1;
    unsigned short anchor_node = previous;
    unsigned short route_parent = nodes[route_node].parent_node;
    while (route_parent != 0) {
        W8PathSearchNode* node = &nodes[route_parent];
        if ((node->flags & W8_PATH_SEARCH_INACTIVE_CELL) != 0) {
            if ((attachment->flags & 0x08000000) == 0) {
                if (const auto prop = ProcessSearchNodeProps(route_parent, false, nodes))
                    annotate(prop_count++, static_cast<unsigned short>(prop));
            } else {
                ProcessSearchNodeProps(route_parent, true, nodes);
            }
        }

        if (!CanReachSearchNode(path_search, &nodes[anchor_node].position, route_parent, radius,
                                nodes)) {
            append(&nodes[route_node].position);
            anchor_node = route_node;
        }

        unsigned short next = node->parent_node;
        if (visibility) {
            if ((node->flags & W8_PATH_SEARCH_EXPANDED) != 0 ||
                !TestSearchPositionVisibility(&node->position, movement, trace)) {
                node->flags |= 8;
            } else {
                next = 0;
                result = 1;
                path_search.best_key = route_parent;
            }
        }

        route_node = route_parent;
        route_parent = next;
        if (direct_visibility_node != 0 && route_node == direct_visibility_node) {
            path_search.best_key = route_node;
            break;
        }
        /* 0x00464714: the route ends at the last node before the one that
           exceeds this move's remaining callback distance. */
        if (nodes[route_parent].path_cost > remaining_callback) {
            path_search.best_key = route_node;
            break;
        }
    }

    // 0x0046473A: the route ends at the last node walked, not at the last anchor.
    append(&nodes[route_node].position);
    annotate(prop_count, 0);
    for (std::size_t index = 0; index < std::min(values.size(), attachment->path.size()); ++index)
        attachment->path[index].value = values[index];

    if (attachment->lastWaypoint() > 1) {
        attachment->path.pop_back();
        attachment->path_destination = attachment->path[attachment->lastWaypoint()].position;
    }
    if ((attachment->flags & 0x08000000) == 0) {
        attachment->RecordPosition(&nodes[path_search.best_key].position);
        owner_octree->RegisterNavigatorCell(movement->location_id,
                                            nodes[path_search.best_key].position);
    }
    if (movement->target_location_id >= 0 && !explicit_target) {
        AdjustFinalPathEndpoint(movement, radius, separation);
    }
    attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
    attachment->flags |= result;
    if (Navigator* party = partyNavigator()) party->radius = party->movement.alternate_radius;
    return result;
}

/* Plan away from an explicit position, without contact-shell adjustment or
   changing how subsequent searches select and score their targets. */
unsigned short NavigationMesh::PlanMovementToPosition(PathSearchScratch& path_search,
                                                      NavigatorMovement* movement,
                                                      const glm::vec3* target, float radius,
                                                      float separation) const
{
    movement->target_position = *target;
    return PlanMovement(path_search, movement, radius, separation, true);
}

/* Refresh one planner node's distance and accumulated score. Explicit-target
   mode scores from the shared ceiling; ordinary mode starts from the node's
   base score and adds a range penalty only when the adjusted gap is positive.
   Flag 0x2000 applies the final fixed penalty in either mode. */
float NavigationMesh::UpdateSearchNodeScore(unsigned short node_index, const glm::vec3* position,
                                            float minimum, float maximum,
                                            std::vector<W8PathSearchNode>& nodes,
                                            bool explicit_target) const
{
    W8PathSearchNode* node = &nodes[node_index];
    float distance = glm::length((*position - node->position));
    node->distance = distance;

    if (!explicit_target) {
        node->score = distance * 1.5f + node->base_score;
    } else {
        node->score = 1e+06f - distance;
    }

    float gap = maximum - minimum;
    if (!explicit_target) {
        float adjusted_gap = gap;
        if (distance <= gap) {
            adjusted_gap = (gap - distance) * 0.9f;
        }
        if (adjusted_gap > 0.0f) {
            node->score += gap * kPathGapPenaltyScale;
        }
    } else if (gap > 0.0f) {
        node->score += 1e+06f;
    }

    if ((node->flags & W8_PATH_SEARCH_PROBE_OVERLAP) != 0) {
        node->score += 1e+06f;
    }
    return node->score;
}

/* Resolve dynamic navigator overlap for one candidate search node.

   The player is tag zero; a target navigator receives the caller's separation
   allowance, while every other live monster uses only the two radii. Probe
   volumes can mark the node as hard-blocked before the live object lookup.
   Shallow overlaps move the node outward and set flag 0x200; deeper or
   directionally conflicting overlaps return the retail collision state. */
unsigned short NavigationMesh::ResolveSearchNodeCollisions(PathSearchScratch& path_search,
                                                           NavigatorMovement* movement,
                                                           unsigned short node_index, float radius,
                                                           float separation,
                                                           std::vector<W8PathSearchNode>& nodes,
                                                           bool explicit_target) const
{
    unsigned short result = 0;
    if (explicit_target) {
        separation = 0.0f;
    }

    W8PathSearchNode* node = &nodes[node_index];
    glm::vec3 blocking_direction;
    if (const Navigator* party = partyNavigator()) {
        const glm::vec3 player_delta = party->GetPosition() - node->position;
        float threshold = party->movement.alternate_radius + radius;
        if (movement->target_location_id == 0) {
            threshold += separation;
        }
        if (glm::length(player_delta) < threshold) {
            if (movement->target_location_id != 0 || explicit_target) {
                return 1;
            }
            blocking_direction = normalizedOrZero(player_delta);
            result = 3;
        }
    }

    for (int location_id : path_search.candidates) {
        W8MonsterInfo* info = (owner_octree->owner_world
                                   ? owner_octree->owner_world->population->findMonster(location_id)
                                   : nullptr);
        if (info == 0) {
            continue;
        }

        unsigned int probe_index;
        for (probe_index = 0; probe_index < path_search.probe_count; ++probe_index) {
            W8PathProbeVolume* probe = &path_search.probes[probe_index];
            if (probe->tag == static_cast<unsigned int>(location_id)) {
                float probe_distance = glm::length((probe->center - node->position));
                if (probe_distance < radius + probe->outer_radius &&
                    probe->inner_radius < probe_distance) {
                    node->flags |= W8_PATH_SEARCH_PROBE_OVERLAP;
                    break;
                }
            }
        }
        if (probe_index < path_search.probe_count) {
            continue;
        }
        if (!info->entity || !info->entity->nav.active) {
            continue;
        }

        Monster& monster = *info->entity;
        glm::vec3 monster_position = monster.nav.GetPosition();
        glm::vec3 delta = node->position - monster_position;
        float distance = glm::length(delta);
        float threshold = monster.nav.movement.alternate_radius + radius;
        if (location_id == movement->target_location_id && !explicit_target) {
            threshold += separation;
        }

        if (distance < threshold) {
            if (location_id == movement->target_location_id && !explicit_target) {
                blocking_direction = glm::vec3(delta.x, delta.y, delta.z);
                blocking_direction = normalizedOrZero(blocking_direction);
                result = 3;
                continue;
            }

            bool adjust = false;
            if ((node->flags & W8_PATH_SEARCH_DISPLACED) == 0 &&
                threshold <= distance + kPathOverlapTolerance) {
                if (result == 3) {
                    glm::vec3 direction = delta;
                    direction = normalizedOrZero(direction);
                    float dot = glm::dot(direction, blocking_direction);
                    if (dot <= kPathBlockingAlignmentLowerBound ||
                        kPathBlockingAlignmentUpperBound <= dot) {
                        adjust = true;
                    }
                } else if (result != 1) {
                    adjust = true;
                }
            }

            if (adjust) {
                delta = glm::dot(delta, delta) == 0
                            ? delta
                            : glm::normalize(delta) * static_cast<float>(threshold - distance);
                node->position += delta;
                node->flags |= W8_PATH_SEARCH_DISPLACED;
                if (result != 1) {
                    continue;
                }
            }

            return 1;
        }
    }
    return result;
}

/* Test whether a candidate search position has the configured range and line
   of sight to the movement target. The trace origin is an offset rotated from
   the candidate-to-target bearing into the configured target yaw; the trace
   endpoint is the movement target with its configured vertical adjustment. */
bool NavigationMesh::TestSearchPositionVisibility(const glm::vec3* position,
                                                  NavigatorMovement* movement,
                                                  const W8MovementTrace& trace) const
{
    Monster* monster = findMonster(static_cast<unsigned int>(movement->location_id));
    float distance;
    if (trace.target_location == -1) {
        distance = monster->GetPointDistanceToPlayer(*position);
    } else {
        Monster* target = findMonster(trace.target_location);
        distance = monster->GetPointDistanceToMonster(target, *position);
    }
    if (trace.maximum_distance < distance) {
        return false;
    }

    glm::vec3 movement_target = movement->target_position;
    float bearing = NormalizeAngle(GetHeadingAngle(position, &movement_target));
    glm::vec3 transformed = GetSearchTraceOffset(bearing, trace);

    glm::vec3 trace_source = *position + transformed;
    glm::vec3 trace_target = movement->target_position;
    trace_target.y += trace.target_height_offset;

    unsigned char range_mode = 0;
    if (CalcRangeDistance(W8_RANGE_TOUCH) < distance && trace.mode == 1) {
        range_mode = 1;
    }
    const auto hit = owner_octree->traceSight(
        trace_source, trace_target,
        {.locations =
             SightLocations{movement->location_id, trace.target_location, range_mode != 0}});
    if (!hit ||
        (hit->blocker == SightBlocker::Location && TraceModeRejectsNoHit(trace.mode) != 0)) {
        return true;
    }
    return false;
}

/* Configure and run one movement search. An attachment already in path mode
   first gets a direct current-to-target segment; a nearby visible target can
   collapse that segment to the current position and finish without planning.
   The optional probe runs the same core planner under its two temporary flags
   before the ordinary authoritative call. */
unsigned short NavigationMesh::ConfigureMovementSearch(PathSearchScratch& path_search,
                                                       NavigatorMovement* movement, float radius,
                                                       float separation,
                                                       const W8MovementTrace& trace,
                                                       unsigned char* probe_result) const
{
    movement->target_position =
        owner_octree->settleToGround(movement->target_position, true, 500).position;

    unsigned short result = 0;
    NavigatorRoute* attachment = movement->attachment.get();
    if ((attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) != 0) {
        glm::vec3 delta = movement->target_position - movement->position;
        // NOLINTNEXTLINE(misc-include-cleaner)
        float horizontal_clearance = glm::length(((glm::vec2(delta.x, delta.z)))) - radius;
        float target_radius;
        if (trace.target_location == -1) {
            target_radius = partyNavigator()->movement.alternate_radius;
        } else {
            Monster* target = findMonster(trace.target_location);
            target_radius = target->nav.movement.alternate_radius;
        }

        if (horizontal_clearance - target_radius <= separation &&
            TestSearchPositionVisibility(&movement->position, movement, trace)) {
            attachment->InitializeSegment(&movement->position, &movement->position);
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
            result = PlanMovement(path_search, movement, radius, separation, false, &trace);
            attachment->flags &= ~(W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION |
                                   W8_NAV_ATTACHMENT_SEARCH_RESULT_ONLY);
            *probe_result = result == 1;
            attachment->InitializeSegment(&movement->position, &movement->target_position);
        }
        result = PlanMovement(path_search, movement, radius, separation, false, &trace);
    }

    return result;
}

/* Resolve one packed path-cell entry from the open-chained index. Compatible
   height entries must be unblocked; dynamic entries additionally require the
   caller's permission and their own enabled bit. The selected packed value
   updates height, vertical offset, compass direction, and the dynamic byte. */
bool NavigationMesh::ResolvePathCell(unsigned int key, bool allow_dynamic, unsigned int* height,
                                     float* direction, float* vertical, bool* dynamic) const
{
    auto* index = &cells;
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
            *vertical += magnitude * span * 1.5f;
            *height = value & W8_PATH_CELL_HEIGHT_MASK;
            if ((value & W8_PATH_CELL_HAS_DIRECTIONS) == 0) {
                *direction = static_cast<float>(value >> 16 & 0xff);
            } else {
                *direction = 0.0f;
            }
            *direction = (*direction + 0.5f) * kWorldScale;
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
std::optional<glm::vec3> NavigationMesh::neighborSlideDirection(const glm::vec3& position,
                                                                const glm::vec3& delta) const
{
    const std::unordered_multimap<unsigned int, unsigned int>* index;
    int range;
    int height;
    glm::ivec2 cell;
    unsigned int key;
    unsigned int value;

    bool in_range;

    range = cell_count * 2;
    in_range = false;
    height = static_cast<int>((position.y - level_bounds.minimum.y) / span) + 1;
    const auto converted_cell = pathCell(position);
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    key = converted_cell.key;
    index = &cells;
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

/* One movement step for a navigator walking an attachment path. The 0x10000
   mode follows the recorded route directly; otherwise the steering context
   advances the position and the waypoint cursor, falls through to the
   edge-transition handler, and relinks to a linked navigator's route when
   the end is reached but still out of contact. Returns whether the step
   completed the path. */
unsigned int NavigationMesh::StepAlongPath(PathSearchScratch& path_search,
                                           NavigatorMovement* movement, float radius,
                                           float separation)
{
    if (auto* world = owner_octree->owner_world) world->navigation_changed = true;
    NavigatorRoute* attachment = movement->attachment.get();
    unsigned int flags = attachment->flags;
    bool arrived;
    if ((flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) != 0) {
        arrived = true;
        if ((flags & 0x8000000) == 0) {
            glm::vec3* position = &movement->position;
            glm::vec3 advanced = *position;
            bool on_path = attachment->AdvanceAlongPathPositions(
                navigationClocks(*this).motionDelta() * movement->movement_speed *
                    movement->movement_scale * g_rate * kWorldScale,
                &advanced);
            arrived = !on_path;
            if (arrived) {
                attachment->flags &= ~W8_NAV_ATTACHMENT_RESULT_MASK;
            }
            glm::vec3 delta = advanced - *position;
            if (((delta.x != 0.0f) || (delta.y != 0.0f)) || (delta.z != 0.0f)) {
                glm::vec3 target = delta + advanced;
                *position = advanced;
                attachment->path_length_origin = advanced;
                attachment->segment_start = advanced;
                movement->target_position = target;
                owner_octree->UpdateMonsterLocation(movement->location_id, advanced);
            }
        }
        return arrived;
    }
    arrived = false;
    auto* linked_route = findLinkedAttachment(movement, (flags & W8_NAV_HUGE) == 0);
    SteeringStep steering(*this);
    if ((attachment->path_cursor == 1) || (attachment->path[attachment->path_cursor].value == 0)) {
        bool ignore_linked_navigator =
            (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) != 0;
        steering.SteerFromPathStart(movement, ignore_linked_navigator);
        unsigned int destination_flag = attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT;
        glm::vec3* waypoint;
        if (destination_flag == 0) {
            if (attachment->path_cursor < attachment->lastWaypoint()) {
                waypoint = &attachment->path[attachment->path_cursor].position;
            } else {
                waypoint = &attachment->path_destination;
            }
        } else {
            waypoint = &attachment->start_waypoint;
        }
        glm::vec3 target = *waypoint;
        float distance = glm::length((target - movement->position));
        if ((destination_flag != 0) &&
            (distance < static_cast<float>(g_monster_poster_max_distance))) {
            attachment->flags &= ~W8_NAV_ATTACHMENT_START_WAYPOINT;
        }
        std::size_t cursor = attachment->path_cursor;
        if (((cursor == 1) && (1 < attachment->lastWaypoint())) &&
            (attachment->path[2].value != 0)) {
            if (attachment->checkPositionHopHeight(m_waypoints, movement->position)) {
                ActivateMovementTrigger(movement, true);
                ++attachment->path_cursor;
            }
        } else if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0) {
            if (cursor == attachment->lastWaypoint()) {
                bool in_range;
                if (movement->target_location_id < 0) {
                    float limit = kStartupNearLimit;
                    if ((attachment->flags & W8_NAV_ATTACHMENT_COLLISION_PREDICTED) != 0) {
                        limit = 1e+03f;
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
                    NavigatorRoute* linked_attachment = linked_route;
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
                         static_cast<float>(1.0))) {
                        arrived = true;
                    } else {
                        PrepareLinkedNavigator(path_search, movement);
                    }
                }
            } else if (distance < static_cast<float>(g_monster_poster_max_distance)) {
                attachment->path_cursor = cursor + 1;
            }
        }
        ActivateMovementTrigger(movement, false);
        owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
        return arrived;
    }
    bool ignore_linked_navigator =
        (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) != 0;
    bool stepped = steering.SteerAlongPath(movement, ignore_linked_navigator);
    float distance = 0.0f;
    if (!stepped) {
        glm::vec3 target;
        if ((attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0) {
            if (attachment->path_cursor < attachment->lastWaypoint()) {
                glm::vec3* waypoint = &attachment->path[attachment->path_cursor].position;
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
        if (attachment->path_cursor == attachment->lastWaypoint()) {
            if (-1 < movement->target_location_id) {
                if (distance < attachment->separation + separation) {
                    goto transitioned;
                }
                limit = static_cast<float>(g_monster_poster_max_distance);
            } else {
                limit = kStartupNearLimit;
                if ((attachment->flags & W8_NAV_ATTACHMENT_COLLISION_PREDICTED) != 0) {
                    limit = 1e+03f;
                }
            }
        } else {
            limit = static_cast<float>(g_monster_poster_max_distance);
        }
        if (limit <= distance) {
            owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
            return arrived;
        }
    }
transitioned:
    {
        unsigned char transition = HandlePathEdgeTransition(movement);
        if (transition == 1) {
            if (attachment->path_cursor != attachment->lastWaypoint()) {
                ActivateMovementTrigger(movement, true);
                m_waypoints[attachment->path[attachment->path_cursor].value].visit_stamp =
                    static_cast<unsigned int>(navigationClocks(*this).motionElapsed());
                ++attachment->path_cursor;
                owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
                return arrived;
            }
            NavigatorRoute* linked_attachment = linked_route;
            if ((linked_attachment != 0) &&
                (static_cast<float>(1.0) <=
                 sqrt((attachment->path_destination.x - linked_attachment->path_destination.x) *
                          (attachment->path_destination.x - linked_attachment->path_destination.x) +
                      (attachment->path_destination.y - linked_attachment->path_destination.y) *
                          (attachment->path_destination.y - linked_attachment->path_destination.y) +
                      (attachment->path_destination.z - linked_attachment->path_destination.z) *
                          (attachment->path_destination.z -
                           linked_attachment->path_destination.z)))) {
                PrepareLinkedNavigator(path_search, movement);
                owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
                return arrived;
            }
        } else if (transition != 0) {
            owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
            return arrived;
        }
    }
    arrived = true;
    owner_octree->UpdateMonsterLocation(movement->location_id, movement->position);
    return arrived;
}

/* Find the moving monster's linked route, if this movement mode allows it. */
NavigatorRoute* NavigationMesh::findLinkedAttachment(NavigatorMovement* movement,
                                                     bool enabled) const
{
    if (!enabled) return nullptr;
    Monster* monster = findMonster(static_cast<unsigned>(movement->location_id));
    if (!monster || !monster->nav.linked_navigator) return nullptr;
    return monster->nav.linked_navigator->movement.attachment.get();
}

/* Rebuild the attachment route against the linked navigator's recorded path.
   The linked attachment is resolved through the moving monster's link (or
   cleared when the 0x800000 flag or a missing link says there is none), the
   route is rebuilt from the navigator's current position to the linked
   path's next live waypoint, and the linked route's remaining positions are
   appended verbatim. Returns the path-build result. */
bool NavigationMesh::PrepareLinkedNavigator(PathSearchScratch& path_search,
                                            NavigatorMovement* movement) const
{
    NavigatorRoute* attachment = movement->attachment.get();
    auto* linked_attachment = findLinkedAttachment(
        movement, (attachment->flags & W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR) == 0);
    if (linked_attachment == 0) {
        return false;
    }
    std::size_t index = linked_attachment->path_cursor;
    if (index < linked_attachment->lastWaypoint()) {
        do {
            if (linked_attachment->path[index].value != 0) {
                break;
            }
            ++index;
        } while (index < linked_attachment->lastWaypoint());
    }
    attachment->InitializeSegment(&movement->position, &linked_attachment->path[index].position);
    bool built = BuildAttachmentPath(path_search, attachment, movement->navigation_filter);
    if (built) {
        ++index;
        attachment->follow_offset = static_cast<std::ptrdiff_t>(attachment->lastWaypoint()) -
                                    static_cast<std::ptrdiff_t>(linked_attachment->path_cursor);
        if (index <= linked_attachment->lastWaypoint()) {
            do {
                unsigned short surface = linked_attachment->path[index].value;
                glm::vec3* source = &linked_attachment->path[index].position;
                attachment->path.push_back({*source, surface});
                attachment->path_destination = *source;
                ++index;
            } while (index <= linked_attachment->lastWaypoint());
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
unsigned int NavigationMesh::StepMonsterAlongPath(NavigatorMovement* movement, float radius,
                                                  float separation)
{
    assert(owner_octree->owner_world);
    owner_octree->owner_world->navigation_changed = true;
    bool arrived = false;
    NavigatorRoute* attachment = movement->attachment.get();
    W8MonsterInfo* monster_info =
        owner_octree->owner_world->population->findMonster(movement->location_id);
    W8MonsterGroup* group =
        owner_octree->owner_world->population->findGroup(monster_info->monster_group_id);
    monster_info->entity->nav.group_linked = true;
    float remaining = g_rate * navigationClocks(*this).motionDelta();
    do {
        if (remaining <= 0.0f) {
            break;
        }
        float step;
        if (remaining <= 5.0f) {
            step = remaining;
        } else {
            step = 5.0f;
        }
        unsigned short previous = attachment->path_cursor;
        glm::vec3 direction;
        arrived = attachment->AdvancePositionWithDirection(
            &movement->position, step * movement->movement_scale * kWorldScale, &direction);
        unsigned short reached = attachment->path_cursor;
        if (previous < reached) {
            do {
                attachment->path_cursor = previous;
                m_waypoints[attachment->path[attachment->path_cursor].value].visit_stamp =
                    static_cast<unsigned int>(navigationClocks(*this).motionElapsed());
                ++previous;
            } while (previous < reached);
            attachment->path_cursor = reached;
        }
        float angle = NormalizeAngle(static_cast<float>(atan2(direction.x, direction.z)));
        movement->target_yaw = angle;
        movement->yaw = angle;
        if (group->ubDisposition == W8_DISPOSITION_HOSTILE) {
            CurrentGame().updateMonsterSight(monster_info, true, false);
            DoMonsterRTAI(monster_info, true);
            if (monster_info->pCombat) {
                break;
            }
        }
        remaining -= step;
    } while (!arrived);
    return arrived;
}

/* Containers own the graph and search tables; retire the global service slot. */
NavigationMesh::~NavigationMesh() = default;

/* Build the pathing service.

   Everything starts cleared except three hundred-bit sets, a reserve table
   sized from the shared bound, and one state object. The containing octree owns the service; construction does not change the
   active world. */
NavigationMesh::NavigationMesh(LevelSpatialIndex* owner) : owner_octree(owner)
{
    LoadPathParameters(settings);
    grid_scale = 0;
    span = 0;
    path_node_count = 0;
}

/* Take the octree's own minimum/maximum bounds pair and level name. The span is the vertical extent
   of that box scaled down to one height unit; the cell count is how many of those units fit in
   1.25 grid cells, plus one, and bounds the vertical band the height lookups accept. */
void NavigationMesh::ConfigureForLevel(int size, float grid_scale, const BoundingBox& bounds,
                                       std::string_view name)
{
    path_node_count = size;
    this->grid_scale = grid_scale;
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
unsigned int NavigationMesh::ClassifyWaypoint(const glm::vec3* position)
{
    unsigned int key = pathCell(*position).key;
    unsigned int result = 0;

    if (key != 0) {
        auto* index = &cells;
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

unsigned int NavigationMesh::FindPathCell(glm::vec3* position, glm::uvec2* cell, bool adjust) const
{
    int path_height = static_cast<int>((position->y - level_bounds.minimum.y) / span) + 1;
    glm::ivec2 source_cell;
    const auto converted_cell = pathCell(*position);
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
    auto slot = cells.equal_range(key).first;

    while (slot != cells.end()) {
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
        slot = NextIndexValue(cells, key, slot);
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
            slot = cells.equal_range(key).first;
            while (slot != cells.end()) {
                unsigned int value = slot->second;
                unsigned int height = value & W8_PATH_CELL_HEIGHT_MASK;
                int difference = path_height - height;

                if ((value & W8_PATH_CELL_INACTIVE) == 0 && -cell_count < difference &&
                    difference < cell_count) {
                    found = true;
                    float x =
                        (candidate_x + 0.5f) * grid_scale - (position->x - level_bounds.minimum.x);
                    float z =
                        (candidate_z + 0.5f) * grid_scale - (position->z - level_bounds.minimum.z);
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
                slot = NextIndexValue(cells, key, slot);
            }
        }
    }

    if (adjust && found) {
        (*position) = glm::vec3((selected_x + 0.5f) * grid_scale + level_bounds.minimum.x,
                                (selected_height - 1) * span + level_bounds.minimum.y,
                                (selected_z + 0.5f) * grid_scale + level_bounds.minimum.z);
    }
    if (cell != 0) {
        cell->x = selected_x;
        cell->y = selected_z;
    }
    return selected_key;
}

/* The two near-cell operations share the height window and optional
   snap. Clearance additionally requires an active cell and its packed flags. */
bool NavigationMesh::FindNearbyPathCell(glm::vec3* position, bool snap_to_cell, bool require_active,
                                        unsigned int* packed_out) const
{
    const int vertical_window = cell_count * 2;
    const unsigned int height =
        static_cast<unsigned int>(static_cast<int>((position->y - level_bounds.minimum.y) / span)) +
        1;
    glm::ivec2 cell;
    const auto converted_cell = pathCell(*position);
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    const unsigned int key = converted_cell.key;
    auto slot = cells.equal_range(key).first;

    while (slot != cells.end()) {
        const unsigned int packed = slot->second;
        const unsigned int matched_height = packed & W8_PATH_CELL_HEIGHT_MASK;
        const int delta = static_cast<int>(height - matched_height);
        if ((!require_active || (packed & W8_PATH_CELL_INACTIVE) == 0) &&
            -vertical_window < delta && delta < vertical_window) {
            if (snap_to_cell) {
                (*position) = glm::vec3((cell.x + 0.5f) * grid_scale + level_bounds.minimum.x,
                                        (matched_height - 1) * span + level_bounds.minimum.y,
                                        (cell.y + 0.5f) * grid_scale + level_bounds.minimum.z);
            }
            if (packed_out) *packed_out = packed;
            return true;
        }
        slot = NextIndexValue(cells, key, slot);
    }
    return false;
}

/* Test whether a position lies in the vertical neighborhood represented by its
   X/Z path-index cell, optionally snapping it onto that indexed cell.

   The accepted vertical window is twice the service's cell count in either
   direction. X and Z snap to the horizontal cell centers; Y snaps to the exact
   one-based height carried by the matching packed index value. */
bool NavigationMesh::SnapWaypointPosition(glm::vec3* position, bool snap_to_cell) const
{
    return FindNearbyPathCell(position, snap_to_cell, false, nullptr);
}

/* Test the first static path-index value in the position's vertical
   neighborhood. The packed direction byte becomes a world-space clearance;
   the special direction flag selects the global fallback instead. A successful
   lookup may also move the position onto the indexed cell before testing that
   clearance. */
bool NavigationMesh::TestPathCellClearance(glm::vec3* position, float clearance, bool snap_to_cell)
{
    unsigned int packed;
    if (!FindNearbyPathCell(position, snap_to_cell, true, &packed)) return false;

    float direction = 0.0f;
    if ((packed & W8_PATH_CELL_HAS_DIRECTIONS) == 0)
        direction = static_cast<float>((packed >> 16) & 0xff);
    return clearance < direction * kWorldScale + grid_scale * 0.5f;
}

/* Search the short arc between an attachment's two endpoint positions. The
   temporary index is rebuilt before the paired directed probes, so both walks
   share only the path position they discover. */
bool NavigationMesh::ProbeAttachmentPath(PathSearchScratch& path_search,
                                         NavigatorRoute* attachment) const
{
    float distance = glm::length((attachment->segment_start - attachment->path_destination));

    if (distance > g_path_segment_split_distance) {
        return false;
    }

    path_search.probe_bounded = false;
    std::unordered_map<unsigned int, int>* visited = &path_search.visited_cells;
    visited->clear();

    path_search.best_key = 0;
    path_search.probe_limit = 0;
    ProbeWaypointArc(path_search, &attachment->segment_start, &attachment->path_destination);
    path_search.probe_bounded = false;
    path_search.probe_limit = 0xffffffff;
    ProbeWaypointArc(path_search, &attachment->path_destination, &attachment->segment_start);

    if (path_search.best_key == 0) {
        return false;
    }
    attachment->start_waypoint = path_search.probe_position;
    attachment->flags |= W8_NAV_ATTACHMENT_START_WAYPOINT;
    return true;
}

/* Sweep probes around the arc defined by a pair of waypoint positions.

   The accumulator starts perpendicular to the pair's horizontal direction.
   Every iteration probes that offset, advances by one grid-scale tangent step,
   and renormalizes to the pair's original radius. The dot product identifies
   when the sweep has passed its forward threshold and the walk stops after it
   subsequently crosses behind the starting direction. */
void NavigationMesh::ProbeWaypointArc(PathSearchScratch& path_search, const glm::vec3* from,
                                      const glm::vec3* to) const
{
    glm::vec3 direction;
    glm::vec3 arc;
    float radius;
    bool passed_forward = false;
    unsigned int iteration;

    direction = *to - *from;
    radius = glm::length(direction);
    arc = glm::vec3(-direction.z, 0.0f, direction.x);

    for (iteration = 0; iteration < 50000; ++iteration) {
        glm::vec3 probe;
        glm::vec3 step;
        float dot;

        probe = *from + arc;
        ProbeWaypointSegment(path_search, from, &probe);

        step = glm::vec3(arc.z, arc.y, -arc.x);
        step = glm::dot(step, step) == 0 ? step
                                         : glm::normalize(step) * static_cast<float>(grid_scale);
        arc += step;

        arc = glm::dot(arc, arc) == 0 ? arc : glm::normalize(arc) * static_cast<float>(radius);

        dot = glm::dot(direction, arc);
        if (dot > 0.9f) {
            passed_forward = true;
        } else if (!passed_forward) {
            continue;
        }
        if (dot < 0.0f) {
            return;
        }
    }
}

/* Convert the signed steps on the walk's driving and secondary axes into the
   four horizontal direction codes consumed by the segment probe. */
void NavigationMesh::GetPathGridStepDirections(const W8PathGridWalk* walk, int* directions) const
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
void NavigationMesh::BuildPathGridWalk(const glm::vec2* from, const glm::vec2* to,
                                       const glm::vec2* origin, W8PathGridWalk* walk) const
{
    int cell_size = static_cast<int>(grid_scale);
    glm::ivec2 coordinate;
    glm::ivec2 destination;
    glm::ivec2 step;
    glm::ivec2 absolute_delta;
    glm::vec2 boundary_offset;
    glm::vec2 signed_delta;
    int major_axis = 0;
    int largest_delta = 0;
    int axis;

    for (axis = 0; axis < 2; ++axis) {
        coordinate[axis] = static_cast<int>((*from)[axis] - (*origin)[axis]);
        destination[axis] = static_cast<int>((*to)[axis] - (*origin)[axis]);

        int delta = destination[axis] - coordinate[axis];
        boundary_offset[axis] = coordinate[axis] % cell_size / grid_scale;
        signed_delta[axis] = delta;

        if (delta < 0) {
            step[axis] = -1;
            delta = -delta;
        } else {
            step[axis] = 1;
            boundary_offset[axis] = 1.0f - boundary_offset[axis];
        }
        if (largest_delta < delta) {
            largest_delta = delta;
            major_axis = axis;
        }
        absolute_delta[axis] = delta;
    }

    int minor_axis = (major_axis + 1) % 2;
    float ratio = signed_delta[minor_axis] / signed_delta[major_axis];
    int error_delta = static_cast<int>((ratio < 0.0f ? -ratio : ratio) * grid_scale);
    int error = static_cast<int>(cell_size * boundary_offset[minor_axis] -
                                 error_delta * boundary_offset[major_axis]);
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
    walk->cell = {destination.x / cell_size, destination.y / cell_size, 0};
    walk->step = {step.x, step.y, 0};
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
bool NavigationMesh::ProbeWaypointSegment(PathSearchScratch& path_search, const glm::vec3* from,
                                          const glm::vec3* to) const
{
    float distance = glm::length((*to - *from));

    if (grid_scale + grid_scale > distance) {
        return true;
    }

    glm::ivec2 cell;
    glm::vec2 walk_from;
    glm::vec2 walk_to;
    glm::vec2 origin;
    W8PathGridWalk walk;
    int directions[2];

    const auto converted_cell = pathCell(*from);
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    walk_from = glm::vec2((*from).x, (*from).z);
    walk_to = glm::vec2((*to).x, (*to).z);
    origin = glm::vec2(level_bounds.minimum.x, level_bounds.minimum.z);
    BuildPathGridWalk(&walk_from, &walk_to, &origin, &walk);
    GetPathGridStepDirections(&walk, directions);

    int error = walk.error0;
    bool bounded_probe = path_search.probe_bounded && path_search.probe_limit != 0;
    unsigned int height =
        static_cast<unsigned int>(static_cast<int>(((from->y - level_bounds.minimum.y) / span))) +
        1;
    bool blocked = false;
    int iteration = 0;

    while (iteration < walk.count && !blocked) {
        unsigned int cell_key = pathCellKey(cell.x, cell.y);
        std::unordered_map<unsigned int, int>* visited_index = &path_search.visited_cells;
        unsigned int visited = LookupIndexValue(*visited_index, cell_key);

        if (bounded_probe && (visited & 0xffff) != 0xffff) {
            bounded_probe = false;
        }

        unsigned int direction_mask = 0;
        unsigned int path_value = 0;
        bool found = false;

        if (iteration == 0 || (visited & 0xffff0000) != 0xffff0000 || bounded_probe) {
            auto* path_index = &cells;
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
            } else if (!bounded_probe &&
                       ((path_search.probe_limit == 0 && visited == 0) ||
                        (path_search.probe_limit != 0 && (visited & 0xffff) != 0 &&
                         (visited & 0xffff0000) == 0))) {
                glm::vec3 position;
                unsigned int initial_cost;

                position = glm::vec3((cell.x + 0.5f) * grid_scale + level_bounds.minimum.x,
                                     (height - 1) * span + level_bounds.minimum.y,
                                     (cell.y + 0.5f) * grid_scale + level_bounds.minimum.z);

                initial_cost = static_cast<unsigned int>(
                    static_cast<int>((glm::length((position - *to)) * 0.1)));

                if (path_search.probe_limit == 0) {
                    int stored_cost = initial_cost;
                    visited_index->emplace(cell_key, stored_cost);
                } else {
                    unsigned int step_cost = static_cast<unsigned int>(
                        static_cast<int>(initial_cost * g_path_initial_cost_slack));
                    unsigned int total_cost = (visited & 0xffff) + step_cost;

                    if (total_cost < path_search.probe_limit) {
                        path_search.probe_limit = total_cost;
                        path_search.best_key = cell_key;
                        path_search.probe_position = position;
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
            cell[walk.major_axis] += walk.step[walk.major_axis];
            error -= walk.error_delta0;
        } else {
            direction = directions[1];
            --iteration;
            cell[walk.minor_axis0] += walk.step[walk.minor_axis0];
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
unsigned int NavigationMesh::ComputeWaypointNeighborMask(const glm::ivec2* cell,
                                                         unsigned int path_value) const
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
            glm::ivec2 neighbor;
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

            unsigned int key = pathCellKey(neighbor.x, neighbor.y);
            auto* index = &cells;
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
PathSpanResult NavigationMesh::tracePathSpan(const glm::vec3& source, const glm::vec3& requested,
                                             bool adjust_destination, bool diagonal_steps) const
{
    glm::vec3 destination = requested;
    bool conditional = false;
    unsigned neighbor_mask = 0;
    bool blocked = false;

    if (!adjust_destination &&
        (source.x < level_bounds.minimum.x || source.y < level_bounds.minimum.y ||
         source.z < level_bounds.minimum.z || level_bounds.maximum.x < source.x ||
         level_bounds.maximum.y < source.y || level_bounds.maximum.z < source.z ||
         destination.x < level_bounds.minimum.x || destination.y < level_bounds.minimum.y ||
         destination.z < level_bounds.minimum.z || level_bounds.maximum.x < destination.x ||
         level_bounds.maximum.y < destination.y || level_bounds.maximum.z < destination.z)) {
        return {false, conditional, neighbor_mask, destination};
    }

    conditional = false;
    glm::ivec2 cell;
    const auto converted_cell = pathCell(source);
    cell = {converted_cell.coordinates.x, converted_cell.coordinates.y};
    unsigned int cell_key = converted_cell.key;
    unsigned int destination_key = pathCell(destination).key;
    neighbor_mask = 0;

    auto* path_index = &cells;

    if (cell_key == destination_key) {
        const auto findCellAtHeight = [&](const glm::vec3& position, unsigned int& result) {
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
        if (!findCellAtHeight(source, source_value))
            return {false, conditional, neighbor_mask, destination};
        neighbor_mask = ComputeWaypointNeighborMask(&cell, source_value);
        unsigned int destination_value;
        return {findCellAtHeight(destination, destination_value) &&
                    destination_value == source_value,
                conditional, neighbor_mask, destination};
    }

    glm::vec2 walk_source;
    glm::vec2 walk_destination;
    glm::vec2 origin;
    W8PathGridWalk walk;
    int directions[2];
    walk_source = glm::vec2((source).x, (source).z);
    walk_destination = glm::vec2((destination).x, (destination).z);
    origin = glm::vec2(level_bounds.minimum.x, level_bounds.minimum.z);
    BuildPathGridWalk(&walk_source, &walk_destination, &origin, &walk);
    GetPathGridStepDirections(&walk, directions);

    int error = walk.error0;
    unsigned int height =
        static_cast<unsigned int>(static_cast<int>(((source.y - level_bounds.minimum.y) / span))) +
        1;
    unsigned int previous_key = 0;
    unsigned int previous_value = height;
    int iteration = 0;
    const auto updateNeighborMask = [&](unsigned int current_key, unsigned int current_value) {
        const unsigned int key = previous_key == 0 ? current_key : previous_key;
        const unsigned int value = previous_key == 0 ? current_value : previous_value;
        if (previous_key != 0 || current_key != 0) {
            glm::ivec2 cell;
            cell.x = key & 0xffff;
            cell.y = key >> 16;
            neighbor_mask = ComputeWaypointNeighborMask(&cell, value);
        }
    };

    while (iteration < walk.count && !blocked) {
        auto slot = path_index->equal_range(cell_key).first;
        unsigned int direction_mask = 0;
        unsigned int path_value = height;
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
                    conditional = true;
                }
            }
            slot = NextIndexValue(*path_index, cell_key, slot);
        }

        if (!found) {
            updateNeighborMask(cell_key, path_value);
            blocked = true;
        }

        if ((previous_value & W8_PATH_CELL_CONDITIONAL) != 0) {
            conditional = true;
        }

        unsigned int direction = directions[0];
        if (!diagonal_steps) {
            if (error >= 0 || blocked) {
                cell[walk.major_axis] += walk.step[walk.major_axis];
                error -= walk.error_delta0;
            } else {
                cell[walk.minor_axis0] += walk.step[walk.minor_axis0];
                direction = directions[1];
                --iteration;
                error += walk.error_reset0;
            }
        } else if (error < 0 && !blocked) {
            cell[walk.minor_axis0] += walk.step[walk.minor_axis0];
            error += walk.error_reset0;
            if (pathCellKey(cell.x, cell.y) == destination_key) {
                --iteration;
                direction = directions[1];
            } else {
                if ((directions[0] == 0 && directions[1] == 6) ||
                    (directions[0] == 6 && directions[1] == 0)) {
                    direction = 7;
                } else {
                    direction = (directions[0] + directions[1]) / 2;
                }
                cell[walk.major_axis] += walk.step[walk.major_axis];
                error -= walk.error_delta0;
            }
        } else {
            cell[walk.major_axis] += walk.step[walk.major_axis];
            error -= walk.error_delta0;
        }

        if (cell_key == destination_key) {
            iteration = walk.count;
        } else if (direction_mask != 0 && (direction_mask & 1 << (direction & 0x1f)) == 0) {
            updateNeighborMask(cell_key, path_value);
            blocked = true;
        }

        cell_key = pathCellKey(cell.x, cell.y);
        ++iteration;
    }

    if (!adjust_destination) {
        if (!blocked) {
            int destination_height =
                static_cast<int>((destination.y - level_bounds.minimum.y) / span) + 1;
            int difference = destination_height - height;
            if (difference < -cell_count || cell_count < difference) {
                blocked = true;
            }
        }
    } else if (previous_key == 0) {
        destination = source;
    } else {
        if (blocked) {
            destination.x = ((previous_key & 0xffff) + 0.5f) * grid_scale + level_bounds.minimum.x;
            destination.z = ((previous_key >> 16) + 0.5f) * grid_scale + level_bounds.minimum.z;
        }
        destination.y = (height - 1) * span + level_bounds.minimum.y;
    }

    return {!blocked, conditional, neighbor_mask, destination};
}

/* Compare clearance along the two compass rays bracketing a horizontal
   direction. The normalized Z component selects the pair; the sign of X
   selects which half of the compass owns the middle bands. */
float NavigationMesh::CompareDirectionalClearance(const glm::vec3* position,
                                                  const glm::vec3* direction, float distance) const
{
    glm::vec2 horizontal = glm::vec2((*direction).x, (*direction).z);
    horizontal = normalizedOrZero(horizontal);
    float normalized_x = horizontal.x;
    float normalized_z = horizontal.y;

    int first_direction;
    int second_direction;
    if (normalized_x <= 0.0f) {
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

    glm::ivec2 cell;
    const auto converted_cell = pathCell(*position);
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
float NavigationMesh::MeasureDirectionalPath(const glm::ivec2* cell, int direction,
                                             unsigned int height, float distance) const
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
        unsigned int key = pathCellKey(cell_x, cell_z);
        auto* index = &cells;
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
unsigned short NavigationMesh::FindWaypoint(PathSearchScratch& path_search,
                                            const glm::vec3& position, bool exhaustive) const
{
    glm::vec3 query = position;
    unsigned short result = 0;
    path_search.start_waypoint = 0;

    if (!SnapWaypointPosition(&query, false)) {
        return 0;
    }

    glm::vec3 lower;
    glm::vec3 upper;
    glm::vec3 half_extent;
    half_extent = glm::vec3(2.5e+04f, 1.25e+04f, 2.5e+04f);
    lower = query - half_extent;
    upper = query + half_extent;

    auto candidates = owner_octree->queryObjects({lower, upper}, W8_OCTREE_KIND_WAYPOINT, -1);
    const auto count = std::ssize(candidates);
    if (count == 0) {
        return result;
    }
    int index;
    if (count > 1) {
        const auto distance = [&](unsigned candidate) {
            return static_cast<unsigned>(glm::length(query - m_waypoints[candidate].position));
        };
        for (const auto candidate : candidates) {
            const auto delta = query - m_waypoints[candidate].position;
            if (distance(candidate) < 1000 && glm::length(glm::vec2(delta.x, delta.z)) < 500)
                result = static_cast<unsigned short>(candidate);
        }
        if (result == 0)
            std::stable_sort(
                candidates.begin(), candidates.end(),
                [&](unsigned left, unsigned right) { return distance(left) < distance(right); });
    }

    for (index = 0; index < count; ++index) {
        if (result != 0) {
            return result;
        }
        if (tracePathSpan(query, m_waypoints[candidates[index]].position, false, false).reachable) {
            result = static_cast<unsigned short>(candidates[index]);
        }
    }

    if (result == 0 && exhaustive) {
        path_search.probe_position = glm::vec3(0);

        std::unordered_map<unsigned int, int>* visited = &path_search.visited_cells;
        visited->clear();
        path_search.start_waypoint = 0;
        path_search.probe_bounded = false;

        for (index = 0; index < count; ++index) {
            if (path_search.start_waypoint != 0) {
                return result;
            }

            bool saved_flag = path_search.probe_bounded;
            visited = &path_search.visited_cells;
            path_search.probe_bounded = false;
            visited->clear();

            path_search.best_key = 0;
            path_search.probe_limit = 0;
            const glm::vec3* candidate = &m_waypoints[candidates[index]].position;
            ProbeWaypointArc(path_search, &query, candidate);
            path_search.probe_bounded = saved_flag;
            path_search.probe_limit = 0xffffffff;
            ProbeWaypointArc(path_search, candidate, &query);
            if (path_search.best_key != 0) {
                path_search.start_waypoint = static_cast<unsigned short>(candidates[index]);
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
glm::vec3 NavigationMesh::snapPathHeight(glm::vec3 position) const
{
    unsigned int key = pathCell(position).key;

    if (key == 0) {
        return position;
    }

    const auto* index = &cells;
    auto slot = index->equal_range(key).first;
    int height = static_cast<int>((position.y - level_bounds.minimum.y) / span) + 1;
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
        position.y = (matched_height - 1) * span + level_bounds.minimum.y;
    }
    return position;
}

/* Build the path surface normal from height-snapped samples at the current
   point, one X cell over, and one Z cell over. The Z-edge crossed with the
   X-edge gives the upward normal on flat ground, matching the retail order. */
glm::vec3 NavigationMesh::pathSurfaceNormal(const glm::vec3& position) const
{
    glm::vec3 x_sample = position;
    glm::vec3 z_sample = position;
    x_sample.x += grid_scale;
    z_sample.z += grid_scale;

    const glm::vec3 origin = snapPathHeight(position);
    x_sample = snapPathHeight(x_sample);
    z_sample = snapPathHeight(z_sample);

    return normalizedOrZero(
        glm::cross(z_sample - origin, x_sample - origin)); // NOLINT(misc-include-cleaner)
}

/* Activate the eligible trigger prop intersecting a navigator's next path
   segment.

   Ordinary movement supplies a box from the current position to twice the
   velocity. Edge mode instead resolves the attachment's current waypoint pair
   and accepts only an edge carrying both dynamic bits. Kind-eight octree hits
   are filtered to active props whose trigger owner permits this activation;
   when several remain, the owner nearest the segment midpoint wins. */
void NavigationMesh::ActivateMovementTrigger(NavigatorMovement* movement, bool use_path_edge)
{
    if ((movement->navigation_filter & W8_NAV_THROUGH_DOORS) == 0) {
        return;
    }

    glm::vec3 lower;
    glm::vec3 upper;

    if (!use_path_edge) {
        if (movement->velocity == glm::vec3(0)) {
            return;
        }
        lower = movement->position;
        upper = movement->position + movement->velocity * 2.0f;
    } else {
        NavigatorRoute* attachment = movement->attachment.get();

        if (!attachment || attachment->path_cursor >= attachment->lastWaypoint() ||
            static_cast<std::size_t>(attachment->path_cursor) + 1 >= attachment->path.size())
            return;
        {
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

    const auto candidates = owner_octree->queryObjects({lower, upper}, W8_OCTREE_KIND_PROP, -1);
    const auto count = std::ssize(candidates);
    if (count <= 0) {
        return;
    }

    Trigger* selected = 0;
    if (count == 1) {
        Prop* prop = owner_octree->owner_world->collidable_props[candidates[0]];
        Trigger* trigger = prop->boundTrigger();
        if (prop->GetActivationState() == 0 || trigger == 0 ||
            (trigger->flags & W8_TRIGGER_ENABLED) == 0) {
            return;
        }
        selected = trigger;
    } else {
        glm::vec3 midpoint;
        midpoint = glm::vec3(glm::dvec3(upper - lower) * 0.5) + lower;
        double nearest_distance = 1e32;

        for (int index = 0; index < count; ++index) {
            Prop* prop = owner_octree->owner_world->collidable_props[candidates[index]];
            Trigger* trigger = prop->boundTrigger();
            if (prop->GetActivationState() != 0 && trigger != 0 &&
                (trigger->flags & W8_TRIGGER_ENABLED) != 0) {
                glm::vec3 center;
                center = prop->GetPosition();
                glm::vec3 difference = center - midpoint;
                double distance = glm::dot(difference, difference);
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

/* Edit the directed path edge joining a teleportal's two settled endpoints.

   An endpoint only counts as an existing teleportal waypoint when the lookup
   lands on a flagged surface within the shared snap distance. Missing ends are
   inserted with their respective inbound/outbound defaults. When both ends
   already existed, the first end's edge chain is searched so the dialog edits
   the current flags rather than starting from zero. The resulting edge is
   forced dynamic, its cached distance is invalidated, and both the renderer
   and path-edit state are marked dirty. */

unsigned int NavigationMesh::FindPathHandle(std::string_view path_name,
                                            W8PathGridBounds* path_bounds,
                                            W8PathVerticalRange* path_range)
{
    unsigned int index;
    unsigned int lookup_index;
    unsigned short value;
    float height;

    if (path_name.empty() || m_pCondPaths.empty()) {
        return 0;
    }
    for (index = 0; index < static_cast<unsigned int>(m_pCondPaths.size()); ++index) {
        if (std::string_view(m_pCondPaths[index].name) == path_name) {
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

struct PathParameter {
    const char* name;
    float NavigationSettings::* value;
};

static constexpr PathParameter path_parameters[] = {
    {"ACCELERATION_FACTOR", &NavigationSettings::acceleration_factor},
    {"ANGULAR_ACCEL_FACTOR", &NavigationSettings::angular_acceleration_factor},
    {"ANGULAR_DECEL_FACTOR", &NavigationSettings::angular_deceleration_factor},
    {"PREDICTION_TIME", &NavigationSettings::prediction_time},
    {"APPROACH_SLOW_TIME", &NavigationSettings::approach_slow_time},
    {"APPROACH_RUN_TIME", &NavigationSettings::approach_run_time},
    {"APPROACH_RUN_RATE", &NavigationSettings::approach_run_rate},
    {"GROUP_REPULSION_FACTOR", &NavigationSettings::group_repulsion_factor},
    {"PARTY_BOUNDARY_RADIUS", &NavigationSettings::party_boundary_radius},
    {"OBSTACLE_STEER_FACTOR", &NavigationSettings::obstacle_steering_factor},
    {"OBSTACLE_BRAKE_FACTOR", &NavigationSettings::obstacle_braking_factor},
    {"MIN_ANIMATION_RATE", &NavigationSettings::minimum_animation_rate},
    {"FLY_SWIM_MIN_ANIM_RATE", &NavigationSettings::flying_swimming_minimum_animation_rate},
    {0, 0}};

bool SteeringStep::InitializeSteeringContext(NavigatorMovement* movement)
{
    Navigator* linked;

    this->movement = movement;
    monster = navigation.findMonster(movement->location_id);
    if (!monster) return false;
    radius = monster->nav.radius;
    velocity_length = glm::length(movement->velocity);
    linked = monster->nav.linked_navigator;
    if (linked == 0) {
        speed_limit = movement->movement_scale * kWorldScale;
    } else {
        speed_limit = linked->movement.movement_scale * kWorldScale;
    }
    acceleration = settings.acceleration_factor * speed_limit;
    if (movement->velocity == glm::vec3(0)) {
        direction = glm::vec3(0.0, 0.0, 1.0);
        direction = glm::vec3(glm::angleAxis(std::atan2(static_cast<double>(sin(movement->yaw)),
                                                        static_cast<double>(cos(movement->yaw))),
                                             glm::dvec3(0, 1, 0)) *
                              glm::dvec3(direction));
    } else {
        direction = movement->velocity;
        direction = normalizedOrZero(direction);
    }
    perpendicular = glm::vec3(direction.z, 0.0, -direction.x);
    force = glm::vec3(0);
    blocked = false;
    nearby_queried = false;
    return true;
}

bool SteeringStep::QueryNearbyNavigators()
{
    glm::vec3 lower;
    glm::vec3 upper;
    float extent;

    if (nearby_queried) {
        return !nearby_locations.empty();
    }
    extent = radius * 5.0f;
    nearby_queried = true;
    lower = glm::vec3(movement->position.x - extent, movement->position.y - extent,
                      movement->position.z - extent);
    upper = glm::vec3(extent + movement->position.x, extent + movement->position.y,
                      extent + movement->position.z);
    nearby_locations =
        navigation.owner_octree->locationsInBox({lower, upper}, movement->location_id);

    return !nearby_locations.empty();
}

void SteeringStep::IntegrateSteering()
{
    float step;
    glm::vec3 velocity;
    glm::vec3 position;
    glm::vec3 delta;
    glm::vec3 slide;
    float scale;
    bool snapped;

    step = g_rate * navigationClocks(navigation).motionDelta();
    if (!blocked) {
        force.y = 0.0f;
        if (speed_limit <= 0.0f) {
            velocity = glm::vec3(0);
            velocity_length = 0.0f;
            movement->target_yaw = NormalizeAngle(static_cast<float>(atan2(force.x, force.z)));
        } else {
            if (acceleration < glm::length(force)) {
                force = glm::dot(force, force) == 0
                            ? force
                            : glm::normalize(force) * static_cast<float>(acceleration);
            }
            velocity = movement->velocity + force * step;
            velocity_length = glm::length(velocity);
            if (speed_limit < velocity_length) {
                velocity = glm::dot(velocity, velocity) == 0
                               ? velocity
                               : glm::normalize(velocity) * static_cast<float>(speed_limit);
                velocity_length = speed_limit;
            }
            movement->target_yaw =
                NormalizeAngle(static_cast<float>(atan2(velocity.x, velocity.z)));
        }
        if (movement->target_yaw != movement->yaw) {
            UpdateYawSteering(step, true);
            movement->yaw = movement->target_yaw;
            velocity = glm::vec3(0.0, 0.0, velocity_length);
            velocity =
                glm::vec3(glm::angleAxis(std::atan2(static_cast<double>(sin(movement->target_yaw)),
                                                    static_cast<double>(cos(movement->target_yaw))),
                                         glm::dvec3(0, 1, 0)) *
                          glm::dvec3(velocity));
        }
        position = movement->position + velocity * step;
        delta = position;
        snapped = navigation.SnapWaypointPosition(&position, false);
        if (!snapped) {
            delta = position - movement->position;
            const auto slide_direction =
                navigation.neighborSlideDirection(movement->position, delta);
            if (!slide_direction) {
                blocked = true;
            } else {
                slide = *slide_direction;
                scale = glm::dot(*slide_direction, delta);
                delta += slide * scale;
                position = movement->position + delta;
                snapped = navigation.SnapWaypointPosition(&position, false);
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
    velocity = glm::vec3(0);
    position = movement->position;
    navigation.FindPathCell(&position, 0, true);
    movement->velocity = velocity;
    movement->position = position;
}

void SteeringStep::UpdateYawSteering(float time_step, bool use_turn_rate)
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
        direction = 1.0f;
    }
    if (!use_turn_rate) {
        rate = movement->turn_rate;
        angular_velocity = 0.0f;
    } else {
        angular_velocity =
            settings.angular_acceleration_factor * movement->turn_rate * direction * time_step +
            movement->yaw_velocity;
        rate = static_cast<float>(fabs(angular_velocity));
    }
    if (remaining <= rate * time_step) {
        movement->yaw_velocity = settings.angular_deceleration_factor * angular_velocity;
        return;
    }
    movement->target_yaw = NormalizeAngle(rate * time_step * direction + movement->yaw);
    movement->yaw_velocity = angular_velocity;
}

bool SteeringStep::PredictNavigatorCollision()
{
    float lookahead;
    float nearest;
    float lateral;
    bool found;
    unsigned int index;
    Monster* monster;
    Navigator* navigator;
    glm::vec3 position;
    glm::vec3 other_velocity;
    glm::vec3 relative;
    glm::vec3 delta;
    float approach;
    float combined;
    float steer;
    float brake;
    float scale;

    found = false;
    if (!this->monster->nav.active) {
        return false;
    }
    if (velocity_length == 0.0) {
        return false;
    }
    lookahead = settings.prediction_time * velocity_length + radius;
    nearest = lookahead + kCameraSnapEpsilon;
    if (QueryNearbyNavigators()) {
        for (index = 0; index < nearby_locations.size(); ++index) {
            monster = navigation.findMonster(nearby_locations[index]);
            if (!monster) continue;
            navigator = &monster->nav;
            if (this->monster->nav.IsLinkedToNavigator(navigator)) {
                continue;
            }
            navigator = &(monster)->nav;
            position = navigator->GetPosition();
            delta = position - movement->position;
            if (0.0f < glm::dot(delta, direction)) {
                other_velocity = navigator->GetVelocity();
                relative = movement->velocity - other_velocity;
                relative = normalizedOrZero(relative);
                approach = glm::dot(delta, relative);
                approach = approach - (monster->nav.radius * approach) / glm::length(delta);
                if (0.0f < approach && approach < nearest) {
                    lateral = relative.z * delta.x + -relative.x * delta.z;
                    combined = monster->nav.radius + radius;
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
        position = navigation.partyNavigator()->GetPosition();
        delta = position - movement->position;
        combined = settings.party_boundary_radius * kWorldScale;
        approach = radius * 5.0f + combined;
        if (approach * approach > glm::dot(delta, delta)) {
            approach = glm::dot(delta, direction);
            if (approach > 0.0f) {
                relative = movement->velocity - navigation.owner_octree->owner_world->party.camera
                                                    .motion.camera_motion_velocity;
                relative = normalizedOrZero(relative);
                approach = glm::dot(delta, relative);
                approach = approach - (combined * approach) / glm::length(delta);
                if (approach > 0.0f && approach < nearest) {
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
    if (scale >= 0.0f) {
        steer = scale - 1.0f;
    } else {
        steer = scale + 1.0f;
    }
    steer = (1.0f - nearest / lookahead) * acceleration * steer;
    brake = settings.obstacle_braking_factor * steer;
    steer *= settings.obstacle_steering_factor;
    force += perpendicular * steer;
    force += direction * -brake;
    return found;
}

bool SteeringStep::HandleObstacleAhead()
{
    glm::vec3 escape;
    glm::vec3 waypoint;
    glm::vec3 ahead;
    float reach;
    float distance;
    float clearance;
    float steer;
    float brake;
    float side;

    if (velocity_length == 0.0f) {
        if (const auto slide = navigation.neighborSlideDirection(movement->position, direction)) {
            escape = *slide;
            waypoint = movement->attachment->GetNextPosition();
            waypoint -= movement->position;
            waypoint.y = 0.0f;
            if ((escape.z * waypoint.x + waypoint.z * -escape.x) *
                    (escape.z * direction.x + -escape.x * direction.z) <
                0.0) {
                speed_limit = 0.0f;
                waypoint = glm::dot(waypoint, waypoint) == 0
                               ? waypoint
                               : glm::normalize(waypoint) * static_cast<float>(acceleration);
                force += waypoint;
                return true;
            }
        }
    } else {
        reach = settings.prediction_time * velocity_length + radius;
        ahead = movement->position + direction * reach;
        const auto span_result = navigation.tracePathSpan(movement->position, ahead, true, true);
        ahead = span_result.endpoint;
        if (!span_result.reachable) {
            ahead -= movement->position;
            distance = glm::length(ahead);
            if (distance <= reach) {
                waypoint = movement->attachment->GetNextPosition();
                waypoint -= movement->position;
                waypoint.y = 0.0f;
                waypoint = glm::dot(waypoint, waypoint) == 0
                               ? waypoint
                               : glm::normalize(waypoint) * static_cast<float>(1.0);
                if (0.0 <= glm::dot(waypoint, direction)) {
                    steer = (1.0f - distance / reach) * acceleration;
                    brake = settings.obstacle_braking_factor * steer;
                    steer = settings.obstacle_steering_factor * steer;
                    if (const auto slide = slideDirection(span_result.neighbor_mask, direction)) {
                        escape = *slide;
                        side = glm::dot(escape, perpendicular);
                        if (kObstacleSlideSideThreshold <= fabs(side)) {
                            if (side < 0.0f) {
                                steer = -steer;
                            }
                        } else {
                            escape = normalizedOrZero(escape);
                            if (glm::dot(escape, perpendicular) < 0.0f) {
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
        } else if (kWorldScale < radius) {
            clearance =
                navigation.CompareDirectionalClearance(&movement->position, &direction, radius);
            if (1.0f < static_cast<float>(fabs(clearance))) {
                force += perpendicular * clearance;
                return true;
            }
        }
    }
    return false;
}

void SteeringStep::AccumulateSeekForce()
{
    glm::vec3 desired;
    glm::vec3 seek;

    if (velocity_length == 0.0f) {
        desired = target - movement->position;
        desired.y = 0.0f;
        desired = normalizedOrZero(desired);
        if (glm::dot(desired, direction) < 0.0) {
            speed_limit = 0.0f;
            force += desired * acceleration;
            return;
        }
    }
    desired = target - movement->position;
    desired.y = 0.0f;
    desired = glm::dot(desired, desired) == 0
                  ? desired
                  : glm::normalize(desired) * static_cast<float>(speed_limit);
    seek = desired - movement->velocity;
    seek *= settings.acceleration_factor;
    if (acceleration < glm::length(seek)) {
        seek = glm::dot(seek, seek) == 0 ? seek
                                         : glm::normalize(seek) * static_cast<float>(acceleration);
    }
    force += seek;
}

void SteeringStep::SeekWithApproachSpeed()
{
    float distance;
    float scale;

    distance = glm::length((target - movement->position));
    scale = distance / (settings.approach_slow_time * speed_limit);
    if (1.0f < scale) {
        distance /= settings.approach_run_time * speed_limit;
        if (1.0f < distance) {
            distance = 1.0f;
        }
        scale = (settings.approach_run_rate - 1.0f) * distance + 1.0f;
    }
    speed_limit = scale * speed_limit;
    AccumulateSeekForce();
}

void SteeringStep::AccumulateGroupRepulsion()
{
    unsigned int index;
    Monster* monster;
    Navigator* navigator;
    glm::vec3 position;
    glm::vec3 delta;
    float distance;
    float combined;
    float falloff;

    if (QueryNearbyNavigators()) {
        for (index = 0; index < nearby_locations.size(); ++index) {
            monster = navigation.findMonster(nearby_locations[index]);
            if (!monster) continue;
            navigator = &monster->nav;
            if (!this->monster->nav.IsLinkedToNavigator(navigator)) {
                continue;
            }
            position = monster->nav.GetPosition();
            delta = position - movement->position;
            distance = glm::length(delta);
            combined = (radius + radius + monster->nav.radius) * g_obstacle_radius_average_scale;
            if (distance < combined * 3.0f) {
                falloff = 1.0f;
                if (combined < distance) {
                    falloff = combined / distance;
                }
                delta = delta * static_cast<float>(-1.0);
                if (glm::dot(delta, delta) != 0.0) {
                    delta = glm::dot(delta, delta) == 0
                                ? delta
                                : glm::normalize(delta) *
                                      static_cast<float>(settings.group_repulsion_factor *
                                                         acceleration * falloff * falloff);
                }
                force += delta;
            }
        }
    }
}

bool SteeringStep::SteerAroundLeader(bool allow_path_fallback)
{
    Navigator* leader;
    glm::vec3 heading;
    glm::vec3 delta;
    glm::vec3 offset;
    float leader_radius;
    float ahead;
    float lateral;
    float distance;
    double side;

    leader = monster->nav.linked_navigator;
    leader_radius = leader->radius;
    if (leader->movement.velocity == glm::vec3(0)) {
        heading = glm::vec3(0.0, 0.0, 1.0);
        heading =
            glm::vec3(glm::angleAxis(std::atan2(static_cast<double>(sin(leader->movement.yaw)),
                                                static_cast<double>(cos(leader->movement.yaw))),
                                     glm::dvec3(0, 1, 0)) *
                      glm::dvec3(heading));
    } else {
        heading = leader->movement.velocity;
        heading = normalizedOrZero(heading);
    }
    if (leader->movement.velocity.x != 0.0f || leader->movement.velocity.y != 0.0f ||
        leader->movement.velocity.z != 0.0f) {
        delta.x = movement->position.x - leader->movement.position.x;
        delta.z = movement->position.z - leader->movement.position.z;
        ahead = delta.x * heading.x + delta.z * heading.z +
                (movement->position.y - leader->movement.position.y) * heading.y;
        if (0.0f < ahead &&
            ahead < glm::length(leader->movement.velocity) * settings.prediction_time) {
            lateral = heading.z * delta.x - heading.x * delta.z;
            if (fabs(lateral) < static_cast<double>(leader_radius) + radius) {
                side = 1.0;
                if (lateral < 0.0) {
                    side = -1.0;
                }
                offset = glm::vec3(heading.z * side * 500.0, 0.0, -heading.x * side * 500.0);
                target = movement->position + offset;
                speed_limit = settings.approach_run_rate * speed_limit;
                AccumulateSeekForce();
                return true;
            }
        }
    }
    if (allow_path_fallback && static_cast<int>(movement->attachment->path_cursor) <
                                   static_cast<int>(leader->movement.attachment->path_cursor) +
                                       static_cast<int>(movement->attachment->follow_offset)) {
        delta = leader->movement.position - movement->position;
        distance = glm::length(delta) - leader_radius;
        if (settings.approach_slow_time * speed_limit < distance) {
            distance /= settings.approach_run_time * speed_limit;
            if (1.0f < distance) {
                distance = 1.0f;
            }
            speed_limit = ((settings.approach_run_rate - 1.0f) * distance + 1.0f) * speed_limit;
        }
        return false;
    }
    offset = heading * (leader_radius * static_cast<float>(-1.0));
    target = leader->movement.position + offset;
    SeekWithApproachSpeed();
    return true;
}

void SteeringStep::SteerFromPathStart(NavigatorMovement* movement, bool alternate)
{
    if (!InitializeSteeringContext(movement)) return;
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

bool SteeringStep::SteerAlongPath(NavigatorMovement* movement, bool alternate)
{
    glm::vec3 ahead;
    bool advanced;
    float reach;

    advanced = false;
    if (!InitializeSteeringContext(movement)) return false;
    movement->attachment->flags &= ~W8_NAV_ATTACHMENT_COLLISION_PREDICTED;
    if (!HandleObstacleAhead()) {
        if (0.0f < velocity_length) {
            reach = settings.prediction_time * velocity_length;
            ahead = this->movement->position + direction * reach;
            if (!movement->attachment->checkPredictedHopHeight(navigation.m_waypoints, ahead)) {
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

bool LoadPathParameters(NavigationSettings& settings)
try {
    std::istringstream input(
        wiz8::open_file(wiz8::asset_path("data/monsters/pathparms.txt"))->read_text());
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        std::string name;
        if (!(fields >> name) || name.starts_with('#')) continue;
        for (const auto& parameter : path_parameters) {
            if (parameter.name && name == parameter.name) {
                if (!(fields >> (settings.*parameter.value))) return false;
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

const float g_prepath_link_height = 1.1f;
