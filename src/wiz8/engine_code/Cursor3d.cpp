#include "wiz8/engine_code/Cursor3d.h"

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "input.h"
#include "layouts/targeting.h"
#include "layouts/world.h"
#include "particle_modes.h"
#include "soundman.h"
#include "surrender/srDebug.h"
#include "surrender/srMaterial.h"
#include "surrender/srNode.h"
#include "surrender/srShader.h"
#include "surrender/srTexture.h" // IWYU pragma: keep - complete texture type for shared_ptr base conversion.
#include "wiz8/containers.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/ui_input.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

constexpr float kCursorGroundHeightSentinel = -10000000.0f;
constexpr float kCursorGroundProbeLift = 25.0f;
constexpr float kCursorGroundStepThreshold = 83.333335876464844f;
constexpr double kCursorProbeShrinkScale = 0.97;
constexpr float kCursorSightProbeHeight = 750.0f;

#define CURSOR3D_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Cursor3d.cpp"

/* Engine Code\Cursor3d.cpp. The cursor state, its visibility query and the
   range reset the update path calls. */

// GLOBAL: WIZ8 0x0065ba8c
W8WorldCursorState* gp3DCursor;
static std::unique_ptr<W8WorldCursorState> world_cursor;

// GLOBAL: WIZ8 0x0065ba94
static srNode* g_cursor_value;

/* The cursor scene node relocated to position on each move. */
// GLOBAL: WIZ8 0x0065ba90
static srNode* g_cursor_node0;

/* Set when the cursor is opened while a shift key is held; while set the
   update keeps the latched dragged monster instead of re-picking. */
// GLOBAL: WIZ8 0x0065ba98
static bool g_cursor_pick_latch;

/* 0x60ab44: the saved world-cursor monster group id; -1 until a cursor is
   torn down. Seeds and restores monster_group_id. */
// GLOBAL: WIZ8 0x0060ab44
static int g_cursor_saved_group_id = -1;

// GLOBAL: WIZ8 0x0060ab48
static float g_cursor_projection_distance = 4000.0f;

/* Build the world cursor on demand: allocate and clear the state block, load
   the 3DCursor monster cycle into it, create its tracking light and seed the
   probe box from the monster's animation bounds. The particle block is gated
   on particle which is never set here - it stays dormant until the cursor
   gains a particle. */
// FUNCTION: WIZ8 0x00490210
void InitializeWorldCursor(void)
{
    W8GrCycleLoadContext context;
    glm::vec3 position;
    glm::vec3 camera_position;
    glm::vec4 colour;
    srShader shader;

    DisableCursorScene();
    SetMouseCursorHotspot(0, 0);
    if (gp3DCursor == 0) {
        world_cursor = std::make_unique<W8WorldCursorState>();
        gp3DCursor = world_cursor.get();
        gp3DCursor->enabled = true;
        gp3DCursor->track_ground = true;
        gp3DCursor->range = 50000.0f;
        gp3DCursor->left_held = false;
        gp3DCursor->monster_group_id = g_cursor_saved_group_id;
        gp3DCursor->detached = false;
        gp3DCursor->march_enabled = true;
        gp3DCursor->footprint_mode = false;
        gp3DCursor->dragged_info = 0;
        context.directory = "Data\\Monsters";
        context.world = g_world;
        auto monster = LoadMonsterCycle(&context, "3DCursor", -1, 1);
        gp3DCursor->monster = monster.get();
        g_world->monsters.push_back(std::move(monster));
        MonsterSetCycle(gp3DCursor->monster, 0);
        gp3DCursor->last_published = glm::vec3(-100000000.0f);
        gp3DCursor->position = glm::vec3(0);
        gp3DCursor->cam_rel_offset = glm::vec3(0);
        gp3DCursor->monster->inactive = true;
        WarpSystemCursor(0x140, 0xf0);
        gp3DCursor->input_delta.x = 0;
        gp3DCursor->input_delta.y = 0;
        gp3DCursor->input_delta.z = 0;
        position = gp3DCursor->position;
        MonsterSetAdjustedPosition(gp3DCursor->monster, &position);
        g_world->active_monsters.push_back(gp3DCursor->monster);
        UpdateCycleRepresentation(gp3DCursor->monster, g_world);
        MonsterSetActive(gp3DCursor->monster, false);
        gp3DCursor->light = CreateRangedWorldLight(g_world, "3D Cursor Light");
        gp3DCursor->light->intensity = 1.0f;
        ConfigureWorldLight(gp3DCursor->light, 2500.0f);
        gp3DCursor->light->ambient = glm::vec3(0);
        gp3DCursor->light->diffuse = glm::vec3(1);
        gp3DCursor->light->specular = glm::vec3(0);
        gp3DCursor->light->setLocation(1000.0, 0.0, 0.0);
        if (gp3DCursor->particle != 0) {
            auto material = std::make_shared<srMaterial>();
            colour = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            material->setEmissive(colour);
            material->setDiffuse(colour);
            gp3DCursor->particle->SetMaterial(std::move(material));
            gp3DCursor->particle->SetTexture(
                LoadTextureFromFolder("Data\\Monsters\\Bitmaps\\", "particle.tga", true));
            shader.value = 0x100c433;
            gp3DCursor->particle->SetRenderFlags(shader);
            gp3DCursor->particle->rotateX(-1.5707963);
            gp3DCursor->particle->particle_size = 100.0;
            gp3DCursor->particle->emission_interval = 300;
            gp3DCursor->particle->acceleration = {0.0f, -1000.0f, 0.0f};
            gp3DCursor->particle->has_acceleration = 1;
            gp3DCursor->particle->initial_speed = 500.0f;
            gp3DCursor->particle->speed_mode = W8_PARTICLE_SPEED_RANDOM;
            gp3DCursor->particle->emission_mode = W8_PARTICLE_EMISSION_SINGLE;
            gp3DCursor->particle->bounds_mode = W8_PARTICLE_BOUNDS_NONE;
            gp3DCursor->particle->expiry_mode = W8_PARTICLE_EXPIRY_TIMED;
            gp3DCursor->particle->lifetime_ms = 6000;
            gp3DCursor->particle->cone_yaw = 1.5707963f;
            gp3DCursor->particle->cone_pitch = 1.5707963f;
            gp3DCursor->particle->speed_min = 500.0f;
            gp3DCursor->particle->speed_max = 1000.0f;
            gp3DCursor->particle->SetFlutter(W8_PARTICLE_FLUTTER_VELOCITY_SCALED);
            gp3DCursor->particle->flutter_amplitude = 50.0f;
            gp3DCursor->particle->flutter_period = 1000;
        }
        ApplyWorldCursorInput();
        if (gp3DCursor != 0) {
            camera_position = cameraLocation();
            if (!gp3DCursor->detached) {
                gp3DCursor->cam_rel_offset += camera_position;
            }
            gp3DCursor->detached = true;
        }
        ClearCombatSelection();
        const auto bounds = gp3DCursor->monster->GetAnimationBounds();
        if (!bounds) return;
        const auto& [minimum, maximum] = *bounds;
        gp3DCursor->probe_center = bounds->center();
        gp3DCursor->probe_offsets[0] = maximum;
        gp3DCursor->probe_offsets[1] = glm::vec3(minimum.x, maximum.y, maximum.z);
        gp3DCursor->probe_offsets[2] = glm::vec3(minimum.x, maximum.y, minimum.z);
        gp3DCursor->probe_offsets[3] = glm::vec3(maximum.x, maximum.y, minimum.z);
        gp3DCursor->probe_offsets[4] = glm::vec3(maximum.x, minimum.y, maximum.z);
        gp3DCursor->probe_offsets[5] = glm::vec3(minimum.x, minimum.y, maximum.z);
        gp3DCursor->probe_offsets[6] = minimum;
        gp3DCursor->probe_offsets[7] = glm::vec3(maximum.x, minimum.y, minimum.z);
        UpdateWorldCursorPlacement();
        if (g_dev_mode && (((GetInputState().modifiers & SHIFT_DOWN) != 0) ||
                           ((GetInputState().modifiers & CTRL_DOWN) != 0))) {
            g_cursor_pick_latch = true;
        }
    }
}

/* Tear the world cursor down completely: detach and delete its monster,
   release the particle, the tracked light and the carried value, then publish
   the retirement through the UI state. The shared camera-distance slot returns
   to the cursor's own last value and the cursor's vertical position is
   flattened before that distance is taken. */
// FUNCTION: WIZ8 0x004909C0
void ReleaseWorldCursor(void)
{
    W8WorldCursorState* cursor = gp3DCursor;
    glm::vec3 camera;
    glm::vec3 delta;

    if (cursor == 0) {
        return;
    }
    camera = cameraLocation();
    camera.y = 0.0f;
    cursor->position.y = 0.0f;
    delta = cursor->position - camera;
    g_cursor_projection_distance = glm::length(delta);

    DetachMonsterRepresentation(cursor->monster, g_world);
    DestroyWorldMonster(g_world, cursor->monster);
    ReleaseRendererObject(cursor->particle);
    ReleaseRendererObject(g_cursor_value);
    if (cursor->light != 0) {
        WorldRemoveLight(g_world, cursor->light);
        cursor->light = 0;
    }
    cursor->group_bind_pending = false;
    g_cursor_saved_group_id = cursor->monster_group_id;
    EnableCursorScene();
    RequestRefreshPartyState();
    ClearTargetMarker();
    gp3DCursor = nullptr;
    world_cursor.reset();
}

/* Consume the accumulated input deltas: scale them into a world-space step,
   latch the shift-dragged monster, then move the cursor. While the cursor is
   camera-locked the input feeds cam_rel_offset and position is rebuilt from the
   dynamic scene node (optionally settled onto the terrain); while detached the
   yaw-rotated delta moves position directly inside the range and
   poster-distance clamps, optionally marched to a ground/sight target. A
   changed position is republished to the cursor monster, the cursor nodes and
   the dragged monster. */
// FUNCTION: WIZ8 0x00490C60
void ApplyWorldCursorInput(void)
{
    glm::mat3 rotation;
    glm::dvec3 node_location;
    glm::vec3 camera;
    glm::vec3 delta;
    glm::vec3 lifted;
    glm::vec3 clamped;

    if (gp3DCursor == 0) {
        srAssertFail("gp3DCursor", CURSOR3D_CPP, 0x188, 0);
    }
    if (!gp3DCursor->enabled) {
        srAssertFail("gp3DCursor->fEnabled", CURSOR3D_CPP, 0x189, 0);
    }
    const float saved_y = gp3DCursor->position.y;
    delta = glm::vec3(gp3DCursor->input_delta.x * 1e+01f, gp3DCursor->input_delta.y * 1e+01f,
                      gp3DCursor->input_delta.z * 1e+01f);
    gp3DCursor->input_delta.x = 0;
    gp3DCursor->input_delta.y = 0;
    gp3DCursor->input_delta.z = 0;
    if (g_dev_mode) {
        if (((GetInputState().modifiers & SHIFT_DOWN) == 0) &&
            ((GetInputState().modifiers & CTRL_DOWN) == 0)) {
            if (g_cursor_pick_latch) {
                g_cursor_pick_latch = false;
            }
            if (gp3DCursor->dragged_info != 0) {
                gp3DCursor->dragged_info = 0;
            }
        } else if (!g_cursor_pick_latch && gp3DCursor->dragged_info == 0) {
            gp3DCursor->dragged_info = FindNearestMonsterInfo(&gp3DCursor->position, 2500.0);
        }
    }
    if (!gp3DCursor->detached) {
        lifted = glm::vec3(delta.x, delta.y + 1e+03f, delta.z);
        if (gp3DCursor->range > 0.0f && gp3DCursor->range < glm::length(lifted)) {
            delta = glm::dot(delta, delta) == 0
                        ? delta
                        : glm::normalize(delta) * static_cast<float>(gp3DCursor->range);
        }
        rotation = g_world->dynamic_scene->getRotation<glm::mat3>();
        gp3DCursor->cam_rel_offset += delta;
        gp3DCursor->position = rotation * (gp3DCursor->cam_rel_offset);
        node_location = g_world->dynamic_scene->getLocation();
        glm::vec3 scene_location;
        scene_location = glm::vec3(node_location);
        gp3DCursor->position += scene_location;
        if (gp3DCursor->track_ground) {
            if (gp3DCursor->position.y < saved_y) {
                gp3DCursor->position.y = saved_y;
            }
            gp3DCursor->position =
                g_octree->settleToGround(gp3DCursor->position, true, 500).position;
        }
    } else {
        camera = cameraLocation();
        camera.y -= g_default_world_height;
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(g_gd_camera->m_yaw), glm::vec3(0, 1, 0)));
        delta = rotation * delta;
        delta += gp3DCursor->position;
        if (gp3DCursor->range > 0.0f && gp3DCursor->range < glm::length((camera - delta))) {
            clamped = delta - camera;
            clamped = glm::dot(clamped, clamped) == 0
                          ? clamped
                          : glm::normalize(clamped) * static_cast<float>(gp3DCursor->range);
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
        if (gp3DCursor->march_enabled) {
            MarchWorldCursorTarget(&delta);
        }
        gp3DCursor->position = delta;
        gp3DCursor->cam_rel_offset = delta;
    }
    if (!(gp3DCursor->last_published == gp3DCursor->position)) {
        lifted = gp3DCursor->position;
        MonsterSetAdjustedPosition(gp3DCursor->monster, &lifted);
        if (g_cursor_node0 != 0) {
            node_location = glm::dvec3((*(&gp3DCursor->position)));
            g_cursor_node0->setLocation(node_location);
        }
        if (g_cursor_value != 0) {
            node_location = glm::dvec3((*(&gp3DCursor->position)));
            g_cursor_value->setLocation(node_location);
        }
        if (gp3DCursor->particle != 0) {
            node_location = glm::dvec3((*(&gp3DCursor->position)));
            gp3DCursor->particle->setLocation(node_location);
        }
        if (gp3DCursor->light != 0) {
            node_location = glm::dvec3((*(&gp3DCursor->position)));
            gp3DCursor->light->setLocation(node_location);
        }
        if (gp3DCursor->dragged_info != 0) {
            gp3DCursor->dragged_info->p3D->SetPositionInternal(&gp3DCursor->position);
            g_octree->UpdateMonsterLocation(gp3DCursor->dragged_info->location_id,
                                            &gp3DCursor->position);
            if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
                MonsterForwardReferencePosition(gp3DCursor->dragged_info->p3D, 1);
            }
        }
        gp3DCursor->last_published = gp3DCursor->position;
    }
}

// FUNCTION: WIZ8 0x004914C0
bool IsWorldCursorVisible(void)
{
    return gp3DCursor != 0 && gp3DCursor->enabled;
}

/* Bind the cursor monster to its monster group once flagged: reset the
   particle timing, find the group recorded on the cursor (or the first group
   in the list as a fallback), walk up to the leader group, arm the located
   monster with the Test.msf script and move the group leader to the cursor
   point. */
// FUNCTION: WIZ8 0x004914E0
void BindCursorMonsterToGroup(void)
{
    glm::vec3 position;
    W8MonsterGroup* monster_group;
    W8MonsterInfo* monster_info;

    if (gp3DCursor->group_bind_pending && gp3DCursor->light != 0) {
        gp3DCursor->group_bind_pending = false;
        ++gp3DCursor->input_delta.z;
        if (gp3DCursor->particle != 0) {
            gp3DCursor->particle->speed_min = 1000.0f;
            gp3DCursor->particle->speed_max = 2000.0f;
            gp3DCursor->particle->emission_interval = 300;
        }
        if (gp3DCursor == 0) {
            position = glm::vec3(0);
        } else {
            position = gp3DCursor->position;
        }
        monster_group = FindMonsterGroup(gp3DCursor->monster_group_id, false);
        if (monster_group == 0) {
            if (gXStatus.monster_groups.empty()) {
                return;
            }
            monster_group = GetMonsterGroupByListIndex(0);
        }
        gp3DCursor->monster_group_id = monster_group->group_id;
        if (monster_group != 0) {
            if (monster_group->leader_group_id != 0) {
                monster_group = FindMonsterGroup(monster_group->leader_group_id);
            }
            monster_info = FindMonster(monster_group->leader_location_id);
            if (monster_info != 0 && monster_info->p3D != 0 &&
                !monster_info->p3D->SetScript("Test.msf", true)) {
                ApplyToMonsterGroupLeader(monster_group, &position, true);
            }
        }
    }
}

/* Reset the cursor range to its default 4000 units. The cursor release path
   (ReleaseWorldCursor) writes the flattened camera distance to the
   same slot before the cursor is freed. */
// FUNCTION: WIZ8 0x00492530
void ResetWorldCursorRange(void)
{
    g_cursor_projection_distance = 4000.0f;
}

/* Install an action-range distance into the live cursor: subtract
   2e+03f / distance * world_scale, clamp to 50000, write range,
   and force last_published to the -1e8 republish sentinel. */
// FUNCTION: WIZ8 0x00491650
void SetWorldCursorRange(float distance)
{
    if (gp3DCursor != 0) {
        distance = distance - (2e+03f / distance) * kWorldScale;
        if (distance >= 5e+04f) {
            distance = 5e+04f;
        }
        gp3DCursor->range = distance;
        gp3DCursor->last_published = glm::vec3(-100000000.0f);
    }
}

/* Toggle the 3D world cursor: release it if one exists, or initialize one
   if none does. Both branches tail-call into the respective functions. */
// FUNCTION: WIZ8 0x00490af0
void ToggleWorldCursor(void)
{
    if (gp3DCursor != 0) {
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
// FUNCTION: WIZ8 0x004916C0
void UpdateWorldCursor(void)
{
    SDL_Point cursor_point;
    glm::vec3 camera;
    glm::vec3 old_position;
    glm::vec3 position;
    glm::vec3 resolved;
    glm::vec3 box_min;
    glm::vec3 box_max;

    if (gp3DCursor == 0 || !gp3DCursor->enabled) {
        return;
    }
    old_position = gp3DCursor->position;
    SyncSystemCursor();
    SGPMouseGetPos(&cursor_point);
    if (IsRightButtonHeld()) {
        if (!gp3DCursor->group_bind_pending && gp3DCursor->light != 0) {
            gp3DCursor->group_bind_pending = true;
            ++gp3DCursor->input_delta.z;
            if (gp3DCursor->particle != 0) {
                gp3DCursor->particle->speed_min = 3000.0f;
                gp3DCursor->particle->speed_max = 6000.0f;
                gp3DCursor->particle->emission_interval = 0x14;
            }
        }
    } else {
        BindCursorMonsterToGroup();
    }
    if (IsRightButtonHeld()) {
        if (gp3DCursor->detached) {
            gp3DCursor->track_ground = false;
        }
        gp3DCursor->input_delta.y += 0xf0 - cursor_point.y;
    } else {
        gp3DCursor->input_delta.x += cursor_point.x - 0x140;
        gp3DCursor->input_delta.z += 0xf0 - cursor_point.y;
    }
    WarpSystemCursor(0x140, 0xf0);
    ApplyWorldCursorInput();
    if (gp3DCursor != 0) {
        position = gp3DCursor->position;
    } else {
        position = glm::vec3(0);
    }
    if (!IsLeftButtonHeld()) {
        if (gp3DCursor->left_held && gXStatus.iTargetingMode == W8_TARGET_NEED_PLACE) {
            camera = cameraLocation();
            if (ResolveWorldCursorTarget(&resolved) != 0 &&
                g_octree->TraceLineOfSight(&camera, &resolved, true, -3, -3, true, 0) == 0) {
                box_min = resolved + gp3DCursor->extent_min;
                box_max = resolved + gp3DCursor->extent_max;
                if (!gp3DCursor->footprint_mode ||
                    g_octree->TestBoxOccupied(&box_min, &box_max) == 0) {
                    AimAtPlace(g_status.selected_character);
                    ToggleWorldCursor();
                    return;
                }
            }
            wiz8::sound::play("Data\\Sound\\Misc\\ErrorBeep.wav");
        }
        gp3DCursor->left_held = false;
    } else {
        gp3DCursor->left_held = true;
    }
    if (!(old_position == position)) {
        if (gp3DCursor->detached) {
            position.y += kCursorSightProbeHeight;
            PointCameraAtTarget(&position, true, false);
        }
        if (gXStatus.iTargetingMode == W8_TARGET_NEED_PLACE) {
            RefreshTargetMarker();
        }
    }
}

// FUNCTION: WIZ8 0x004919E0
char MarchWorldCursorTarget(glm::vec3* target)
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

    trace_from = *target + gp3DCursor->probe_center;
    origin = gp3DCursor->position;
    dist = glm::length((*target - origin));
    if (dist == 0.0) {
        return 0;
    }
    last_valid = gp3DCursor->position;
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
                probe = gp3DCursor->probe_offsets[i];
                probe *= kCursorProbeShrinkScale;
                probe += step_pos;
                const auto ground_probe = g_octree->settleToGround(probe, 1, 500.0f);
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
            if (gp3DCursor->track_ground) {
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
            trace_from = end_pos + gp3DCursor->probe_center;
            for (i = 0; i < 8; i++) {
                probe = end_pos + gp3DCursor->probe_offsets[i];
                if (g_octree->TraceLineOfSight(&trace_from, &probe, true, -3, -3, true, 0) != 0) {
                    *target = last_valid;
                    return 1;
                }
            }
            last_valid = end_pos;
        } while (step < dist);
    }
    *target = last_valid;
    if (target->y - lower_best < kCursorGroundStepThreshold) {
        gp3DCursor->track_ground = true;
    }
    return 0;
}

/* Update the world cursor's placement: derive a point g_cursor_projection_distance units
   in front of the yaw-rotated camera, resolve it through the cursor walk and
   store the result as the cursor position and camera-relative offset. */
// FUNCTION: WIZ8 0x00491EC0
void UpdateWorldCursorPlacement(void)
{
    glm::vec3 camera(0.0f, 1.0f, 0.0f);
    glm::vec3 target;
    W8WorldCursorState* cursor;
    camera = cameraLocation();
    camera.y -= g_default_world_height;
    cursor = gp3DCursor;
    cursor->last_published = glm::vec3(-100000000.0f);
    target =
        camera + glm::rotateY(glm::vec3(0, 0, g_cursor_projection_distance), g_gd_camera->m_yaw);
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
// FUNCTION: WIZ8 0x00492190
void SetWorldCursorExtents(const glm::vec3* minimum, const glm::vec3* maximum)
{
    if (gp3DCursor == 0) {
        return;
    }
    gp3DCursor->footprint_mode = true;
    gp3DCursor->extent_min = *minimum;
    gp3DCursor->extent_max = *maximum;
}

/* Resolve the world cursor's target position: start from the cursor's stored
   position, ground-probe its candidate corner offsets and lift the target to
   the best settled height. With the fixed-offset flag the two stored offsets
   and their crossed corner combinations are probed instead, and a settled
   height too far from the input height fails the resolution. */
// FUNCTION: WIZ8 0x004921E0
int ResolveWorldCursorTarget(glm::vec3* position)
{
    W8WorldCursorState* cursor = gp3DCursor;
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
        const auto ground = g_octree->settleToGround((*position) + offset, true, 500);
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

// FUNCTION: WIZ8 0x00492500
void GetWorldCursorTargetPosition(glm::vec3* position)
{
    glm::vec3 resolved;

    ResolveWorldCursorTarget(&resolved);
    *position = resolved;
}
