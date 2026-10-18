#include "wiz8/engine_code/Navigator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "surrender/segment_math.h"
#include "surrender/srBoundingBox.h"
#include "surrender/srNode.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GameTimeAccumulator.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/PolyPick.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

// STRING: WIZ8 0x006081fc

/* The world object the navigator notifies when it leaves a location, and
   the notification itself. 0x0042E880 sits outside every assertion-backed
   interval, so it keeps an address-qualified name. */

namespace {

// GLOBAL: WIZ8 0x00659b30
std::vector<W8Navigator*> g_registered_navigators;

} // namespace

/* Trims the recorded route to the boundary of the `radius` sphere around
   `target`: walks stored positions while they stay inside, then lerps the
   crossing point into path_destination and over the first outside waypoint, moves
   path_position_index there, and clears flag 0x00400000. */
// FUNCTION: WIZ8 0x004566C0
bool W8NavigatorAttachment::TruncatePathAtRadius(const glm::vec3* target, float radius)
{
    unsigned int index = path_cursor;
    float distance = glm::length((path[index].position - *target));
    float previous_distance = distance;

    while (distance < radius && index < path_position_index) {
        previous_distance = distance;
        ++index;
        distance = glm::length((path[index].position - *target));
    }
    /* Interpolation needs a previous point inside the radius. When the cursor
       point is already outside, retail interpolated against an uninitialized
       distance (and the point before the cursor); leave the route untouched. */
    if (index != path_cursor && distance > radius) {
        float fraction = (distance - radius) / (distance - previous_distance);
        path_destination =
            glm::vec3((path[index].position.x - path[index - 1].position.x) * fraction +
                          path[index - 1].position.x,
                      (path[index].position.y - path[index - 1].position.y) * fraction +
                          path[index - 1].position.y,
                      (path[index].position.z - path[index - 1].position.z) * fraction +
                          path[index - 1].position.z);
        path[index].position = path_destination;
        path_position_index = static_cast<unsigned short>(index);
        flags &= ~W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED;
        return true;
    }
    flags &= ~W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED;
    return false;
}

/* The stored route's total length, measured on first use from path_length_origin
   through every recorded position at or past the current index and cached in
   path_length under the 0x00400000 flag. Entries whose preceding path value
   carries bit 0x2 contribute nothing. */
// FUNCTION: WIZ8 0x00456B00
float W8NavigatorAttachment::MeasurePathLength()
{
    unsigned int index = 1;
    if ((flags & W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED) == 0) {
        path_length = 0;
        if (path_cursor > 0) {
            index = path_cursor;
        }
        glm::vec3 previous = path_length_origin;
        for (; index <= path_position_index; ++index) {
            if ((path[index - 1].value & 2) == 0) {
                path_length += glm::length((path[index].position - previous));
            }
            previous = path[index].position;
        }
        flags |= W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED;
    }
    return path_length;
}

/* Grow route storage, keeping positions and their values together. */
void W8NavigatorAttachment::GrowPathStorage()
{
    path.resize(path.size() + 10);
}

// FUNCTION: WIZ8 0x00451ec0
W8Navigator::W8Navigator() : reactivated(0)
{
    navigation_mode = 0;
    flags = 0;
    collision_margin = 0.0;
    movement_target = glm::vec3(0);
    movement_stopped = true;
    halted = false;
    movement_complete = true;
    position_dirty = false;
    unknown_027 = false;
    patrol_home = glm::vec3(0);
    unknown_048 = 0;
    linked_navigator = 0;
    unknown_060 = 0;
    unknown_064 = 0;
    target_navigator = 0;
    minimum = glm::vec3(-500.0f);
    maximum = glm::vec3(500.0f);
    movement.vertical_offset = 0.0f;
    active = true;
    trace_mask = 0;
    target_last_position = glm::vec3(0);
    minimum_height = 10000.0f;
    maximum_height = 20000.0f;
    movement.alternate_radius = 500.0f;
    movement.collision_radius = 500.0f;
    radius = 500.0f;
    movement.height_offset = 500.0f;
    movement.secondary_height_offset = 500.0f;
    movement.scale = 1.0f;
    movement.position_adjusted = false;
    movement_callback = NavigatorDefaultCallback;
    unknown_094 = 0;
    unknown_098 = 0;
    owned_object = 0;
    tracked_distance = 500.0f;
    group_linked = false;
    tracked_dirty = false;
    linked_update_time = 0;
    movement.Reset();
    tracked_position = glm::vec3(0);
    g_registered_navigators.push_back(this);
    /* 0x004520A4 re-zeroes the movement target, the eight-byte collision margin
       and the target link immediately before the node allocation, so the retail
       constructor initialises that group twice. */
    movement_target = glm::vec3(0);
    collision_margin = 0.0;
    target_navigator = 0;
    node = std::make_unique<srNode>(nullptr);
}

/* The callback a navigator starts with: mark it and stop it dead. */
// FUNCTION: WIZ8 0x00451ea0
void NavigatorDefaultCallback(W8Navigator* navigator)
{
    navigator->halted = true;
    navigator->movement.velocity = glm::vec3(0);
}

/* Switch every registered navigator to the requested link mode. Mode zero
   clears their stop flags and resets their path state, then re-bases any
   monster group already following a navigator onto that navigator's path and
   position; every other mode stops them and clears their velocity. */
// FUNCTION: WIZ8 0x00452F50
void SetNavigatorLinkMode(unsigned char mode)
{
    if (g_navigator_link_mode == mode) {
        return;
    }
    g_navigator_link_mode = mode;
    if (mode == 0) {
        for (auto navigator : g_registered_navigators) {
            navigator->halted = false;
            if (navigator->path_ai != 0) {
                PathAIResetTick(navigator->path_ai.get());
            }
            if (navigator->group_linked) {
                W8MonsterInfo* monster_info = FindMonster(navigator->movement.location_id);
                if (monster_info->fActive && monster_info->monster_group_id != 0) {
                    W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id);
                    glm::vec3 position = navigator->movement.position;

                    if ((!group->fInCombat || !PositionMonsterGroupNearCamera(
                                                  group, 0.0f, navigator->movement.yaw, false)) &&
                        (MoveMonsterGroupToPosition(group, &position, navigator->movement.yaw,
                                                    false, true, false, false),
                         g_combat_inactive != 0)) {
                        navigator->linked_update_time = 0;
                        g_navigator_group.clear();
                        navigator->CollectGroupNavigators(g_navigator_group);
                        for (auto group_navigator : g_navigator_group) {
                            group_navigator->movement.attachment->CopyPathFrom(
                                navigator->movement.attachment.get());
                            group_navigator->patrol_home = navigator->patrol_home;
                            group_navigator->movement.attachment->flags &=
                                ~(W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR |
                                  W8_NAV_ATTACHMENT_FOLLOW_PATH);
                            group_navigator->linked_update_time = 0;
                        }
                    }
                }
            }
        }
    } else {
        for (auto navigator : g_registered_navigators) {
            NavigatorDefaultCallback(navigator);
            navigator->group_linked = false;
        }
    }
}

// FUNCTION: WIZ8 0x00453160
void StopAllNavigators(void)
{
    int count = static_cast<int>(g_registered_navigators.size());
    for (int index = 0; index < count; ++index) {
        W8Navigator* navigator = g_registered_navigators[index];
        NavigatorDefaultCallback(navigator);
    }
}

// FUNCTION: WIZ8 0x004531a0
void ResumeAllNavigators(void)
{
    int count = static_cast<int>(g_registered_navigators.size());
    for (int index = 0; index < count; ++index) {
        W8Navigator* navigator = g_registered_navigators[index];
        navigator->halted = false;
        if (navigator->path_ai != 0) {
            PathAIResetTick(navigator->path_ai.get());
        }
        if (navigator->path_ai != 0) {
            PathAIResetTick(navigator->path_ai.get());
        }
    }
}

/* Start with ten route slots and clear per-position values. */
// FUNCTION: WIZ8 0x00456210
W8NavigatorAttachment::W8NavigatorAttachment()
{
    flags = 0;
    path_position_index = 0;
    path_cursor = 0;
    follow_offset = 0;
    path.resize(10);
    path_length = 0;
    separation = 0.0f;
    segment_start = glm::vec3(0);
    path_destination = glm::vec3(0);
}

/* The from/to attachment: a ready-made two-position route that starts at the
   first recorded position, with path_length carrying the direct segment length
   until a real path is built over it. */
// FUNCTION: WIZ8 0x00456280
W8NavigatorAttachment::W8NavigatorAttachment(const glm::vec3* from, const glm::vec3* to)
{
    flags = W8_NAV_ATTACHMENT_POSITION_RECORDED;
    path_position_index = 1;
    path_cursor = 1;
    follow_offset = 0;
    path.resize(10);
    path_length = 0;
    separation = 0.0f;
    segment_start = *from;
    path[0].position = *from;
    path_destination = *to;
    path[1].position = *to;
    path_length_origin = segment_start;
    path_length = glm::length((path_destination - segment_start));
}

/* A second pass of defaults over the same tail, run straight after the
   constructor. Where the constructor cleared the orientation, this one gives it
   a facing of three quarter turns, a ten-thousand callback threshold, a turn
   rate of a sixteenth turn, unit scale and speed, and an identity basis in the
   three vectors at +0x88. */
// FUNCTION: WIZ8 0x004573d0
void W8NavigatorMovementState::Reset()
{
    navigation_filter = 0;
    flags = 0;
    yaw_velocity = 0.0f;
    pitch = 0.0f;
    target_pitch = 0.0f;
    roll = 0.0f;
    target_roll = 0.0f;
    target_yaw = 4.712389f;
    yaw = 4.712389f;
    velocity = glm::vec3(0);
    position = glm::vec3(0);
    callback_progress = 0.0f;
    pitch_enabled = false;
    roll_enabled = false;
    vertical_velocity = 0.0f;
    callback_threshold = 10000.0f;
    turn_rate = 0.19634953f;
    boundary_enabled = true;
    target_location_id = -1;
    movement_scale = 1.0f;
    movement_speed = 1.0f;
    basis = glm::mat3(1);
}

/* The whole 0xCC tail starts cleared apart from a unit movement speed, the byte
   at +0x76, the invalidated target_location_id and the attachment, which the tail
   allocates and owns from construction rather than acquiring later. */
// FUNCTION: WIZ8 0x004572c0
W8NavigatorMovementState::W8NavigatorMovementState()
{
    navigation_filter = 0;
    location_id = 0;
    leadership_rank = 0;
    active_rank = 0;
    yaw = 0.0f;
    target_yaw = 0.0f;
    yaw_velocity = 0.0f;
    pitch = 0.0f;
    target_pitch = 0.0f;
    roll = 0.0f;
    target_roll = 0.0f;
    unknown_030 = 0.0f;
    velocity = glm::vec3(0);
    position = glm::vec3(0);
    target_position = glm::vec3(0);
    callback_threshold = 0.0f;
    callback_progress = 0.0f;
    movement_scale = 0.0f;
    movement_speed = 1.0f;
    turn_rate = 0.0f;
    pitch_enabled = false;
    roll_enabled = false;
    boundary_enabled = true;
    vertical_velocity = 0.0f;
    target_location_id = -1;
    vertical_base = 0.0f;
    vertical_amplitude = 0.0f;
    vertical_phase = 0.0f;
    basis = glm::mat3(0);
    attachment = std::make_unique<W8NavigatorAttachment>();
    flags = 0;
}

/* Only these eleven fields survive a transfer between navigators; everything
   else in the tail stays whatever the destination already had, and target_location_id is
   invalidated rather than copied. */
// FUNCTION: WIZ8 0x004574d0
void W8NavigatorMovementState::CopySettingsFrom(const W8NavigatorMovementState& other)
{
    navigation_filter = other.navigation_filter;
    leadership_rank = other.leadership_rank;
    callback_threshold = other.callback_threshold;
    callback_progress = other.callback_progress;
    movement_scale = other.movement_scale;
    turn_rate = other.turn_rate;
    pitch_enabled = other.pitch_enabled;
    roll_enabled = other.roll_enabled;
    boundary_enabled = other.boundary_enabled;
    vertical_base = other.vertical_base;
    vertical_amplitude = other.vertical_amplitude;
    target_location_id = -1;
}

/* Owning members release the route attachment. */
// FUNCTION: WIZ8 0x00457530
W8NavigatorMovementState::~W8NavigatorMovementState() = default;

/* A copy shares nothing that ties it to the original's navigation. The path is
   DISCARDED rather than cloned or shared, both navigator links are cleared, and
   the owned object is not carried over. The movement tail is default
   constructed, then six trailing radius/height/offset/scale fields are copied
   directly and CopySettingsFrom transfers its separate eleven-field settings
   subset. The outer navigator also carries across its height band, bounding
   corners, radius and callback. The copy allocates its own scene node and
   registers itself, so it is a live navigator from birth. */
// FUNCTION: WIZ8 0x00452220
W8Navigator::W8Navigator(const W8Navigator& other)
    : flags(0), collision_margin(0.0), movement_target(0.0f, 0.0f, 0.0f), movement_stopped(true),
      halted(false), movement_complete(true), unknown_027(0), minimum_height(other.minimum_height),
      maximum_height(other.maximum_height), patrol_home(other.patrol_home), unknown_048(0),
      target_navigator(0), target_last_position(0.0f, 0.0f, 0.0f), linked_navigator(0),
      unknown_060(0), unknown_064(0), path_ai(nullptr), radius(other.radius), active(true),
      movement_callback(other.movement_callback), trace_mask(other.trace_mask),
      unknown_094(other.unknown_094), unknown_098(0), reactivated(other.reactivated),
      owned_object(nullptr), tracked_distance(other.tracked_distance),
      group_linked(other.group_linked)
{
    movement.collision_radius = other.movement.collision_radius;
    movement.alternate_radius = other.movement.alternate_radius;
    configureStartupDepth(other.movement.height_offset, other.movement.secondary_height_offset);
    movement.vertical_offset = other.movement.vertical_offset;
    movement.scale = other.movement.scale;
    minimum = other.minimum;
    maximum = other.maximum;
    g_runtime_world_scale = std::max(
        {g_runtime_world_scale, movement.collision_radius, movement.alternate_radius, radius});
    movement.CopySettingsFrom(other.movement);
    movement.height_offset -= other.movement.vertical_base;
    movement.secondary_height_offset -= other.movement.vertical_base;
    SetNavigationMode(other.navigation_mode);
    position_dirty = false;
    movement.position_adjusted = false;
    tracked_dirty = false;
    linked_update_time = 0;
    tracked_position = glm::vec3(0);
    node = std::make_unique<srNode>(nullptr);
    node->setLocation(other.node->getLocation());
    g_registered_navigators.push_back(this);
}

/* Everything this navigator owns, in the order the retail body releases it.
   The path goes through the kind-guarded PathAI helper rather than the general
   one; the optional polymorphic object and scene node have exclusive owners,
   and the movement tail owns its attachment.
   Leaving the world's location index is a notification, not a release. */
// FUNCTION: WIZ8 0x00452120
W8Navigator::~W8Navigator()
{
    wiz8::erase_first(g_registered_navigators, this);
    owned_object.reset();
    if (movement.location_id != 0 && GetWorldOctree() != 0) {
        GetWorldOctree()->UnregisterLocationObject(movement.location_id, W8_OCTREE_KIND_NAVIGATOR);
    }
    node.reset();
}

// FUNCTION: WIZ8 0x00452E10
bool W8Navigator::IsLinkedToNavigator(W8Navigator* other)
{
    W8Navigator* linked;

    linked = linked_navigator;
    if (linked == 0) {
        return other->linked_navigator == this;
    }
    if (other != linked && other->linked_navigator != linked) {
        return false;
    }
    return true;
}

/* Six of the seven modes give the navigator a fresh path record; mode four only
   resets the two movement enables. What the modes actually differ in is that
   pair: mode one and four clear both, two, three and five enable pitch, and six
   enables pitch and roll. */
// FUNCTION: WIZ8 0x00452e50
void W8Navigator::SetNavigationMode(int mode)
{
    std::unique_ptr<W8PathAI> path;

    navigation_mode = mode;
    switch (mode) {
    case 1:
        path = std::make_unique<W8PathAI>();
        path->animated = true;
        SetPathAI(std::move(path));
        /* Falls into mode four's body: the retail block ends where mode four's
           jump-table entry lands. */
        [[fallthrough]];
    case 4:
        SetPitchRollEnabled(false, false);
        break;
    case 2:
    case 3:
    case 5:
        path = std::make_unique<W8PathAI>();
        path->animated = true;
        SetPathAI(std::move(path));
        SetPitchRollEnabled(true, false);
        break;
    case 6:
        path = std::make_unique<W8PathAI>();
        path->animated = true;
        SetPathAI(std::move(path));
        SetPitchRollEnabled(true, true);
        break;
    default:
        break;
    }
}

/* Replace the collision bounds and keep the broad-phase radius synchronized
   with the movement state's alternate radius. */
// FUNCTION: WIZ8 0x00452f10
void W8Navigator::SetBounds(const srBoundingBox& bounds)
{
    minimum = bounds.minimum;
    maximum = bounds.maximum;
    radius = movement.alternate_radius;
}

// FUNCTION: WIZ8 0x004534c0
glm::vec3 W8Navigator::GetPosition()
{
    return movement.position;
}

// FUNCTION: WIZ8 0x004534f0
glm::vec3 W8Navigator::GetVelocity() const
{
    return movement.velocity;
}

// FUNCTION: WIZ8 0x00454950
bool W8Navigator::UpdateTrackedPosition()
{
    float distance = glm::length((tracked_position - movement.position));

    if (distance > tracked_distance) {
        tracked_position = movement.position;
        tracked_dirty = true;
    }
    return tracked_dirty;
}

// FUNCTION: WIZ8 0x00453880
void W8Navigator::SetMovementStopped()
{
    if (!movement_stopped) {
        movement_stopped = true;
        if (navigation_mode != 5 && navigation_mode != 6) {
            movement.target_pitch = NormalizeAngle(0.0f);
        }
    }
}

/* Clear the stop flag and, outside combat, clear it for the collected
   unlinked group too. This operation repeats through movement setup/loading
   and propagation; ClearMovementStopped is a descriptive recovered name. */
void W8Navigator::ClearMovementStopped()
{
    movement_stopped = false;
    if (g_combat_inactive != 0 && linked_navigator == 0) {
        g_navigator_group.clear();
        CollectGroupNavigators(g_navigator_group);
        for (auto& index : g_navigator_group) {
            index->movement_stopped = false;
        }
    }
}

void W8Navigator::CopyPathToGroup()
{
    linked_update_time = 0;
    g_navigator_group.clear();
    CollectGroupNavigators(g_navigator_group);
    for (W8Navigator* navigator : g_navigator_group) {
        navigator->movement.attachment->CopyPathFrom(movement.attachment.get());
        navigator->patrol_home = patrol_home;
        navigator->movement.attachment->flags &=
            ~(W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR | W8_NAV_ATTACHMENT_FOLLOW_PATH);
        navigator->linked_update_time = 0;
    }
}

/* Push this navigator's path and position onto every navigator in its group,
   and let the whole group move again if this one may. */
// FUNCTION: WIZ8 0x00454c80
void W8Navigator::PropagateGroupPosition()
{
    if (g_combat_inactive != 0) {
        CopyPathToGroup();
    }
    if (!movement_stopped) {
        /* Retail writes the zero the caller already proved; the store is dead
           but faithful. */
        ClearMovementStopped();
    }
}

// FUNCTION: WIZ8 0x004538d0
void W8Navigator::SetTargetYaw(float angle)
{
    movement.target_yaw = NormalizeAngle(angle);
}

// FUNCTION: WIZ8 0x004538f0
void W8Navigator::SetAngles(float angle)
{
    movement.yaw = NormalizeAngle(angle);
    movement.target_yaw = NormalizeAngle(angle);
}

// FUNCTION: WIZ8 0x00453920
void W8Navigator::SetTargetPitch(float angle)
{
    movement.target_pitch = NormalizeAngle(angle);
}

// FUNCTION: WIZ8 0x00453940
void W8Navigator::SetPitch(float pitch)
{
    movement.pitch = NormalizeAngle(pitch);
    movement.target_pitch = NormalizeAngle(pitch);
}

// FUNCTION: WIZ8 0x00453970
float W8Navigator::GetYaw()
{
    return movement.yaw;
}

// FUNCTION: WIZ8 0x00453980
float W8Navigator::GetPitch()
{
    return movement.pitch;
}

// FUNCTION: WIZ8 0x00453c90
void W8Navigator::SetTurnRate(float turn_rate)
{
    movement.turn_rate = turn_rate;
}

// FUNCTION: WIZ8 0x00453ef0
void W8Navigator::SetHeightRange(float minimum, float maximum)
{
    if (minimum >= 0.0f) {
        minimum_height = minimum;
    }
    if (maximum >= 0.0f) {
        maximum_height = maximum;
    }
}

// FUNCTION: WIZ8 0x004538b0
void W8Navigator::SetPathAI(std::unique_ptr<W8PathAI> path)
{
    path_ai = std::move(path);
}

// FUNCTION: WIZ8 0x004538c0
W8PathAI* W8Navigator::GetPathAI()
{
    return path_ai.get();
}

// FUNCTION: WIZ8 0x00454930
void W8Navigator::ResetPathAI()
{
    if (path_ai != 0) {
        path_ai->position = 0.0f;
        path_ai->interpolation_fraction = 0.0f;
        PathAIResetTick(path_ai.get());
    }
}

// GLOBAL: WIZ8 0x005ec2f4
const float g_navigator_default_turn_rate = 4.398229598999023f;

// GLOBAL: WIZ8 0x005ec2f0
const float g_navigator_snap_angle = 0.029999999329447746f;
// GLOBAL: WIZ8 0x006081e4
unsigned char g_combat_inactive = 1;

// FUNCTION: WIZ8 0x004526c0
unsigned short W8Navigator::SetMovementTargetToNavigator(W8Navigator* target, double separation)
{
    unsigned short result = 0;

    movement_target = glm::vec3(0);
    collision_margin = separation;
    target_navigator = target;
    if (target == PartyNavigator() || target->movement.location_id != 0) {
        movement.target_location_id = target->movement.location_id;
    } else {
        movement.target_location_id = -1;
    }
    PathAIClearOwned(path_ai.get());
    if (g_combat_inactive == 0) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
    }
    if (SetMovementTarget(&target->movement.position, false) == 0) {
        if (g_combat_inactive == 0) {
            flags = 0;
        }
    } else {
        flags = 5;
        result = 1;
        if (g_combat_inactive == 0) {
            result = static_cast<unsigned short>(movement.attachment->flags & 7);
        }
    }
    target_last_position = target->movement.position;
    return result;
}

// FUNCTION: WIZ8 0x00453cc0
bool W8Navigator::StartPatrol(const glm::vec3* home, float distance, float variation)
{
    W8Navigator* navigator = this;

    while (navigator->linked_navigator != 0) {
        navigator = navigator->linked_navigator;
    }
    navigator->flags = 0;
    navigator->patrol_home = *home;
    navigator->minimum_height = distance;
    navigator->maximum_height = variation;
    return navigator->ConfigureMovement(distance, variation);
}
// GLOBAL: WIZ8 0x00659c10
unsigned char g_navigator_link_mode;
// GLOBAL: WIZ8 0x005ebc98
const float g_navigator_linked_radius_scale = 4.0f;
// GLOBAL: WIZ8 0x005ebcc8
const float g_navigator_vertical_phase_step = 0.25f;
// GLOBAL: WIZ8 0x006081ec
float g_navigator_minimum_speed = 0.5f;
// GLOBAL: WIZ8 0x006081f0
float g_navigator_minimum_speed_mode23 = 0.8999999761581421f;
/* Runtime scale for the camera-sphere radius in the octree trace resolver;
   the retail image carries link-time 1.0 here. */
// GLOBAL: WIZ8 0x006081f4
float g_camera_sphere_radius_scale = 1.0f;
// GLOBAL: WIZ8 0x00659bf8
std::vector<W8Navigator*> g_navigator_group;

// GLOBAL: WIZ8 0x005ec314
static const float NAVIGATOR_THREE_QUARTER_TURN = 4.712389f;
// GLOBAL: WIZ8 0x005ec310
static const float NAVIGATOR_MAXIMUM_DROP = 1500.0f;
// GLOBAL: WIZ8 0x00603acc
float g_navigator_gravity = 187.5f;
// GLOBAL: WIZ8 0x005ec320
static const float NAVIGATOR_LINK_DISTANCE_SQUARED = 400.0f;

// FUNCTION: WIZ8 0x00454d70
void W8Navigator::UpdateLinkedNavigator()
{
    if (linked_navigator == 0) {
        return;
    }
    glm::vec3 own_position = movement.position;
    glm::vec3 linked_position = linked_navigator->movement.position;
    glm::vec3 delta = linked_position - own_position;
    if (glm::dot(delta, delta) <= radius * radius * NAVIGATOR_LINK_DISTANCE_SQUARED) {
        linked_update_time = 0;
        return;
    }
    if (static_cast<W8Monster*>(this)->IsWithinWorldRange() == 0 &&
        static_cast<W8Monster*>(linked_navigator)->IsWithinWorldRange() == 0) {
        UpdateLinkedPosition();
        return;
    }
    int tick = static_cast<int>(GameTimeAccumulator().GetElapsed());
    if (static_cast<unsigned int>(tick - linked_update_time) <= 50) {
        return;
    }
    linked_update_time = tick;
    glm::vec3 camera = cameraLocation();
    if (static_cast<W8Monster*>(linked_navigator)->IsWithinWorldRange() != 0) {
        const auto bounds =
            MonsterGetWorldAnimationBounds(static_cast<W8Monster*>(linked_navigator));
        if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
            goto follow_path;
        }
    }
    if (static_cast<W8Monster*>(this)->IsWithinWorldRange() != 0) {
        const auto bounds = MonsterGetWorldAnimationBounds(static_cast<W8Monster*>(this));
        if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
            goto follow_path;
        }
    }
    if (UpdateLinkedPosition()) {
        return;
    }
follow_path:
    if (GetWorldOctree()->pathing->PrepareLinkedNavigator(&movement)) {
        if (movement_stopped) {
            ClearMovementStopped();
        }
    } else {
        SetMovementStopped();
        AimAtPosition(&camera);
    }
}

// FUNCTION: WIZ8 0x00454fe0
bool W8Navigator::UpdateLinkedPosition()
{
    if (linked_navigator == 0) {
        return false;
    }
    glm::vec3 position;
    if (linked_navigator->movement_stopped) {
        const auto placement = GetWorldOctree()->navigatorPositions(
            linked_navigator->movement.position, linked_navigator->movement.yaw,
            movement.collision_radius + movement.collision_radius, 1, true, false, false, 5, true);
        linked_navigator->movement.position = placement.source;
        if (placement.positions.empty()) return false;
        position = placement.positions.front();
        SetMovementStopped();
    } else {
        position = linked_navigator->movement.position;
        if (movement_stopped) {
            movement_stopped = false;
        }
    }
    movement.attachment->CopyPathFrom(linked_navigator->movement.attachment.get());
    movement.attachment->flags &= ~W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
    movement.yaw = linked_navigator->movement.yaw;
    movement.velocity = linked_navigator->movement.velocity * 0.5f;
    SetPosition(&position);
    GetWorldOctree()->RegisterNavigatorCell(movement.location_id, &position);
    linked_update_time = 0;
    return true;
}

// FUNCTION: WIZ8 0x00454440
glm::vec3* W8Navigator::AdjustPosition(glm::vec3* result, const glm::vec3* current,
                                       const glm::vec3* previous, W8Octree& octree)
{
    float acceleration_scale = 0.25f;
    if (movement.location_id == 0) {
        *result = *current;
        return result;
    }
    /* 0x0045445D skips the whole settle-and-drop body when navigation_mode is
       4 and falls into the shared stop path below, so the mode test guards the
       body rather than sharing the location-id early return. */
    if (navigation_mode != 4) {
        glm::vec3 probe = *current;
        probe.y += kWorldScale;
        bool hit;
        const auto ground_probe = octree.settleToGround(probe, 1, 500.0f);
        probe = ground_probe.position;
        hit = ground_probe.hit;
        float ground = ground_probe.position.y;
        if (hit) {
            if (octree.current_prop < 0) {
                movement.position_adjusted = false;
            } else {
                movement.position_adjusted = true;
                acceleration_scale = 0.5f;
            }
            if (current->y - ground <= NAVIGATOR_MAXIMUM_DROP ||
                (previous->x == probe.x && previous->z == probe.z)) {
                if (kStartupNearLimit < current->y - ground) {
                    glm::vec3 falling = *current;
                    float distance;
                    if (g_combat_inactive == 0) {
                        movement.vertical_velocity += GameTimeAccumulator().GetFrameDelta() *
                                                      g_settings.monster_movement_speed *
                                                      g_navigator_gravity * acceleration_scale;
                        distance = movement.vertical_velocity *
                                   GameTimeAccumulator().GetFrameDelta() *
                                   g_settings.monster_movement_speed;
                    } else {
                        movement.vertical_velocity += GameTimeAccumulator().GetFrameDelta() *
                                                      g_navigator_gravity * acceleration_scale;
                        distance =
                            movement.vertical_velocity * GameTimeAccumulator().GetFrameDelta();
                    }
                    falling.y -= distance;
                    if (falling.y >= ground) {
                        position_dirty = true;
                        *result = falling;
                        return result;
                    }
                }
                movement.vertical_velocity = 0.0f;
                position_dirty = false;
            } else {
                movement.velocity = glm::vec3(0);
                if (g_combat_inactive == 0) {
                    ClearMovement();
                }
                *result = *previous;
                return result;
            }
            probe.y = ground;
            *result = probe;
            return result;
        }
    }
    movement.velocity = glm::vec3(0);
    if (g_combat_inactive == 0) {
        ClearMovement();
    }
    *result = *previous;
    return result;
}

// FUNCTION: WIZ8 0x00454780
void W8Navigator::UpdateFacing(bool immediate)
{
    if (!movement.pitch_enabled && !movement.roll_enabled) {
        return;
    }
    glm::vec3 forward(0.0f, 0.0f, 1.0f);
    glm::vec3 normal;
    forward = glm::vec3(
        glm::angleAxis(std::atan2(sin(movement.yaw), cos(movement.yaw)), glm::dvec3(0, 1, 0)) *
        glm::dvec3(forward));
    GetWorldOctree()->GetPathSurfaceNormal(&movement.position, &normal);
    if (movement.pitch_enabled) {
        float angle = static_cast<float>(acos(glm::dot(normal, forward)));
        if (angle < kQuarterTurn) {
            angle += NAVIGATOR_THREE_QUARTER_TURN;
        } else {
            angle -= kQuarterTurn;
        }
        if (immediate) {
            movement.pitch = NormalizeAngle(angle);
        }
        movement.target_pitch = NormalizeAngle(angle);
    }
    if (movement.roll_enabled) {
        glm::vec3 side(-forward.z, 0.0f, forward.x);
        float angle = static_cast<float>(acos(glm::dot(side, normal)));
        if (angle < kQuarterTurn) {
            angle += NAVIGATOR_THREE_QUARTER_TURN;
        } else {
            angle -= kQuarterTurn;
        }
        if (immediate) {
            movement.roll = NormalizeAngle(angle);
        }
        movement.target_roll = NormalizeAngle(angle);
    }
}

// FUNCTION: WIZ8 0x00453230
W8Navigator* W8Navigator::ResolveBlockingNavigator(const glm::vec3* from, glm::vec3* to,
                                                   bool include_target)
{
    int hit_location;
    int location;

    if (!active) {
        return 0;
    }
    if (target_navigator == 0) {
        hit_location = -3;
    } else {
        hit_location = target_navigator->movement.location_id;
    }
    location = -3;
    if (include_target && target_navigator != 0) {
        location = target_navigator->movement.location_id;
    }
    if (const auto hit = GetWorldOctree()->traceSphereHit(
            *from, *to, movement.location_id, hit_location, location, trace_mask, false)) {
        *to = hit->position;
        hit_location = hit->location_id;
        if (hit_location == 0) {
            return PartyNavigator();
        }
        if (hit_location > 0) {
            W8MonsterInfo* monster_info = FindMonster(hit_location);
            if (monster_info->p3D != 0) {
                return monster_info->p3D;
            }
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x00453300
double W8Navigator::MeasurePathDistance(const glm::vec3* target, float max_range, int location_id)
{
    static W8NavigatorMovementState movement;
    auto& attachment = *movement.attachment;
    unsigned char ready;

    movement.CopySettingsFrom(this->movement);
    movement.target_location_id = location_id;
    movement.basis = this->movement.basis;
    movement.position = this->movement.position;
    movement.target_position = *target;
    attachment.InitializeSegment(&this->movement.position, target);
    if (g_combat_inactive == 0) {
        attachment.flags |= 0xc010000;
    } else {
        attachment.flags &= 0xf3feffff;
    }
    ready = GetWorldOctree()->PrepareNavigatorTarget(&movement, max_range, radius);
    if (ready != 0 &&
        ((attachment.flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0 || (attachment.flags & 7) != 3)) {
        return attachment.MeasurePathLength();
    }
    return -1.0;
}

// FUNCTION: WIZ8 0x00453480
int W8Navigator::FindNavigatorPathDistance(float max_range, float* out_distance)
{
    double distance = MeasurePathDistance(&PartyNavigator()->movement.position, max_range, 0);

    *out_distance = static_cast<float>(distance);
    if (distance != g_negative_one) {
        return 1;
    }
    return 0;
}

// FUNCTION: WIZ8 0x00453520
void W8Navigator::SetVelocity(const glm::vec3* velocity)
{
    movement.velocity = *velocity;
}

// FUNCTION: WIZ8 0x00453540
bool W8Navigator::CheckNavigatorCollision(const glm::vec3* from, const glm::vec3* to)
{
    W8Navigator* blocker = ResolveBlockingNavigator(from, const_cast<glm::vec3*>(to), false);

    if (blocker != 0 && OnCollision(blocker) && blocker->OnCollision(this)) {
        return true;
    }
    return false;
}
/* Re-prime the attachment for a new movement segment: keep only the link flag
   bits, reseed the path cursors at one, copy source and destination into the
   position fields and the first two waypoints, clear the recorded path values
   and store the segment length. */
// FUNCTION: WIZ8 0x004563e0
void W8NavigatorAttachment::InitializeSegment(const glm::vec3* source, const glm::vec3* destination)
{
    flags = (flags & 0xc810000) | W8_NAV_ATTACHMENT_POSITION_RECORDED;
    path_position_index = 1;
    path_cursor = 1;
    path_length = 0;
    separation = 0;
    segment_start = *source;
    path[0].position = *source;
    path_destination = *destination;
    path[1].position = *destination;
    path_length_origin = segment_start;
    follow_offset = 0;
    for (auto& entry : path) entry.value = 0;
    path_length = glm::length((path_destination - segment_start));
}

// FUNCTION: WIZ8 0x004564f0
void W8NavigatorAttachment::CopyPathFrom(const W8NavigatorAttachment* other)
{
    flags = other->flags;
    path_cursor = other->path_cursor;
    path_position_index = other->path_position_index;
    follow_offset = other->follow_offset;
    path_length = other->path_length;
    separation = other->separation;
    segment_start = other->segment_start;
    path_length_origin = other->segment_start;
    path_destination = other->path_destination;
    if ((flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        start_waypoint = other->start_waypoint;
    }
    if (path_position_index + 1 >= path.size()) {
        path.resize((path_position_index / 10 + 1) * 10);
    }
    for (unsigned int index = 1; index <= path_position_index; ++index) {
        path[index].position = other->path[index].position;
        path[index].value = other->path[index].value;
    }
}

// FUNCTION: WIZ8 0x00456660
glm::vec3 W8NavigatorAttachment::GetNextPosition() const
{
    if ((flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        return start_waypoint;
    }
    if (path_cursor < path_position_index) {
        return path[path_cursor].position;
    } else {
        return path_destination;
    }
}

// FUNCTION: WIZ8 0x00456830
bool W8NavigatorAttachment::AdvanceAlongPathPositions(float distance, glm::vec3* position)
{
    glm::vec3 local;
    glm::vec3 delta;
    float segment;
    float t;
    bool on_path;

    local = *position;
    on_path = true;
    /* Retail evaluates both halves of this predicate and writes one either
       way; the check is dead in the recovered body exactly as shipped. */
    if ((flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0 && distance > NAVIGATOR_MAXIMUM_DROP) {
        on_path = true;
    }
    flags &= ~W8_NAV_ATTACHMENT_PATH_LENGTH_CACHED;
    delta = path[path_cursor].position - local;
    segment = glm::length(((glm::vec2(delta.x, delta.z))));
    if (segment < 1.0 && path_cursor == path_position_index) {
        return false;
    }
    if (segment < distance) {
        do {
            if (path_position_index < path_cursor) {
                break;
            }
            distance -= segment;
            local = path[path_cursor].position;
            ++path_cursor;
            /* Retail reads path[path_cursor].position before the loop head re-tests
               the cursor: when the consumed waypoint was the last one this
               samples one slot past path_position_index, inside the
               ten-entry allocation but never initialized. */
            delta = path[path_cursor].position - local;
            segment = glm::length(((glm::vec2(delta.x, delta.z))));
        } while (segment < distance);
    }
    if (path_position_index < path_cursor) {
        *position = path_destination;
        path_cursor = path_position_index;
        on_path = false;
    } else {
        t = distance / segment;
        *position = local * static_cast<float>(1.0 - t) + path[path_cursor].position * t;
    }
    if (path_cursor > 1) {
        for (position_cursor = 0; path_cursor + position_cursor <= path_position_index;
             ++position_cursor) {
            path[position_cursor + 1].position = path[position_cursor + path_cursor].position;
        }
        path_position_index += 1 - path_cursor;
        path_cursor = 1;
    }
    path[0].position = *position;
    segment_start = path[0].position;
    path_length_origin = path[0].position;
    return on_path;
}

bool W8NavigatorAttachment::withinHopHeight(const glm::vec2& point, unsigned base,
                                            bool include_next) const
{
    auto projection =
        srSegmentMath::project(point, glm::vec2(path[base].position.x, path[base].position.z),
                               glm::vec2(path[base + 1].position.x, path[base + 1].position.z));
    if (include_next && path_cursor < path_position_index && path[path_cursor + 1].value != 0) {
        const auto next = srSegmentMath::project(
            point, glm::vec2(path[path_cursor].position.x, path[path_cursor].position.z),
            glm::vec2(path[path_cursor + 1].position.x, path[path_cursor + 1].position.z));
        if (next.distance < projection.distance) {
            base = path_cursor;
            projection = next;
        }
    }
    const auto* surfaces = GetWorldOctree()->pathing->m_waypoints.data();
    float height = (surfaces[path[base].value].flags >> 0xc) * kWorldScale;
    const float next_height = (surfaces[path[base + 1].value].flags >> 0xc) * kWorldScale;
    if (height != next_height) height = (next_height - height) * projection.fraction + height;
    return projection.distance <= height;
}

// FUNCTION: WIZ8 0x00456CB0
bool W8NavigatorAttachment::checkPositionHopHeight(const glm::vec3& position) const
{
    const unsigned base = path_cursor == 1 ? 1 : path_cursor - 1;
    if (path_position_index < base + 1) return false;
    return withinHopHeight({position.x, position.z}, base, false);
}

// FUNCTION: WIZ8 0x00456DD0
bool W8NavigatorAttachment::checkPredictedHopHeight(const glm::vec3& position) const
{
    return withinHopHeight({position.x, position.z}, path_cursor - 1, true);
}

// FUNCTION: WIZ8 0x00456F60
bool W8NavigatorAttachment::AdvancePositionTowardWaypoint(glm::vec3* position, float distance)
{
    glm::vec2 point;
    glm::vec2 from;
    glm::vec2 to;
    glm::vec2 dir;
    float fraction;
    float remainder;
    float segment;
    bool reached;

    point = glm::vec2((*position).x, (*position).z);
    from = glm::vec2(path[path_cursor - 1].position.x, path[path_cursor - 1].position.z);
    to = glm::vec2(path[path_cursor].position.x, path[path_cursor].position.z);
    const auto projection = srSegmentMath::project(point, from, to);
    point = projection.point;
    fraction = projection.fraction;
    dir.x = to.x - from.x;
    dir.y = to.y - from.y;
    remainder = (1.0f - fraction) * glm::length(dir);
    if (distance <= remainder) {
        reached = false;
    } else {
        reached = true;
        if (path_cursor < path_position_index && path[path_cursor + 1].value != 0) {
            distance -= remainder;
            point = to;
            dir.x = path[path_cursor + 1].position.x - to.x;
            dir.y = path[path_cursor + 1].position.z - to.y;
            remainder = glm::length(dir);
        }
    }
    if (remainder < distance) {
        distance = remainder;
    }
    if (distance <= 0.0f) {
        position->x = point.x;
        position->z = point.y;
        return reached;
    }
    segment = glm::length(dir);
    if (segment != 0.0) {
        distance /= segment;
        dir.x *= distance;
        dir.y *= distance;
    }
    position->x = dir.x + point.x;
    position->z = dir.y + point.y;
    return reached;
}

/* Advances `position` along the recorded route by `distance`, writing the unit
   direction toward the current waypoint into `direction`; one once the final
   waypoint is reached. */
// FUNCTION: WIZ8 0x00457150
bool W8NavigatorAttachment::AdvancePositionWithDirection(glm::vec3* position, float distance,
                                                         glm::vec3* direction)
{
    while (path_cursor <= path_position_index) {
        if (distance <= 0.0) {
            break;
        }
        glm::vec3* waypoint = &path[path_cursor].position;
        *direction = *waypoint - *position;
        float length = glm::length((*direction));
        if (0.0 < length) {
            float scale = static_cast<float>(1.0 / length);
            *direction *= scale;
        }
        if (length <= distance) {
            *position = *waypoint;
            distance -= length;
            if (path_cursor == path_position_index) {
                return true;
            }
            ++path_cursor;
        } else {
            position->x = direction->x * distance + position->x;
            position->y = direction->y * distance + position->y;
            position->z = direction->z * distance + position->z;
            distance = static_cast<float>(0.0);
        }
    }
    return false;
}

// FUNCTION: WIZ8 0x00452560
void W8Navigator::SetScale(float scale)
{
    float ratio = scale / movement.scale;
    movement.scale = scale;
    radius *= ratio;
    movement.collision_radius *= ratio;
    movement.alternate_radius *= ratio;
    movement.height_offset *= ratio;
    movement.secondary_height_offset *= ratio;
    movement.movement_scale *= ratio;
    g_runtime_world_scale = std::max(
        {g_runtime_world_scale, movement.collision_radius, movement.alternate_radius, radius});
}

// FUNCTION: WIZ8 0x00452630
unsigned char W8Navigator::ConfigureMovementToPosition(const glm::vec3* position)
{
    flags = 6;
    movement.target_location_id = -1;
    movement_target = glm::vec3(0);
    collision_margin = 0.0;
    target_navigator = 0;
    if (movement_stopped) {
        PathAIClearOwned(path_ai.get());
    }
    glm::vec3 target = *position;
    flags = 6;
    movement.target_position = target;
    movement_target = target;
    if (movement.active_rank == 0) {
        movement.active_rank = movement.leadership_rank;
    }
    return SetMovementTarget(&movement_target, false);
}

// FUNCTION: WIZ8 0x004531f0
void W8Navigator::SetHalted(bool value)
{
    halted = value;
    if (!value) {
        if (path_ai != 0) {
            PathAIResetTick(path_ai.get());
        }
    } else {
        movement.velocity = glm::vec3(0);
    }
}

// FUNCTION: WIZ8 0x00453ca0
void W8Navigator::SetPitchRollEnabled(bool pitch, bool roll)
{
    movement.pitch_enabled = pitch;
    movement.roll_enabled = roll;
}

/* 0x004527AD is a split address inside this body: it resumes at the
   GetWorldPathing() nonnull branch with the call registers still live, not a
   separate authored function. */
// FUNCTION: WIZ8 0x004527a0
unsigned char W8Navigator::LinkToNavigator(W8Navigator* target, double separation)
{
    unsigned char result = 0;
    if (GetWorldPathing() == 0) {
        return 0;
    }
    movement_target = glm::vec3(0);
    collision_margin = separation;
    flags = 0;
    target_navigator = target;
    movement.target_location_id = target->movement.location_id;
    PathAIClearOwned(path_ai.get());
    movement.attachment->InitializeSegment(&movement.position, &target->movement.position);
    if (g_combat_inactive == 0) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
        if (GetWorldPathing()->PlanMovementToPosition(&movement, &target->movement.position, radius,
                                                      static_cast<float>(separation)) == 0) {
            return 0;
        }
        flags = 9;
        target_last_position = target->movement.position;
        result = static_cast<unsigned char>(movement.attachment->flags & 7);
        ClearMovementStopped();
    } else if (GetWorldOctree()->LinkNavigatorTarget(&movement, &target->movement.position,
                                                     static_cast<float>(separation)) != 0) {
        flags = 9;
        target_last_position = target->movement.position;
        ClearMovementStopped();
        result = 1;
    }
    return result;
}

// FUNCTION: WIZ8 0x004529a0
unsigned short W8Navigator::ConfigureMovementToNavigator(W8Navigator* target, float separation,
                                                         float maximum_distance, glm::vec3 position,
                                                         int trace_mode, float facing,
                                                         unsigned char* probe_result)
{
    if (GetWorldPathing() == 0) {
        return 0;
    }
    if (g_combat_inactive != 0) {
        return SetMovementTargetToNavigator(target, separation);
    }
    movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
    movement_target = glm::vec3(0);
    collision_margin = separation;
    flags = 0x21;
    target_navigator = target;
    movement.target_location_id = target->movement.location_id;
    movement.target_position = target->movement.position;
    PathAIClearOwned(path_ai.get());
    radius = movement.alternate_radius;
    unsigned short result = GetWorldPathing()->ConfigureMovementSearch(
        &movement, target->movement.location_id, radius, separation, maximum_distance, position,
        trace_mode, target->movement.height_offset, facing, probe_result);
    target_last_position = target->movement.position;
    if (result == 0) {
        ClearMovement();
        flags = 0;
        return 0;
    }
    if (movement_stopped) {
        movement_complete = false;
        ClearMovementStopped();
    }
    return result;
}

// FUNCTION: WIZ8 0x00452bd0
void W8Navigator::LinkGroupNavigator(W8Navigator* target, double, int)
{
    if (g_combat_inactive != 0) {
        movement_target = glm::vec3(0);
        collision_margin = 0.0;
        target_navigator = 0;
    }
    linked_navigator = target;
    if (target == 0) {
        if (g_combat_inactive != 0) {
            flags &= 0xfffffdfe;
            if (flags == 0) {
                movement.attachment->InitializeSegment(&movement.position, &movement.position);
                SetMovementStopped();
            }
        }
    } else {
        movement.flags |= 2;
        if (g_combat_inactive != 0) {
            flags = (flags & 0xff000201) | 0x201;
            movement.attachment->InitializeSegment(&movement.position, &target->movement.position);
        }
    }
}

// FUNCTION: WIZ8 0x00452c90
void W8Navigator::ResetMovementAndGroupState()
{
    movement_target = glm::vec3(0);
    W8Navigator* linked = linked_navigator;
    collision_margin = 0.0;
    target_navigator = 0;
    if (linked != 0) {
        movement.flags |= 2;
        if (g_combat_inactive == 0) {
            return;
        }
        flags = 0x201;
    } else {
        if (g_combat_inactive == 0) {
            return;
        }
        flags &= 0xfffffdfe;
        if (flags == 0) {
            movement.attachment->InitializeSegment(&movement.position, &movement.position);
            SetMovementStopped();
        }
        PropagateGroupPosition();
    }
    if (g_combat_inactive != 0) {
        movement.attachment->flags &= ~W8_NAV_ATTACHMENT_FOLLOW_PATH;
    }
}

// FUNCTION: WIZ8 0x00453d20
bool W8Navigator::ConfigureMovement(float minimum, float maximum)
{
    flags |= 0x20000000;
    movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
    if (minimum > 0.0f) {
        minimum_height = minimum;
    }
    if (maximum > 5e+03f) {
        maximum_height = maximum;
    }
    if (minimum_height + 5e+03f < maximum_height) {
        movement.target_position = patrol_home;
        if (GetWorldOctree()->PrepareNavigatorPatrol(&movement, minimum_height, maximum_height) !=
            0) {
            flags |= 6;
            ClearMovementStopped();
            halted = false;
            movement_target = movement.attachment->path_destination;
            movement.attachment->separation = 0.0f;
            if (g_combat_inactive != 0) {
                CopyPathToGroup();
            }
            return true;
        }
        flags &= 0xdffffff9;
    }
    return false;
}

// FUNCTION: WIZ8 0x00454170
unsigned char W8Navigator::SetMovementTarget(const glm::vec3* target, bool propagate)
{
    movement.target_position = *target;
    movement_target = *target;
    unsigned char result;
    if (g_combat_inactive == 0) {
        radius = movement.alternate_radius;
        result = GetWorldOctree()->PrepareNavigatorTarget(&movement, radius,
                                                          static_cast<float>(collision_margin));
    } else {
        radius = movement.collision_radius;
        if ((flags & 4) != 0 && (flags & 1) != 0) {
            result = GetWorldOctree()->PrepareNavigatorTarget(
                &movement, radius, static_cast<float>(target_navigator->radius + collision_margin));
        } else {
            result = GetWorldOctree()->PrepareNavigatorTarget(&movement, radius,
                                                              static_cast<float>(collision_margin));
        }
    }
    if (result == 0 || IsNavigatorAtTarget(&movement)) {
        radius = movement.alternate_radius;
    }
    if (result == 0) {
        ClearMovement();
    } else if (movement_stopped) {
        movement_complete = false;
        ClearMovementStopped();
    }
    if (!propagate && result != 0 && movement.attachment != 0 &&
        (movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0 &&
        g_combat_inactive != 0) {
        CopyPathToGroup();
    }
    return result;
}

// FUNCTION: WIZ8 0x00453690
void W8Navigator::AddPathPoint(const glm::vec3* position)
{
    if (path_ai == 0) {
        path_ai = std::make_unique<W8PathAI>();
    }
    flags &= 0x00ffffff;
    if (flags == 0) {
        flags = 6;
        if (movement.active_rank == 0) {
            movement.active_rank = movement.leadership_rank;
        }
        movement.target_position = *position;
        movement_target = *position;
        ClearMovementStopped();
        PathAIAddPoint(*path_ai, movement.position);
        SetMovementTarget(&movement_target, false);
    }
    PathAIAddPoint(*path_ai, *position);
}

// FUNCTION: WIZ8 0x004537c0
void W8Navigator::SetPathLooping(char value)
{
    if (path_ai != 0) {
        if (value != 0) {
            path_ai->looping = 1;
            return;
        }
        path_ai->looping = 0;
    }
}

// FUNCTION: WIZ8 0x00453c50
void W8Navigator::SetMovementScale(float value)
{
    movement.movement_scale = value;
}

// FUNCTION: WIZ8 0x00453c60
float W8Navigator::GetMovementScale()
{
    return movement.movement_scale;
}

// FUNCTION: WIZ8 0x00454040
void W8Navigator::SetFacingToward(const glm::vec3* target)
{
    glm::vec3 current = movement.position;
    current.y += movement.height_offset;
    if (target->x != current.x || target->y != current.y || target->z != current.z) {
        float angle = GetHeadingAngle(&current, target);
        SetAngles(angle);
        if (navigation_mode == 2 || navigation_mode == 3) {
            angle = -GetElevationAngle(&current, target);
            SetPitch(angle);
        } else if (navigation_mode == 5 || navigation_mode == 6) {
            UpdateFacing(true);
        }
        if (movement.location_id != 0) {
            W8MonsterInfo* monster_info = FindMonster(movement.location_id);
            if (monster_info->fInCombat) {
                monster_info->pCombat->sight_refresh_pending = true;
            }
        }
    }
}

// FUNCTION: WIZ8 0x00456020
void W8Navigator::SetPosition(const glm::vec3* position)
{
    SetPositionInternal(position);
    position_dirty = true;
}

// FUNCTION: WIZ8 0x00453590
void W8Navigator::SetPositionInternal(const glm::vec3* position)
{
    glm::dvec3 widened;

    if (position->x != movement.position.x || position->y != movement.position.y ||
        position->z != movement.position.z) {
        movement.position = *position;
        widened = glm::dvec3(*position);
        node->setLocation(widened);
        if (movement.location_id != 0 || this == PartyNavigator()) {
            g_navigator_position_changed = true;
        }
        UpdateFacing(true);
        if (movement.attachment != 0) {
            movement.attachment->path[0].position = movement.position;
            movement.attachment->path_length_origin = movement.attachment->path[0].position;
            movement.attachment->segment_start = movement.attachment->path[0].position;
        }
    }
}

// FUNCTION: WIZ8 0x004537e0
void W8Navigator::ClearMovement()
{
    SetMovementStopped();

    PathAIClearOwned(path_ai.get());
    movement.velocity = glm::vec3(0);
    if ((movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
        flags &= 0xff000000;
    } else {
        flags = 0;
    }
    movement.attachment->RecordPosition(&movement.position);
    GetWorldOctree()->RegisterNavigatorCell(movement.location_id, &movement.position);
    movement_complete = true;
}

// FUNCTION: WIZ8 0x00453990
void W8Navigator::UpdateAngles()
{
    float step = g_navigator_default_turn_rate;
    float distance;
    float reverse_distance;
    float direction;

    if (!gXStatus.fCombatMode) {
        step = movement.turn_rate;
    }
    step *= g_rate * GameTimeAccumulator().GetFrameDelta();

    if (movement.yaw != movement.target_yaw) {
        distance = NormalizeAngle(movement.yaw - movement.target_yaw);
        reverse_distance = NormalizeAngle(movement.target_yaw - movement.yaw);
        direction = g_negative_one;
        if (reverse_distance < distance) {
            distance = reverse_distance;
            direction = 1.0f;
        }
        if (step <= distance) {
            movement.yaw = NormalizeAngle(step * direction + movement.yaw);
        } else {
            movement.yaw = movement.target_yaw;
        }
        if (movement_stopped) {
            UpdateFacing(false);
        }
        if (static_cast<float>(fabs(movement.yaw - movement.target_yaw)) < g_navigator_snap_angle) {
            movement.yaw = movement.target_yaw;
        }
    }

    if (navigation_mode == 3) {
        step *= kNavigatorMode3Scale;
    }
    if (movement.pitch_enabled && movement.pitch != movement.target_pitch) {
        distance = NormalizeAngle(movement.pitch - movement.target_pitch);
        reverse_distance = NormalizeAngle(movement.target_pitch - movement.pitch);
        direction = g_negative_one;
        if (reverse_distance < distance) {
            distance = reverse_distance;
            direction = 1.0f;
        }
        if (step <= distance) {
            movement.pitch = NormalizeAngle(step * direction + movement.pitch);
        } else {
            movement.pitch = movement.target_pitch;
        }
    }

    if (movement.roll_enabled && movement.roll != movement.target_roll) {
        distance = NormalizeAngle(movement.roll - movement.target_roll);
        reverse_distance = NormalizeAngle(movement.target_roll - movement.roll);
        direction = g_negative_one;
        if (reverse_distance < distance) {
            distance = reverse_distance;
            direction = 1.0f;
        }
        if (distance < step) {
            movement.roll = movement.target_roll;
            return;
        }
        movement.roll = NormalizeAngle(step * direction + movement.roll);
    }
}

// FUNCTION: WIZ8 0x00453f30
void W8Navigator::AimAtPosition(const glm::vec3* target)
{
    glm::vec3 current = movement.position;
    current.y += movement.height_offset;
    if (target->x != current.x || target->y != current.y || target->z != current.z) {
        movement.target_yaw = NormalizeAngle(GetHeadingAngle(&current, target));
        if (navigation_mode == 2 || navigation_mode == 3) {
            movement.target_pitch = NormalizeAngle(-GetElevationAngle(&current, target));
        } else if (navigation_mode == 5 || navigation_mode == 6) {
            UpdateFacing(false);
        }

        if (movement.location_id != 0) {
            W8MonsterInfo* monster_info = FindMonster(movement.location_id);
            if (monster_info->fInCombat) {
                monster_info->pCombat->sight_refresh_pending = true;
            }
        }
    }
}

// FUNCTION: WIZ8 0x00455140
void W8Navigator::CollectGroupNavigators(std::vector<W8Navigator*>& navigators)
{
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;
    unsigned int member;
    int ally;

    if (linked_navigator != 0) {
        return;
    }

    monster_info = FindMonster(movement.location_id);
    group = FindMonsterGroup(monster_info->monster_group_id);

    for (member = 0; member < group->MemberCount(); ++member) {
        int location_id = group->MemberLocationId(member);
        if (location_id != movement.location_id) {
            monster_info = FindMonster(location_id);
            navigators.push_back(monster_info->p3D);
        }
    }

    for (ally = 0; ally < 4; ++ally) {
        if (group->allied_group_ids[ally] != 0) {
            W8MonsterGroup* allied_group = FindMonsterGroup(group->allied_group_ids[ally]);
            for (member = 0; member < allied_group->MemberCount(); ++member) {
                monster_info = FindMonster(allied_group->MemberLocationId(member));
                navigators.push_back(monster_info->p3D);
            }
        }
    }
}

// FUNCTION: WIZ8 0x004553a0
void W8Navigator::UpdateNavigation(unsigned char skip_movement, bool slowed)
{
    glm::vec3 previous = movement.position;
    glm::vec3 adjusted;
    int movement_result = 0;
    unsigned int movement_kind;
    bool was_stopped;

    tracked_dirty = false;
    if (position_dirty || movement.position_adjusted) {
        movement.position =
            *AdjustPosition(&adjusted, &movement.position, &previous, *GetWorldOctree());
    }

    if (movement.vertical_amplitude != 0.0f) {
        if (!g_navigator_vertical_enabled) {
            movement.vertical_offset = movement.vertical_base;
        } else {
            float phase = movement.vertical_phase + g_rate * GameTimeAccumulator().GetFrameDelta() *
                                                        g_navigator_vertical_phase_step;
            phase -= static_cast<float>(floor(phase));
            movement.vertical_phase = phase;
            movement.vertical_offset = static_cast<float>(sin(phase * kMotionFullTurnRadians)) *
                                           movement.vertical_amplitude +
                                       movement.vertical_base;
        }
    }

    if (skip_movement != 0) {
        return;
    }
    if (g_combat_inactive == 0 && movement_complete) {
        UpdateAngles();
        return;
    }
    if ((flags & 0x200000) != 0) {
        return;
    }
    if (g_navigator_link_mode != 0 && linked_navigator != 0) {
        return;
    }
    if (linked_navigator == 0) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
    }

    if (g_combat_inactive == 0) {
        movement.movement_speed = g_settings.monster_movement_speed;
        if (slowed) {
            movement.movement_speed *= 0.5f;
        }
    }
    if ((flags & 0x20000000) != 0 && (flags & 0xffffff) == 0) {
        ConfigureMovement(-1.0f, -1.0f);
    }
    if (halted && g_navigator_link_mode == 0) {
        UpdateAngles();
        return;
    }

    if (movement.attachment != 0 &&
        movement.attachment->path_cursor >= movement.attachment->path_position_index &&
        (movement.attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0 &&
        PathAIIsComplete(path_ai.get())) {
        radius = movement.alternate_radius;
    }
    if ((flags & 0x100000) != 0) {
        return;
    }

    was_stopped = movement_stopped;
    movement_kind = flags & 0xffffff;
    switch (movement_kind) {
    case 5:
    case 0x21:
    case 0x41:
    case 0x81:
        movement_result = ResolveMovement();
        break;

    case 6:
    case 10: {
        glm::vec3 next(0);

        movement_result = GetWorldOctree()->AdvanceNavigator(&movement, radius, 0.0f);
        if (movement_result == 1) {
            if (PathAINextPoint(path_ai.get(), &next) != 0) {
                glm::vec3 target;
                target = next;
                SetMovementTarget(&target, false);
            } else {
                SetPathAI(nullptr);
                ClearMovement();
                if (movement_kind == 6 && (flags & 0x20000000) != 0) {
                    ConfigureMovement(-1.0f, -1.0f);
                }
            }
        } else if (movement_result == 3) {
            SetMovementTarget(&movement_target, false);
        }
        break;
    }

    case 9:
        if (target_navigator != 0) {
            /* 0x004556C7: FLD qword ptr [ESI+0x10] - the separation is this
               navigator's collision margin (the value LinkToNavigator stored),
               narrowed to float. The linked navigator's radius load at
               0x004559A2 belongs to the 0x201 case below. */
            movement_result = GetWorldOctree()->AdvanceNavigator(
                &movement, radius, static_cast<float>(collision_margin));
            if (movement_result == 1 ||
                (movement_result == 3 &&
                 LinkToNavigator(target_navigator, collision_margin) == 0)) {
                ClearMovement();
            }
        }
        break;

    case 0x201:
        if (linked_navigator != 0 && g_combat_inactive != 0) {
            if (linked_navigator->movement_stopped) {
                glm::vec3 delta = linked_navigator->movement.position - movement.position;
                if (glm::length(delta) <
                    radius * g_navigator_linked_radius_scale + linked_navigator->radius) {
                    SetMovementStopped();
                    break;
                }
            }

            halted = false;
            if (linked_update_time == 0 || !movement_stopped) {
                if (movement_stopped) {
                    movement_stopped = false;
                    if (g_combat_inactive != 0 && linked_navigator == 0) {
                        int index;
                        g_navigator_group.clear();
                        CollectGroupNavigators(g_navigator_group);
                        for (index = 0; index < static_cast<int>(g_navigator_group.size());
                             ++index) {
                            g_navigator_group[index]->movement_stopped = false;
                        }
                    }
                }
                GetWorldOctree()->AdvanceNavigator(&movement, radius, linked_navigator->radius);
            }
            UpdateLinkedNavigator();
        }
        break;

    default:
        break;
    }

    if ((flags & 0x800000) != 0 && movement_result == 1) {
        unsigned int previous_flags = flags;
        flags &= 0xff7fffff;
        if (flags == 0) {
            ClearMovement();
        } else {
            movement.active_rank = movement.leadership_rank;
            movement.target_location_id = -1;
            if ((previous_flags & 0xff000000) != 0) {
                flags |= 6;
            }
            SetMovementTarget(&movement_target, false);
        }
    }

    if (!was_stopped || !movement_stopped) {
        if (g_combat_inactive == 0 && !movement_stopped) {
            if (!(movement.target_position == movement.position)) {
                glm::vec3 target;
                target = movement.target_position;
                AimAtPosition(&target);
            }
        }

        movement.position =
            *AdjustPosition(&adjusted, &movement.position, &previous, *GetWorldOctree());
        UpdateFacing(false);
        if (!movement_stopped) {
            glm::vec3 velocity;
            float minimum_speed;

            if (g_combat_inactive == 0) {
                velocity.x = movement.movement_speed * movement.movement_scale * kWorldScale;
                velocity.z = 0.0f;
            } else {
                velocity.x = movement.velocity.x;
                velocity.z = movement.velocity.z;
            }
            /* Two frames within one clock tick have no elapsed time; dividing by it
               made movement_speed NaN, which then stuck (NaN fails every comparison). */
            const float frame_time = g_rate * GameTimeAccumulator().GetFrameDelta();
            velocity.y = frame_time > 0.0f ? (movement.position.y - previous.y) / frame_time : 0.0f;
            if (navigation_mode == 2 || navigation_mode == 3) {
                minimum_speed = g_navigator_minimum_speed_mode23;
            } else {
                minimum_speed = g_navigator_minimum_speed;
            }
            movement.movement_speed =
                glm::length(velocity) / (movement.movement_scale * kWorldScale);
            if (movement.movement_speed < minimum_speed) {
                movement.movement_speed = minimum_speed;
            }
        }
    }

    UpdateAngles();
    if (!movement_stopped && g_combat_inactive == 0 && !movement_complete && flags != 0) {
        movement.callback_progress +=
            GameTimeAccumulator().GetFrameDelta() * movement.movement_scale * g_rate * kWorldScale;
        if (movement.callback_threshold <= movement.callback_progress) {
            if (movement_callback != 0) {
                movement_callback(this);
            }
            movement_complete = true;
        }
    }

    {
        if (tracked_distance < glm::length((tracked_position - movement.position))) {
            tracked_position = movement.position;
            tracked_dirty = true;
        }
    }
}

// FUNCTION: WIZ8 0x00455cc0
int W8Navigator::ResolveMovement()
{
    W8Navigator* target = target_navigator;

    if (target == 0) {
        return 0;
    }
    if (g_combat_inactive == 0) {
        int result = GetWorldOctree()->AdvanceNavigator(&movement, radius,
                                                        static_cast<float>(collision_margin));
        if (result == 1) {
            ClearMovement();
        }
        return result;
    }

    if ((movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
        float target_motion = glm::length((target->movement.position - target_last_position));

        if (target_motion > 2500.0 || (target == PartyNavigator() && target_motion > 500.0)) {
            target_last_position = target->movement.position;

            if (target->radius + radius + collision_margin <
                glm::length((target->movement.position - movement.position))) {
                int index;

                movement_stopped = false;
                if (linked_navigator == 0) {
                    g_navigator_group.clear();
                    CollectGroupNavigators(g_navigator_group);
                    for (index = 0; index < static_cast<int>(g_navigator_group.size()); ++index) {
                        g_navigator_group[index]->movement_stopped = false;
                    }
                }
                halted = false;
                SetMovementTarget(&target->movement.position, false);
                flags = 5;
            }
        }

        if (!halted && !movement_stopped) {
            int result = GetWorldOctree()->AdvanceNavigator(&movement, radius,
                                                            static_cast<float>(collision_margin));
            if (result != 1) {
                if (result == 3 && SetMovementTarget(&target->movement.position, false) == 0) {
                    ClearMovement();
                }
                flags |= 5;
                return result;
            }

            SetMovementStopped();
            PathAIClearOwned(path_ai.get());
            movement.velocity = glm::vec3(0);
            if ((movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
                flags &= 0xff000000;
            } else {
                flags = 0;
            }
            movement.attachment->RecordPosition(&movement.position);
            GetWorldOctree()->RegisterNavigatorCell(movement.location_id, &movement.position);
            flags |= 5;
            movement_complete = true;
            return 1;
        }
    } else {
        movement.attachment->flags &= ~W8_NAV_ATTACHMENT_FOLLOW_PATH;
        flags &= 0xff000000;
        ClearMovement();
    }
    return 0;
}

// Construction and reuse share endpoint copying and direction normalization.
void W8OctreeTrace::reset(const glm::vec3& from, const glm::vec3& to)
{
    // Copy before mutation so resetting from this trace's endpoints is safe.
    const auto from_value = from;
    const auto to_value = to;
    start = from_value;
    end = to_value;
    step = end - start;
    length = glm::length(step);
    hit_limit = length;
    step = glm::vec3(glm::dvec3(step) * (1.0 / static_cast<double>(length)));
    state = 0;
}

W8OctreeTrace::W8OctreeTrace(const glm::vec3& from, const glm::vec3& to)
{
    reset(from, to);
}

// FUNCTION: WIZ8 0x00453c70
void W8Navigator::SetMonsterTurnSpeed(float speed)
{
    movement.callback_threshold = speed * kWorldScale;
}
