#include "wiz8/entities/cursor.h"

#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_rect.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/cursor.h"
#include "wiz8/entities/monster.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/ui_input.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"
#include "wiz8/world_cursor.h"

constexpr float kCursorGroundHeightSentinel = -10000000.0f;
constexpr float kCursorGroundProbeLift = 25.0f;
constexpr float kCursorGroundStepThreshold = 83.333335876464844f;
constexpr double kCursorProbeShrinkScale = 0.97;
constexpr float kCursorSightProbeHeight = 750.0f;

/* Engine Code\Cursor3d.cpp. The cursor state, its visibility query and the
   range reset the update path calls. */

static W8WorldCursorState* WorldCursor()
{
    return GetWorld() ? GetWorld()->cursor.get() : nullptr;
}

/* Build the world cursor on demand: allocate and clear the state block, load
   the 3DCursor monster cycle into it, create its tracking light and seed the
   probe box from the monster's animation bounds. */
void InitializeWorldCursor(void)
{

    glm::vec3 position;
    glm::vec3 camera_position;

    DisableCursorScene();
    SetMouseCursorHotspot(0, 0);
    if (WorldCursor() == 0) {
        auto& cursor = *(GetWorld()->cursor = std::make_unique<W8WorldCursorState>());
        cursor.enabled = true;
        cursor.track_ground = true;
        cursor.range = 50000.0f;
        cursor.left_held = false;
        cursor.monster_group_id = GetWorld()->cursor_recall.monster_group_id;
        cursor.detached = false;
        cursor.march_enabled = true;
        cursor.footprint_mode = false;
        cursor.dragged_location_id = 0;
        auto monster = LoadMonsterCycle(GetWorld(), "3DCursor", -1);
        cursor.monster = monster.get();
        GetWorld()->monsters.push_back(std::move(monster));
        cursor.monster->SetCycle(0);
        cursor.last_published = glm::vec3(-100000000.0f);
        cursor.position = glm::vec3(0);
        cursor.cam_rel_offset = glm::vec3(0);
        cursor.monster->inactive = true;
        WarpSystemCursor(0x140, 0xf0);
        cursor.input_delta.x = 0;
        cursor.input_delta.y = 0;
        cursor.input_delta.z = 0;
        position = cursor.position;
        cursor.monster->SetAdjustedPosition(&position);
        GetWorld()->active_monsters.push_back(cursor.monster);
        cursor.monster->UpdateRepresentation(GetWorld());
        cursor.monster->SetActive(false);
        cursor.light = CreateRangedWorldLight(GetWorld(), "3D Cursor Light");
        cursor.light->intensity = 1.0f;
        ConfigureWorldLight(cursor.light, 2500.0f);
        cursor.light->ambient = glm::vec3(0);
        cursor.light->diffuse = glm::vec3(1);
        cursor.light->specular = glm::vec3(0);
        cursor.light->transform.setLocation(1000.0, 0.0, 0.0);
        ApplyWorldCursorInput();
        camera_position = cameraLocation();
        if (!cursor.detached) {
            cursor.cam_rel_offset += camera_position;
        }
        cursor.detached = true;
        ClearCombatSelection();
        const auto bounds = cursor.monster->GetAnimationBounds();
        if (!bounds) return;
        const auto& [minimum, maximum] = *bounds;
        cursor.probe_center = bounds->center();
        cursor.probe_offsets[0] = maximum;
        cursor.probe_offsets[1] = glm::vec3(minimum.x, maximum.y, maximum.z);
        cursor.probe_offsets[2] = glm::vec3(minimum.x, maximum.y, minimum.z);
        cursor.probe_offsets[3] = glm::vec3(maximum.x, maximum.y, minimum.z);
        cursor.probe_offsets[4] = glm::vec3(maximum.x, minimum.y, maximum.z);
        cursor.probe_offsets[5] = glm::vec3(minimum.x, minimum.y, maximum.z);
        cursor.probe_offsets[6] = minimum;
        cursor.probe_offsets[7] = glm::vec3(maximum.x, minimum.y, minimum.z);
        UpdateWorldCursorPlacement();
        if (g_dev_mode && (((GetInputState().modifiers & SHIFT_DOWN) != 0) ||
                           ((GetInputState().modifiers & CTRL_DOWN) != 0))) {
            cursor.pick_latched = true;
        }
    }
}

/* Tear the world cursor down completely: detach and delete its monster,
   release the tracked light and the carried value, then publish
   the retirement through the UI state. The world recalls the cursor's flattened
   distance from the camera and its monster group for the next cursor. */
void ReleaseWorldCursor(void)
{
    W8WorldCursorState* cursor = WorldCursor();
    glm::vec3 camera;
    glm::vec3 delta;

    if (cursor == 0) {
        return;
    }
    camera = cameraLocation();
    camera.y = 0.0f;
    cursor->position.y = 0.0f;
    delta = cursor->position - camera;
    GetWorld()->cursor_recall.projection_distance = glm::length(delta);

    DetachMonsterRepresentation(cursor->monster);
    DestroyWorldMonster(cursor->monster);
    if (cursor->light != 0) {
        WorldRemoveLight(GetWorld(), cursor->light);
        cursor->light = 0;
    }
    cursor->group_bind_pending = false;
    GetWorld()->cursor_recall.monster_group_id = cursor->monster_group_id;
    EnableCursorScene();
    RequestRefreshPartyState();

    GetWorld()->cursor.reset();
}

/* Consume the accumulated input deltas: scale them into a world-space step,
   latch the shift-dragged monster, then move the cursor. While the cursor is
   camera-locked the input feeds cam_rel_offset and position is rebuilt from the
   dynamic scene node (optionally settled onto the terrain); while detached the
   yaw-rotated delta moves position directly inside the range and
   poster-distance clamps, optionally marched to a ground/sight target. A
   changed position is republished to the cursor monster, the cursor nodes and
   the dragged monster. */
void ApplyWorldCursorInput(void)
{
    glm::mat3 rotation;
    glm::vec3 camera;
    glm::vec3 delta;
    glm::vec3 lifted;
    glm::vec3 clamped;

    assert(WorldCursor() != 0);
    auto& cursor = *WorldCursor();
    assert(cursor.enabled);
    const float saved_y = cursor.position.y;
    delta = glm::vec3(cursor.input_delta.x * 1e+01f, cursor.input_delta.y * 1e+01f,
                      cursor.input_delta.z * 1e+01f);
    cursor.input_delta.x = 0;
    cursor.input_delta.y = 0;
    cursor.input_delta.z = 0;
    if (g_dev_mode) {
        if (((GetInputState().modifiers & SHIFT_DOWN) == 0) &&
            ((GetInputState().modifiers & CTRL_DOWN) == 0)) {
            cursor.pick_latched = false;
            cursor.dragged_location_id = 0;
        } else if (!cursor.pick_latched && cursor.dragged_location_id == 0) {
            if (auto* nearest = FindNearestMonsterInfo(&cursor.position, 2500.0))
                cursor.dragged_location_id = nearest->location_id;
        }
    }
    if (!cursor.detached) {
        lifted = glm::vec3(delta.x, delta.y + 1e+03f, delta.z);
        if (cursor.range > 0.0f && cursor.range < glm::length(lifted)) {
            delta = glm::dot(delta, delta) == 0
                        ? delta
                        : glm::normalize(delta) * static_cast<float>(cursor.range);
        }
        rotation = cameraRotation(*GetWorld());
        cursor.cam_rel_offset += delta;
        cursor.position = rotation * (cursor.cam_rel_offset);
        cursor.position += cameraLocation();
        if (cursor.track_ground) {
            if (cursor.position.y < saved_y) {
                cursor.position.y = saved_y;
            }
            cursor.position =
                GetWorldSpatialIndex()->settleToGround(cursor.position, true, 500).position;
        }
    } else {
        camera = cameraLocation();
        camera.y -= g_default_world_height;
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(partyCamera().m_yaw), glm::vec3(0, 1, 0)));
        delta = rotation * delta;
        delta += cursor.position;
        if (cursor.range > 0.0f && cursor.range < glm::length((camera - delta))) {
            clamped = delta - camera;
            clamped = glm::dot(clamped, clamped) == 0
                          ? clamped
                          : glm::normalize(clamped) * static_cast<float>(cursor.range);
            delta = camera + clamped;
        }
        if (glm::length((camera - delta)) < g_monster_poster_max_distance) {
            clamped = delta - camera;
            clamped =
                glm::dot(clamped, clamped) == 0
                    ? clamped
                    : glm::normalize(clamped) * static_cast<float>(g_monster_poster_max_distance);
            delta = camera + clamped;
        }
        if (cursor.march_enabled) {
            MarchWorldCursorTarget(&delta);
        }
        cursor.position = delta;
        cursor.cam_rel_offset = delta;
    }
    if (!(cursor.last_published == cursor.position)) {
        lifted = cursor.position;
        cursor.monster->SetAdjustedPosition(&lifted);

        if (cursor.light != 0) {
            cursor.light->transform.setLocation(glm::dvec3(cursor.position));
        }
        // The dragged monster may have been removed or unloaded since the drag began.
        const auto* dragged = FindMonster(cursor.dragged_location_id, false);
        if (dragged == nullptr || !dragged->entity) {
            cursor.dragged_location_id = 0;
        } else {
            dragged->entity->nav.SetPositionInternal(&cursor.position);
            GetWorldSpatialIndex()->UpdateMonsterLocation(dragged->location_id, cursor.position);
            if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
                dragged->entity->ForwardReferencePosition(1);
            }
        }
        cursor.last_published = cursor.position;
    }
}

bool IsWorldCursorVisible(void)
{
    return WorldCursor() != 0 && WorldCursor()->enabled;
}

/* Bind the cursor monster to its monster group once flagged: find the group
   recorded on the cursor (or the first group in the list as a fallback), walk up to the leader group, arm the located
   monster with the Test.msf script and move the group leader to the cursor
   point. */
void BindCursorMonsterToGroup(void)
{
    glm::vec3 position;
    W8MonsterGroup* monster_group;
    W8MonsterInfo* monster_info;

    auto& cursor = *WorldCursor();
    if (cursor.group_bind_pending && cursor.light != 0) {
        cursor.group_bind_pending = false;
        ++cursor.input_delta.z;
        position = cursor.position;
        monster_group = FindMonsterGroup(cursor.monster_group_id, false);
        if (monster_group == 0) {
            if (CurrentLevelPopulation().monster_groups.empty()) {
                return;
            }
            monster_group = CurrentLevelPopulation().monster_groups[0].get();
        }
        cursor.monster_group_id = monster_group->group_id;
        if (monster_group != 0) {
            if (monster_group->leader_group_id != 0) {
                monster_group = FindMonsterGroup(monster_group->leader_group_id);
            }
            monster_info = FindMonster(monster_group->leader_location_id);
            if (monster_info != 0 && monster_info->entity &&
                !monster_info->entity->SetScript("Test.msf", true)) {
                ApplyToMonsterGroupLeader(monster_group, &position, true);
            }
        }
    }
}

void ResetWorldCursorDistance(World& world)
{
    world.cursor_recall.projection_distance = W8WorldCursorRecall{}.projection_distance;
}

/* Install an action-range distance into the live cursor: subtract
   2e+03f / distance * world_scale, clamp to 50000, write range,
   and force last_published to the -1e8 republish sentinel. */
void SetWorldCursorRange(float distance)
{
    if (auto* cursor = WorldCursor()) {
        distance = distance - (2e+03f / distance) * kWorldScale;
        if (distance >= 5e+04f) {
            distance = 5e+04f;
        }
        cursor->range = distance;
        cursor->last_published = glm::vec3(-100000000.0f);
    }
}

/* Toggle the 3D world cursor: release it if one exists, or initialize one
   if none does. Both branches tail-call into the respective functions. */
void ToggleWorldCursor(void)
{
    if (WorldCursor() != 0) {
        ReleaseWorldCursor();
    } else {
        InitializeWorldCursor();
    }
}

/* March the cursor target from the stored position toward the requested
   point in 15.625-unit steps. Each step ground-settles the four lower probe
   offsets scaled inward by 0.97; with ground tracking on, the end point
   lifts to the best settled height (an upper probe wins, otherwise all four
   lower probes must have settled). The eight-offset sight fan from the
   probe-box center then guards the step: the first blocked ray writes the
   last clear end point back through `target` and returns 1. Reaching the
   requested point writes it back and re-arms ground tracking when the final
   height ends within 83.33 of the lowest settled probe. */
/* Per-frame world-cursor update from MainGameScreenFrame: recenter the mouse
   onto the view, fold the movement into the accumulated input deltas, run the
   cursor move, then handle the left-button click - a release while the place
   mode is active line-of-sights and box-tests the resolved target, aims the
   selected character and resets the cursor, or plays the error beep. A moved
   cursor points the camera at it while detached and refreshes the target
   marker in place mode. */
void UpdateWorldCursor(void)
{
    SDL_Point cursor_point;
    glm::vec3 camera;
    glm::vec3 old_position;
    glm::vec3 position;
    glm::vec3 resolved;

    if (WorldCursor() == 0 || !WorldCursor()->enabled) {
        return;
    }
    auto& cursor = *WorldCursor();
    old_position = cursor.position;
    SyncSystemCursor();
    cursor_point = CursorPosition();
    if (IsRightButtonHeld()) {
        if (!cursor.group_bind_pending && cursor.light != 0) {
            cursor.group_bind_pending = true;
            ++cursor.input_delta.z;
        }
    } else {
        BindCursorMonsterToGroup();
    }
    if (IsRightButtonHeld()) {
        if (cursor.detached) {
            cursor.track_ground = false;
        }
        cursor.input_delta.y += 0xf0 - cursor_point.y;
    } else {
        cursor.input_delta.x += cursor_point.x - 0x140;
        cursor.input_delta.z += 0xf0 - cursor_point.y;
    }
    WarpSystemCursor(0x140, 0xf0);
    ApplyWorldCursorInput();
    position = cursor.position;
    if (!IsLeftButtonHeld()) {
        if (cursor.left_held && ScreenPointer().targeting() == W8_TARGET_NEED_PLACE) {
            camera = cameraLocation();
            if (ResolveWorldCursorTarget(&resolved) != 0 &&
                !GetWorldSpatialIndex()->traceSight(camera, resolved)) {
                if (!cursor.footprint_mode ||
                    !GetWorldSpatialIndex()->isBoxOccupied(
                        {resolved + cursor.extent_min, resolved + cursor.extent_max})) {
                    AimAtPlace(CurrentGame().status.selected_character);
                    ToggleWorldCursor();
                    return;
                }
            }
            wiz8::sound::play("Data\\Sound\\Misc\\ErrorBeep.wav");
        }
        cursor.left_held = false;
    } else {
        cursor.left_held = true;
    }
    if (!(old_position == position)) {
        if (cursor.detached) {
            position.y += kCursorSightProbeHeight;
            PointCameraAtTarget(&position, true, false);
        }
    }
}

signed char MarchWorldCursorTarget(glm::vec3* target)
{
    glm::vec3 trace_from;
    glm::vec3 origin;
    glm::vec3 last_valid;
    glm::vec3 step_pos;
    glm::vec3 end_pos;
    glm::vec3 dir;
    glm::vec3 scaled;
    glm::vec3 probe;
    double dist;
    double step;
    float lower_best = -1e+10f;
    float upper_best;
    float ground;
    int lower_count;
    bool upper_found;
    int i;

    auto& cursor = *WorldCursor();
    trace_from = *target + cursor.probe_center;
    origin = cursor.position;
    dist = glm::length((*target - origin));
    if (dist == 0.0) {
        return 0;
    }
    last_valid = cursor.position;
    step = 0.0;
    if (0.0 < dist) {
        do {
            lower_count = 0;
            step += 15.625;
            upper_found = false;
            upper_best = -1e+10f;
            lower_best = -1e+10f;
            if (dist < step) {
                step = dist;
            }
            dir = *target - origin;
            scaled = dir;
            scaled = glm::dot(scaled, scaled) == 0
                         ? scaled
                         : glm::normalize(scaled) * static_cast<float>(step);
            step_pos = origin + scaled;
            end_pos = step_pos;
            for (i = 4; i < 8; i++) {
                probe = cursor.probe_offsets[i];
                probe *= kCursorProbeShrinkScale;
                probe += step_pos;
                const auto ground_probe = GetWorldSpatialIndex()->settleToGround(probe, 1, 500.0f);
                probe = ground_probe.position;
                ground = ground_probe.position.y;
                if (ground <= step_pos.y) {
                    if (ground < step_pos.y) {
                        lower_count++;
                        if (lower_best < ground) {
                            lower_best = ground;
                        }
                    }
                } else if (ground - step_pos.y < 1e+03f && upper_best < ground) {
                    upper_found = true;
                    upper_best = ground;
                }
            }
            if (cursor.track_ground) {
                if (upper_found) {
                    end_pos.y = upper_best + kCursorGroundProbeLift;
                    target->y = end_pos.y;
                    origin.y = end_pos.y;
                } else if (lower_count == 4) {
                    end_pos.y = lower_best + kCursorGroundProbeLift;
                    target->y = end_pos.y;
                    origin.y = end_pos.y;
                }
            }
            trace_from = end_pos + cursor.probe_center;
            for (i = 0; i < 8; i++) {
                probe = end_pos + cursor.probe_offsets[i];
                if (GetWorldSpatialIndex()->traceSight(trace_from, probe).has_value()) {
                    *target = last_valid;
                    return 1;
                }
            }
            last_valid = end_pos;
        } while (step < dist);
    }
    *target = last_valid;
    if (target->y - lower_best < kCursorGroundStepThreshold) {
        cursor.track_ground = true;
    }
    return 0;
}

/* Update the world cursor's placement: derive a point the recalled projection distance
   in front of the yaw-rotated camera, resolve it through the cursor walk and
   store the result as the cursor position and camera-relative offset. */
void UpdateWorldCursorPlacement(void)
{
    glm::vec3 camera(0.0f, 1.0f, 0.0f);
    glm::vec3 target;
    W8WorldCursorState* cursor;
    camera = cameraLocation();
    camera.y -= g_default_world_height;
    cursor = WorldCursor();
    cursor->last_published = glm::vec3(-100000000.0f);
    target = camera + glm::rotateY(glm::vec3(0, 0, GetWorld()->cursor_recall.projection_distance),
                                   partyCamera().m_yaw);
    cursor->position = camera;
    cursor->track_ground = true;
    if (MarchWorldCursorTarget(&target) == 0) {
        cursor->position = target;
        cursor->cam_rel_offset = cursor->position;
        if (!cursor->detached) {
            cursor->cam_rel_offset -= camera;
        }
        target.y += kCursorSightProbeHeight;
        PointCameraAtTarget(&target, true, false);
    } else {
        cursor->position = target;
    }
}

/* Arm the world cursor's footprint mode and install the two fixed probe
   offsets the target resolver probes in place of the probe box. */
void SetWorldCursorExtents(const glm::vec3* minimum, const glm::vec3* maximum)
{
    if (auto* cursor = WorldCursor()) {
        cursor->footprint_mode = true;
        cursor->extent_min = *minimum;
        cursor->extent_max = *maximum;
    }
}

/* Resolve the world cursor's target position: start from the cursor's stored
   position, ground-probe its candidate corner offsets and lift the target to
   the best settled height. With the fixed-offset flag the two stored offsets
   and their crossed corner combinations are probed instead, and a settled
   height too far from the input height fails the resolution. */
int ResolveWorldCursorTarget(glm::vec3* position)
{
    W8WorldCursorState* cursor = WorldCursor();
    float best = kCursorGroundHeightSentinel;

    if (cursor == 0) {
        (*position) = glm::vec3(0);
    } else {
        *position = cursor->position;
    }
    if (cursor == 0) {
        return 1;
    }
    for (int corner = 0; corner < 4; ++corner) {
        glm::vec3 offset;
        if (!cursor->footprint_mode) {
            offset = cursor->probe_offsets[corner + 4];
        } else {
            switch (corner) {
            case 0:
                offset = cursor->extent_min;
                break;
            case 1:
                offset = cursor->extent_max;
                break;
            case 2:
                offset = {cursor->extent_min.x, 0, cursor->extent_max.z};
                break;
            default:
                offset = {cursor->extent_max.x, 0, cursor->extent_min.z};
                break;
            }
        }
        const auto ground = GetWorldSpatialIndex()->settleToGround((*position) + offset, true, 500);
        if (best <= ground.position.y) best = ground.position.y;
    }
    if (cursor->footprint_mode &&
        static_cast<float>(g_monster_poster_max_distance) <= fabs(best - position->y)) {
        position->y = best + 1e+03f;
        return 0;
    }
    position->y = best + 1e+03f;
    return 1;
}

void GetWorldCursorTargetPosition(glm::vec3* position)
{
    glm::vec3 resolved;

    ResolveWorldCursorTarget(&resolved);
    *position = resolved;
}
