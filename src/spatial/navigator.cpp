#include "wiz8/spatial/navigator.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/combat/combat.h"
#include "wiz8/containers.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/render/picking.h"
#include "wiz8/render/segment_math.h"
#include "wiz8/spatial/bounding_box.h"
#include "wiz8/spatial/bounding_sphere.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/startup_world.h"
#include "wiz8/utility.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

bool g_navigator_position_changed;

/* The world object the navigator notifies when it leaves a location, and
   the notification itself. 0x0042E880 sits outside every assertion-backed
   interval, so it keeps an address-qualified name. */

namespace {
bool NavigationCombatActive(const Navigator& navigator)
{
    return CurrentGame().combat.has_value() &&
           (!navigator.owner_world || navigator.owner_world == GetWorld());
}

std::vector<Navigator*> activeNavigators()
{
    std::vector<Navigator*> navigators;
    if (const auto* world = GetWorld()) navigators = world->navigators;
    if (auto* party = PartyNavigator();
        party && std::ranges::find(navigators, party) == navigators.end())
        navigators.push_back(party);
    return navigators;
}

} // namespace

/* Trims the recorded route to the boundary of the `radius` sphere around
   `target`: walks stored positions while they stay inside, then lerps the
   crossing point into path_destination and over the first outside waypoint, moves
   lastWaypoint() there. */
bool NavigatorRoute::TruncatePathAtRadius(const glm::vec3* target, float radius)
{
    std::size_t index = path_cursor;
    float distance = glm::length((path[index].position - *target));
    float previous_distance = distance;

    while (distance < radius && index < lastWaypoint()) {
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
        path.resize(index + 1);
        return true;
    }
    return false;
}

/* Measure the remaining route, excluding teleportal hops (value bit 0x2). */
float NavigatorRoute::MeasurePathLength() const
{
    float length = 0.0f;
    glm::vec3 previous = path_length_origin;
    for (unsigned int index = std::max(1u, static_cast<unsigned int>(path_cursor));
         index <= lastWaypoint(); ++index) {
        if ((path[index - 1].value & 2) == 0) {
            length += glm::length(path[index].position - previous);
        }
        previous = path[index].position;
    }
    return length;
}

Navigator::Navigator(World* owner) : owner_world(owner), reactivated(0)
{
    navigation_mode = W8_NAVIGATOR_MODE_DEFAULT;
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
    tracked_distance = 500.0f;
    group_linked = false;
    tracked_dirty = false;
    linked_update_time = 0;
    movement.Reset();
    tracked_position = glm::vec3(0);
    if (owner_world) owner_world->navigators.push_back(this);
    movement_target = glm::vec3(0);
    collision_margin = 0.0;
    target_navigator = 0;
}

/* The callback a navigator starts with: mark it and stop it dead. */
void NavigatorDefaultCallback(Navigator* navigator)
{
    navigator->halted = true;
    navigator->movement.velocity = glm::vec3(0);
}

/* Switch every registered navigator to the requested link mode. Mode zero
   clears their stop flags and resets their path state, then re-bases any
   monster group already following a navigator onto that navigator's path and
   position; every other mode stops them and clears their velocity. */
// FUNCTION: WIZ8 0x00452F50
void SetNavigatorLinkMode(W8NavigatorLinkMode mode)
{
    if (g_navigator_link_mode == mode) {
        return;
    }
    g_navigator_link_mode = mode;
    if (mode == W8_NAVIGATOR_LINK_MODE_DEFAULT) {
        for (auto navigator : activeNavigators()) {
            if (navigator->owner_world && navigator->owner_world != GetWorld()) continue;
            navigator->halted = false;
            if (navigator->path_ai.has_value()) {
                PathAIResetTick((navigator->path_ai ? &*navigator->path_ai : nullptr));
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
                         (!CurrentGame().combat.has_value()) != 0)) {
                        navigator->linked_update_time = 0;
                        for (auto* group_navigator : navigator->CollectGroupNavigators()) {
                            *group_navigator->movement.attachment = *navigator->movement.attachment;
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
        for (auto navigator : activeNavigators()) {
            if (navigator->owner_world && navigator->owner_world != GetWorld()) continue;
            NavigatorDefaultCallback(navigator);
            navigator->group_linked = false;
        }
    }
}

void StopAllNavigators(void)
{
    for (auto* navigator : activeNavigators()) {
        if (navigator->owner_world && navigator->owner_world != GetWorld()) continue;
        NavigatorDefaultCallback(navigator);
    }
}

void ResumeAllNavigators(void)
{
    for (auto* navigator : activeNavigators()) {
        if (navigator->owner_world && navigator->owner_world != GetWorld()) continue;
        navigator->halted = false;
        if (navigator->path_ai.has_value()) {
            PathAIResetTick((navigator->path_ai ? &*navigator->path_ai : nullptr));
        }
        if (navigator->path_ai.has_value()) {
            PathAIResetTick((navigator->path_ai ? &*navigator->path_ai : nullptr));
        }
    }
}

/* Start with ten route slots and clear per-position values. */
NavigatorRoute::NavigatorRoute()
{
    flags = 0;
    path_cursor = 0;
    follow_offset = 0;
    path.resize(2);
    separation = 0.0f;
    segment_start = glm::vec3(0);
    path_destination = glm::vec3(0);
    path_length_origin = segment_start;
}

/* The from/to attachment: a ready-made two-position route that starts at the
   first recorded position. */
NavigatorRoute::NavigatorRoute(const glm::vec3* from, const glm::vec3* to)
{
    flags = W8_NAV_ATTACHMENT_POSITION_RECORDED;
    path_cursor = 1;
    follow_offset = 0;
    path.resize(2);
    separation = 0.0f;
    segment_start = *from;
    path[0].position = *from;
    path_destination = *to;
    path[1].position = *to;
    path_length_origin = segment_start;
}

/* A second pass of defaults over the same tail, run straight after the
   constructor. Where the constructor cleared the orientation, this one gives it
   a facing of three quarter turns, a ten-thousand callback threshold, a turn
   rate of a sixteenth turn, unit scale and speed, and an identity basis in the
   three vectors at +0x88. */
void NavigatorMovement::Reset()
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
NavigatorMovement::NavigatorMovement()
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
    attachment = std::make_unique<NavigatorRoute>();
    flags = 0;
}

/* Only these eleven fields survive a transfer between navigators; everything
   else in the tail stays whatever the destination already had, and target_location_id is
   invalidated rather than copied. */
void NavigatorMovement::CopySettingsFrom(const NavigatorMovement& other)
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
NavigatorMovement::~NavigatorMovement() = default;

/* A copy shares nothing that ties it to the original's navigation. The path is
   DISCARDED rather than cloned or shared, both navigator links are cleared, and
   the owned object is not carried over. The movement tail is default
   constructed, then six trailing radius/height/offset/scale fields are copied
   directly and CopySettingsFrom transfers its separate eleven-field settings
   subset. The outer navigator also carries across its height band, bounding
   corners, radius and callback. The copy allocates its own scene node and
   registers itself, so it is a live navigator from birth. */
Navigator::Navigator(const Navigator& other, World* owner)
    : owner_world(owner), flags(0), collision_margin(0.0), movement_target(0.0f, 0.0f, 0.0f),
      movement_stopped(true), halted(false), movement_complete(true), unknown_027(0),
      minimum_height(other.minimum_height), maximum_height(other.maximum_height),
      patrol_home(other.patrol_home), unknown_048(0), target_navigator(0),
      target_last_position(0.0f, 0.0f, 0.0f), linked_navigator(0), unknown_060(0), unknown_064(0),
      path_ai(std::nullopt), radius(other.radius), active(true),
      movement_callback(other.movement_callback), trace_mask(other.trace_mask),
      unknown_094(other.unknown_094), unknown_098(0), reactivated(other.reactivated),
      tracked_distance(other.tracked_distance), group_linked(other.group_linked)
{
    movement.collision_radius = other.movement.collision_radius;
    movement.alternate_radius = other.movement.alternate_radius;
    configureStartupDepth(other.movement.height_offset, other.movement.secondary_height_offset);
    movement.vertical_offset = other.movement.vertical_offset;
    movement.scale = other.movement.scale;
    minimum = other.minimum;
    maximum = other.maximum;
    if (auto* world = owner_world ? owner_world : GetWorld(); world && world->compiledIndex())
        world->compiledIndex()->registry.maximum_navigation_radius =
            std::max({world->compiledIndex()->registry.maximum_navigation_radius,
                      movement.collision_radius, movement.alternate_radius, radius});
    movement.CopySettingsFrom(other.movement);
    movement.height_offset -= other.movement.vertical_base;
    movement.secondary_height_offset -= other.movement.vertical_base;
    SetNavigationMode(other.navigation_mode);
    position_dirty = false;
    movement.position_adjusted = false;
    tracked_dirty = false;
    linked_update_time = 0;
    tracked_position = glm::vec3(0);
    if (owner_world) owner_world->navigators.push_back(this);
}

/* Navigators in the same world target and follow each other by address, so a departing
   navigator drops every link to itself: a mover whose target or leader is gone stops
   following it instead of reading freed memory. */
Navigator::~Navigator()
{
    if (owner_world) wiz8::erase_first(owner_world->navigators, this);
    auto* world = owner_world ? owner_world : GetWorld();
    if (world) {
        for (Navigator* other : world->navigators) {
            if (other->target_navigator == this) other->target_navigator = nullptr;
            if (other->linked_navigator == this) other->linked_navigator = nullptr;
        }
    }
    if (movement.location_id != 0 && world && world->compiledIndex())
        world->compiledIndex()->UnregisterLocationObject(movement.location_id,
                                                         W8_OCTREE_KIND_NAVIGATOR);
}

bool Navigator::IsLinkedToNavigator(Navigator* other)
{
    Navigator* linked;

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
void Navigator::SetNavigationMode(W8NavigatorMode mode)
{
    std::optional<KeyframeTrack> path;

    navigation_mode = mode;
    switch (mode) {
    case W8_NAVIGATOR_MODE_GROUNDED:
        path = std::optional<KeyframeTrack>(std::in_place);
        path->animated = true;
        SetPathAI(std::move(path));
        /* Falls into mode four's body: the retail block ends where mode four's
           jump-table entry lands. */
        [[fallthrough]];
    case W8_NAVIGATOR_MODE_PATH_ONLY:
        SetPitchRollEnabled(false, false);
        break;
    case W8_NAVIGATOR_MODE_FLYING:
    case W8_NAVIGATOR_MODE_SWIMMING:
    case W8_NAVIGATOR_MODE_QUADRUPED:
        path = std::optional<KeyframeTrack>(std::in_place);
        path->animated = true;
        SetPathAI(std::move(path));
        SetPitchRollEnabled(true, false);
        break;
    case W8_NAVIGATOR_MODE_CRAWLING:
        path = std::optional<KeyframeTrack>(std::in_place);
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
void Navigator::SetBounds(const BoundingBox& bounds)
{
    minimum = bounds.minimum;
    maximum = bounds.maximum;
    radius = movement.alternate_radius;
}

glm::vec3 Navigator::GetPosition() const
{
    return movement.position;
}

glm::vec3 Navigator::GetVelocity() const
{
    return movement.velocity;
}

bool Navigator::UpdateTrackedPosition()
{
    float distance = glm::length((tracked_position - movement.position));

    if (distance > tracked_distance) {
        tracked_position = movement.position;
        tracked_dirty = true;
    }
    return tracked_dirty;
}

void Navigator::SetMovementStopped()
{
    if (!movement_stopped) {
        movement_stopped = true;
        if (navigation_mode != W8_NAVIGATOR_MODE_QUADRUPED &&
            navigation_mode != W8_NAVIGATOR_MODE_CRAWLING) {
            movement.target_pitch = NormalizeAngle(0.0f);
        }
    }
}

/* Clear the stop flag and, outside combat, clear it for the collected
   unlinked group too. This operation repeats through movement setup/loading
   and propagation; ClearMovementStopped is a descriptive recovered name. */
void Navigator::ClearMovementStopped()
{
    movement_stopped = false;
    if (!NavigationCombatActive(*this) && linked_navigator == 0) {
        for (auto* navigator : CollectGroupNavigators()) {
            navigator->movement_stopped = false;
        }
    }
}

void Navigator::CopyPathToGroup()
{
    linked_update_time = 0;
    for (auto* navigator : CollectGroupNavigators()) {
        *navigator->movement.attachment = *movement.attachment;
        navigator->patrol_home = patrol_home;
        navigator->movement.attachment->flags &=
            ~(W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR | W8_NAV_ATTACHMENT_FOLLOW_PATH);
        navigator->linked_update_time = 0;
    }
}

/* Push this navigator's path and position onto every navigator in its group,
   and let the whole group move again if this one may. */
void Navigator::PropagateGroupPosition()
{
    if (!NavigationCombatActive(*this)) {
        CopyPathToGroup();
    }
    if (!movement_stopped) {
        /* Retail writes the zero the caller already proved; the store is dead
           but faithful. */
        ClearMovementStopped();
    }
}

void Navigator::SetTargetYaw(float angle)
{
    movement.target_yaw = NormalizeAngle(angle);
}

void Navigator::SetAngles(float angle)
{
    movement.yaw = NormalizeAngle(angle);
    movement.target_yaw = NormalizeAngle(angle);
}

void Navigator::SetTargetPitch(float angle)
{
    movement.target_pitch = NormalizeAngle(angle);
}

void Navigator::SetPitch(float pitch)
{
    movement.pitch = NormalizeAngle(pitch);
    movement.target_pitch = NormalizeAngle(pitch);
}

float Navigator::GetYaw() const
{
    return movement.yaw;
}

float Navigator::GetPitch() const
{
    return movement.pitch;
}

void Navigator::SetTurnRate(float turn_rate)
{
    movement.turn_rate = turn_rate;
}

void Navigator::SetHeightRange(float minimum, float maximum)
{
    if (minimum >= 0.0f) {
        minimum_height = minimum;
    }
    if (maximum >= 0.0f) {
        maximum_height = maximum;
    }
}

void Navigator::SetPathAI(std::optional<KeyframeTrack> path)
{
    path_ai = std::move(path);
}

KeyframeTrack* Navigator::GetPathAI()
{
    return (path_ai ? &*path_ai : nullptr);
}

void Navigator::ResetPathAI()
{
    if (path_ai.has_value()) {
        path_ai->position = 0.0f;
        path_ai->interpolation_fraction = 0.0f;
        PathAIResetTick((path_ai ? &*path_ai : nullptr));
    }
}

const float g_navigator_default_turn_rate = 4.398229598999023f;

const float g_navigator_snap_angle = 0.029999999329447746f;

unsigned short Navigator::SetMovementTargetToNavigator(Navigator* target, double separation)
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
    PathAIClearOwned((path_ai ? &*path_ai : nullptr));
    if (NavigationCombatActive(*this)) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
    }
    if (SetMovementTarget(&target->movement.position, false) == 0) {
        if (NavigationCombatActive(*this)) {
            flags = 0;
        }
    } else {
        flags = 5;
        result = 1;
        if (NavigationCombatActive(*this)) {
            result = static_cast<unsigned short>(movement.attachment->flags & 7);
        }
    }
    target_last_position = target->movement.position;
    return result;
}

bool Navigator::StartPatrol(const glm::vec3* home, float distance, float variation)
{
    Navigator* navigator = this;

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
W8NavigatorLinkMode g_navigator_link_mode;
// GLOBAL: WIZ8 0x005ebc98
const float g_navigator_linked_radius_scale = 4.0f;
const float g_navigator_vertical_phase_step = 0.25f;
/* Runtime scale for the camera-sphere radius in the octree trace resolver;
   the retail image carries link-time 1.0 here. */
float g_camera_sphere_radius_scale = 1.0f;

static const float NAVIGATOR_THREE_QUARTER_TURN = 4.712389f;
static const float NAVIGATOR_MAXIMUM_DROP = 1500.0f;
float g_navigator_gravity = 187.5f;
static const float NAVIGATOR_LINK_DISTANCE_SQUARED = 400.0f;

// FUNCTION: WIZ8 0x00454d70
void Navigator::UpdateLinkedNavigator(std::optional<BoundingSphere> player)
{
    PathSearchScratch path_search;
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
    if ((!monster_owner || monster_owner->IsWithinWorldRange() == 0) &&
        (!linked_navigator->monster_owner ||
         linked_navigator->monster_owner->IsWithinWorldRange() == 0)) {
        UpdateLinkedPosition(player);
        return;
    }
    int tick = static_cast<int>(GameTimeAccumulator().GetElapsed());
    if (static_cast<unsigned int>(tick - linked_update_time) <= 50) {
        return;
    }
    linked_update_time = tick;
    glm::vec3 camera = cameraLocation();
    if (linked_navigator->monster_owner &&
        linked_navigator->monster_owner->IsWithinWorldRange() != 0) {
        const auto bounds = linked_navigator->monster_owner->GetWorldAnimationBounds();
        if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
            goto follow_path;
        }
    }
    if (monster_owner && monster_owner->IsWithinWorldRange() != 0) {
        const auto bounds = monster_owner->GetWorldAnimationBounds();
        if (bounds && targetMarkerVisible(camera, *bounds) != 0) {
            goto follow_path;
        }
    }
    if (UpdateLinkedPosition(player)) {
        return;
    }
follow_path:
    if (NavigationOctree()->pathing->PrepareLinkedNavigator(path_search, &movement)) {
        if (movement_stopped) {
            ClearMovementStopped();
        }
    } else {
        SetMovementStopped();
        AimAtPosition(&camera);
    }
}

// FUNCTION: WIZ8 0x00454fe0
bool Navigator::UpdateLinkedPosition(std::optional<BoundingSphere> player)
{
    if (linked_navigator == 0) {
        return false;
    }
    glm::vec3 position;
    if (linked_navigator->movement_stopped) {
        const auto placement = NavigationOctree()->navigatorPositions(
            linked_navigator->movement.position, linked_navigator->movement.yaw,
            movement.collision_radius + movement.collision_radius, 1, true, false, false, 5, true,
            player);
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
    *movement.attachment = *linked_navigator->movement.attachment;
    movement.attachment->flags &= ~W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
    movement.yaw = linked_navigator->movement.yaw;
    movement.velocity = linked_navigator->movement.velocity * 0.5f;
    SetPosition(&position);
    NavigationOctree()->RegisterNavigatorCell(movement.location_id, position);
    linked_update_time = 0;
    return true;
}

glm::vec3* Navigator::AdjustPosition(glm::vec3* result, const glm::vec3* current,
                                     const glm::vec3* previous, LevelSpatialIndex& octree)
{
    float acceleration_scale = 0.25f;
    if (movement.location_id == 0) {
        *result = *current;
        return result;
    }
    /* 0x0045445D skips the whole settle-and-drop body when navigation_mode is
       4 and falls into the shared stop path below, so the mode test guards the
       body rather than sharing the location-id early return. */
    if (navigation_mode != W8_NAVIGATOR_MODE_PATH_ONLY) {
        glm::vec3 probe = *current;
        probe.y += kWorldScale;
        const auto ground_probe = octree.settleToGround(probe, 1, 500.0f);
        probe = ground_probe.position;
        float ground = ground_probe.position.y;
        if (ground_probe.contact) {
            if (!ground_probe.prop()) {
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
                    if (NavigationCombatActive(*this)) {
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
                if (NavigationCombatActive(*this)) {
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
    if (NavigationCombatActive(*this)) {
        ClearMovement();
    }
    *result = *previous;
    return result;
}

void Navigator::UpdateFacing(bool immediate)
{
    if (!movement.pitch_enabled && !movement.roll_enabled) {
        return;
    }
    glm::vec3 forward(0.0f, 0.0f, 1.0f);
    forward = glm::vec3(
        glm::angleAxis(std::atan2(sin(movement.yaw), cos(movement.yaw)), glm::dvec3(0, 1, 0)) *
        glm::dvec3(forward));
    const glm::vec3 normal = NavigationOctree()->pathSurfaceNormal(movement.position);
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

Navigator* Navigator::ResolveBlockingNavigator(const glm::vec3* from, glm::vec3* to,
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
    if (const auto hit = NavigationOctree()->traceSphereHit(
            *from, *to, movement.location_id, hit_location, location, trace_mask, false)) {
        *to = hit->position;
        hit_location = hit->location_id;
        if (hit_location == 0) {
            return PartyNavigator();
        }
        if (hit_location > 0) {
            auto* world = owner_world ? owner_world : GetWorld();
            W8MonsterInfo* monster_info = world->population->findMonster(hit_location);
            if (monster_info->entity) {
                return &monster_info->entity->nav;
            }
        }
    }
    return 0;
}

double Navigator::MeasurePathDistance(const glm::vec3* target, float max_range, int location_id)
{
    NavigatorMovement movement;
    auto& attachment = *movement.attachment;
    unsigned char ready;

    movement.CopySettingsFrom(this->movement);
    movement.target_location_id = location_id;
    movement.basis = this->movement.basis;
    movement.position = this->movement.position;
    movement.target_position = *target;
    attachment.InitializeSegment(&this->movement.position, target);
    if (NavigationCombatActive(*this)) {
        attachment.flags |= 0xc010000;
    } else {
        attachment.flags &= 0xf3feffff;
    }
    ready = NavigationOctree()->PrepareNavigatorTarget(&movement, max_range, radius);
    if (ready != 0 &&
        ((attachment.flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0 || (attachment.flags & 7) != 3)) {
        return attachment.MeasurePathLength();
    }
    return -1.0;
}

int Navigator::FindNavigatorPathDistance(float max_range, float* out_distance)
{
    double distance = MeasurePathDistance(&PartyNavigator()->movement.position, max_range, 0);

    *out_distance = static_cast<float>(distance);
    if (distance != g_negative_one) {
        return 1;
    }
    return 0;
}

void Navigator::SetVelocity(const glm::vec3* velocity)
{
    movement.velocity = *velocity;
}

bool Navigator::CheckNavigatorCollision(const glm::vec3* from, const glm::vec3* to)
{
    Navigator* blocker = ResolveBlockingNavigator(from, const_cast<glm::vec3*>(to), false);

    if (blocker != 0 && OnCollision(blocker) && blocker->OnCollision(this)) {
        return true;
    }
    return false;
}
/* Re-prime the attachment for a new movement segment: keep only the link flag
   bits, reseed the path cursors at one, copy source and destination into the
   position fields and the first two waypoints, clear the recorded path values
   and store the segment length. */
void NavigatorRoute::InitializeSegment(const glm::vec3* source, const glm::vec3* destination)
{
    const auto from = *source;
    const auto to = *destination;
    path.assign({PathEntry{from, 0}, PathEntry{to, 0}});
    flags = (flags & 0xc810000) | W8_NAV_ATTACHMENT_POSITION_RECORDED;
    path_cursor = 1;
    separation = 0;
    segment_start = from;
    path_destination = to;
    path_length_origin = segment_start;
    follow_offset = 0;
}

glm::vec3 NavigatorRoute::GetNextPosition() const
{
    if ((flags & W8_NAV_ATTACHMENT_START_WAYPOINT) != 0) {
        return start_waypoint;
    }
    if (path_cursor < lastWaypoint()) {
        return path[path_cursor].position;
    } else {
        return path_destination;
    }
}

bool NavigatorRoute::AdvanceAlongPathPositions(float distance, glm::vec3* position)
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
    delta = path[path_cursor].position - local;
    segment = glm::length(((glm::vec2(delta.x, delta.z))));
    if (segment < 1.0 && path_cursor == lastWaypoint()) {
        return false;
    }
    if (segment < distance) {
        do {
            if (lastWaypoint() < path_cursor) {
                break;
            }
            distance -= segment;
            local = path[path_cursor].position;
            ++path_cursor;
            if (path_cursor >= path.size()) break;
            delta = path[path_cursor].position - local;
            segment = glm::length(((glm::vec2(delta.x, delta.z))));
        } while (segment < distance);
    }
    if (lastWaypoint() < path_cursor) {
        *position = path_destination;
        path_cursor = lastWaypoint();
        on_path = false;
    } else {
        t = segment == 0.0f ? 0.0f : distance / segment;
        *position = local * static_cast<float>(1.0 - t) + path[path_cursor].position * t;
    }
    segment_start = *position;
    path_length_origin = *position;
    return on_path;
}

bool NavigatorRoute::withinHopHeight(std::span<const W8PathSurface> surfaces,
                                     const glm::vec2& point, std::size_t base,
                                     bool include_next) const
{
    auto projection =
        segmentMath::project(point, glm::vec2(path[base].position.x, path[base].position.z),
                             glm::vec2(path[base + 1].position.x, path[base + 1].position.z));
    if (include_next && path_cursor < lastWaypoint() && path[path_cursor + 1].value != 0) {
        const auto next = segmentMath::project(
            point, glm::vec2(path[path_cursor].position.x, path[path_cursor].position.z),
            glm::vec2(path[path_cursor + 1].position.x, path[path_cursor + 1].position.z));
        if (next.distance < projection.distance) {
            base = path_cursor;
            projection = next;
        }
    }
    float height = (surfaces[path[base].value].flags >> 0xc) * kWorldScale;
    const float next_height = (surfaces[path[base + 1].value].flags >> 0xc) * kWorldScale;
    if (height != next_height) height = (next_height - height) * projection.fraction + height;
    return projection.distance <= height;
}

// FUNCTION: WIZ8 0x00456CB0
bool NavigatorRoute::checkPositionHopHeight(std::span<const W8PathSurface> surfaces,
                                            const glm::vec3& position) const
{
    const std::size_t base = path_cursor == 1 ? 1 : path_cursor - 1;
    if (lastWaypoint() < base + 1) return false;
    return withinHopHeight(surfaces, {position.x, position.z}, base, false);
}

// FUNCTION: WIZ8 0x00456DD0
bool NavigatorRoute::checkPredictedHopHeight(std::span<const W8PathSurface> surfaces,
                                             const glm::vec3& position) const
{
    return withinHopHeight(surfaces, {position.x, position.z}, path_cursor - 1, true);
}

bool NavigatorRoute::AdvancePositionTowardWaypoint(glm::vec3* position, float distance)
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
    const auto projection = segmentMath::project(point, from, to);
    point = projection.point;
    fraction = projection.fraction;
    dir.x = to.x - from.x;
    dir.y = to.y - from.y;
    remainder = (1.0f - fraction) * glm::length(dir);
    if (distance <= remainder) {
        reached = false;
    } else {
        reached = true;
        if (path_cursor < lastWaypoint() && path[path_cursor + 1].value != 0) {
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
bool NavigatorRoute::AdvancePositionWithDirection(glm::vec3* position, float distance,
                                                  glm::vec3* direction)
{
    while (path_cursor <= lastWaypoint()) {
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
            if (path_cursor == lastWaypoint()) {
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

void Navigator::SetScale(float scale)
{
    float ratio = scale / movement.scale;
    movement.scale = scale;
    radius *= ratio;
    movement.collision_radius *= ratio;
    movement.alternate_radius *= ratio;
    movement.height_offset *= ratio;
    movement.secondary_height_offset *= ratio;
    movement.movement_scale *= ratio;
    if (auto* world = owner_world ? owner_world : GetWorld(); world && world->compiledIndex())
        world->compiledIndex()->registry.maximum_navigation_radius =
            std::max({world->compiledIndex()->registry.maximum_navigation_radius,
                      movement.collision_radius, movement.alternate_radius, radius});
}

unsigned char Navigator::ConfigureMovementToPosition(const glm::vec3* position)
{
    flags = 6;
    movement.target_location_id = -1;
    movement_target = glm::vec3(0);
    collision_margin = 0.0;
    target_navigator = 0;
    if (movement_stopped) {
        PathAIClearOwned((path_ai ? &*path_ai : nullptr));
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

void Navigator::SetHalted(bool value)
{
    halted = value;
    if (!value) {
        if (path_ai.has_value()) {
            PathAIResetTick((path_ai ? &*path_ai : nullptr));
        }
    } else {
        movement.velocity = glm::vec3(0);
    }
}

void Navigator::SetPitchRollEnabled(bool pitch, bool roll)
{
    movement.pitch_enabled = pitch;
    movement.roll_enabled = roll;
}

/* 0x004527AD is a split address inside this body: it resumes at the
   GetWorldPathing() nonnull branch with the call registers still live, not a
   separate authored function. */
unsigned char Navigator::LinkToNavigator(Navigator* target, double separation)
{
    PathSearchScratch path_search;
    unsigned char result = 0;
    auto* octree = NavigationOctree();
    if (!octree || !octree->pathing) {
        return 0;
    }
    movement_target = glm::vec3(0);
    collision_margin = separation;
    flags = 0;
    target_navigator = target;
    movement.target_location_id = target->movement.location_id;
    PathAIClearOwned((path_ai ? &*path_ai : nullptr));
    movement.attachment->InitializeSegment(&movement.position, &target->movement.position);
    if (NavigationCombatActive(*this)) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
        if (octree->pathing->PlanMovementToPosition(path_search, &movement,
                                                    &target->movement.position, radius,
                                                    static_cast<float>(separation)) == 0) {
            return 0;
        }
        flags = 9;
        target_last_position = target->movement.position;
        result = static_cast<unsigned char>(movement.attachment->flags & 7);
        ClearMovementStopped();
    } else if (NavigationOctree()->LinkNavigatorTarget(&movement, &target->movement.position,
                                                       static_cast<float>(separation)) != 0) {
        flags = 9;
        target_last_position = target->movement.position;
        ClearMovementStopped();
        result = 1;
    }
    return result;
}

unsigned short Navigator::ConfigureMovementToNavigator(Navigator* target, float separation,
                                                       float maximum_distance, glm::vec3 position,
                                                       int trace_mode, float facing,
                                                       unsigned char* probe_result)
{
    PathSearchScratch path_search;
    auto* octree = NavigationOctree();
    if (!octree || !octree->pathing) {
        return 0;
    }
    if (!NavigationCombatActive(*this)) {
        return SetMovementTargetToNavigator(target, separation);
    }
    movement.attachment->flags |= W8_NAV_ATTACHMENT_FOLLOW_PATH;
    movement_target = glm::vec3(0);
    collision_margin = separation;
    flags = 0x21;
    target_navigator = target;
    movement.target_location_id = target->movement.location_id;
    movement.target_position = target->movement.position;
    PathAIClearOwned((path_ai ? &*path_ai : nullptr));
    radius = movement.alternate_radius;
    unsigned short result = octree->pathing->ConfigureMovementSearch(
        path_search, &movement, radius, separation,
        {.maximum_distance = maximum_distance,
         .offset = position,
         .mode = trace_mode,
         .target_height_offset = target->movement.height_offset,
         .target_location = target->movement.location_id == 0 ? -1 : target->movement.location_id,
         .target_yaw = facing},
        probe_result);
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

void Navigator::LinkGroupNavigator(Navigator* target, bool combat_active)
{
    if (!combat_active) {
        movement_target = glm::vec3(0);
        collision_margin = 0.0;
        target_navigator = 0;
    }
    linked_navigator = target;
    if (target == 0) {
        if (!combat_active) {
            flags &= 0xfffffdfe;
            if (flags == 0) {
                movement.attachment->InitializeSegment(&movement.position, &movement.position);
                SetMovementStopped();
            }
        }
    } else {
        movement.flags |= 2;
        if (!combat_active) {
            flags = (flags & 0xff000201) | 0x201;
            movement.attachment->InitializeSegment(&movement.position, &target->movement.position);
        }
    }
}

void Navigator::ResetMovementAndGroupState()
{
    movement_target = glm::vec3(0);
    Navigator* linked = linked_navigator;
    collision_margin = 0.0;
    target_navigator = 0;
    if (linked != 0) {
        movement.flags |= 2;
        if (NavigationCombatActive(*this)) {
            return;
        }
        flags = 0x201;
    } else {
        if (NavigationCombatActive(*this)) {
            return;
        }
        flags &= 0xfffffdfe;
        if (flags == 0) {
            movement.attachment->InitializeSegment(&movement.position, &movement.position);
            SetMovementStopped();
        }
        PropagateGroupPosition();
    }
    if (!NavigationCombatActive(*this)) {
        movement.attachment->flags &= ~W8_NAV_ATTACHMENT_FOLLOW_PATH;
    }
}

bool Navigator::ConfigureMovement(float minimum, float maximum)
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
        if (NavigationOctree()->PrepareNavigatorPatrol(&movement, minimum_height, maximum_height) !=
            0) {
            flags |= 6;
            ClearMovementStopped();
            halted = false;
            movement_target = movement.attachment->path_destination;
            movement.attachment->separation = 0.0f;
            if (!NavigationCombatActive(*this)) {
                CopyPathToGroup();
            }
            return true;
        }
        flags &= 0xdffffff9;
    }
    return false;
}

unsigned char Navigator::SetMovementTarget(const glm::vec3* target, bool propagate)
{
    movement.target_position = *target;
    movement_target = *target;
    unsigned char result;
    if (NavigationCombatActive(*this)) {
        radius = movement.alternate_radius;
        result = NavigationOctree()->PrepareNavigatorTarget(&movement, radius,
                                                            static_cast<float>(collision_margin));
    } else {
        radius = movement.collision_radius;
        if ((flags & 4) != 0 && (flags & 1) != 0) {
            result = NavigationOctree()->PrepareNavigatorTarget(
                &movement, radius, static_cast<float>(target_navigator->radius + collision_margin));
        } else {
            result = NavigationOctree()->PrepareNavigatorTarget(
                &movement, radius, static_cast<float>(collision_margin));
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
        !NavigationCombatActive(*this)) {
        CopyPathToGroup();
    }
    return result;
}

void Navigator::SetMovementScale(float value)
{
    movement.movement_scale = value;
}

void Navigator::SetFacingToward(const glm::vec3* target)
{
    glm::vec3 current = movement.position;
    current.y += movement.height_offset;
    if (target->x != current.x || target->y != current.y || target->z != current.z) {
        float angle = GetHeadingAngle(&current, target);
        SetAngles(angle);
        if (navigation_mode == W8_NAVIGATOR_MODE_FLYING ||
            navigation_mode == W8_NAVIGATOR_MODE_SWIMMING) {
            angle = -GetElevationAngle(&current, target);
            SetPitch(angle);
        } else if (navigation_mode == W8_NAVIGATOR_MODE_QUADRUPED ||
                   navigation_mode == W8_NAVIGATOR_MODE_CRAWLING) {
            UpdateFacing(true);
        }
        if (movement.location_id != 0) {
            auto* world = owner_world ? owner_world : GetWorld();
            W8MonsterInfo* monster_info = world->population->findMonster(movement.location_id);
            if (monster_info->pCombat) {
                monster_info->pCombat->sight_refresh_pending = true;
            }
        }
    }
}

void Navigator::SetPosition(const glm::vec3* position)
{
    SetPositionInternal(position);
    position_dirty = true;
}

void Navigator::SetPositionInternal(const glm::vec3* position)
{
    if (position->x != movement.position.x || position->y != movement.position.y ||
        position->z != movement.position.z) {
        movement.position = *position;
        if ((movement.location_id != 0 || this == PartyNavigator()) &&
            (!owner_world || owner_world == GetWorld())) {
            g_navigator_position_changed = true;
        }
        UpdateFacing(true);
        if (movement.attachment != 0) {
            movement.attachment->path_length_origin = movement.position;
            movement.attachment->segment_start = movement.position;
        }
    }
}

void Navigator::ClearMovement()
{
    SetMovementStopped();

    PathAIClearOwned((path_ai ? &*path_ai : nullptr));
    movement.velocity = glm::vec3(0);
    if ((movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
        flags &= 0xff000000;
    } else {
        flags = 0;
    }
    movement.attachment->RecordPosition(&movement.position);
    NavigationOctree()->RegisterNavigatorCell(movement.location_id, movement.position);
    movement_complete = true;
}

void Navigator::UpdateAngles()
{
    float step = g_navigator_default_turn_rate;
    float distance;
    float reverse_distance;
    float direction;

    if (!CurrentGame().combat.has_value()) {
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

    if (navigation_mode == W8_NAVIGATOR_MODE_SWIMMING) {
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

void Navigator::AimAtPosition(const glm::vec3* target)
{
    glm::vec3 current = movement.position;
    current.y += movement.height_offset;
    if (target->x != current.x || target->y != current.y || target->z != current.z) {
        movement.target_yaw = NormalizeAngle(GetHeadingAngle(&current, target));
        if (navigation_mode == W8_NAVIGATOR_MODE_FLYING ||
            navigation_mode == W8_NAVIGATOR_MODE_SWIMMING) {
            movement.target_pitch = NormalizeAngle(-GetElevationAngle(&current, target));
        } else if (navigation_mode == W8_NAVIGATOR_MODE_QUADRUPED ||
                   navigation_mode == W8_NAVIGATOR_MODE_CRAWLING) {
            UpdateFacing(false);
        }

        if (movement.location_id != 0) {
            auto* world = owner_world ? owner_world : GetWorld();
            W8MonsterInfo* monster_info = world->population->findMonster(movement.location_id);
            if (monster_info->pCombat) {
                monster_info->pCombat->sight_refresh_pending = true;
            }
        }
    }
}

// FUNCTION: WIZ8 0x00455140
std::vector<Navigator*> Navigator::CollectGroupNavigators() const
{
    std::vector<Navigator*> navigators;
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;
    unsigned int member;
    int ally;

    if (linked_navigator != 0) {
        return navigators;
    }

    auto* world = owner_world ? owner_world : GetWorld();
    if (!world) return navigators;
    const auto& population = *world->population;
    monster_info = population.findMonster(movement.location_id);
    if (!monster_info) return navigators;
    group = population.findGroup(monster_info->monster_group_id);
    if (!group) return navigators;

    for (member = 0; member < group->MemberCount(); ++member) {
        int location_id = group->MemberLocationId(member);
        if (location_id != movement.location_id) {
            monster_info = population.findMonster(location_id);
            if (monster_info && monster_info->entity.find())
                navigators.push_back(&monster_info->entity->nav);
        }
    }

    for (ally = 0; ally < 4; ++ally) {
        if (group->allied_group_ids[ally] != 0) {
            W8MonsterGroup* allied_group = population.findGroup(group->allied_group_ids[ally]);
            if (!allied_group) continue;
            for (member = 0; member < allied_group->MemberCount(); ++member) {
                monster_info = population.findMonster(allied_group->MemberLocationId(member));
                if (monster_info && monster_info->entity.find())
                    navigators.push_back(&monster_info->entity->nav);
            }
        }
    }
    return navigators;
}

// FUNCTION: WIZ8 0x004553a0
void Navigator::UpdateNavigation(unsigned char skip_movement, bool slowed,
                                 std::optional<BoundingSphere> player)
{
    glm::vec3 previous = movement.position;
    glm::vec3 adjusted;
    int movement_result = 0;
    unsigned int movement_kind;
    bool was_stopped;

    tracked_dirty = false;
    if (position_dirty || movement.position_adjusted) {
        movement.position =
            *AdjustPosition(&adjusted, &movement.position, &previous, *NavigationOctree());
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
    if (NavigationCombatActive(*this) && movement_complete) {
        UpdateAngles();
        return;
    }
    if ((flags & 0x200000) != 0) {
        return;
    }
    if (g_navigator_link_mode != W8_NAVIGATOR_LINK_MODE_DEFAULT && linked_navigator != 0) {
        return;
    }
    if (linked_navigator == 0) {
        movement.attachment->flags |= W8_NAV_ATTACHMENT_IGNORE_LINKED_NAVIGATOR;
    }

    if (NavigationCombatActive(*this)) {
        movement.movement_speed = g_settings.monster_movement_speed;
        if (slowed) {
            movement.movement_speed *= 0.5f;
        }
    }
    if ((flags & 0x20000000) != 0 && (flags & 0xffffff) == 0) {
        ConfigureMovement(-1.0f, -1.0f);
    }
    if (halted && g_navigator_link_mode == W8_NAVIGATOR_LINK_MODE_DEFAULT) {
        UpdateAngles();
        return;
    }

    if (movement.attachment != 0 &&
        movement.attachment->path_cursor >= movement.attachment->lastWaypoint() &&
        (movement.attachment->flags & W8_NAV_ATTACHMENT_START_WAYPOINT) == 0 &&
        PathAIIsComplete((path_ai ? &*path_ai : nullptr))) {
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

        movement_result = NavigationOctree()->AdvanceNavigator(&movement, radius, 0.0f);
        if (movement_result == 1) {
            if (PathAINextPoint((path_ai ? &*path_ai : nullptr), &next) != 0) {
                glm::vec3 target;
                target = next;
                SetMovementTarget(&target, false);
            } else {
                SetPathAI(std::nullopt);
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
            movement_result = NavigationOctree()->AdvanceNavigator(
                &movement, radius, static_cast<float>(collision_margin));
            if (movement_result == 1 ||
                (movement_result == 3 &&
                 LinkToNavigator(target_navigator, collision_margin) == 0)) {
                ClearMovement();
            }
        }
        break;

    case 0x201:
        if (linked_navigator != 0 && !NavigationCombatActive(*this)) {
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
                    if (!NavigationCombatActive(*this) && linked_navigator == 0) {
                        for (auto* navigator : CollectGroupNavigators()) {
                            navigator->movement_stopped = false;
                        }
                    }
                }
                NavigationOctree()->AdvanceNavigator(&movement, radius, linked_navigator->radius);
            }
            UpdateLinkedNavigator(player);
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
        if (NavigationCombatActive(*this) && !movement_stopped) {
            if (!(movement.target_position == movement.position)) {
                glm::vec3 target;
                target = movement.target_position;
                AimAtPosition(&target);
            }
        }

        movement.position =
            *AdjustPosition(&adjusted, &movement.position, &previous, *NavigationOctree());
        UpdateFacing(false);
        if (!movement_stopped) {
            glm::vec3 velocity;
            float minimum_speed;

            if (NavigationCombatActive(*this)) {
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
            const auto* spatial = NavigationOctree();
            const auto tuning =
                spatial && spatial->pathing ? spatial->pathing->settings : NavigationSettings{};
            if (navigation_mode == W8_NAVIGATOR_MODE_FLYING ||
                navigation_mode == W8_NAVIGATOR_MODE_SWIMMING) {
                minimum_speed = tuning.flying_swimming_minimum_animation_rate;
            } else {
                minimum_speed = tuning.minimum_animation_rate;
            }
            movement.movement_speed =
                glm::length(velocity) / (movement.movement_scale * kWorldScale);
            if (movement.movement_speed < minimum_speed) {
                movement.movement_speed = minimum_speed;
            }
        }
    }

    UpdateAngles();
    if (!movement_stopped && NavigationCombatActive(*this) && !movement_complete && flags != 0) {
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

int Navigator::ResolveMovement()
{
    Navigator* target = target_navigator;

    if (target == 0) {
        return 0;
    }
    if (NavigationCombatActive(*this)) {
        int result = NavigationOctree()->AdvanceNavigator(&movement, radius,
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
                movement_stopped = false;
                if (linked_navigator == 0) {
                    for (auto* navigator : CollectGroupNavigators()) {
                        navigator->movement_stopped = false;
                    }
                }
                halted = false;
                SetMovementTarget(&target->movement.position, false);
                flags = 5;
            }
        }

        if (!halted && !movement_stopped) {
            int result = NavigationOctree()->AdvanceNavigator(&movement, radius,
                                                              static_cast<float>(collision_margin));
            if (result != 1) {
                if (result == 3 && SetMovementTarget(&target->movement.position, false) == 0) {
                    ClearMovement();
                }
                flags |= 5;
                return result;
            }

            SetMovementStopped();
            PathAIClearOwned((path_ai ? &*path_ai : nullptr));
            movement.velocity = glm::vec3(0);
            if ((movement.attachment->flags & W8_NAV_ATTACHMENT_FOLLOW_PATH) == 0) {
                flags &= 0xff000000;
            } else {
                flags = 0;
            }
            movement.attachment->RecordPosition(&movement.position);
            NavigationOctree()->RegisterNavigatorCell(movement.location_id, movement.position);
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

void Navigator::SetMonsterTurnSpeed(float speed)
{
    movement.callback_threshold = speed * kWorldScale;
}

LevelSpatialIndex* Navigator::NavigationOctree() const
{
    auto* world = owner_world ? owner_world : GetWorld();
    return world ? world->compiledIndex() : nullptr;
}
