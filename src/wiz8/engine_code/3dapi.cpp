#include "wiz8/engine_code/3dapi.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "asset_paths.h"
#include "camera_motion.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "input.h"
#include "layouts/world.h"
#include "named_position.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "surrender/srFog.h" // IWYU pragma: keep - completes world-owned fog nodes.
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/Camera.h"
#include "wiz8/engine_code/Environment.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/GDFileIO.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/GrCycle.h"
#include "wiz8/engine_code/GrObject.h"
#include "wiz8/engine_code/Item.h"
#include "wiz8/engine_code/Level.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/OctPath.h"
#include "wiz8/engine_code/Octree.h"
#include "wiz8/engine_code/PathAI.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/ReadLevel.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/UpdateMesh.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stCube.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stParticle.h"
#include "wiz8/filesystem.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/world_cursor.h"
// Completes MonGen for templated world-registry destruction.
#include "wiz8/local_code/MonsterGenerator.h" // IWYU pragma: keep
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/SpellEffect.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/startup_world.h"
#include "wiz8/xstatus.h"

constexpr double kAutomapRefreshDistanceSquared = 250000.0;

class srModelInstance;

#define THREE_D_API_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\3dapi.cpp"

// GLOBAL: WIZ8 0x00659757
bool g_world_cleanup_flag;
W8GameData* GetWorldGameData()
{
    return GetWorld() ? GetWorld()->game_data.get() : nullptr;
}
W8Octree* GetWorldOctree()
{
    return GetWorld() ? GetWorld()->octree.get() : nullptr;
}
W8PathingService* WorldPathing(const W8World& world)
{
    return world.octree ? world.octree->pathing.get() : nullptr;
}
W8PathingService* GetWorldPathing()
{
    return GetWorld() ? WorldPathing(*GetWorld()) : nullptr;
}
// GLOBAL: WIZ8 0x006081f8
bool g_navigator_vertical_enabled = true;

// GLOBAL: WIZ8 0x00609c88
float g_mesh_view_half_angle_degrees = 60.0f;

/* Build scene resources under a local owner. No active world is required;
   malformed resources destroy only this unpublished result. */
// FUNCTION: WIZ8 0x0044F5F0
std::unique_ptr<W8World> LoadWorld(int level_id, std::string source_path, bool use_octree)
{
    auto owner = std::make_unique<W8World>(level_id, std::move(source_path));
    auto* world = owner.get();
    const auto& resource_path = world->source_path;
    ResetGrObjectIdCounter();
    const auto path = wiz8::path_from_utf8(resource_path);
    std::string level_path(resource_path);
    const auto sibling = [&](const char* extension) {
        auto result = path;
        result.replace_extension(extension);
        return wiz8::path_to_utf8(result);
    };
    const auto pvl_path = sibling(".pvl");
    const auto oct_path = sibling(".oct");
    const auto game_data_path = sibling(".wgd");
    const auto material_folder = wiz8::path_to_utf8(path.parent_path() / "Bitmaps");

    if (CheckLevelAssetSet(oct_path.c_str()) >= 0) {
        world->octree = std::make_unique<W8Octree>(oct_path.c_str(), &world->game_data);
        if (world->octree->HasLoadError())
            world->octree.reset();
        else
            use_octree = false;
    }

    if (world->game_data == nullptr) {
        world->game_data = ReadGameData(game_data_path.c_str(), false);
        if (world->game_data && InitializeGameData(world->game_data.get()) == 0) return nullptr;
    }

    world->camera = world->static_scene->adoptChild(std::make_unique<srCamera>());
    world->camera->setName("Sirtech Camera");
    world->camera->setLocation(glm::dvec3(0.0));
    world->camera->setClipRange(250.0, 75000.0);
    world->camera->setEnvironmentRange(0.0f, 1.0f);
    world->camera->setViewPlane(std::acos(-1.0) / 2.0, std::acos(-1.0) / 2.0);
    world->camera_light = CreateRangedWorldLight(world, "CameraLight");
    world->camera_light->ambient = glm::vec3(0);
    world->camera_light->diffuse = glm::vec3(1.0f, 0.85f, 0.39f);
    world->camera_light->specular = glm::vec3(0);
    world->camera_light->intensity = 1.0f;
    world->camera_light->setGroupMask(2);
    ConfigureWorldLight(world->camera_light, 4000.0f);

    if (world->octree != nullptr && world->octree->GetMeshCount() != 0) {
        world->psrMeshes.resize(static_cast<std::size_t>(world->octree->GetMeshCount()) + 1,
                                nullptr);
        level_path = pvl_path;
    }

    std::unique_ptr<wiz8::File> handle;
    try {
        handle = wiz8::open_file(wiz8::asset_path(level_path), wiz8::OpenMode::read);
    } catch (const std::exception&) {
        srAssertFail("hFile", THREE_D_API_CPP, 0x1d8, "Could not open level file.");
    }
    if (!ReadLevel(world, handle.get(), use_octree, wiz8::asset_path(material_folder))) {
        srAssertFail("fSuccess", THREE_D_API_CPP, 0x1dd,
                     "Problem loading level, please check files and versions.");
    }
    return owner;
}

// The sky world only retires particles and disables queued nodes.
// FUNCTION: WIZ8 0x0044F4E0
static void UpdateWorld(W8World& world)
{
    if (&world != GetSecondaryWorld()) {
        UpdateWorldMonsters(&world);
        UpdateWorldMissiles(&world);
        UpdateWorldSpellVisuals(&world);
        UpdateAmbientSounds(&world);
        WorldUpdateLights(&world);
        RunMonsterGenerators();
        UpdateWorldTriggers(&world);
        if (world.octree != 0) {
            RunMasterFunctions();
        }
        UpdateSpellEffects();
    }

    SweepFinishedParticles(&world);

    for (srNode* node : world.nodes_to_disable) {
        node->setFlag(srNode::FLAG_DISABLE);
    }
    world.nodes_to_disable.clear();
}

// FUNCTION: WIZ8 0x0044F400
void UpdateWorlds(void)
{
    g_navigator_vertical_enabled =
        !(((GetInputState().modifiers & CTRL_DOWN) != 0) && g_combat_state != 0 &&
          (g_combat_state->round_active || gXStatus.fPartyMovementMode));

    // Loading registered the sky first; preserve that update order.
    for (W8World* world : {GetSecondaryWorld(), GetWorld()}) {
        if (world) UpdateWorld(*world);
    }

    RefreshWorldView();

    UpdateTimedTriggerEvents();
    UpdateShakeEffects();
    UpdateEnvironment();
    UpdateNpcEvents();
}

/* Detach the item meshes in the level and sky worlds before the renderer-side
   resource transition. */
// FUNCTION: WIZ8 0x0044f5b0
void DetachAllWorldItems(void)
{
    for (W8World* world : {GetWorld(), GetSecondaryWorld()}) {
        if (world != 0) DetachWorldItemMeshes(world);
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
            double radians = g_world_camera_pi * 0.0055555557f;
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

    double radians = g_world_camera_pi * 0.0055555557f;
    camera->setViewPlane(
        (sin(radians * g_camera_sway_horizontal_phase) * 0.75 + g_camera_base_horizontal_fov) *
            radians,
        (sin(radians * g_camera_sway_vertical_phase) * 0.63f + g_camera_base_vertical_fov) *
            radians);
}

// Drop borrowed mesh indexes before destroying their scene-owned nodes.
static void ClearWorldIndexes(W8World* world)
{
    world->psrMeshes.clear();
    world->update_mesh_source = nullptr;
    world->collidable_props.clear();
}

// Phase 3: destroy entities first, then the level data and spatial index they
// referenced, then the scene itself.
static void DestroyWorldResources(W8World* world)
{
    world->timed_events.clear();
    world->standalone_trigger_events.clear();
    world->monster_generators.clear();
    world->cursor.reset();
    world->cursor_nodes.clear();
    world->active_monsters.clear();
    world->monsters.clear();
    world->plsItems.clear();
    world->plsProps.clear();
    DestroyAllWorldTriggers(world);
    world->plsAmbientSounds.clear();
    world->ambient_tracks.clear();
    world->missiles.clear();
    world->spell_visuals.clear();
    world->particles.clear();
    world->retired_particles.clear();
    DestroyWorldLights(world);
    world->plsCameras.clear();
    world->named_positions.clear();

    world->game_data.reset();
    world->octree.reset();
    world->quads.reset();

    world->static_fog.reset();
    world->dynamic_fog.reset();
    world->static_scene.reset();
}

/* A world starts with its static scene, the level node and the dynamic scene.
   Authored environment ramps are seeded before the level overrides them. */
// FUNCTION: WIZ8 0x0044F1C0
W8World::W8World(int level_id, std::string path)
    : level_id(level_id),
      source_path(path.empty() ? std::string{} : wiz8::normalize_asset_path(path).generic_string()),
      static_scene(std::make_unique<srScene>(nullptr))
{
    static_scene->setName("Sir-Tech Scene Static Scene");
    level = static_scene->adoptChild(std::make_unique<stLevel>(nullptr));
    level->setName("Sir-Tech Level");
    level->m_render_exclusion_mask = 1;
    dynamic_scene = static_scene->adoptChild(std::make_unique<srNode>());
    dynamic_scene->setName("Sir-Tech Dynamic Scene");
    SetSceneAmbientLightWhite(static_scene.get());
}

W8World::~W8World()
{
    ClearWorldIndexes(this);
    DestroyWorldResources(this);
}

/* Per-frame camera / camera-path update for one world. Main-game frames the
   primary world, then the sky world with flag 0x40 so the motion pass is
   skipped there. Function-local statics keep the last refresh position and the
   last applied rotation; their atexit thunks are at 0x00450070 / 0x00450060. */
// FUNCTION: WIZ8 0x0044FC20
void UpdateWorldCameraAndPaths(W8World* world, unsigned int flags)
{
    static glm::vec3 s_last_automap_refresh_position(0);
    static glm::mat3 s_saved_camera_rotation(1);
    glm::vec3 camera_position;
    glm::vec3 navigator_position;
    glm::dvec3 render_position;
    glm::mat3 path_rotation;
    W8CameraPath* camera_path;
    W8PathAI* path;
    float yaw;
    float pitch;
    int camera_count;
    int index;

    RefreshDirtyAutomap();
    camera_position = cameraLocation();
    if (world->game_data != 0) {
        world->game_data->motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        if ((flags & W8_CAMERA_MOTION_SKIP) == 0) {
            const auto orientation =
                world->game_data->cameraRotation(flags, world->camera->getRotation<glm::mat3>());
            world->camera->setRotation(orientation.rotation);
            s_saved_camera_rotation = orientation.rotation;
            yaw = GetCameraYawInDegrees();
            pitch = GetCameraPitchInDegrees();
            PartyNavigator()->SetAngles(yaw);
            PartyNavigator()->SetPitch(pitch);
            const auto motion =
                world->game_data->cameraMotion(flags, camera_position, orientation.movement_basis);
            if (motion.moved) {
                camera_position += motion.displacement;
                navigator_position =
                    glm::vec3(camera_position.x, camera_position.y - g_default_world_height,
                              camera_position.z);
                if (world->camera_light != 0) {
                    render_position = glm::dvec3(camera_position);
                    static_cast<srNode*>(world->camera_light)->setLocation(render_position);
                }
                render_position = glm::dvec3(camera_position);
                static_cast<srNode*>(world->camera)->setLocation(render_position);
                PartyNavigator()->SetPositionInternal(&navigator_position);
                const auto refresh_offset = camera_position - s_last_automap_refresh_position;
                if (glm::dot(refresh_offset, refresh_offset) > kAutomapRefreshDistanceSquared) {
                    s_last_automap_refresh_position = camera_position;
                    camera_position.y -= g_default_world_height;
                    const glm::vec3 automap_position = camera_position;
                    if (AutomapHasCellAt(&automap_position)) {
                        SetWorldMeshVertexLightTable(1);
                        UpdateAutomapBounds();
                        SetWorldMeshVertexLightTable(0);
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
                    glm::dvec3 location = world->camera->getLocation();
                    glm::vec3 party_point;
                    party_point = glm::vec3(location);
                    PlacePartyAtPoint(&party_point);
                }
                path_rotation = world->camera->getRotation<glm::mat3>();
                if (GetSecondaryWorld() != 0 && GetSecondaryWorld()->camera != 0) {
                    GetSecondaryWorld()->camera->setRotation(path_rotation);
                }
                ApplyCameraRotation(&path_rotation);
                if (path->discrete_mode != 0) {
                    if (path->position >= static_cast<int>(path->nodes.size()) - 1.0f) {
                        UpdateCameraPathState(world, camera_path, 0);
                    }
                } else if (path->position >= 1.0) {
                    UpdateCameraPathState(world, camera_path, 0);
                }
            }
        }
    }
}

/* Camera or geometry changes invalidate only this world's cached view. */
// FUNCTION: WIZ8 0x00451010

/* Resolve a particle by its runtime name from the current world's particle
   vector. Names are case-insensitive in the original registry-facing lookup. */
// FUNCTION: WIZ8 0x00451080
stParticle* FindParticleByName(const char* name)
{
    if (GetWorld() != nullptr) {
        int count = static_cast<int>(GetWorld()->particles.size());

        for (int index = 0; index < count; ++index) {
            stParticle* particle = GetWorld()->particles[index].get();

            if (SDL_strcasecmp(particle->getName().c_str(), name) == 0) {
                return particle;
            }
        }
    }
    return 0;
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
void SetWorldScenePosition(W8World* world, const glm::vec3* location)
{
    glm::vec3 position;
    glm::dvec3 render_position;

    if (!world) {
        srAssertFail("pWorld", THREE_D_API_CPP, 1043, 0);
    }

    position = *location;
    if (world->camera != 0) {
        render_position = glm::dvec3(position);
        static_cast<srNode*>(world->camera)->setLocation(render_position);
        PlacePartyAtPoint(&position);
    }
    if (world->camera_light != 0) {
        render_position = glm::dvec3(position);
        static_cast<srNode*>(world->camera_light)->setLocation(render_position);
    }
}

// FUNCTION: WIZ8 0x00451020
void RefreshWorldView()
{
    auto* world = GetWorld();
    if (!world || !world->camera) return;
    if (world->octree)
        UpdateWorldOctree(world);
    else if (world->quads)
        UpdateWorldMeshFromQuads(world);
    RepositionAmbientSounds();
    RequestRefreshPartyState();
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
void WorldSetCameraLocation(const glm::vec3* location)
{
    glm::dvec3 position;

    if (!GetWorld()) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x422, 0);
    }
    if (GetWorld()->camera != 0) {
        position = glm::dvec3((*location));
        GetWorld()->camera->setLocation(position);
        PlacePartyAtPoint(location);
    }
    if (GetWorld()->camera_light != 0) {
        position = glm::dvec3((*location));
        GetWorld()->camera_light->setLocation(position);
    }
}

/* Apply a CamPos record to the world's camera and camera light. A non-null
   source world repeats the orientation write; automap restore passes null and
   recall / LoadLevel pass GetSecondaryWorld(). */
// FUNCTION: WIZ8 0x004504B0
void SetWorldCameraState(W8World* world, W8World* source_world, W8WorldCameraState* state)
{
    if (world == nullptr) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x43f, 0);
    }
    if (state == nullptr) {
        srAssertFail("CamPos", THREE_D_API_CPP, 0x444, 0);
    }
    if (world->camera == nullptr) {
        srAssertFail("pWorld->pCamera", THREE_D_API_CPP, 0x444, 0);
    }

    glm::vec3 location = state->position;
    world->camera->setLocation(glm::dvec3(location));
    PlacePartyAtPoint(&location);
    if (world->camera_light != nullptr) {
        world->camera_light->setLocation(glm::dvec3(location));
    }

    const int orientation_writes = source_world != nullptr ? 2 : 1;
    for (int write = 0; write < orientation_writes; ++write) {
        auto rotation = world->camera->getRotation<glm::mat3>();
        SetCameraOrientation(state->yaw, state->pitch, &rotation);
        world->camera->setRotation(rotation);
    }
}

/* Reinstall a CamPos record, then copy the resulting camera pose onto the
   startup navigator in degrees with the default world height removed. */
// FUNCTION: WIZ8 0x00450610
void RestoreWorldCameraState(W8World* source_world, W8WorldCameraState* state)
{
    glm::vec3 navigator_position;

    if (GetWorld() == nullptr) {
        srAssertFail("pWorld", THREE_D_API_CPP, 0x46b, 0);
    }
    SetWorldCameraState(GetWorld(), source_world, state);
    navigator_position = glm::vec3(GetWorld()->camera->getLocation());
    PartyNavigator()->SetAngles(GetCameraYawInDegrees());
    PartyNavigator()->SetPitch(GetCameraPitchInDegrees());
    navigator_position.y -= g_default_world_height;
    PartyNavigator()->SetPositionInternal(&navigator_position);
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
        if (WorldGetFarClip(world) >= 5e+04f) {
            scale = 1.5f;
        }
        WorldSetRenderRange(world, static_cast<float>(WorldGetFarClip(world)) * scale);
    }
    if ((flags & W8_WORLD_DECREASE_FAR_CLIP) != 0) {
        WorldSetFarClip(world,
                        static_cast<float>(WorldGetFarClip(world)) - g_position_height_epsilon);
        scale = 2.0f;
        if (WorldGetFarClip(world) >= 5e+04f) {
            scale = 1.5f;
        }
        WorldSetRenderRange(world, static_cast<float>(WorldGetFarClip(world)) * scale);
    }
    if ((flags & W8_WORLD_WIDEN_MESH_VIEW) != 0 && g_mesh_view_half_angle_degrees < 1.8e+02f) {
        g_mesh_view_half_angle_degrees += 1e+01f;
    }
    if ((flags & W8_WORLD_NARROW_MESH_VIEW) != 0 && g_mesh_view_half_angle_degrees > 1e+01f) {
        g_mesh_view_half_angle_degrees -= 1e+01f;
    }
    if ((flags & W8_WORLD_INCREASE_ENVIRONMENT) != 0) {
        SetWorldEnvironmentIntensity(GetWorld(), GetWorld()->environment_intensity + 0.02f);
    }
    if ((flags & W8_WORLD_DECREASE_ENVIRONMENT) != 0) {
        SetWorldEnvironmentIntensity(GetWorld(), GetWorld()->environment_intensity - 0.02f);
    }
}

/* Resolve the PARTY alias or a named position stored with the current world. */
// FUNCTION: WIZ8 0x004512C0
bool FindEntityByName(const char* name, glm::vec3* position, float* angle, glm::vec3* direction)
{
    if (name == 0) {
        return false;
    }
    if (SDL_strcasecmp("PARTY", name) == 0) {
        *position = cameraLocation();
        return true;
    }

    int count = static_cast<int>(GetWorld()->named_positions.size());
    for (int index = 0; index < count; ++index) {
        const W8NamedPosition* entry = &GetWorld()->named_positions[index];
        if (SDL_strcasecmp(name, entry->name) == 0) {
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
unsigned char AdjustWorldCollisionPosition(float radius, glm::vec3* position, bool check_items,
                                           bool check_monsters)
{
    float threshold = radius * g_path_endpoint_scale;
    float angle = 0.0f;

    do {
        glm::vec3 first(0.0f, 0.0f, radius);
        first =
            glm::vec3(glm::angleAxis(std::atan2(std::sin(double(angle)), std::cos(double(angle))),
                                     glm::dvec3(0, 1, 0)) *
                      glm::dvec3(first));
        first += *position;

        double opposite_angle = angle + g_camera_pi;
        glm::vec3 second(0.0f, 0.0f, radius);
        second = glm::vec3(glm::angleAxis(std::atan2(sin(opposite_angle), cos(opposite_angle)),
                                          glm::dvec3(0, 1, 0)) *
                           glm::dvec3(second));
        second += *position;

        GetWorldOctree()->TraceLineOfSight(position, &first, true, -3, -3, true, 0);
        GetWorldOctree()->TraceLineOfSight(position, &second, true, -3, -3, true, 0);

        first -= *position;
        second -= *position;
        float first_distance = glm::length(first);
        if (first_distance < threshold && glm::length(second) < threshold) {
            return 0;
        }
        if (first_distance < threshold) {
            first = glm::dot(first, first) == 0
                        ? first
                        : glm::normalize(first) * static_cast<float>(radius - first_distance);
            *position -= first;
        } else {
            second =
                glm::dot(second, second) == 0
                    ? second
                    : glm::normalize(second) * static_cast<float>(radius - glm::length(second));
            *position -= second;
        }
        angle += 0.7853981256484985f;
    } while (angle < g_camera_pi);

    if (check_items) {
        for (const auto& item : gXStatus.world_items) {
            W8Item* owner = item->p3D;
            if (owner != 0) {
                glm::vec3 center;
                center = owner->GetSearchPosition();
                const auto other_radius = owner->GetBoundsRadius();
                glm::vec3 delta = center - *position;
                if (glm::length(delta) < other_radius + radius) {
                    return 0;
                }
            }
        }
    }

    if (check_monsters) {
        for (const auto& monster_owner : gXStatus.monster_roster.live()) {
            auto* info = monster_owner.get();
            W8Monster* monster = info->p3D;
            if (monster != 0) {
                const auto center = monster->GetAnimationCenter();
                float other_radius;
                if (center && monster->GetAnimationRadius(&other_radius) &&
                    glm::length((*center - *position)) < other_radius + radius) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Try the requested point first, then the two retail half-turn probe points
   around it; only an unobstructed probe is handed to the overlap resolver. */
// FUNCTION: WIZ8 0x00451800
unsigned char FindNearbyFreePosition(float radius, glm::vec3* position, bool check_items,
                                     bool check_monsters)
{
    glm::vec3 candidate = *position;
    if (AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
        *position = candidate;
        return 1;
    }

    float angle = 0.0f;
    float diameter = radius + radius;
    do {
        glm::vec3 offset(0.0f, 0.0f, diameter);
        offset =
            glm::vec3(glm::angleAxis(std::atan2(std::sin(double(angle)), std::cos(double(angle))),
                                     glm::dvec3(0, 1, 0)) *
                      glm::dvec3(offset));
        candidate = offset + *position;
        glm::vec3 unblocked = candidate;

        GetWorldOctree()->TraceLineOfSight(position, &candidate, true, -3, -3, true, 0);
        if (candidate == unblocked &&
            AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
            *position = candidate;
            return 1;
        }
        angle += kQuarterTurn;
    } while (angle < g_camera_pi);
    return 0;
}
