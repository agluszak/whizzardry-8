#include "wiz8/engine_code/Cursor3d.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_rect.h>
#include <glm/ext/quaternion_trigonometric.hpp>
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
#include "surrender/srMaterial.h"
#include "surrender/srMath.h"
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
#include "wiz8/float_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/sr_api.h"
#include "wiz8/ui_input.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

#define CURSOR3D_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Cursor3d.cpp"

/* Engine Code\Cursor3d.cpp. The cursor state, its visibility query and the
   range reset the update path calls. */

// GLOBAL: WIZ8 0x0065ba8c
W8WorldCursorState* gp3DCursor;

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

// GLOBAL: WIZ8 0x005ebc88
const float g_float_ten = 10.0f;

// GLOBAL: WIZ8 0x005ecb08
const float g_cursor_sight_probe_height = 750.0f;

// GLOBAL: WIZ8 0x005ecb20
const float g_cursor_ground_height_sentinel = -10000000.0f;

// GLOBAL: WIZ8 0x005ec8d8
const double g_double_fifteen_and_five_eighths = 15.625;

// GLOBAL: WIZ8 0x005ecb18
const double g_cursor_probe_shrink_scale = 0.97;

// GLOBAL: WIZ8 0x005ecb10
const float g_cursor_ground_probe_lift = 25.0f;

// GLOBAL: WIZ8 0x005ecb0c
const float g_cursor_ground_step_threshold = 83.333335876464844f;

/* Build the world cursor on demand: allocate and clear the state block, load
   the 3DCursor monster cycle into it, create its tracking light and seed the
   probe box from the monster's animation bounds. The particle block is gated
   on particle which is never set here - it stays dormant until the cursor
   gains a particle. */
// FUNCTION: WIZ8 0x00490210
void InitializeWorldCursor(void)
{
    W8GrCycleLoadContext context;
    srVector3T<float> position;
    srVector3T<float> camera_position;
    glm::vec4 colour;
    srShader shader;

    DisableCursorScene();
    SetMouseCursorHotspot(0, 0);
    if (gp3DCursor == 0) {
        gp3DCursor = static_cast<W8WorldCursorState*>(malloc(sizeof(*gp3DCursor)));
        if (gp3DCursor != 0) {
            memset(gp3DCursor, 0, sizeof(*gp3DCursor));
            gp3DCursor->monster = 0;
            gp3DCursor->unknown_08 = 1;
            gp3DCursor->group_bind_pending = false;
            gp3DCursor->input_delta.x = 0;
            gp3DCursor->input_delta.y = 0;
            gp3DCursor->input_delta.z = 0;
            gp3DCursor->light = 0;
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
            LoadMonsterCycle(&context, "3DCursor", &gp3DCursor->monster, -1, 1);
            MonsterSetCycle(gp3DCursor->monster, 0);
            gp3DCursor->last_published = -100000000.0f;
            gp3DCursor->position.SetZero();
            gp3DCursor->cam_rel_offset.SetZero();
            gp3DCursor->monster->inactive = true;
            WarpSystemCursor(0x140, 0xf0);
            gp3DCursor->input_delta.x = 0;
            gp3DCursor->input_delta.y = 0;
            gp3DCursor->input_delta.z = 0;
            position = gp3DCursor->position;
            MonsterSetAdjustedPosition(gp3DCursor->monster, &position);
            g_world->plsMonsters.push_back(gp3DCursor->monster);
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
            gp3DCursor->probe_offsets[1].Set(minimum.x, maximum.y, maximum.z);
            gp3DCursor->probe_offsets[2].Set(minimum.x, maximum.y, minimum.z);
            gp3DCursor->probe_offsets[3].Set(maximum.x, maximum.y, minimum.z);
            gp3DCursor->probe_offsets[4].Set(maximum.x, minimum.y, maximum.z);
            gp3DCursor->probe_offsets[5].Set(minimum.x, minimum.y, maximum.z);
            gp3DCursor->probe_offsets[6] = minimum;
            gp3DCursor->probe_offsets[7].Set(maximum.x, minimum.y, minimum.z);
            UpdateWorldCursorPlacement();
            if (g_dev_mode && (IsKeyHeld(VK_SHIFT) || IsKeyHeld(VK_CONTROL))) {
                g_cursor_pick_latch = true;
            }
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
    srVector3T<float> camera;
    srVector3T<float> delta;

    if (cursor == 0) {
        return;
    }
    camera = cameraLocation();
    camera.y = 0.0f;
    cursor->position.y = 0.0f;
    delta = cursor->position - camera;
    g_cursor_projection_distance = delta.Length();

    wiz8::erase_first(g_world->plsMonsters, cursor->monster);
    DetachMonsterRepresentation(cursor->monster, g_world);
    DeleteMonster(cursor->monster);
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
    free(cursor);
    gp3DCursor = 0;
}

/* The tracked cursor position, or the origin while there is no cursor. */
// FUNCTION: WIZ8 0x00490BF0
void GetWorldCursorPosition(srVector3T<float>* position)
{
    if (gp3DCursor != 0) {
        *position = gp3DCursor->position;
    } else {
        position->SetZero();
    }
}

/* Show the world cursor: reattach its monster to the world lists, mark it
   visible, reactivate its particle, then park the system cursor and clear the
   combat selection. */
// FUNCTION: WIZ8 0x00490B10
void ShowWorldCursor(void)
{
    if (gp3DCursor != 0 && !gp3DCursor->enabled) {
        UpdateMonster(gp3DCursor->monster);
        UpdateCycleRepresentation(gp3DCursor->monster, g_world);
        g_world->plsMonsters.push_back(gp3DCursor->monster);
        gp3DCursor->enabled = true;
        if (gp3DCursor->particle != 0) {
            gp3DCursor->particle->SetActive(1);
        }
        SetMouseCursorHotspot(0, 0);
        DisableCursorScene();
        ClearCombatSelection();
    }
}

/* Hide the world cursor: detach its monster from the world lists, clear the
   visible flag, deactivate its particle, then refresh the party state. */
// FUNCTION: WIZ8 0x00490B90
void HideWorldCursor(void)
{
    W8World* world;
    W8Monster* monster;
    W8WorldCursorState* cursor = gp3DCursor;

    if (cursor == 0 || !cursor->enabled) {
        return;
    }
    monster = cursor->monster;
    world = g_world;
    wiz8::erase_first(world->plsMonsters, monster);
    DetachMonsterRepresentation(monster, world);
    cursor->enabled = false;
    if (cursor->particle != 0) {
        cursor->particle->SetActive(0);
    }
    EnableCursorScene();
    RequestRefreshPartyState();
}

// FUNCTION: WIZ8 0x00490C20
void GetWorldCursorAnchor(srVector3T<float>* position)
{
    if (gp3DCursor != 0) {
        *position = gp3DCursor->position;
    } else {
        position->SetZero();
    }
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
    srVector3T<float> camera;
    srVector3T<float> delta;
    srVector3T<float> lifted;
    srVector3T<float> clamped;
    float saved_y = gp3DCursor->position.y;

    if (gp3DCursor == 0) {
        srAssertFail("gp3DCursor", CURSOR3D_CPP, 0x188, 0);
    }
    if (!gp3DCursor->enabled) {
        srAssertFail("gp3DCursor->fEnabled", CURSOR3D_CPP, 0x189, 0);
    }
    delta.Set(gp3DCursor->input_delta.x * g_float_ten, gp3DCursor->input_delta.y * g_float_ten,
              gp3DCursor->input_delta.z * g_float_ten);
    gp3DCursor->input_delta.x = 0;
    gp3DCursor->input_delta.y = 0;
    gp3DCursor->input_delta.z = 0;
    if (g_dev_mode) {
        if (!IsKeyHeld(VK_SHIFT) && !IsKeyHeld(VK_CONTROL)) {
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
        lifted.Set(delta.x, delta.y + g_float_one_thousand, delta.z);
        if (gp3DCursor->range > g_float_zero && gp3DCursor->range < lifted.Length()) {
            delta.SetLength(gp3DCursor->range);
        }
        rotation = g_world->dynamic_scene->getRotation<glm::mat3>();
        gp3DCursor->cam_rel_offset += delta;
        gp3DCursor->position = rotation * (gp3DCursor->cam_rel_offset).glmValue();
        node_location = g_world->dynamic_scene->getLocation();
        srVector3T<float> scene_location;
        scene_location = glm::vec3(node_location);
        gp3DCursor->position += scene_location;
        if (gp3DCursor->track_ground) {
            if (gp3DCursor->position.y < saved_y) {
                gp3DCursor->position.y = saved_y;
            }
            gp3DCursor->position =
                g_octree->settleToGround(gp3DCursor->position.glmValue(), true, 500).position;
        }
    } else {
        camera = cameraLocation();
        camera.y -= g_default_world_height;
        rotation = glm::mat3(1);
        rotation *= glm::mat3_cast(glm::angleAxis(float(g_gd_camera->m_yaw), glm::vec3(0, 1, 0)));
        delta = rotation * delta.glmValue();
        delta += gp3DCursor->position;
        if (gp3DCursor->range > g_float_zero && gp3DCursor->range < (camera - delta).Length()) {
            clamped = delta - camera;
            clamped.SetLength(gp3DCursor->range);
            delta = camera + clamped;
        }
        if ((camera - delta).Length() < g_monster_poster_max_distance) {
            clamped = delta - camera;
            clamped.SetLength(g_monster_poster_max_distance);
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
            node_location = glm::dvec3((&gp3DCursor->position)->glmValue());
            g_cursor_node0->setLocation(node_location);
        }
        if (g_cursor_value != 0) {
            node_location = glm::dvec3((&gp3DCursor->position)->glmValue());
            g_cursor_value->setLocation(node_location);
        }
        if (gp3DCursor->particle != 0) {
            node_location = glm::dvec3((&gp3DCursor->position)->glmValue());
            gp3DCursor->particle->setLocation(node_location);
        }
        if (gp3DCursor->light != 0) {
            node_location = glm::dvec3((&gp3DCursor->position)->glmValue());
            gp3DCursor->light->setLocation(node_location);
        }
        if (gp3DCursor->dragged_info != 0) {
            gp3DCursor->dragged_info->p3D->SetPositionInternal(&gp3DCursor->position);
            g_octree->UpdateMonsterLocation(gp3DCursor->dragged_info->location_id,
                                            &gp3DCursor->position);
            if (IsKeyHeld(VK_SHIFT)) {
                MonsterForwardReferencePosition(gp3DCursor->dragged_info->p3D, 1);
            }
        }
        gp3DCursor->last_published = gp3DCursor->position;
    }
    g_octree->UpdatePathVisualization();
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
    srVector3T<float> position;
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
            position.SetZero();
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

/* 0x005EC260: hard ceiling for SetWorldCursorRange. */
// GLOBAL: WIZ8 0x005ec260
const float g_float_fifty_thousand = 50000.0f;

/* Install an action-range distance into the live cursor: subtract
   g_float_two_thousand / distance * world_scale, clamp to 50000, write range,
   and force last_published to the -1e8 republish sentinel. */
// FUNCTION: WIZ8 0x00491650
void SetWorldCursorRange(float distance)
{
    if (gp3DCursor != 0) {
        distance = distance - (g_float_two_thousand / distance) * g_world_scale;
        if (distance >= g_float_fifty_thousand) {
            distance = g_float_fifty_thousand;
        }
        gp3DCursor->range = distance;
        gp3DCursor->last_published = -100000000.0f;
    }
}

/* Store the selected monster group id on the world cursor. */
// FUNCTION: WIZ8 0x004916a0
void SetWorldCursorGroupId(int group_id)
{
    if (gp3DCursor != 0) {
        gp3DCursor->monster_group_id = group_id;
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
    srVector3T<float> camera;
    srVector3T<float> old_position;
    srVector3T<float> position;
    srVector3T<float> resolved;
    srVector3T<float> box_min;
    srVector3T<float> box_max;

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
        position.SetZero();
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
            position.y += g_cursor_sight_probe_height;
            PointCameraAtTarget(&position, true, false);
        }
        if (gXStatus.iTargetingMode == W8_TARGET_NEED_PLACE) {
            RefreshTargetMarker();
        }
    }
}

// FUNCTION: WIZ8 0x004919E0
char MarchWorldCursorTarget(srVector3T<float>* target)
{
    srVector3T<float> trace_from;
    srVector3T<float> origin;
    srVector3T<float> last_valid;
    srVector3T<float> step_pos;
    srVector3T<float> end_pos;
    srVector3T<float> dir;
    srVector3T<float> scaled;
    srVector3T<float> probe;
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
    dist = (*target - origin).Length();
    if (dist == g_double_zero) {
        return 0;
    }
    last_valid = gp3DCursor->position;
    step = 0.0;
    if (g_double_zero < dist) {
        do {
            lower_count = 0;
            step += g_double_fifteen_and_five_eighths;
            upper_found = false;
            upper_best = -1e+10f;
            lower_best = -1e+10f;
            if (dist < step) {
                step = dist;
            }
            dir = *target - origin;
            scaled = dir;
            scaled.SetLength(step);
            step_pos = origin + scaled;
            end_pos = step_pos;
            for (i = 4; i < 8; i++) {
                probe = gp3DCursor->probe_offsets[i];
                probe *= g_cursor_probe_shrink_scale;
                probe += step_pos;
                const auto ground_probe = g_octree->settleToGround(probe.glmValue(), 1, 500.0f);
                probe = ground_probe.position;
                ground = ground_probe.position.y;
                if (ground <= step_pos.y) {
                    if (ground < step_pos.y) {
                        lower_count++;
                        if (lower_best < ground) {
                            lower_best = ground;
                        }
                    }
                } else if (ground - step_pos.y < g_float_one_thousand && upper_best < ground) {
                    upper_found = true;
                    upper_best = ground;
                }
            }
            if (gp3DCursor->track_ground) {
                if (upper_found) {
                    end_pos.y = upper_best + g_cursor_ground_probe_lift;
                    target->y = end_pos.y;
                    origin.y = end_pos.y;
                } else if (lower_count == 4) {
                    end_pos.y = lower_best + g_cursor_ground_probe_lift;
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
    if (target->y - lower_best < g_cursor_ground_step_threshold) {
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
    srVector3T<float> camera(0.0f, 1.0f, 0.0f);
    srVector3T<float> target;
    W8WorldCursorState* cursor;
    camera = cameraLocation();
    camera.y -= g_default_world_height;
    cursor = gp3DCursor;
    cursor->last_published = -100000000.0f;
    target = camera.glmValue() +
             glm::rotateY(glm::vec3(0, 0, g_cursor_projection_distance), g_gd_camera->m_yaw);
    cursor->position = camera;
    cursor->track_ground = true;
    if (MarchWorldCursorTarget(&target) == 0) {
        cursor->position = target;
        cursor->cam_rel_offset = cursor->position;
        if (!cursor->detached) {
            cursor->cam_rel_offset -= camera;
        }
        target.y += g_cursor_sight_probe_height;
        PointCameraAtTarget(&target, true, false);
    } else {
        cursor->position = target;
    }
}

/* Arm the world cursor's footprint mode and install the two fixed probe
   offsets the target resolver probes in place of the probe box. */
// FUNCTION: WIZ8 0x00492190
void SetWorldCursorExtents(const srVector3T<float>* minimum, const srVector3T<float>* maximum)
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
int ResolveWorldCursorTarget(srVector3T<float>* position)
{
    W8WorldCursorState* cursor = gp3DCursor;
    float best = g_cursor_ground_height_sentinel;

    if (cursor == 0) {
        position->SetZero();
    } else {
        *position = cursor->position;
    }
    if (cursor == 0) {
        return 1;
    }
    for (int corner = 0; corner < 4; ++corner) {
        glm::vec3 offset;
        if (!cursor->footprint_mode) {
            offset = cursor->probe_offsets[corner + 4].glmValue();
        } else {
            switch (corner) {
            case 0:
                offset = cursor->extent_min.glmValue();
                break;
            case 1:
                offset = cursor->extent_max.glmValue();
                break;
            case 2:
                offset = {cursor->extent_min.x, 0, cursor->extent_max.z};
                break;
            default:
                offset = {cursor->extent_max.x, 0, cursor->extent_min.z};
                break;
            }
        }
        const auto ground = g_octree->settleToGround(position->glmValue() + offset, true, 500);
        if (best <= ground.position.y) best = ground.position.y;
    }
    if (cursor->footprint_mode &&
        static_cast<float>(g_monster_poster_max_distance) <= fabs(best - position->y)) {
        position->y = best + g_float_one_thousand;
        return 0;
    }
    position->y = best + g_float_one_thousand;
    return 1;
}

// FUNCTION: WIZ8 0x00492500
void GetWorldCursorTargetPosition(srVector3T<float>* position)
{
    srVector3T<float> resolved;

    ResolveWorldCursorTarget(&resolved);
    *position = resolved;
}
