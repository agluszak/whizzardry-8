#pragma once

/* Engine Code\Navigator.cpp owns these declarations. */

struct srBoundingBox;

#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"

class srNode;
class W8Octree;
struct W8World;
struct W8PathAI;
namespace wiz8 {
class File;
}

enum W8NavigatorMode {
    W8_NAVIGATOR_MODE_DEFAULT = 0,
    W8_NAVIGATOR_MODE_GROUNDED = 1,
    W8_NAVIGATOR_MODE_FLYING = 2,
    W8_NAVIGATOR_MODE_SWIMMING = 3,
    W8_NAVIGATOR_MODE_PATH_ONLY = 4,
    W8_NAVIGATOR_MODE_QUADRUPED = 5,
    W8_NAVIGATOR_MODE_CRAWLING = 6
};

enum W8NavigatorLinkMode : unsigned char {
    W8_NAVIGATOR_LINK_MODE_DEFAULT = 0,
    W8_NAVIGATOR_LINK_MODE_STOPPED = 1
};

/* W8NavigatorAttachment::flags bits with established meaning.
   - RESULT_MASK holds the last path-search result; the planners clear it before
     a search and store the result code there.
   - FOLLOW_PATH makes the mover advance along the recorded waypoint positions
     (AdvanceAlongPathPositions) instead of steering directly at the target.
   - PATH_LENGTH_CACHED latches MeasurePathLength's cached path_length; any
     waypoint edit clears it.
   - START_WAYPOINT marks start_waypoint as the next position to steer to;
     the planner sets both together and the mover clears the bit on arrival.
   - POSITION_RECORDED is raised whenever recorded_position is recorded. */
enum W8NavigatorAttachmentFlag {
    W8_NAV_ATTACHMENT_RESULT_MASK = 0x0000000f,
    W8_NAV_ATTACHMENT_SEARCH_RESULT_ONLY = 0x00001000,
    W8_NAV_ATTACHMENT_FOLLOW_PATH = 0x00010000,
    W8_NAV_ATTACHMENT_START_WAYPOINT = 0x00080000,
    W8_NAV_ATTACHMENT_COLLISION_PREDICTED = 0x00100000,
    W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED = 0x00400000,
    W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR = 0x00800000,
    W8_NAV_ATTACHMENT_TELEPORT_PHASE = 0x01000000,
    W8_NAV_ATTACHMENT_POSITION_RECORDED = 0x02000000,
    W8_NAV_ATTACHMENT_SKIP_CANDIDATE_COLLECTION = 0x04000000
};

struct W8NavigatorAttachment {
    unsigned int flags;
    /* Live waypoint cursor. AdvanceAlongPathPositions compacts consumed
       waypoints and returns this cursor to one. */
    unsigned short path_cursor;
    unsigned short position_cursor;
    unsigned short path_position_index;
    unsigned short follow_offset;
    glm::vec3 segment_start;
    glm::vec3 path_destination;
    glm::vec3 start_waypoint; /* valid while W8_NAV_ATTACHMENT_START_WAYPOINT */
    glm::vec3 path_length_origin;
    struct PathEntry {
        glm::vec3 position{0.0f};
        unsigned short value = 0;
    };
    std::vector<PathEntry> path;
    float separation;
    /* Direct segment length after initialization; MeasurePathLength later
       replaces it with the eligible route length and latches flag 0x00400000. */
    float path_length;

    W8NavigatorAttachment(); /* 0x00456210 */
    /* The from/to form 0x004604B0 constructs on the stack: both endpoints of
       the segment are seeded as recorded positions and path_length holds the
       straight-line distance. */
    W8NavigatorAttachment(const glm::vec3* from, const glm::vec3* to); /* 0x00456280 */
    /* Lazily sums the stored segment lengths into path_length, skipping
       entries whose preceding path value carries bit 0x2. */
    float MeasurePathLength(); /* 0x00456B00 */

    /* Also expanded in OctPath.cpp at 0x00465F63, 0x0046400A and
       0x00464820: set the recorded flag before copying the three components. */
    // FUNCTION: WIZ8 0x00456ae0
    void RecordPosition(const glm::vec3* position) { flags |= W8_NAV_ATTACHMENT_POSITION_RECORDED; }
    void GrowPathStorage();
    /* Append a waypoint and its value, reserving a spare endpoint slot. */
    void AppendPathPosition(const glm::vec3* position, unsigned short value)
    {
        const auto saved_position = *position;
        if (path.size() <= static_cast<unsigned int>(path_position_index + 1)) {
            GrowPathStorage();
        }
        path[path_position_index] = {saved_position, value};
        ++path_position_index;
        flags &= ~W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED;
    }

    void CopyPathFrom(const W8NavigatorAttachment* other);
    glm::vec3 GetNextPosition() const;
    void InitializeSegment(const glm::vec3* source, const glm::vec3* destination);
    /* Step `position` forward along the recorded route by the 2-D `distance`,
       consuming waypoints the step covers; returns zero once the route's last
       waypoint is reached. */
    bool AdvanceAlongPathPositions(float distance, glm::vec3* position); /* 0x00456830 */
    /* Whether `position`'s plan-view distance to the hop leaving the current
       index stays under the path height interpolated along that segment. */
    bool checkPositionHopHeight(const glm::vec3& position) const; /* 0x00456CB0 */
    /* The two-segment form of the hop-height check used on predicted
       positions: the nearer of the current or following segment wins. */
    bool checkPredictedHopHeight(const glm::vec3& position) const; /* 0x00456DD0 */
    /* Move `position` toward the route's next waypoint by up to `distance`,
       spilling into the following segment; returns nonzero once `distance`
       exceeded the remainder of the live segment. */
    bool AdvancePositionTowardWaypoint(glm::vec3* position, float distance); /* 0x00456F60 */
    /* Trims the recorded route to end at the sphere of `radius` around
       `target`: walks stored positions while they stay inside, interpolates
       the boundary point into path_destination and the route slot, moves the end
       index there, and clears flag 0x400000. One when a boundary point was
       installed. */
    bool TruncatePathAtRadius(const glm::vec3* target, float radius); /* 0x004566C0 */
    /* Advances `position` along the recorded route by `distance`, writing the
       unit direction toward the current waypoint into `direction`; one once
       the final waypoint is reached. */
    bool AdvancePositionWithDirection(glm::vec3* position, float distance,
                                      glm::vec3* direction); /* 0x00457150 */

private:
    bool withinHopHeight(const glm::vec2& point, unsigned base, bool include_next) const;
};

/* The polymorphic object the navigator owns at +0xa0. 0x00452120 deletes it
   through its own virtual slot. Recovered writes only store null
   (Navigator constructors/destructor and Monster.cpp); no allocation site or
   constructor target is known, so the identity stays unestablished. The name
   is positional and claims nothing. */
class W8NavigatorOwned0A0 {
public:
    virtual ~W8NavigatorOwned0A0();
};

/* Navigator.cpp constructs the 0xCC-byte movement/collision tail at +0xC0
   independently. World collision routines receive this subobject, while the
   surrounding Navigator owns the path and group-following state. */
struct W8NavigatorMovementState {
    unsigned int navigation_filter;
    unsigned short location_id;
    int leadership_rank;
    int active_rank;
    /* -1 means no resolved target. Navigation and OctPath use this as the
       location id of the tracked target; OctPath's single-candidate path
       search aliases the slot itself as its one-element candidate array
       (LEA [movement+0x10]); native path queries copy this ID into their owned candidate vector. */
    int target_location_id;
    float yaw;
    float target_yaw;
    /* UpdateYawSteering accelerates/decelerates this signed angular rate. */
    float yaw_velocity;
    float pitch;
    float target_pitch;
    float roll;
    float target_roll;
    float unknown_030;
    glm::vec3 velocity;
    glm::vec3 position;
    glm::vec3 target_position;
    float callback_threshold;
    float callback_progress;
    float movement_scale;
    float movement_speed;
    float turn_rate;
    unsigned short flags;
    bool pitch_enabled;
    bool roll_enabled;
    bool boundary_enabled;
    float vertical_velocity;
    float vertical_base;
    float vertical_amplitude;
    float vertical_phase;
    /* Reset installs the identity here; the constructor starts every row at
       zero before the owner calls Reset. MeasurePathDistance copies it as
       one 36-byte object (0x0045333F). */
    glm::mat3 basis;
    std::unique_ptr<W8NavigatorAttachment> attachment;
    /* Collision/path radius, initialized to 500 by the outer navigator and
       scaled with the navigator in SetScale. */
    float collision_radius;
    float alternate_radius;
    float height_offset;
    float secondary_height_offset;
    float vertical_offset;
    float scale;
    bool position_adjusted;

    W8NavigatorMovementState(); /* 0x004572C0 */
    /* A second, different set of defaults over the same subobject, run by
       W8Navigator's constructor immediately after this one. */
    void Reset();                /* 0x004573D0 */
    ~W8NavigatorMovementState(); /* 0x00457530 */

    /* Copies the eleven fields a navigator carries across from another's
       movement tail and invalidates target_location_id. It returns nothing, so it is a
       named member rather than an assignment operator. */
    void CopySettingsFrom(const W8NavigatorMovementState& other);
};

/* 0x004572C0 allocates one with operator new(0x60) before running its
   constructor at 0x00456210, which is what fixes the size; the destructor
   at 0x00457530 only proves it reaches +0x50. */

/* Navigator.cpp owns the path, position, orientation, and scene-node state
   below. It is GrCycle's ordinary second base, not a representation object. */
class W8Navigator {
public:
    explicit W8Navigator(W8World* owner = nullptr);                         /* 0x00451EC0 */
    W8Navigator(const W8Navigator& other) : W8Navigator(other, other.owner_world) {}
    W8Navigator(const W8Navigator& other, W8World* owner); /* 0x00452220 */
    W8Navigator& operator=(const W8Navigator&) = delete;
    W8Octree* NavigationOctree() const;
    W8World* const owner_world; // Null for persistent navigation, such as the party.
    virtual ~W8Navigator();                /* 0x00452120 */
    /* Takes ownership, releasing the previous path. Null clears it;
       assigning the current path preserves it. GetPathAI only borrows. */
    virtual void SetPathAI(std::unique_ptr<W8PathAI> path);
    virtual W8PathAI* GetPathAI();
    void ResetPathAI();
    void SetScale(float scale);
    /* Called with what this navigator ran into: the startup world navigator
       or another mover. True once the collision was consumed. Same folded
       body as W8GrCycle::CanEnterCycle at 0x004A7140; /OPT:NOICF emits this
       copy. */
    virtual bool OnCollision(W8Navigator*) { return true; }
    virtual void SetPosition(const glm::vec3* position); /* 0x00456020 */

    /* The missile homing step scales this snapshot of movement.velocity
       by its step count to predict the next position. */
    glm::vec3 GetVelocity() const; /* 0x004534F0 */
    /* movement.velocity = *velocity - the launch direction step. */
    void SetVelocity(const glm::vec3* velocity); /* 0x00453520 */
    /* movement.target_yaw = NormalizeAngle(angle). */
    void SetTargetYaw(float angle); /* 0x004538D0 */
    /* movement.target_pitch = NormalizeAngle(angle). */
    void SetTargetPitch(float angle); /* 0x00453920 */
    /* The navigator a from->to trace runs into: the startup world, a monster's
       navigator, or null. `include_target` lets the trace report the tracked
       movement target instead of skipping its location id. */
    W8Navigator* ResolveBlockingNavigator(const glm::vec3* from, glm::vec3* to,
                                          bool include_target); /* 0x00453230 */
    /* Copies this navigator's movement state into a scratch probe, asks the
       octree to route toward `target`, and returns the measured path length or
       -1 when no route inside `max_range` exists. */
    double MeasurePathDistance(const glm::vec3* target, float max_range,
                               int location_id); /* 0x00453300 */
    /* Find the navigator occupying `to`; the move collides when both sides'
       OnCollision accept it. */
    bool CheckNavigatorCollision(const glm::vec3* from, const glm::vec3* to);

    /* No retail emission: the startup world and the copy constructor expand
       both setters in place. */
    void configureStartupRange(float range)
    {
        radius = range;
        trace_mask = 1;
        movement.collision_radius = range;
        movement.alternate_radius = range;
    }
    void configureStartupDepth(float near_depth, float far_depth)
    {
        movement.height_offset = near_depth;
        movement.secondary_height_offset = far_depth;
    }

    glm::vec3 GetPosition();
    bool UpdateTrackedPosition();                               /* 0x00454950 */
    void UpdateNavigation(unsigned char value, bool condition); /* 0x004553A0 */
    void SetAngles(float angle);                                /* 0x004538F0 */
    void SetPitch(float pitch);                                 /* 0x00453940 */
    float GetYaw();                                             /* 0x00453970 */
    float GetPitch();                                           /* 0x00453980 */
    /* The world-path reachability probe the group engagement check runs:
       fills `out_distance` with the route length and returns nonzero when a
       route inside `max_range` exists. */
    int FindNavigatorPathDistance(float max_range, float* out_distance);          /* 0x00453480 */
    void SetMovementScale(float value);                                           /* 0x00453C50 */
    float GetMovementScale();                                                     /* 0x00453C60 */
    void SetMonsterTurnSpeed(float speed);                                        /* 0x00453C70 */
    unsigned char ConfigureMovementToPosition(const glm::vec3* position); /* 0x00452630 */
    /* Point the movement target at another navigator's position and enter the
       moving mode; the result is nonzero once the target was accepted. */
    unsigned short SetMovementTargetToNavigator(W8Navigator* target,
                                                double separation); /* 0x004526C0 */
    void LinkGroupNavigator(W8Navigator* target, bool combat_active);
    /* Whether `other` belongs to this navigator's link group: either side may
       nominate the shared linked navigator directly. */
    bool IsLinkedToNavigator(W8Navigator* other); /* 0x00452E10 */
    /* Stop this navigator, clear its movement/target state, and either mark
       the linked movement stopped or re-sync the collected group. */
    void ResetMovementAndGroupState();               /* 0x00452C90 */
    void SetPitchRollEnabled(bool pitch, bool roll); /* 0x00453CA0 */
    unsigned short ConfigureMovementToNavigator(W8Navigator* target, float separation,
                                                float maximum_distance, glm::vec3 position,
                                                int trace_mode, float facing,
                                                unsigned char* probe_result); /* 0x004529A0 */
    void AddPathPoint(const glm::vec3* position);                     /* 0x00453690 */
    void SetPositionInternal(const glm::vec3* position);
    void SetPathLooping(char value);                                       /* 0x004537C0 */
    unsigned char LinkToNavigator(W8Navigator* target, double separation); /* 0x004527A0 */
    void SetFacingToward(const glm::vec3* position);               /* 0x00454040 */
    void AimAtPosition(const glm::vec3* position);                 /* 0x00453F30 */
    bool StartPatrol(const glm::vec3* home, float distance, float variation); /* 0x00453CC0 */
    /* Stores each non-negative bound as the minimum and maximum height. */
    void SetHeightRange(float minimum, float maximum); /* 0x00453EF0 */
    void SetHalted(bool value);                        /* 0x004531F0 */
    void SetMovementStopped();                         /* 0x00453880 */
    void ClearMovementStopped();
    void CopyPathToGroup();
    void PropagateGroupPosition();                        /* 0x00454C80 */
    void UpdateAngles();                                  /* 0x00453990 */
    bool ConfigureMovement(float minimum, float maximum); /* 0x00453D20 */
    unsigned char SetMovementTarget(const glm::vec3* target, bool propagate); /* 0x00454170 */
    glm::vec3* AdjustPosition(glm::vec3* result, const glm::vec3* current,
                              const glm::vec3* previous, W8Octree& octree); /* 0x00454440 */
    void UpdateFacing(bool immediate);                                    /* 0x00454780 */
    void UpdateLinkedNavigator();                                         /* 0x00454D70 */
    bool UpdateLinkedPosition();
    void CollectGroupNavigators(std::vector<W8Navigator*>& navigators);      /* 0x00455140 */
    int ResolveMovement();                                                   /* 0x00455CC0 */
    void ClearMovement();                                                    /* 0x004537E0 */
    /* Modes 1 and 4 clear pitch/roll; 2, 3 and 5 enable pitch; 6 enables
       both. Mode 4 keeps the existing path; the other named modes build a
       fresh animated path. Later facing logic distinguishes 2/3 from 5/6. */
    void SetNavigationMode(W8NavigatorMode mode); /* 0x00452E50 */
    void SetBounds(const srBoundingBox& bounds); /* 0x00452F10 */
    void SetTurnRate(float turn_rate);                /* 0x00453C90 */

public:
    W8NavigatorMode navigation_mode;
    unsigned int flags;
    double collision_margin;
    /* Current 3D movement target. */
    glm::vec3 movement_target;
    /* Set when the navigator's movement has stopped - the constructors raise
       it, SetMovementStopped raises it when motion halts (levelling
       pitch unless the navigation mode banks), and a successful
       PrepareLinkedNavigator clears it while a path is active. A
       monster scripts wait on it: CanContinueScript blocks a WALKTO
       until it is set. */
    bool movement_stopped;
    /* The stop latch the default callback and StopAllNavigators raise and
       ResumeAllNavigators clears; distinct from movement_stopped, which
       reports motion actually halting. */
    bool halted;
    bool movement_complete;
    bool unknown_027;
    float minimum_height;
    float maximum_height;
    glm::vec3 patrol_home;
    unsigned int unknown_048;
    W8Navigator* target_navigator;
    glm::vec3 target_last_position;
    W8Navigator* linked_navigator;
    unsigned int unknown_060;
    unsigned int unknown_064;
    std::unique_ptr<W8PathAI> path_ai;
    /* 0x00451EC0 fills these as -500 and +500 triples. */
    glm::vec3 minimum;
    glm::vec3 maximum;
    float radius;
    /* Whether the navigator participates in world queries: cleared when the
       owner deactivates (monster despawn, cursor teardown), restored on
       activation; traceSphereHit and GameData skip navigators with it zero. */
    bool active;
    void (*movement_callback)(W8Navigator* navigator);
    /* Trace-hit exclusion mask: traceSphereHit skips monsters whose
       (trace_mask & flags) is nonzero; configureStartupRange raises bit 0. */
    unsigned int trace_mask;
    unsigned int unknown_094;
    unsigned int unknown_098;
    /* 0x09c: byte flag - SetMonsterGroupNavigatorDirty stores its uchar
       parameter raw, with no bool normalization. */
    bool position_dirty;
    /* 0x09d: raised by the trigger sweep after it reactivates the monster
       (members_active, animation restart, rep/path reset). */
    bool reactivated;
    std::unique_ptr<W8NavigatorOwned0A0> owned_object;
    glm::vec3 tracked_position;
    float tracked_distance;
    bool tracked_dirty;
    int linked_update_time;
    /* Raised on combat entry for the group leader's navigator (or the monster
       itself when unlinked); SetNavigatorLinkMode uses it to re-base the group
       onto the navigator's path when free-roam resumes. */
    bool group_linked;
    /* Constructed first as its own 0xcc-byte subobject, then Reset by this
       owner. Copy construction constructs a fresh attachment and transfers
       selected settings; it never shares the source's path allocation. */
    W8NavigatorMovementState movement;
    std::unique_ptr<srNode> node;
}; /* 0x190 */

void SetNavigatorLinkMode(W8NavigatorLinkMode mode);
void StopAllNavigators(void);
void ResumeAllNavigators(void);

void NavigatorDefaultCallback(W8Navigator* navigator);

extern const float g_navigator_vertical_phase_step;
extern const float g_navigator_snap_angle;
extern unsigned char g_combat_inactive;
extern W8NavigatorLinkMode g_navigator_link_mode;
extern const float g_navigator_linked_radius_scale;
extern std::vector<W8Navigator*> g_navigator_group;
/* Runtime scale applied to the startup navigator's radius when the trace
   resolver tests the camera sphere; written during startup, not a constant. */
extern float g_camera_sphere_radius_scale;
extern float g_navigator_minimum_speed;
extern float g_navigator_minimum_speed_mode23;
