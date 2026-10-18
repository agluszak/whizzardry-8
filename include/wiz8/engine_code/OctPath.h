#include <cstdint>
#include <optional>
#pragma once

class W8Octree;

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
#include "surrender/srBoundingBox.h"
#include "wiz8/geometry.h"

class stModelInstance;
class GDProp;
class W8Monster;
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

struct W8NavigatorMovementState;

/* Retail allocates this 0x58-byte object, calls its sole observed constructor,
   and later releases it with delete. Its constructor's entire effect is
   LoadPathParameters - reading Data\Monsters\pathparms.txt into the
   path-tuning globals - so the class is the path-parameter owner. The object
   doubles as the per-step steering context StepAlongPath hands to the
   0x004CAE50-0x004CCB60 method cluster: InitializeSteeringContext
   seeds it from the movement state each step, the steering helpers accumulate
   into force, and IntegrateSteering applies the result. */
class W8PathParameters {
public:
    W8PathParameters(); /* 0x004CAE40 */

    /* Seeds the context from `movement`: the owning monster, its radius, the
       speed limit (the linked navigator's movement scale when linked), the
       normalized velocity or a yaw-derived default direction, the right-hand
       perpendicular, and cleared force/query state. */
    void InitializeSteeringContext(W8NavigatorMovementState* movement); /* 0x004CAE50 */
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
    void SteerFromPathStart(W8NavigatorMovementState* movement, bool alternate); /* 0x004CCAD0 */
    /* The mid-route driver: additionally predicts the hop height along the
       path and advances the target past waypoints the step covers, returning
       the advance result. */
    bool SteerAlongPath(W8NavigatorMovementState* movement, bool alternate); /* 0x004CCB60 */

private:
    W8NavigatorMovementState* movement;
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
    std::vector<w8_ulong> nearby_locations;
    W8Monster* monster;
};

struct W8NavigatorAttachment;

/* OctPath.cpp's two compact graph records. Surface zero and edge zero are
   sentinels; live records are addressed by their unsigned-short indices. */
struct W8PathSurface {
    unsigned short flags;
    unsigned short index;
    glm::vec3 position{0, 0, 0};
    unsigned short search_parent;
    /* Monotonic visit stamp: patrol selection picks the smallest value, and
       both mover paths write elapsed game time as each waypoint is consumed. */
    unsigned int visit_stamp;
    /* A* heuristic: distance to the goal scaled by kPathHeuristicScale, cached by
       FindPath while the surface is open. */
    float heuristic;
    float path_cost;
    float estimated_total_cost;
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
   (GDProp keeps its bound midpoint there). */
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

/* The fixed 0x2c search node allocated by W8PathingService's constructor.
   Scoring at 0x00464FF0 proves the flag word, base score, current distance,
   accumulated score and world position; the remaining planner state stays
   positional until its readers are recovered. */
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

class W8PathingService {
public:
    PathCellCoordinates pathCell(const glm::vec3& position) const;
    W8Octree* const owner_octree;
    explicit W8PathingService(W8Octree* owner = nullptr); /* 0x004578E0 */
    unsigned int FindPathHandle(const char* path_name, W8PathGridBounds* path_bounds,
                                W8PathVerticalRange* path_range); /* 0x00457CF0 */
    /* Neither takes a prop: both walk the service's own surface and edge
       tables, and their receiver is the service. */
    /* The two operations W8Octree::AdvanceNavigator delegates to: it loads
       the service into ecx before either call, so both are its methods and
       not the free functions they were declared as. */
    unsigned int StepAlongPath(W8NavigatorMovementState* movement, float radius, float separation);
    unsigned int StepMonsterAlongPath(W8NavigatorMovementState* movement, float radius,
                                      float separation);
    void LinkSurfaces(GDProp* prop); /* 0x00460020 */
    void LinkEdges(GDProp* prop);    /* 0x004600B0 */
    void CheckConditionalWayPtStatus(std::span<const unsigned short> waypoints);
    void CheckConditionalLinkStatus(std::span<const unsigned short> edges);
    void SetConditionalPathFrame(unsigned int path_handle, unsigned short frame);
    unsigned int FindConditionalPathValue(unsigned int key, unsigned int value);
    unsigned char HandlePathEdgeTransition(W8NavigatorMovementState* movement);
    void ReduceWaypointCosts(unsigned int waypoint, float amount);
    bool AdvanceAttachmentWaypoint(const glm::vec3* source,
                                   struct W8NavigatorAttachment* attachment);
    bool MatchesPathProbe(unsigned int tag, const float* radius, const glm::vec3* position);
    unsigned short AllocateSearchNode();
    bool CanReachSearchNode(const glm::vec3* position, unsigned short target_node, float clearance);
    void AdjustFinalPathEndpoint(W8NavigatorMovementState* movement, float radius,
                                 float separation);
    void UpdateConditionalPathFlags(unsigned int path_handle, unsigned short frame,
                                    unsigned int flags);
    int ProcessSearchNodeProps(unsigned short node_index, bool first_only);
    unsigned int CollectPathProbes(W8NavigatorMovementState* movement, float radius);
    unsigned short PlanMovement(W8NavigatorMovementState* movement, float radius, float separation);
    unsigned short PlanMovementToPosition(W8NavigatorMovementState* movement,
                                          const glm::vec3* target, float radius, float separation);
    float UpdateSearchNodeScore(unsigned short node, const glm::vec3* position, float minimum,
                                float maximum);
    unsigned short ResolveSearchNodeCollisions(W8NavigatorMovementState* movement,
                                               unsigned short node, float radius, float separation);
    glm::vec3 GetSearchTraceOffset(float bearing);
    short TraceSearchNodeToTarget(unsigned short node, W8NavigatorMovementState* movement);
    bool TestSearchPositionVisibility(const glm::vec3* position,
                                      W8NavigatorMovementState* movement);
    unsigned short ConfigureMovementSearch(W8NavigatorMovementState* movement, int target_location,
                                           float radius, float separation, float maximum_distance,
                                           glm::vec3 trace_offset, int trace_mode,
                                           float target_height_offset, float target_yaw,
                                           unsigned char* probe_result);
    bool ResolvePathCell(unsigned int key, bool allow_dynamic, unsigned int* height,
                         float* direction, float* vertical, bool* dynamic);
    unsigned short FindWaypoint(const glm::vec3& position, bool exhaustive);
    void SnapPathHeight(glm::vec3* position);
    void GetPathSurfaceNormal(const glm::vec3* position, glm::vec3* normal);
    void ActivateMovementTrigger(W8NavigatorMovementState* movement, bool use_path_edge);
    unsigned int ClassifyWaypoint(const glm::vec3* position);
    bool FindNearbyPathCell(glm::vec3* position, bool snap_to_cell, bool require_active,
                            unsigned int* packed_out);
    bool SnapWaypointPosition(glm::vec3* position, bool snap_to_cell);
    bool TestPathCellClearance(glm::vec3* position, float clearance, bool snap_to_cell);
    bool SnapToLowerPathCell(glm::vec3* position, bool allow_directional);
    bool ProbeAttachmentPath(W8NavigatorAttachment* attachment);
    unsigned int FindPathCell(glm::vec3* position, glm::uvec2* cell, bool adjust);
    void AppendScratchPath(W8NavigatorAttachment* attachment, unsigned int count);
    bool BuildAttachmentPath(W8NavigatorAttachment* attachment, unsigned int flags);
    void ResolveLinkedNavigatorAttachment(W8NavigatorMovementState* movement, bool enabled);
    bool PrepareLinkedNavigator(W8NavigatorMovementState* movement);
    bool LinkAttachmentTarget(W8NavigatorAttachment* attachment, unsigned int flags,
                              const glm::vec3* target, float separation);
    bool BuildPatrolPath(W8NavigatorAttachment* attachment, unsigned int flags,
                         const glm::vec3* target, float minimum, const glm::vec3* velocity,
                         float maximum);
    void ProbeWaypointArc(const glm::vec3* from, const glm::vec3* to);
    void GetPathGridStepDirections(const W8PathGridWalk* walk, int* directions);
    void BuildPathGridWalk(const glm::vec2* from, const glm::vec2* to, const glm::vec2* origin,
                           W8PathGridWalk* walk);
    bool ProbeWaypointSegment(const glm::vec3* from, const glm::vec3* to);
    unsigned int ComputeWaypointNeighborMask(const glm::ivec2* cell, unsigned int path_value);
    // Return a blocked-neighbor slide direction from a live cell or the stored mask.
    std::optional<glm::vec3> neighborSlideDirection(const glm::vec3& position,
                                                    const glm::vec3& delta);
    std::optional<glm::vec3> obstacleDirection(const glm::vec3& delta);
    /* A* from the attachment's start to its destination over the surface
       graph; returns the destination surface index, zero when unreachable. */
    unsigned short FindPath(W8NavigatorAttachment* attachment, unsigned int flags);
    /* Depth-first patrol search from `waypoint`: accumulates per-link path
       costs against the randomized patrol_distance target, tracking the
       argmin-key fallback nodes, and returns the reached endpoint or zero.
       Retail names it in the "Too many links" assert. */
    /* Depth-first link search from `waypoint` toward the target stored in
       m_patrol_start by LinkAttachmentTarget: collects admissible
       edge destinations (filtered like FindPath), prices each by accumulated
       link cost plus distance-to-target, sorts by that key, then returns the
       first candidate beyond m_patrol_distance or the first nonzero
       recursive result. m_probe_cell_key tracks the farthest candidate. */
    unsigned short RecurseTargetLinks(unsigned short waypoint); /* 0x004615D0 */
    unsigned short RecursePatrolLinks(unsigned short waypoint);
    float MeasureDirectionalPath(const glm::ivec2* cell, int direction, unsigned int height,
                                 float distance);
    float CompareDirectionalClearance(const glm::vec3* position, const glm::vec3* direction,
                                      float distance);
    bool TestWaypointSpan(const glm::vec3* source, glm::vec3* destination, bool adjust_destination,
                          bool diagonal_steps);
    /* `range` carries the walk budget in and the path cost back out; `hops`
       returns the reached-waypoint count. */
    bool MeasureAttachmentPath(const glm::vec3* from, glm::vec3* to, float* range,
                               int* hops); /* 0x004604B0 */
    /* Whether the hop at the attachment's current index crosses a disabled
       conditional edge whose segment box holds a door prop - and that prop's
       door-trigger action data is a type-10 record with flag bit0 clear. */
    bool TestAttachmentHopDoor(W8NavigatorAttachment* attachment); /* 0x00460680 */
    /* Configure the node count, grid pitch, bounds and level name. */
    void ConfigureForLevel(int size, float grid_scale, const srBoundingBox& bounds,
                           const char* name); /* 0x00458A50 */
    unsigned char ReadPathNodes(wiz8::File* handle); /* 0x00458CE0 */
    unsigned char ReadWaypointFile(W8Octree& octree);
    /* Owning destructor: releases owned tables and bit sets. Single caller
       destroys the service at octree teardown. */
    ~W8PathingService(); /* 0x00457B10 */

    /* The active edge-filter mask for patrol/path searches. ConfigureForLevel
       loads it from the octree header; BuildPatrolPath stores its `flags` here
       for FindPatrolPath. */
    unsigned int navigation_filter;
    int path_node_count; /* 0x04 */
    /* The grid divisor both linking walks divide by. */
    float grid_scale; /* 0x1c */
    float span; /* 0x20 */
    short cell_count; /* 0x24 */
    srBoundingBox level_bounds; /* 0x2c: minimum/maximum pair */
    // Zero is the sentinel in both indexed graph tables.
    std::vector<W8PathSurface> m_waypoints;
    std::vector<W8PathEdge> m_pEdges;
    std::vector<std::uint16_t> m_path_scratch;
    std::vector<bool> m_marked_path_nodes;
    std::vector<bool> m_visited_path_nodes;
    /* Two hash indexes the loader builds and 0x00457B10 destroys. The path
       value words are bitfields (height in the low half, state flags in the
       high bits), so 0x64 takes unsigned values; 0x74 is the visited-cell set
       and keeps the signed value the octree registry also instantiates. The
       template only copies and compares values, so the two instantiations are
       body-equivalent and retail's linker folds them. */
    std::unique_ptr<std::unordered_multimap<unsigned int, unsigned int>> m_pPathValues;
    const char* level_name; /* 0x68 */
    std::unique_ptr<W8PathHeapHandle> path_heap;
    float m_path_cost_limit; /* 0x70: starts 1.0e10f */
    std::unique_ptr<std::unordered_map<unsigned int, int>> m_pVisitedCells;
    unsigned int m_probe_cell_key; /* 0x78 */
    glm::vec3 m_probe_position; /* 0x7c */
    unsigned int m_probe_limit; /* 0x88 */
    bool m_probe_bounded; /* 0x8c */
    std::vector<w8_ulong> m_path_candidates;
    bool explicit_target; /* 0x9c */
    unsigned int m_waypoint_neighbor_mask; /* 0xa0 */
    bool m_trace_configured; /* 0xa4 */
    float m_trace_max_distance;
    glm::vec3 trace_offset;
    int m_trace_mode;
    float trace_height_offset;
    int m_trace_target_location;
    float m_trace_target_yaw;
    std::vector<W8PathSearchNode> m_search_nodes;
    unsigned int m_path_probe_count;
    W8PathProbeVolume m_path_probes[10];
    unsigned short start_waypoint;
    /* Patrol-search state laid down by BuildPatrolPath and consulted by the
       recursive FindPatrolPath: the argmin-key candidate node, the accepted
       min/max start-to-destination range, the randomized target path cost,
       the start and destination positions, and the best alternate
       candidate's cost. */
    unsigned int m_patrol_node;
    float m_patrol_min;
    float m_patrol_max;
    float m_patrol_distance;
    glm::vec3 m_patrol_start;
    glm::vec3 m_patrol_destination;
    float m_patrol_cost;
    W8PathParameters m_steering_context;
    W8NavigatorAttachment* m_linked_attachment;
    // Named conditional paths and their sentinel-terminated frame/key runs.
    std::vector<GDPropCondPaths> m_pCondPaths;
    std::vector<unsigned int> m_pulCondLookup;
    std::vector<unsigned short> m_pusCondNodeFrames;
    std::vector<unsigned int> m_pulCondNodeKeys;
    std::vector<unsigned int> m_pulCondNodeValues;
    bool span_blocked;
};

/* Move an integer path cell one compass step; directions outside the
   eight-value range wrap once. */
void StepPathCell(int* x, int* z, int direction);

struct W8World;
// The pathing service of `world`'s octree, or null for quad-mesh levels.
W8PathingService* WorldPathing(const W8World& world);
W8PathingService* GetWorldPathing();
extern unsigned short g_path_reserve;
extern const float g_path_span_scale;
/* 0x005ED300: OctPrePath.cpp's vertical-link slack; retail Combat.cpp reads
   it directly when sizing a monster's move. */
extern const float g_prepath_link_height;
