#include <cstdlib>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <exception>
#include <memory>
#include <string>

#include "compat/compiler.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/3dapi.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/GDFileIO.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/float_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/SpellVisual.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/UpdateMesh.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/xstatus.h"
#include "wiz8/sr_api.h"
#include "wiz8/world_cursor.h"
#include "surrender/srNode.h"
#include "surrender/srCamera.h"
#include "surrender/srScene.h"
#include "wiz8/filesystem.h"
#include "input.h"
#include "camera_motion.h"
#include "compat/native.h"
#include "environment_colour.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "named_position.h"
#include "surrender/srMath.h"

class srModelInstance;

#define THREE_D_API_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\3dapi.cpp"

// GLOBAL: WIZ8 0x00607d7c
bool g_renderer_ready = true;

// GLOBAL: WIZ8 0x00607d80
static int g_game_data_runtime_pending = 1;

// GLOBAL: WIZ8 0x005ec240
const double g_automap_refresh_distance_squared = 250000.0;

// GLOBAL: WIZ8 0x00659757
bool g_world_cleanup_flag;
// GLOBAL: WIZ8 0x00659a80
static std::vector<W8World*> g_worlds;

// GLOBAL: WIZ8 0x006081f8
bool g_navigator_vertical_enabled = true;

// GLOBAL: WIZ8 0x00607d7d
bool g_world_mesh_update_enabled = true;

// GLOBAL: WIZ8 0x00609c88
float g_mesh_view_half_angle_degrees = 60.0f;

/* Retail flag 0x10 sets this latch; no reader survives in the shipped image. */
// GLOBAL: WIZ8 0x00609c8c
bool g_world_update_requested = true;

/* Load the renderer-facing half of one level. The subordinate OCT, game-data,
   and ReadLevel parsers remain their original owners; this routine establishes
   their order, inputs, and rollback-visible world  */
// FUNCTION: WIZ8 0x0044F5F0
unsigned char LoadWorld(W8World* world, char* level_file_name, const char* level_folder,
                        const char* asset_folder, bool use_octree)
{
    char extension[4];
    char level_path[1024];
    char pvl_path[1024];
    char oct_path[1024];
    char game_data_path[1024];
    char material_path[1024];
    char material_folder[1024];

    if (world == 0) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x15f, 0);
    }

    ResetGrObjectIdCounter();
    sprintf(level_path, "%s\\%s", level_folder, level_file_name);
    strcpy(extension, level_file_name + strlen(level_file_name) - 3);
    level_file_name[strlen(level_file_name) - 4] = '\0';

    strcpy(pvl_path, level_path);
    pvl_path[strlen(pvl_path) - 3] = 'p';
    sprintf(game_data_path, "%s\\%s.wgd", level_folder, level_file_name);
    sprintf(oct_path, "%s\\%s.oct", level_folder, level_file_name);
    sprintf(material_path, "%s\\%s.mat", level_folder, level_file_name);
    sprintf(material_folder, "%s", asset_folder);

    g_worlds.push_back(world);

    world->m_loaded = true;
    world->nodes_to_disable.clear();
    world->nodes_to_disable.reserve(10);

    world->transient_lights.clear();
    world->transient_lights.reserve(10);

    world->game_data = 0;

    if (CheckLevelAssetSet(oct_path) >= 0) {
        world->octree = new W8Octree(oct_path, &world->game_data);
        if (world->octree != 0 && world->octree->HasLoadError()) {
            delete world->octree;
            world->octree = 0;
        } else if (world->octree != 0) {
            use_octree = false;
        }
    }

    if (game_data_path[0] != '\0' && world->game_data == 0) {
        world->game_data = ReadGameData(game_data_path, false);
        if (world->game_data != 0 && InitializeGameData(world->game_data) == 0) {
            return 0;
        }
    }

    SetSceneAmbientLightWhite(world->static_scene);
    world->camera = CreateOrSetGameCamera(world->static_scene, 0);
    world->camera_light = CreateRangedWorldLight(world, "CameraLight");
    world->camera_light->ambient = glm::vec3(0);
    world->camera_light->diffuse = glm::vec3(1.0f, 0.85f, 0.39f);
    world->camera_light->specular = glm::vec3(0);
    world->camera_light->intensity = 1.0f;
    world->camera_light->setGroupMask(2);
    ConfigureWorldLight(world->camera_light, 4000.0f);

    world->psrMeshes.clear();
    if (world->octree != nullptr && world->octree->GetMeshCount() != 0) {
        world->psrMeshes.resize(static_cast<std::size_t>(world->octree->GetMeshCount()) + 1,
                                nullptr);
        strcpy(level_path, pvl_path);
    }

    world->quads.reset();
    world->update_mesh_source = 0;
    memset(world->m_unknown_07c, 0, 0x10);

    std::unique_ptr<wiz8::File> handle = [&]() { try { return wiz8::open_file(level_path, wiz8::OpenMode::read); } catch (const std::exception&) { return std::unique_ptr<wiz8::File>{}; } }();
    if (handle == 0) {
        srAssertFail("hFile", THREE_D_API_CPP, 0x1d8, "Could not open level file.");
    }
    unsigned char success = ReadLevel(world, handle.get(), use_octree, material_folder);
    if (handle) handle->close();
    handle.reset();
    if (success == 0) {
        srAssertFail("fSuccess", THREE_D_API_CPP, 0x1dd,
                     "Problem loading level, please check files and versions.");
    }
    /* ReadLevel failure is assertion-only in the canonical body. LoadWorld
       still performs the mesh update and returns success. */

    world->m_unknown_0d4[0] = 0;
    if (world->octree != 0) {
        UpdateWorldOctree(world);
    } else if (world->quads != 0) {
        UpdateWorldMeshFromQuads(world);
    }
    return 1;
}

// FUNCTION: WIZ8 0x0044F1C0
W8World* CreateWorld()
{
    auto world = std::make_unique<W8World>();
    world->environment_colour.SetZero();

    // The scene owns its children. Keep both allocations scoped until setup
    // succeeds, including failures while allocating nodes or assigning names.
    auto scene = std::make_unique<srScene>(nullptr);
    world->static_scene = scene.get();
    scene->setName("Sir-Tech Scene Static Scene");

    world->level = new stLevel(scene.get());
    world->level->setName("Sir-Tech Level");
    world->level->m_render_exclusion_mask = 1;

    world->dynamic_scene = new srNode(scene.get());
    world->dynamic_scene->setName("Sir-Tech Dynamic Scene");
    SetSceneAmbientLightWhite(scene.get());
    world->environment_range_end = 1.0f;
    world->environment_range_blue = 1.0f;
    world->environment_range_start = 0.75f;

    // The existing DestroyWorld boundary takes over after successful setup.
    scene.release();
    return world.release();
}

// FUNCTION: WIZ8 0x0044F400
void UpdateWorlds(void)
{
    g_navigator_vertical_enabled = !(gfKeyState[VK_CONTROL] != 0 && g_combat_state != 0 &&
                                     (g_combat_state->round_active || gXStatus.fPartyMovementMode));

    {
        int count = static_cast<int>(g_worlds.size());
        for (int index = 0; index < count; ++index) {
            UpdateWorld(g_worlds[index]);
        }
    }

    UpdateWorldMeshAfterLoad();

    UpdateTimedTriggerEvents();
    UpdateShakeEffects();
    UpdateEnvironment();
    UpdateNpcEvents();
}

// FUNCTION: WIZ8 0x0044F4E0
void UpdateWorld(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x106, 0);
        CreateTriggerShakeEvent(0x6a4, 70.0f, 5000.0f, true);
    }

    if (world != g_secondary_world) {
        UpdateWorldMonsters(world);
        UpdateWorldMissiles(world);
        UpdateWorldSpellVisuals(world);
        UpdateAmbientSounds(world);
        WorldUpdateLights(world);
        RunMonsterGenerators();
        UpdateWorldTriggers(world);
        if (world->octree != 0) {
            RunMasterFunctions();
        }
        UpdateSpellEffects();
    }

    auto& nodes = world->nodes_to_disable;
    for (srNode* node : nodes) {
        node->setFlag(srNode::FLAG_DISABLE);
    }
    nodes.clear();
}

// VTABLE: WIZ8 0x005EC208
// class srClientSupport<srNode,4096>
/* Detach the item meshes in every registered world before the renderer-side
   resource transition. */
// FUNCTION: WIZ8 0x0044f5b0
void DetachAllWorldItems(void)
{
    int count = static_cast<int>(g_worlds.size());

    for (int index = 0; index < count; ++index) {
        DetachWorldItemMeshes(g_worlds[index]);
    }
}

// GLOBAL: WIZ8 0x005ec1f8
const double g_world_camera_pi = 3.141592653589793;
// GLOBAL: WIZ8 0x005ebce8
const double g_world_camera_half_turn_degrees = 180.0;
// GLOBAL: WIZ8 0x00607d84
static float g_camera_base_horizontal_fov = 85.0f;
// GLOBAL: WIZ8 0x00607d88
static float g_camera_base_vertical_fov = 71.0f;
// GLOBAL: WIZ8 0x00659abc
static float g_camera_sway_horizontal_phase;
// GLOBAL: WIZ8 0x00659ac0
static float g_camera_sway_vertical_phase;

/* Drives the swaying camera view. A positive mode captures the camera's
   field of view in degrees and enters the mode, a negative mode restores the
   captured view and leaves it, and zero only advances the sway while it is
   active: both phases run in degrees per second, wrap at a full turn and
   offset the view plane through sine. */
// FUNCTION: WIZ8 0x00450080
void SetCameraSwayMode(srCamera* camera, int mode)
{
    float elapsed = MoveTimer(2);

    if (mode != 0) {
        if (mode > 0) {
            g_camera_base_horizontal_fov =
                static_cast<float>(camera->getHorizontalFOV() *
                                   (g_world_camera_half_turn_degrees / g_world_camera_pi));
            g_camera_base_vertical_fov = static_cast<float>(
                camera->getVerticalFOV() * (g_world_camera_half_turn_degrees / g_world_camera_pi));
            if (!g_camera_sway_active) {
                g_camera_sway_horizontal_phase = 0.0f;
                g_camera_sway_vertical_phase = 0.0f;
                BeginCameraSway();
            }
        } else {
            double radians = g_world_camera_pi * g_float_inverse_half_turn_degrees;
            camera->setViewPlane(radians * g_camera_base_horizontal_fov,
                                 radians * g_camera_base_vertical_fov);
            EndCameraSway();
            return;
        }
    }
    if (!g_camera_sway_active) {
        return;
    }

    g_camera_sway_horizontal_phase += elapsed * 12.0f;
    g_camera_sway_vertical_phase += elapsed * 11.0f;
    if (g_camera_sway_horizontal_phase > 360.0f) {
        g_camera_sway_horizontal_phase -= 360.0f;
    }
    if (g_camera_sway_vertical_phase > 360.0f) {
        g_camera_sway_vertical_phase -= 360.0f;
    }

    double radians = g_world_camera_pi * g_float_inverse_half_turn_degrees;
    camera->setViewPlane(
        (sin(radians * g_camera_sway_horizontal_phase) * 0.75 + g_camera_base_horizontal_fov) *
            radians,
        (sin(radians * g_camera_sway_vertical_phase) * 0.63f + g_camera_base_vertical_fov) *
            radians);
}

/* Entity teardown still has gameplay/rendering side effects. Keep its order
   explicit while the collection storage itself is owned by W8World. */
static void DestroyWorldEntities(W8World* world)
{
    wiz8::remove_all_and_delete_in_order(world->monster_generators);
    wiz8::remove_all_and_delete_in_order(world->plsMonsters);
    if (g_world_cleanup_flag)
        RenderFrame();
    wiz8::remove_all_and_delete_in_order(world->plsItems);
    if (g_world_cleanup_flag)
        RenderFrame();
    wiz8::remove_all_and_delete_in_order(world->plsProps);
    if (g_world_cleanup_flag)
        RenderFrame();

    DestroyAllWorldTriggers(world);
    if (g_world_cleanup_flag)
        RenderFrame();

    world->plsCameras.clear();

    StopAllAmbientSounds();
    wiz8::clear_and_dispose(world->plsAmbientSounds, DestroyAmbientSound);
    if (g_world_cleanup_flag)
        RenderFrame();

    wiz8::remove_all_and_delete_in_order(world->particles);
    world->named_positions.clear();
    DestroyAllMissiles(world);
    DestroyAllSpellVisuals(world);
    DestroyWorldLights(world);
    world->transient_lights.clear();
    world->nodes_to_disable.clear();
    world->lights_to_update.clear();
    world->collidable_props.clear();
}

// FUNCTION: WIZ8 0x0044FAF0
void DestroyWorld(W8World* world)
{
    if (world == 0) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x1ff, 0);
    }
    world->psrMeshes.clear();
    if (world->update_mesh_source != 0) {
        world->update_mesh_source = 0;
    }
    if (world->game_data != 0) {
        delete world->game_data;
        world->game_data = 0;
    }
    if (world->octree != 0) {
        delete world->octree;
        world->octree = 0;
    }
    world->quads.reset();
    if (g_world_cleanup_flag)
        RenderFrame();
    DestroyWorldEntities(world);
    if (g_world_cleanup_flag)
        RenderFrame();
    if (IsWorldCursorVisible())
        HideWorldCursor();

    wiz8::erase_first(g_worlds, world);
    ReleaseRendererObject(world->static_scene);
    delete world;
}

/* Per-frame camera / camera-path update for one world. Main-game frames the
   primary world, then the sky world with flag 0x40 so the motion pass is
   skipped there. Function-local statics keep the last refresh position and the
   last applied rotation; their atexit thunks are at 0x00450070 / 0x00450060. */
// FUNCTION: WIZ8 0x0044FC20
void UpdateWorldCameraAndPaths(W8World* world, unsigned int flags)
{
    static srVector3T<float> s_last_automap_refresh_position;
    static glm::mat3 s_saved_camera_rotation;
    srVector3T<float> camera_position;
    srVector3T<float> navigator_position;
    srVector3T<float> delta;
    glm::dvec3 render_position;
    glm::mat3 rotation;
    glm::mat3 path_rotation;
    glm::mat3 motion_saved;
    W8CameraPath* camera_path;
    W8PathAI* path;
    float yaw;
    float pitch;
    int camera_count;
    int index;
    float dx;
    float dy;
    float dz;

    RefreshDirtyAutomap();
    camera_position = cameraLocation();
    if (g_game_data_runtime_pending != 0 && world->game_data != 0) {
        UpdateGameDataRuntime();
        g_game_data_runtime_pending = 0;
    }
    if (world->game_data != 0) {
        if (g_level_flags != 0) {
            *g_level_flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        }
        if (world->game_data != 0 && (flags & W8_CAMERA_MOTION_SKIP) == 0) {
            rotation = world->camera->getRotation<glm::mat3>();
            world->game_data->ApplyCameraMotionFlags(flags, &rotation, &motion_saved);
            world->camera->setRotation(rotation);
            s_saved_camera_rotation = rotation;
            yaw = GetCameraYawInDegrees();
            pitch = GetCameraPitchInDegrees();
            g_startup_world->SetAngles(yaw);
            g_startup_world->SetPitch(pitch);
            if (world->game_data->ApplyCameraMotion(flags, &camera_position, &delta,
                                                    &motion_saved) != 0) {
                camera_position += delta;
                navigator_position.Set(camera_position.x,
                                       camera_position.y - g_default_world_height,
                                       camera_position.z);
                if (world->camera_light != 0) {
                    render_position = glm::dvec3(camera_position.glmValue());
                    static_cast<srNode*>(world->camera_light)->setLocation(render_position);
                }
                render_position = glm::dvec3(camera_position.glmValue());
                static_cast<srNode*>(world->camera)->setLocation(render_position);
                g_startup_world->SetPositionInternal(&navigator_position);
                dx = camera_position.x - s_last_automap_refresh_position.x;
                dy = camera_position.y - s_last_automap_refresh_position.y;
                dz = camera_position.z - s_last_automap_refresh_position.z;
                if (dx * dx + dy * dy + dz * dz > g_automap_refresh_distance_squared) {
                    s_last_automap_refresh_position = camera_position;
                    camera_position.y -= g_default_world_height;
                    if (AutomapHasCellAt(&camera_position)) {
                        SetWorldMeshVertexLightTable(g_world, 1);
                        UpdateAutomapBounds();
                        SetWorldMeshVertexLightTable(g_world, 0);
                    }
                }
                DispatchWorldCursorNodeCommand(0, 0, 0);
            }
        } else {
            world->camera->setRotation(s_saved_camera_rotation);
        }
    } else {
        world->camera->setRotation(s_saved_camera_rotation);
    }
    SetCameraSwayMode(world->camera, 0);
    camera_count = static_cast<int>(world->plsCameras.size());
    if (camera_count != 0) {
        for (index = 0; index < camera_count; ++index) {
            camera_path = GetWorldCameraPath(world, index);
            if (camera_path != 0 && camera_path->active) {
                path = camera_path->path.get();
                PathAITick(path, 1);
                PathAIApply(path, world->camera);
                {
                    srVector3T<double> location = world->camera->getLocation();
                    srVector3T<float> party_point;
                    party_point = glm::vec3(location.glmValue());
                    PlacePartyAtPoint(&party_point);
                }
                path_rotation = world->camera->getRotation<glm::mat3>();
                if (g_secondary_world != 0 && g_secondary_world->camera != 0) {
                    g_secondary_world->camera->setRotation(path_rotation);
                }
                ApplyCameraRotation(&path_rotation);
                if (path->discrete_mode != 0) {
                    if (path->position >= static_cast<int>(path->nodes.size()) - g_float_one) {
                        UpdateCameraPathState(world, camera_path, 0);
                    }
                } else if (path->position >= g_double_one) {
                    UpdateCameraPathState(world, camera_path, 0);
                }
            }
        }
    }
}

/* Note that the renderer is up. Eight bytes and no branch. */
// FUNCTION: WIZ8 0x00451010
void MarkRendererReady(void)
{
    g_renderer_ready = true;
}

/* Resolve a particle by its runtime name from the current world's particle
   vector. Names are case-insensitive in the original registry-facing lookup. */
// FUNCTION: WIZ8 0x00451080
stParticle* FindParticleByName(W8World* world, const char* name)
{
    if (world != 0) {
        int count = static_cast<int>(world->particles.size());

        for (int index = 0; index < count; ++index) {
            stParticle* particle = world->particles[index];

            if (_stricmp(particle->getName().c_str(), name) == 0) {
                return particle;
            }
        }
    }
    return 0;
}

/* Two forwarders that pass their arguments through unchanged. */
// FUNCTION: WIZ8 0x00451140
void ForwardDestroyWorld(W8World* world)
{
    DestroyWorld(world);
}

/* Read the camera position when one is attached to the world. The sole
   caller preserves this value across a level reload, so a world without a
   camera contributes the zero position. */
// FUNCTION: WIZ8 0x00451160
glm::vec3 cameraLocationOrZero(const W8World& world)
{
    return world.camera ? cameraLocation(world) : glm::vec3(0);
}

/* Set the view position after loading or traversing a portal. The camera light
   follows the camera, while only the camera move publishes the new game-space
   position. */
// FUNCTION: WIZ8 0x004511D0
void SetWorldScenePosition(W8World* world, const srVector3T<float>* location)
{
    srVector3T<float> position;
    glm::dvec3 render_position;

    if (!world) {
        srAssertFail("pWorld", THREE_D_API_CPP, 1043, 0);
    }

    position = *location;
    if (world->camera != 0) {
        render_position = glm::dvec3(position.glmValue());
        static_cast<srNode*>(world->camera)->setLocation(render_position);
        PlacePartyAtPoint(&position);
    }
    if (world->camera_light != 0) {
        render_position = glm::dvec3(position.glmValue());
        static_cast<srNode*>(world->camera_light)->setLocation(render_position);
    }
}

// FUNCTION: WIZ8 0x00451020
void UpdateWorldMeshAfterLoad(void)
{
    if (g_renderer_ready && g_world_mesh_update_enabled) {
        if (g_world->octree != 0) {
            UpdateWorldOctree(g_world);
        } else if (g_world->quads != 0) {
            UpdateWorldMeshFromQuads(g_world);
        }
        RepositionAmbientSounds(g_world);
        RequestRefreshPartyState();
        g_renderer_ready = false;
    }
}

// FUNCTION: WIZ8 0x00451110
unsigned char ForwardLoadWorld(W8World* world, char* level_file_name, const char* level_folder,
                               const char* asset_folder, bool use_octree)
{
    return LoadWorld(world, level_file_name, level_folder, asset_folder, use_octree);
}

/* Report a failed assertion with no message of its own, so the expression and
   the site are all the caller has to give. */
// FUNCTION: WIZ8 0x00450780
void ReportAssertion(const char* expression, const char* source_path, w8_long line)
{
    srAssertFail(expression, source_path, line, 0);
}

/* The camera's rotation basis, read straight back out of the renderer node.
   Both assertions belong to this body: 3dapi.cpp:975 names the world and
   3dapi.cpp:976 names its camera member pWorld->psrCamera, which is what puts
   the camera at 0x44 rather than anywhere else. */
// FUNCTION: WIZ8 0x004503c0
glm::mat3 cameraRotation(const W8World& world)
{
    if (!world.camera) {
        srAssertFail("pWorld->psrCamera", THREE_D_API_CPP, 0x3d0, 0);
    }
    return world.camera->getRotation<glm::mat3>();
}

/* Moves the camera and its camera light together. Only the camera's move
   notifies the level; the light simply follows the view. */
// FUNCTION: WIZ8 0x00450420
void WorldSetCameraLocation(W8World* world, const srVector3T<float>* location)
{
    glm::dvec3 position;

    if (!world) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x422, 0);
    }
    if (world->camera != 0) {
        position = glm::dvec3(location->glmValue());
        world->camera->setLocation(position);
        PlacePartyAtPoint(location);
    }
    if (world->camera_light != 0) {
        position = glm::dvec3(location->glmValue());
        world->camera_light->setLocation(position);
    }
}

/* Apply a CamPos record to the world's camera and camera light. A non-null
   source world repeats the orientation write; automap restore passes null and
   recall / LoadLevel pass GetSecondaryWorld(). */
// FUNCTION: WIZ8 0x004504B0
void SetWorldCameraState(W8World* world, W8World* source_world, W8WorldCameraState* state)
{
    srVector3T<float> location;
    glm::dvec3 position;
    glm::mat3 rotation;

    if (!world) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x43f, 0);
    } else {
        if (state == 0) {
            srAssertFail("CamPos", THREE_D_API_CPP, 0x444, 0);
        }
        location = state->position;
        if (world->camera != 0) {
            position = glm::dvec3(location.glmValue());
            ((srNode*)world->camera)->setLocation(position);
            PlacePartyAtPoint(&location);
        }
        if (world->camera_light != 0) {
            position = glm::dvec3(location.glmValue());
            ((srNode*)world->camera_light)->setLocation(position);
        }
        rotation = world->camera->getRotation<glm::mat3>();
        SetCameraOrientation(state->yaw, state->pitch, &rotation);
        world->camera->setRotation(rotation);
    }
    if (source_world != 0) {
        if (state == 0) {
            srAssertFail("CamPos", THREE_D_API_CPP, 0x44e, 0);
        }
        rotation = world->camera->getRotation<glm::mat3>();
        SetCameraOrientation(state->yaw, state->pitch, &rotation);
        world->camera->setRotation(rotation);
    }
}

/* Reinstall a CamPos record, then copy the resulting camera pose onto the
   startup navigator in degrees with the default world height removed. */
// FUNCTION: WIZ8 0x00450610
void RestoreWorldCameraState(W8World* world, W8World* source_world, W8WorldCameraState* state)
{
    srVector3T<float> navigator_position;

    if (world == 0) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x46b, 0);
    }
    SetWorldCameraState(world, source_world, state);
    navigator_position = glm::vec3(world->camera->getLocation());
    g_startup_world->SetAngles(GetCameraYawInDegrees());
    g_startup_world->SetPitch(GetCameraPitchInDegrees());
    navigator_position.y -= g_default_world_height;
    g_startup_world->SetPositionInternal(&navigator_position);
}

/* Snapshot the world's camera location and the live yaw/pitch into a CamPos
   record. A world without a camera contributes the zero position. */
// FUNCTION: WIZ8 0x004506C0
W8WorldCameraState cameraState(const W8World& world)
{
    W8WorldCameraState state{};
    state.position = cameraLocationOrZero(world);
    GetCameraOrientation(state.yaw, state.pitch);
    return state;
}

/* Read the camera node's double-precision renderer position back into the
   world's float position type. */
// FUNCTION: WIZ8 0x00450750
glm::vec3 cameraLocation(const W8World& world)
{
    return glm::vec3(world.camera->getLocation());
}

/* The second world, read straight out of the global with no guard. Its type is
   settled by the viewport, which reads a camera member through the same
   object. */
// FUNCTION: WIZ8 0x004512a0
W8World* GetSecondaryWorld(void)
{
    return g_secondary_world;
}

/* Runtime/debug adjustment dispatcher for the active world's clipping and
   environment controls. Bits not handled here are intentionally ignored. */
// FUNCTION: WIZ8 0x00450210
void ApplyWorldUpdateFlags(W8World* world, unsigned int flags)
{
    float scale;

    if (world == 0) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x321, 0);
    }
    if ((flags & W8_WORLD_INCREASE_FAR_CLIP) != 0) {
        WorldSetFarClip(world,
                        static_cast<float>(WorldGetFarClip(world)) + g_position_height_epsilon);
        scale = 2.0f;
        if (WorldGetFarClip(world) >= g_float_fifty_thousand) {
            scale = 1.5f;
        }
        WorldSetRenderRange(world, static_cast<float>(WorldGetFarClip(world)) * scale);
    }
    if ((flags & W8_WORLD_DECREASE_FAR_CLIP) != 0) {
        WorldSetFarClip(world,
                        static_cast<float>(WorldGetFarClip(world)) - g_position_height_epsilon);
        scale = 2.0f;
        if (WorldGetFarClip(world) >= g_float_fifty_thousand) {
            scale = 1.5f;
        }
        WorldSetRenderRange(world, static_cast<float>(WorldGetFarClip(world)) * scale);
    }
    if ((flags & W8_WORLD_WIDEN_MESH_VIEW) != 0 &&
        g_mesh_view_half_angle_degrees < g_float_half_turn_degrees) {
        g_mesh_view_half_angle_degrees += g_float_ten;
    }
    if ((flags & W8_WORLD_NARROW_MESH_VIEW) != 0 && g_mesh_view_half_angle_degrees > g_float_ten) {
        g_mesh_view_half_angle_degrees -= g_float_ten;
    }
    if ((flags & 0x10) != 0) {
        g_world_update_requested = true;
    }
    if ((flags & W8_WORLD_TOGGLE_LOADED) != 0) {
        world->m_loaded = !world->m_loaded;
    }
    if ((flags & W8_WORLD_INCREASE_ENVIRONMENT) != 0) {
        SetWorldEnvironmentIntensity(g_world,
                                     GetWorldEnvironmentIntensity(g_world) + g_float_one_fiftieth);
    }
    if ((flags & W8_WORLD_DECREASE_ENVIRONMENT) != 0) {
        SetWorldEnvironmentIntensity(g_world,
                                     GetWorldEnvironmentIntensity(g_world) - g_float_one_fiftieth);
    }
}

// FUNCTION: WIZ8 0x004503B0
int ForwardSelectedPropIndex(W8World*, int, int)
{
    return GetSelectedPropIndex();
}

// FUNCTION: WIZ8 0x00451150
bool ForwardActivateSelectedProp(W8World*, int, int, int)
{
    return ActivateSelectedProp();
}

// FUNCTION: WIZ8 0x00451100
W8World* ForwardCreateWorld(void)
{
    return CreateWorld();
}

/* Resolve the PARTY alias or a named position stored with the current world. */
// FUNCTION: WIZ8 0x004512C0
bool FindEntityByName(const char* name, srVector3T<float>* position, float* angle,
                      srVector3T<float>* direction)
{
    if (name == 0) {
        return false;
    }
    if (_stricmp("PARTY", name) == 0) {
        *position = cameraLocation();
        return true;
    }

    int count = static_cast<int>(g_world->named_positions.size());
    for (int index = 0; index < count; ++index) {
        const W8NamedPosition* entry = &g_world->named_positions[index];
        if (_stricmp(name, entry->name) == 0) {
            *position = entry->position;
            if (angle != 0) {
                *angle = entry->angle;
            }
            if (direction != 0) {
                *direction = entry->direction;
            }
            return true;
        }
    }
    return false;
}

/* Push a candidate position clear of static geometry, then reject it when it
   overlaps another world item or monster. */
// FUNCTION: WIZ8 0x00451390
unsigned char AdjustWorldCollisionPosition(float radius, srVector3T<float>* position,
                                           bool check_items, bool check_monsters)
{
    float threshold = radius * g_path_endpoint_scale;
    float angle = 0.0f;

    do {
        srVector3T<float> first(0.0f, 0.0f, radius);
        first.RotateAboutY(sin(angle), cos(angle));
        first += *position;

        double opposite_angle = angle + g_camera_pi;
        srVector3T<float> second(0.0f, 0.0f, radius);
        second.RotateAboutY(sin(opposite_angle), cos(opposite_angle));
        second += *position;

        g_octree->TraceLineOfSight(position, &first, true, -3, -3, true, 0);
        g_octree->TraceLineOfSight(position, &second, true, -3, -3, true, 0);

        first -= *position;
        second -= *position;
        float first_distance = first.Length();
        if (first_distance < threshold && second.Length() < threshold) {
            return 0;
        }
        if (first_distance < threshold) {
            first.SetLength(radius - first_distance);
            *position -= first;
        } else {
            second.SetLength(radius - second.Length());
            *position -= second;
        }
        angle += 0.7853981256484985f;
    } while (angle < g_camera_pi);

    if (check_items) {
        W8WorldItem* item = GetNextWorldItem(true);
        while (item != 0) {
            W8Item* owner = item->p3D;
            if (owner != 0) {
                srVector3T<float> center;
                center = owner->GetSearchPosition();
                const auto other_radius = owner->GetBoundsRadius();
                srVector3T<float> delta = center - *position;
                if (delta.Length() < other_radius + radius) {
                    return 0;
                }
            }
            item = GetNextWorldItem(false);
        }
    }

    if (check_monsters) {
        W8MonsterInfo* info = GetNextMonsterInfo(true);
        while (info != 0) {
            W8Monster* monster = info->p3D;
            if (monster != 0) {
                const auto center = monster->GetAnimationCenter();
                float other_radius;
                if (center && monster->GetAnimationRadius(&other_radius) &&
                    (*center - *position).Length() < other_radius + radius) {
                    return 0;
                }
            }
            info = GetNextMonsterInfo(false);
        }
    }
    return 1;
}

/* Try the requested point first, then the two retail half-turn probe points
   around it; only an unobstructed probe is handed to the overlap resolver. */
// FUNCTION: WIZ8 0x00451800
unsigned char FindNearbyFreePosition(float radius, srVector3T<float>* position, bool check_items,
                                     bool check_monsters)
{
    srVector3T<float> candidate = *position;
    if (AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
        *position = candidate;
        return 1;
    }

    float angle = 0.0f;
    float diameter = radius + radius;
    do {
        srVector3T<float> offset(0.0f, 0.0f, diameter);
        offset.RotateAboutY(sin(angle), cos(angle));
        candidate = offset + *position;
        srVector3T<float> unblocked = candidate;

        g_octree->TraceLineOfSight(position, &candidate, true, -3, -3, true, 0);
        if (candidate == unblocked &&
            AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
            *position = candidate;
            return 1;
        }
        angle += g_quarter_turn;
    } while (angle < g_camera_pi);
    return 0;
}
