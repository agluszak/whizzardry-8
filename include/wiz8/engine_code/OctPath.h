#include <cstdint>
#include <optional>
#pragma once

#include "surrender/srBoundingBox.h"

#include <queue>
#include <span>
#include "glm/ext/vector_int2.hpp"
#include <memory>
#include <vector>
#include <unordered_map>

#include "surrender/srMath.h"
#include "wiz8/geometry.h"
#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_uint2.hpp"

class stModelInstance;
class GDProp;
class GDPreProp;
class OctPrePathLog;
struct CondPathNode;
class OctPreTree;
class W8Monster;
struct W8LevelFile;
struct W8LevelFileNamedPosition;
namespace wiz8 { class File; }

/* The .oct path hash array: one packed X/Z key and its height/state word.
   Build-time producers and the loader agree on an eight-byte stride. */
struct W8FilePathNode {
    std::uint32_t cell;
    std::uint32_t level_flags;
};

/* Header preceding the conditional path tables. The writer deliberately
   writes zero to flags while the reader accepts the stored word. */
struct W8ConditionalPathHeader {
    std::uint32_t path_count;
    std::uint32_t frame_count;
    std::uint32_t node_count;
    std::uint32_t flags;
};

/* One pre-path prop record handed to LinkCollideableProps: the prop's path
   name plus the GDPreProp array OctPreTree.cpp builds for it (stride 0x48). */
struct W8PreProp {
    char name[0x40];
    std::uint16_t num_stop_meshes;
    /* The running base prop number this record's pStopMeshes indices are
       relative to; InsertConditionalNodes matches a GDProp m_prop_number
       into [first_prop_number, first_prop_number + num_stop_meshes). */
    std::uint16_t first_prop_number;
    GDPreProp* pStopMeshes;
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
    srVector3T<float> target;
    srVector3T<float> direction;
    srVector3T<float> perpendicular;
    srVector3T<float> force;
    float radius;
    bool nearby_queried;
    bool blocked;
    std::vector<std::uint32_t> nearby_locations;
    W8Monster* monster;
};

struct W8NavigatorAttachment;

/* OctPath.cpp's two compact graph records. Surface zero and edge zero are
   sentinels; live records are addressed by their unsigned-short indices. */
struct W8PathSurface {
    std::uint16_t flags;
    std::uint16_t index;
    srVector3T<float> position;
    std::uint16_t search_parent;
    std::uint8_t padding_12[0x02];
    /* Monotonic visit stamp: patrol selection picks the smallest value, and
       both mover paths write elapsed game time as each waypoint is consumed. */
    std::uint32_t visit_stamp;
    /* A* heuristic: distance to the goal scaled by g_path_heuristic_scale, cached by
       FindPath while the surface is open. */
    float heuristic;
    float path_cost;
    float estimated_total_cost;
    std::uint16_t first_edge;
    std::uint16_t padding_26;
};

/* The compact surface record written to a .WPT file. It retains only the
   persistent flags, first edge and world position from the live 0x28-byte
   surface. */
struct W8FileWaypoint {
    std::uint16_t flags;
    std::uint16_t first_edge;
    srVector3T<float> position;
};

#pragma pack(push, 1)
struct W8PathEdge {
    std::uint32_t flags;
    std::uint16_t source;
    std::uint16_t destination;
    float distance;
    std::uint16_t next;
};
#pragma pack(pop)

static_assert(sizeof(W8PathSurface) == 0x28, "W8PathSurface_must_be_0x28");
static_assert(sizeof(W8PathEdge) == 0x0e, "W8PathEdge_must_be_0x0e");
static_assert(sizeof(W8FileWaypoint) == 0x10, "W8FileWaypoint_must_be_0x10");

/* One named conditional-path set. FindPathHandle compares path.name and then
   walks the zero-terminated lookup run starting at lookup_index: each lookup
   names a key, each key packs two region halfwords, and the key's parallel
   value word carries the height in its low half. */
/* The four-cell X/Z rectangle a conditional path covers, written by
   FindPathHandle from the low and high halves of its node keys. */
struct W8PathGridBounds {
    std::uint16_t min_x;
    std::uint16_t max_x;
    std::uint16_t min_z;
    std::uint16_t max_z;
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
    std::uint32_t lookup_index; /* 0x40 */
};

/* The two-dimensional cell walk used by path-surface probing. The cell and
   step runs are modeled as triples like the sibling octree walker; only X/Z
   participate and the third slots are zeroed. Retail does not distinguish
   this grouping from adjacent scalar storage in the original declaration. */
struct W8PathGridWalk {
    srVector3T<std::int32_t> cell; /* 0x00: destination X/Z cells; third component zero */
    srVector3T<std::int32_t> step; /* 0x0c: +1 or -1 per active axis; third component zero */
    std::int32_t major_axis;       /* 0x18: 0 for X, 1 for Z */
    std::int32_t minor_axis0;      /* 0x1c: (major + 1) % 2 */
    std::int32_t minor_axis1;      /* 0x20: unused second secondary axis, zero */
    std::int32_t count;            /* 0x24: cells to visit */
    std::int32_t error_delta0;     /* 0x28 */
    std::int32_t error0;           /* 0x2c */
    std::int32_t error_reset0;     /* 0x30: cell size */
    std::int32_t error_delta1;     /* 0x34: unused second error channel, zero */
    std::int32_t error1;
    std::int32_t error_reset1;
};

/* One of the fixed probe volumes assembled by 0x004656A0. The outer radius
   is the navigator's collision radius; the inner bound is its distance from
   the movement search origin. The player entry leaves the inner bound
   untouched. 0x00465970 tests candidates against these bounds and center. */
struct W8PathProbeVolume {
    std::uint32_t tag;
    float outer_radius;
    float inner_radius; /* Initial distance from the search origin, not a body radius. */
    srVector3T<float> center;
};

/* The fixed 0x2c search node allocated by W8PathingService's constructor.
   Scoring at 0x00464FF0 proves the flag word, base score, current distance,
   accumulated score and world position; the remaining planner state stays
   positional until its readers are recovered. */
struct W8PathSearchNode {
    std::uint16_t flags;
    std::uint16_t node_index;
    std::uint16_t cell_x;
    std::uint16_t cell_z;
    std::uint16_t path_height;
    std::uint16_t parent_node;
    float base_score;
    float path_cost;
    float distance;
    float clearance;
    float score;
    srVector3T<float> position;
};

struct W8PathHeapEntry {
    std::uint32_t node;
    std::uint32_t priority;
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
    std::uint32_t root_node = 0;

    void DeleteRoot(W8PathSearchNode* node);
};

/* The pathing service the octree builds when its file carries one. Its own
   constructor at 0x004578E0 initialises through 0x238 and ReadOctFile allocates
   0x240, which is what fixes the extent; only the fields those two bodies and
   the path lookup reach are named. */
struct PathCellCoordinates { glm::ivec2 coordinates; std::uint32_t key; };

class W8PathingService {
public:
    PathCellCoordinates pathCell(const glm::vec3& position) const;
    W8PathingService(); /* 0x004578E0 */
    std::uint32_t FindPathHandle(const char* path_name, W8PathGridBounds* path_bounds,
                                W8PathVerticalRange* path_range); /* 0x00457CF0 */
    /* Neither takes a prop: both walk the service's own surface and edge
       tables, and their receiver is the service. */
    /* The two operations W8Octree::AdvanceNavigator delegates to: it loads
       the service into ecx before either call, so both are its methods and
       not the free functions they were declared as. */
    std::uint32_t StepAlongPath(W8NavigatorMovementState* movement, float radius, float separation);
    std::uint32_t StepMonsterAlongPath(W8NavigatorMovementState* movement, float radius,
                                      float separation);
    void LinkSurfaces(GDProp* prop); /* 0x00460020 */
    void LinkEdges(GDProp* prop);    /* 0x004600B0 */
    void CheckConditionalWayPtStatus(std::span<const std::uint16_t> waypoints);
    void CheckConditionalLinkStatus(std::span<const std::uint16_t> edges);
    void SetConditionalPathFrame(std::uint32_t path_handle, std::uint16_t frame);
    std::uint32_t FindConditionalPathValue(std::uint32_t key, std::uint32_t value);
    void
    LinkCollideableProps(std::int32_t lNumProps, W8PreProp* pPreProps,
                         std::unordered_multimap<std::uint32_t, CondPathNode*>* pCondValues); /* 0x004CE510 */
    std::uint8_t HandlePathEdgeTransition(W8NavigatorMovementState* movement);
    void ReduceWaypointCosts(std::uint32_t waypoint, float amount);
    bool AdvanceAttachmentWaypoint(const srVector3T<float>* source,
                                   struct W8NavigatorAttachment* attachment);
    bool MatchesPathProbe(std::uint32_t tag, const float* radius, const srVector3T<float>* position);
    std::uint16_t AllocateSearchNode();
    bool CanReachSearchNode(const srVector3T<float>* position, std::uint16_t target_node,
                            float clearance);
    void AdjustFinalPathEndpoint(W8NavigatorMovementState* movement, float radius,
                                 float separation);
    void UpdateConditionalPathFlags(std::uint32_t path_handle, std::uint16_t frame,
                                    std::uint32_t flags);
    std::int32_t ProcessSearchNodeProps(std::uint16_t node_index, bool first_only);
    std::uint32_t CollectPathProbes(W8NavigatorMovementState* movement, float radius);
    std::uint16_t PlanMovement(W8NavigatorMovementState* movement, float radius, float separation);
    std::uint16_t PlanMovementToPosition(W8NavigatorMovementState* movement,
                                          const srVector3T<float>* target, float radius,
                                          float separation);
    float UpdateSearchNodeScore(std::uint16_t node, const srVector3T<float>* position,
                                float minimum, float maximum);
    std::uint16_t ResolveSearchNodeCollisions(W8NavigatorMovementState* movement,
                                               std::uint16_t node, float radius, float separation);
    srVector3T<float> GetSearchTraceOffset(float bearing);
    std::int16_t TraceSearchNodeToTarget(std::uint16_t node, W8NavigatorMovementState* movement);
    bool TestSearchPositionVisibility(const srVector3T<float>* position,
                                      W8NavigatorMovementState* movement);
    std::uint16_t ConfigureMovementSearch(W8NavigatorMovementState* movement, std::int32_t target_location,
                                           float radius, float separation, float maximum_distance,
                                           srVector3T<float> trace_offset, std::int32_t trace_mode,
                                           float target_height_offset, float target_yaw,
                                           std::uint8_t* probe_result);
    bool ResolvePathCell(std::uint32_t key, bool allow_dynamic, std::uint32_t* height,
                         float* direction, float* vertical, bool* dynamic);
    std::uint16_t FindWaypoint(const glm::vec3& position, bool exhaustive);
    void SnapPathHeight(srVector3T<float>* position);
    void GetPathSurfaceNormal(const srVector3T<float>* position, srVector3T<float>* normal);
    void ActivateMovementTrigger(W8NavigatorMovementState* movement, bool use_path_edge);
    void UpdatePathVisualization(const glm::vec3& source, const glm::vec3& destination);
    void DrawPathPosition(const glm::vec3& position, std::uint8_t mode);
    void BuildSearchVisualization();
    stModelInstance* BuildPathVisualization();
    stModelInstance* EnsurePathVisualization();
    glm::vec3 waypointVisualizationColor(std::uint16_t waypoint) const;
    std::int16_t CollectPathVisualization(const glm::vec3& position);
    bool PreparePathVisualization(const glm::vec3& source, const glm::vec3& direction);
    void AddWaypoint(const srVector3T<float>* position);
    std::uint32_t ClassifyWaypoint(const srVector3T<float>* position);
    bool FindNearbyPathCell(srVector3T<float>* position, bool snap_to_cell,
                            bool require_active, std::uint32_t* packed_out);
    bool SnapWaypointPosition(srVector3T<float>* position, bool snap_to_cell);
    bool TestPathCellClearance(srVector3T<float>* position, float clearance, bool snap_to_cell);
    bool SnapToLowerPathCell(srVector3T<float>* position, bool allow_directional);
    bool ProbeAttachmentPath(W8NavigatorAttachment* attachment);
    std::uint32_t FindPathCell(srVector3T<float>* position, glm::uvec2* cell,
                              bool adjust);
    void AppendScratchPath(W8NavigatorAttachment* attachment, std::uint32_t count);
    bool BuildAttachmentPath(W8NavigatorAttachment* attachment, std::uint32_t flags);
    void ResolveLinkedNavigatorAttachment(W8NavigatorMovementState* movement, bool enabled);
    bool PrepareLinkedNavigator(W8NavigatorMovementState* movement);
    bool LinkAttachmentTarget(W8NavigatorAttachment* attachment, std::uint32_t flags,
                              const srVector3T<float>* target, float separation);
    bool BuildPatrolPath(W8NavigatorAttachment* attachment, std::uint32_t flags,
                         const srVector3T<float>* target, float minimum,
                         const srVector3T<float>* velocity, float maximum);
    void ProbeWaypointArc(const srVector3T<float>* from, const srVector3T<float>* to);
    void GetPathGridStepDirections(const W8PathGridWalk* walk, std::int32_t* directions);
    void BuildPathGridWalk(const glm::vec2* from, const glm::vec2* to, const glm::vec2* origin, W8PathGridWalk* walk);
    bool ProbeWaypointSegment(const srVector3T<float>* from, const srVector3T<float>* to);
    std::uint32_t ComputeWaypointNeighborMask(const srVector2i* cell, std::uint32_t path_value);
    // Return a blocked-neighbor slide direction from a live cell or the stored mask.
    std::optional<glm::vec3> neighborSlideDirection(const glm::vec3& position,
                                                   const glm::vec3& delta);
    std::optional<glm::vec3> obstacleDirection(const glm::vec3& delta);
    /* A* from the attachment's start to its destination over the surface
       graph; returns the destination surface index, zero when unreachable. */
    std::uint16_t FindPath(W8NavigatorAttachment* attachment, std::uint32_t flags);
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
    std::uint16_t RecurseTargetLinks(std::uint16_t waypoint); /* 0x004615D0 */
    std::uint16_t RecursePatrolLinks(std::uint16_t waypoint);
    float MeasureDirectionalPath(const srVector2i* cell, std::int32_t direction, std::uint32_t height,
                                 float distance);
    float CompareDirectionalClearance(const srVector3T<float>* position,
                                      const srVector3T<float>* direction, float distance);
    void SetWaypointLinkFlags(std::uint16_t waypoint, std::uint32_t direction);
    void RemoveWaypointLink(std::uint16_t edge);
    void AddWaypointLink(std::uint16_t source, std::uint16_t destination, std::uint32_t flags);
    bool UpdateWaypointLink(std::uint16_t source, std::uint16_t destination, std::uint32_t flags);
    bool HasDirectionalWaypointLink(std::uint16_t source, std::uint16_t destination);
    bool TestWaypointSpan(const srVector3T<float>* source, srVector3T<float>* destination,
                          bool adjust_destination, bool diagonal_steps);
    /* `range` carries the walk budget in and the path cost back out; `hops`
       returns the reached-waypoint count. */
    bool MeasureAttachmentPath(const srVector3T<float>* from, srVector3T<float>* to, float* range,
                               std::int32_t* hops); /* 0x004604B0 */
    /* Whether the hop at the attachment's current index crosses a disabled
       conditional edge whose segment box holds a door prop - and that prop's
       door-trigger action data is a type-10 record with flag bit0 clear. */
    bool TestAttachmentHopDoor(W8NavigatorAttachment* attachment); /* 0x00460680 */
    std::uint32_t EditWaypointLinkFlags(const char* title, std::uint32_t* flags,
                                       std::uint32_t direction);
    void EditTeleportalLink(const srVector3T<float>* destination,
                            const srVector3T<float>* source); /* 0x0045F2D0 */
    /* Takes the size, two loose values, the bounds block out of the octree
       header, and the level name the octree already owns. */
    void ConfigureForLevel(std::int32_t size, float grid_scale, float path_clearance,
                           const srBoundingBox& bounds, const char* name); /* 0x00458A50 */
    std::uint8_t ReadPathNodes(wiz8::File* handle);                                   /* 0x00458CE0 */
    bool WritePathNodes(wiz8::File* handle);
    std::uint8_t SaveWaypointSnapshot(bool force);
    std::uint8_t WriteWaypointFile();
    std::uint8_t ReadWaypointFile();
    void BuildWaypointFileData();
    /* Owning destructor: releases owned tables and bit sets. Single caller
       destroys the service at octree teardown. */
    ~W8PathingService(); /* 0x00457B10 */

    /* The active edge-filter mask for patrol/path searches. ConfigureForLevel
       loads it from the octree header; BuildPatrolPath stores its `flags` here
       for FindPatrolPath. */
    std::uint32_t navigation_filter;
    std::int32_t path_node_count; /* 0x04 */
    /* PrePathing's CreatePathNodeArray counts edge nodes here starting from
       one, and WriteOctFile serializes it beside the node count. */
    std::int32_t edge_node_count;
    /* ReadOctFile tests this beside m_waypoint_editing before settling a portal. */
    std::uint32_t m_ulNumWayPoints;  /* 0x0c */
    std::uint32_t m_ulNumWayPtLinks; /* 0x10 */
    std::int32_t m_unknown_014;
    /* Incremented for each edge removed by the waypoint editor; never read. */
    std::int32_t m_removed_edge_count;
    /* The grid divisor both linking walks divide by. */
    float grid_scale; /* 0x1c */
    float span;       /* 0x20 */
    std::int16_t cell_count; /* 0x24 */
    std::uint16_t m_padding_026;
    /* Path probe-clearance height from the octree
       header; only ConfigureForLevel writes it. */
    float path_clearance;       /* 0x28 */
    W8BoundingBox level_bounds; /* 0x2c: minimum/maximum pair */
    /* Four malloc'd tables and one polymorphic object, all released by
       0x00457B10 - the first four with free, the last through its own
       deleting slot. */
    W8FilePathNode* file_path_nodes; /* 0x44: serialized cell/height-state records */
    /* Surfaces are 0x28 bytes apart, edges 0xe; an edge names two surfaces by
       index in its two shorts at +4 and +6. */
    W8PathSurface* m_waypoints;            /* 0x48 */
    W8PathEdge* m_pEdges;                  /* 0x4c */
    W8FileWaypoint* m_pFileWayPoints;      /* 0x50 */
    std::unique_ptr<stModelInstance> m_path_visualization;
    std::vector<bool> m_marked_path_nodes;
    std::vector<bool> m_visited_path_nodes;
    std::vector<bool> m_collected_path_nodes;
    /* Two hash indexes the loader builds and 0x00457B10 destroys. The path
       value words are bitfields (height in the low half, state flags in the
       high bits), so 0x64 takes unsigned values; 0x74 is the visited-cell set
       and keeps the signed value the octree registry also instantiates. The
       template only copies and compares values, so the two instantiations are
       body-equivalent and retail's linker folds them. */
    std::unordered_multimap<std::uint32_t, std::uint32_t>* m_pPathValues; /* 0x64 */
    const char* level_name;                                 /* 0x68 */
    W8PathHeapHandle* path_heap;                            /* 0x6c */
    float m_path_cost_limit;                                /* 0x70: starts 1.0e10f */
    std::unordered_map<std::uint32_t, std::int32_t>* m_pVisitedCells;        /* 0x74 */
    std::uint32_t m_probe_cell_key;                          /* 0x78 */
    srVector3T<float> m_probe_position;                     /* 0x7c */
    std::uint32_t m_probe_limit;                             /* 0x88 */
    bool m_probe_bounded;                                   /* 0x8c */
    std::uint8_t m_padding_08d[3];
    std::uint32_t planner_location;
    std::vector<std::uint32_t> m_path_candidates;
    bool explicit_target; /* 0x9c */
    std::uint8_t m_padding_09d[3];
    std::uint32_t m_waypoint_neighbor_mask; /* 0xa0 */
    bool m_trace_configured;               /* 0xa4 */
    std::uint8_t m_padding_0a5[3];
    float m_trace_max_distance;
    srVector3T<float> trace_offset;
    std::int32_t m_trace_mode;
    float trace_height_offset;
    std::int32_t m_trace_target_location;
    float m_trace_target_yaw;
    W8PathSearchNode* m_search_nodes; /* 0xc8 */
    std::uint32_t m_search_node_count;
    std::uint32_t m_search_node_capacity;
    std::uint32_t m_path_probe_count;
    W8PathProbeVolume m_path_probes[10];
    bool m_waypoint_editing; /* 0x1c8 */
    bool draw_waypoints;
    bool flag2;
    bool search_visualization;
    bool waypoints_dirty;
    std::uint8_t m_padding_1cd;
    std::uint16_t waypoint_editor_flags; /* 0x1ce: starts 4 */
    std::int32_t link_flags;
    std::uint16_t start_waypoint;
    std::uint16_t destination_waypoint;
    std::uint16_t saved_surface;
    bool path_direction_valid;
    std::uint8_t m_padding_1db;
    /* Patrol-search state laid down by BuildPatrolPath and consulted by the
       recursive FindPatrolPath: the argmin-key candidate node, the accepted
       min/max start-to-destination range, the randomized target path cost,
       the start and destination positions, and the best alternate
       candidate's cost. */
    std::uint32_t m_patrol_node;
    float m_patrol_min;
    float m_patrol_max;
    float m_patrol_distance;
    srVector3T<float> m_patrol_start;
    srVector3T<float> m_patrol_destination;
    std::uint8_t m_padding_204[0x0c];
    float m_patrol_cost;
    W8PathParameters* m_steering_context; /* 0x214 */
    W8NavigatorAttachment* m_linked_attachment;
    /* The conditional path tables. ReadPathNodes at 0x00458CE0 asserts on the
       first by name and names the other four in its own failure messages: a
       lookup, a frame, a key and a value array, sized from the two counts.
       FindPathHandle scans the 0x44-byte path records by name. */
    GDPropCondPaths* m_pCondPaths;       /* 0x21c */
    std::int32_t m_ulNumCondPaths;                /* 0x220 */
    std::int32_t m_ulNumCondFrames;               /* 0x224 */
    std::int32_t m_ulNumCondNodes;                /* 0x228 */
    std::uint32_t* m_pulCondLookup;       /* 0x22c */
    std::uint16_t* m_pusCondNodeFrames; /* 0x230 */
    std::uint32_t* m_pulCondNodeKeys;     /* 0x234 */
    std::uint32_t* m_pulCondNodeValues;   /* 0x238 */
    bool span_blocked;
    std::uint8_t m_padding_23d[3];
};

/* The 8-byte record InsertConditionalNodes hangs on the cond map under a
   (frame << 16 | preprop index + 1) key: the node's serialized flag word
   (height level + 1 in the low half, |0x02000000 when it came from the
   blocker run) and its packed cell.  LinkCollideableProps drains the map into
   the serialized key/value tables and frees each record.  The name is the
   original spelling from the allocation-failure assertion text. */
struct CondPathNode {
    std::uint32_t value; /* 0x00 */
    std::uint32_t cell;  /* 0x04 */
};

/* The 0x10-byte build-time path-node record PrePathing::GetPathNode hands out
   of its 1000-record chunks: floor index in the low bits plus link, clearance
   and state flags; the packed cell; world height; and the same-cell chain. */
struct W8PrePathNode {
    std::uint32_t level_flags; /* 0x00 */
    std::uint32_t cell;        /* 0x04: z << 16 | x */
    float y;                  /* 0x08 */
    W8PrePathNode* next;      /* 0x0c: allocation order, same-cell runs */
};

enum { W8_PREPATH_NODES_PER_CHUNK = 1000 };

/* Build-only mark removed before path cells are serialized. */
enum { W8_PREPATH_NODE_PRUNED = 0x40000000u };

/* OctPrePath.cpp's 0x1204-byte build-time pathing service ("PrePathing" in its
   own assertions): its constructor runs the W8PathingService constructor then
   initialises scratch state through +0x1200, and the pre-tree stores it at
   +0x2a0. */
class PrePathing : public W8PathingService {
public:
    PrePathing();  /* 0x004CCFD0 */
    ~PrePathing(); /* 0x004CD030 */

    /* Copies each named position (scaled to world units) into +0x250 and
       snaps it to the ground through `octree`. */
    std::int32_t SnapNamedPositions(std::span<const W8LevelFileNamedPosition> positions,
                           std::uint32_t min_component_percent, OctPreTree* octree);
    /* Hands out the next path-node record, allocating a new chunk when the
       current one fills. */
    W8PrePathNode* GetPathNode();
    bool BuildPathList(W8PrePathNode* nodes, std::unordered_map<std::uint32_t, std::int32_t>* cell_map);
    W8PrePathNode* FindAdjacentPathNode(const W8PrePathNode* node, std::int32_t direction);
    bool LinkPathNodes();
    void PropagatePathNodeClearance(W8PrePathNode* node, std::uint32_t depth);
    std::uint32_t DeleteUnreachableAreas();
    std::int32_t CreatePathNodeArray();
    bool CreateAutomapNodes(W8LevelFile& level);

    W8PrePathNode** path_node_list; /* path_node_count entries */
    OctPrePathLog* path_log;
    /* A malloc'd buffer the destructor `free`s; no surviving writer. */
    void* owned_248;
    std::int32_t named_position_count;
    srVector3T<float>* named_positions;
    std::unordered_map<std::uint32_t, std::int32_t>* cell_map;
    /* Embedded chunk table: each slot is a malloc'd run of
       W8_PREPATH_NODES_PER_CHUNK path-node records. The constructor fills slot 0, and the
       destructor frees every slot through chunk_index inclusive. */
    W8PrePathNode* node_chunks[0x3e8];
    std::int32_t chunk_index;
    std::int32_t chunk_node_count;
    /* Components smaller than this percent of the node count get deleted
       while linking; capped at 50. */
    std::uint32_t min_component_percent;
};

/* Move an integer path cell one compass step; directions outside the
   eight-value range wrap once. */
void __stdcall StepPathCell(std::int32_t* x, std::int32_t* z, std::int32_t direction);

extern W8PathingService* g_pathing;
extern std::uint16_t g_path_reserve;
extern const float g_path_span_scale;
extern const double g_double_one_tenth;
/* The -1.0 no-route sentinel MeasurePathDistance returns. */
extern const double g_double_negative_one;
/* 0x005ED300: OctPrePath.cpp's vertical-link slack; retail Combat.cpp reads
   it directly when sizing a monster's move. */
extern const float g_prepath_link_height;
