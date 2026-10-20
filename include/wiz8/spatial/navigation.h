#pragma once

#include <array>
#include <cstdint>
#include <optional>

class LevelSpatialIndex;

#include <memory>
#include <queue>
#include <span>
#include <unordered_map>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int2.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/ext/vector_uint2.hpp"
#include "glm/geometric.hpp"
#include "wiz8/geometry.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/navigation_settings.h"

class MeshInstance;
class Prop;
class Monster;
namespace wiz8 {
class File;
}

/* The .oct path hash array: one packed X/Z key and its height/state word.
   Build-time producers and the loader agree on an eight-byte stride. */
struct W8FilePathNode {
    unsigned int cell;
    unsigned int level_flags;
};

/* Header preceding the conditional path tables. The writer deliberately
   writes zero to flags while the reader accepts the stored word. */
struct W8ConditionalPathHeader {
    unsigned int path_count;
    unsigned int frame_count;
    unsigned int node_count;
    unsigned int flags;
};

struct NavigatorMovement;

struct NavigatorRoute;

/* OctPath.cpp's two compact graph records. Surface zero and edge zero are
   sentinels; live records are addressed by their unsigned-short indices. */
struct W8PathSurface {
    unsigned short flags;
    unsigned short index;
    glm::vec3 position{0, 0, 0};
    // Patrol history is updated when movers cross a waypoint, never during search.
    unsigned int visit_stamp;
    unsigned short first_edge;
};

/* The compact surface record written to a .WPT file. It retains only the
   persistent flags, first edge and world position from the live surface. */
struct W8FileWaypoint {
    unsigned short flags;
    unsigned short first_edge;
    struct {
        float x, y, z;
    } position;
};

#pragma pack(push, 1)
struct W8PathEdge {
    unsigned int flags;
    unsigned short source;
    unsigned short destination;
    float distance;
    unsigned short next;
};
#pragma pack(pop)

static_assert(sizeof(W8PathEdge) == 0x0e, "W8PathEdge_must_be_0x0e");
static_assert(sizeof(W8FileWaypoint) == 0x10, "W8FileWaypoint_must_be_0x10");

/* One named conditional-path set. FindPathHandle compares path.name and then
   walks the zero-terminated lookup run starting at lookup_index: each lookup
   names a key, each key packs two region halfwords, and the key's parallel
   value word carries the height in its low half. */
/* The four-cell X/Z rectangle a conditional path covers, written by
   FindPathHandle from the low and high halves of its node keys. */
struct W8PathGridBounds {
    unsigned short min_x;
    unsigned short max_x;
    unsigned short min_z;
    unsigned short max_z;
};

/* The vertical span a conditional path covers: the minimum and maximum are
   filled by FindPathHandle; the middle slot stays a caller-managed sentinel
   (Prop keeps its bound midpoint there). */
struct W8PathVerticalRange {
    float minimum;
    float sentinel;
    float maximum;
};

struct GDPropCondPaths {
    char name[0x40];
    unsigned int lookup_index; /* 0x40 */
};

/* The two-dimensional cell walk used by path-surface probing. The cell and
   step runs are modeled as triples like the sibling octree walker; only X/Z
   participate and the third slots are zeroed. Retail does not distinguish
   this grouping from adjacent scalar storage in the original declaration. */
struct W8PathGridWalk {
    glm::ivec3 cell{0}; /* 0x00: destination X/Z cells; third component zero */
    glm::ivec3 step{0}; /* 0x0c: +1 or -1 per active axis; third component zero */
    int major_axis;       /* 0x18: 0 for X, 1 for Z */
    int minor_axis0;      /* 0x1c: (major + 1) % 2 */
    int minor_axis1;      /* 0x20: unused second secondary axis, zero */
    int count;            /* 0x24: cells to visit */
    int error_delta0;     /* 0x28 */
    int error0;           /* 0x2c */
    int error_reset0;     /* 0x30: cell size */
    int error_delta1;     /* 0x34: unused second error channel, zero */
    int error1;
    int error_reset1;
};

/* One of the fixed probe volumes assembled by 0x004656A0. The outer radius
   is the navigator's collision radius; the inner bound is its distance from
   the movement search origin. The player entry leaves the inner bound
   untouched. 0x00465970 tests candidates against these bounds and center. */
struct W8PathProbeVolume {
    unsigned int tag;
    float outer_radius;
    float inner_radius; /* Initial distance from the search origin, not a body radius. */
    glm::vec3 center;
};

/* The fixed 0x2c search node allocated by NavigationMesh's constructor.
   Scoring at 0x00464FF0 proves the flag word, base score, current distance,
   accumulated score and world position; the remaining planner state stays
   positional until its readers are recovered. */
// Running state of one patrol or target-link walk: the argmin-key candidate
// node, the accepted min/max start-to-destination range, the randomized target
// path cost, the start and destination positions, and the best alternate
// candidate's cost. Each top-level search owns one and its walks take it.
struct W8PatrolSearch {
    unsigned int node = 0;
    float min = 0.0f;
    float max = 0.0f;
    float distance = 0.0f;
    glm::vec3 start{0.0f};
    glm::vec3 destination{0.0f};
    float cost = 0.0f;
};

struct W8PathSearchNode {
    unsigned short flags;
    unsigned short node_index;
    unsigned short cell_x;
    unsigned short cell_z;
    unsigned short path_height;
    unsigned short parent_node;
    float base_score;
    float path_cost;
    float distance;
    float clearance;
    float score;
    glm::vec3 position{0, 0, 0};
};

struct W8PathHeapEntry {
    unsigned int node;
    unsigned int priority;
};

struct W8PathHeapCompare {
    bool operator()(const W8PathHeapEntry& left, const W8PathHeapEntry& right) const
    {
        return left.priority > right.priority;
    }
};

using W8PathOpenSet =
    std::priority_queue<W8PathHeapEntry, std::vector<W8PathHeapEntry>, W8PathHeapCompare>;

struct W8PathHeapHandle {
    W8PathOpenSet open;
    unsigned int root_node = 0;

    void DeleteRoot(W8PathSearchNode* node);
};

/* Runtime navigation owns its indexed graph, search state, and decoded path tables. */
struct PathCellCoordinates {
    glm::ivec2 coordinates;
    unsigned int key;
};

// Visibility constraints for one movement search. Unconstrained searches use
// the default eye/target height without inheriting another actor's trace state.
struct W8MovementTrace {
    float maximum_distance = 0.0f;
    glm::vec3 offset{0.0f, 500.0f, 0.0f};
    int mode = 0;
    float target_height_offset = 500.0f;
    int target_location = -1; // Party; positive values identify monsters.
    float target_yaw = 0.0f;
};

struct PathSpanResult {
    bool reachable;
    bool conditional;
    unsigned neighbor_mask;
    glm::vec3 endpoint;
};

// A route the navigation mesh found between two points.
struct MeasuredPath {
    float length;
    // Hops whose link runs through a closed door.
    int door_hops;
};

struct PathGraphSearchNode {
    unsigned short search_parent = 0;
    float heuristic = 0, path_cost = 0, estimated_total_cost = 0;
    bool in_heap = false;
};
struct PathSearchScratch {
    std::vector<PathGraphSearchNode> graph;
    std::vector<std::uint16_t> route;
    float cost_limit = 1.0e10f;
    std::unordered_map<unsigned int, int> visited_cells;
    unsigned best_key = 0;
    glm::vec3 probe_position{0};
    unsigned probe_limit = 0;
    bool probe_bounded = false;
    std::vector<w8_ulong> candidates;
    unsigned probe_count = 0;
    std::array<W8PathProbeVolume, 10> probes{};
    unsigned short start_waypoint = 0;
    unsigned filter = 0;
};

class NavigationMesh {
public:
    PathCellCoordinates pathCell(const glm::vec3& position) const;
    LevelSpatialIndex* const owner_octree;
    NavigationSettings settings;
    explicit NavigationMesh(LevelSpatialIndex* owner = nullptr); /* 0x004578E0 */
    unsigned int FindPathHandle(std::string_view path_name, W8PathGridBounds* path_bounds,
                                W8PathVerticalRange* path_range); /* 0x00457CF0 */
    /* Neither takes a prop: both walk the service's own surface and edge
       tables, and their receiver is the service. */
    /* The two operations LevelSpatialIndex::AdvanceNavigator delegates to: it loads
       the service into ecx before either call, so both are its methods and
       not the free functions they were declared as. */
    unsigned int StepAlongPath(PathSearchScratch& path_search, NavigatorMovement* movement,
                               float radius, float separation);
    unsigned int StepMonsterAlongPath(NavigatorMovement* movement, float radius, float separation);
    void LinkSurfaces(Prop* prop); /* 0x00460020 */
    void LinkEdges(Prop* prop);    /* 0x004600B0 */
    void CheckConditionalWayPtStatus(std::span<const unsigned short> waypoints);
    void CheckConditionalLinkStatus(std::span<const unsigned short> edges);
    void SetConditionalPathFrame(unsigned int path_handle, unsigned short frame);
    unsigned int FindConditionalPathValue(unsigned int key, unsigned int value);
    unsigned char HandlePathEdgeTransition(NavigatorMovement* movement);
    void ReduceWaypointCosts(PathSearchScratch& path_search, unsigned int waypoint, float amount,
                             std::vector<bool>& marked) const;
    bool AdvanceAttachmentWaypoint(const glm::vec3* source, struct NavigatorRoute* attachment);
    bool MatchesPathProbe(PathSearchScratch& path_search, unsigned int tag, const float* radius,
                          const glm::vec3* position) const;
    unsigned short AllocateSearchNode(std::vector<W8PathSearchNode>& nodes) const;
    bool CanReachSearchNode(PathSearchScratch& path_search, const glm::vec3* position,
                            unsigned short target_node, float clearance,
                            std::vector<W8PathSearchNode>& nodes) const;
    void AdjustFinalPathEndpoint(NavigatorMovement* movement, float radius, float separation) const;
    void UpdateConditionalPathFlags(unsigned int path_handle, unsigned short frame,
                                    unsigned int flags);
    int ProcessSearchNodeProps(unsigned short node_index, bool first_only,
                               std::vector<W8PathSearchNode>& nodes) const;
    unsigned int CollectPathProbes(PathSearchScratch& path_search, NavigatorMovement* movement,
                                   float radius) const;
    unsigned short PlanMovement(PathSearchScratch& path_search, NavigatorMovement* movement,
                                float radius, float separation, bool explicit_target = false,
                                const W8MovementTrace* trace = nullptr) const;
    unsigned short PlanMovementToPosition(PathSearchScratch& path_search,
                                          NavigatorMovement* movement, const glm::vec3* target,
                                          float radius, float separation) const;
    float UpdateSearchNodeScore(unsigned short node, const glm::vec3* position, float minimum,
                                float maximum, std::vector<W8PathSearchNode>& nodes,
                                bool explicit_target) const;
    unsigned short ResolveSearchNodeCollisions(PathSearchScratch& path_search,
                                               NavigatorMovement* movement, unsigned short node,
                                               float radius, float separation,
                                               std::vector<W8PathSearchNode>& nodes,
                                               bool explicit_target) const;
    bool SearchNodeSeesTarget(unsigned short node, NavigatorMovement* movement,
                              std::vector<W8PathSearchNode>& nodes,
                              const W8MovementTrace& trace) const;
    bool TestSearchPositionVisibility(const glm::vec3* position, NavigatorMovement* movement,
                                      const W8MovementTrace& trace) const;
    unsigned short ConfigureMovementSearch(PathSearchScratch& path_search,
                                           NavigatorMovement* movement, float radius,
                                           float separation, const W8MovementTrace& trace,
                                           unsigned char* probe_result) const;
    bool ResolvePathCell(unsigned int key, bool allow_dynamic, unsigned int* height,
                         float* direction, float* vertical, bool* dynamic) const;
    unsigned short FindWaypoint(PathSearchScratch& path_search, const glm::vec3& position,
                                bool exhaustive) const;
    void SnapPathHeight(glm::vec3* position);
    void GetPathSurfaceNormal(const glm::vec3* position, glm::vec3* normal);
    void ActivateMovementTrigger(NavigatorMovement* movement, bool use_path_edge);
    unsigned int ClassifyWaypoint(const glm::vec3* position);
    bool FindNearbyPathCell(glm::vec3* position, bool snap_to_cell, bool require_active,
                            unsigned int* packed_out) const;
    bool SnapWaypointPosition(glm::vec3* position, bool snap_to_cell) const;
    bool TestPathCellClearance(glm::vec3* position, float clearance, bool snap_to_cell);
    bool ProbeAttachmentPath(PathSearchScratch& path_search, NavigatorRoute* attachment) const;
    unsigned int FindPathCell(glm::vec3* position, glm::uvec2* cell, bool adjust) const;
    void AppendScratchPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                           unsigned int count) const;
    bool BuildAttachmentPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                             unsigned int flags) const;
    NavigatorRoute* findLinkedAttachment(NavigatorMovement* movement, bool enabled) const;
    bool PrepareLinkedNavigator(PathSearchScratch& path_search, NavigatorMovement* movement) const;
    bool LinkAttachmentTarget(PathSearchScratch& path_search, NavigatorRoute* attachment,
                              unsigned int flags, const glm::vec3* target, float separation) const;
    bool BuildPatrolPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                         unsigned int flags, const glm::vec3* target, float minimum,
                         const glm::vec3* velocity, float maximum) const;
    void ProbeWaypointArc(PathSearchScratch& path_search, const glm::vec3* from,
                          const glm::vec3* to) const;
    void GetPathGridStepDirections(const W8PathGridWalk* walk, int* directions) const;
    void BuildPathGridWalk(const glm::vec2* from, const glm::vec2* to, const glm::vec2* origin,
                           W8PathGridWalk* walk) const;
    bool ProbeWaypointSegment(PathSearchScratch& path_search, const glm::vec3* from,
                              const glm::vec3* to) const;
    unsigned int ComputeWaypointNeighborMask(const glm::ivec2* cell, unsigned int path_value) const;
    // Return a blocked-neighbor slide direction from a live cell or the stored mask.
    std::optional<glm::vec3> neighborSlideDirection(const glm::vec3& position,
                                                    const glm::vec3& delta) const;
    /* A* from the attachment's start to its destination over the surface
       graph; returns the destination surface index, zero when unreachable. */
    unsigned short FindPath(PathSearchScratch& path_search, NavigatorRoute* attachment,
                            unsigned int flags) const;
    /* Depth-first patrol search from `waypoint`: accumulates per-link path
       costs against the randomized patrol_distance target, tracking the
       argmin-key fallback nodes, and returns the reached endpoint or zero.
       Retail names it in the "Too many links" assert. */
    /* Depth-first link search from `waypoint` toward the target stored in
       patrol.start by LinkAttachmentTarget: collects admissible
       edge destinations (filtered like FindPath), prices each by accumulated
       link cost plus distance-to-target, sorts by that key, then returns the
       first candidate beyond patrol.distance or the first nonzero
       recursive result. The search scratch tracks the farthest candidate. */
    unsigned short RecurseTargetLinks(PathSearchScratch& path_search, unsigned short waypoint,
                                      std::vector<bool>& visited,
                                      W8PatrolSearch& patrol) const; /* 0x004615D0 */
    unsigned short RecursePatrolLinks(PathSearchScratch& path_search, unsigned short waypoint,
                                      std::vector<bool>& visited, W8PatrolSearch& patrol) const;
    float MeasureDirectionalPath(const glm::ivec2* cell, int direction, unsigned int height,
                                 float distance) const;
    float CompareDirectionalClearance(const glm::vec3* position, const glm::vec3* direction,
                                      float distance) const;
    PathSpanResult tracePathSpan(const glm::vec3& source, const glm::vec3& destination,
                                 bool adjust_destination, bool diagonal_steps) const;
    /* The route from `from` to `to`, searched within `budget` when it is
       positive; no value when no route exists. */
    std::optional<MeasuredPath> MeasureAttachmentPath(PathSearchScratch& path_search,
                                                      const glm::vec3& from, const glm::vec3& to,
                                                      float budget) const; /* 0x004604B0 */
    /* Whether the hop at the attachment's current index crosses a disabled
       conditional edge whose segment box holds a door prop - and that prop's
       door-trigger action data is a type-10 record with flag bit0 clear. */
    bool TestAttachmentHopDoor(NavigatorRoute* attachment) const; /* 0x00460680 */
    /* Configure the node count, grid pitch, bounds and level name. */
    void ConfigureForLevel(int size, float grid_scale, const BoundingBox& bounds,
                           std::string_view name);
    unsigned char ReadPathNodes(wiz8::File* handle); /* 0x00458CE0 */
    unsigned char ReadWaypointFile(LevelSpatialIndex& octree);
    /* Owning destructor: releases owned tables and bit sets. Single caller
       destroys the service at octree teardown. */
    ~NavigationMesh(); /* 0x00457B10 */

    int path_node_count; /* 0x04 */
    /* The grid divisor both linking walks divide by. */
    float grid_scale; /* 0x1c */
    float span; /* 0x20 */
    short cell_count; /* 0x24 */
    BoundingBox level_bounds; /* 0x2c: minimum/maximum pair */
    // Zero is the sentinel in both indexed graph tables.
    std::vector<W8PathSurface> m_waypoints;
    std::vector<W8PathEdge> m_pEdges;
    // Persistent grid-cell records loaded from WPT data.
    std::unordered_multimap<unsigned int, unsigned int> cells;
    std::string level_name;
    // Named conditional paths and their sentinel-terminated frame/key runs.
    std::vector<GDPropCondPaths> m_pCondPaths;
    std::vector<unsigned int> m_pulCondLookup;
    std::vector<unsigned short> m_pusCondNodeFrames;
    std::vector<unsigned int> m_pulCondNodeKeys;
    std::vector<unsigned int> m_pulCondNodeValues;
};

/* Move an integer path cell one compass step; directions outside the
   eight-value range wrap once. */
void StepPathCell(int* x, int* z, int direction);

struct World;
// The pathing service of `world`'s octree, or null for quad-mesh levels.
NavigationMesh* WorldPathing(const World& world);
NavigationMesh* GetWorldPathing();
extern unsigned short g_path_reserve;
extern const float g_path_span_scale;
/* 0x005ED300: OctPrePath.cpp's vertical-link slack; retail Combat.cpp reads
   it directly when sizing a monster's move. */
extern const float g_prepath_link_height;
