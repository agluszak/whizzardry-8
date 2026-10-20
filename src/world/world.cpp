#include "wiz8/world/world.h"

#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp> // NOLINT(misc-include-cleaner)

#include "asset_paths.h"
#include "camera_motion.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "named_position.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/fog.h" // IWYU pragma: keep - completes world-owned fog nodes.
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/mesh_updates.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigation.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/unicode.h"
#include "wiz8/world/camera_control.h"
#include "wiz8/world/camera_paths.h"
#include "wiz8/world/camera_pose.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/level_loader.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/party_motion.h"
#include "wiz8/world/update.h"
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

class MeshInstance;

bool g_world_cleanup_flag;
LevelSpatialIndex* GetWorldCollision()
{
    return GetWorld() ? GetWorld()->collisionIndex() : nullptr;
}
LevelSpatialIndex* GetWorldSpatialIndex()
{
    return GetWorld() ? GetWorld()->compiledIndex() : nullptr;
}
NavigationMesh* WorldPathing(const World& world)
{
    return world.compiledIndex() ? world.compiledIndex()->pathing.get() : nullptr;
}
NavigationMesh* GetWorldPathing()
{
    return GetWorld() ? WorldPathing(*GetWorld()) : nullptr;
}
bool g_navigator_vertical_enabled = true;

float g_mesh_view_half_angle_degrees = 60.0f;

/* Build scene resources under a local owner. No active world is required;
   malformed resources destroy only this unpublished result. */
static void UpdateWorld(World& world)
{
    if (&world != GetSecondaryWorld()) {
        UpdateWorldMonsters(&world);
        UpdateWorldMissiles(&world);
        UpdateWorldSpellVisuals(&world);
        UpdateAmbientSounds(&world);
        WorldUpdateLights(&world);
        RunMonsterGenerators();
        UpdateWorldTriggers(&world);
        if (world.compiledIndex() != 0) {
            RunMasterFunctions();
        }
        UpdateSpellEffects();
    }

    SweepFinishedParticles(&world);
}

void UpdateWorlds(void)
{
    g_navigator_vertical_enabled =
        !(((GetInputState().modifiers & CTRL_DOWN) != 0) && CurrentGame().combat.state() != 0 &&
          (CurrentGame().combat.state()->round_active || CurrentGame().party_movement_mode));

    // Loading registered the sky first; preserve that update order.
    for (World* world : {GetSecondaryWorld(), GetWorld()}) {
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
void DetachAllWorldItems(void)
{
    for (World* world : {GetWorld(), GetSecondaryWorld()}) {
        if (world != 0) DetachWorldItemMeshes(world);
    }
}

W8LevelPopulation& CurrentLevelPopulation()
{
    if (!GetWorld()) throw std::logic_error("no gameplay level is loaded");
    return *GetWorld()->population;
}

World::World(int level_id, std::string path)
    : level_id(level_id),
      source_path(path.empty() ? std::string{} : wiz8::normalize_asset_path(path).generic_string()),
      scene(std::make_unique<WorldScene>()), population(std::make_unique<W8LevelPopulation>())
{
    SetSceneAmbientLightWhite(scene.get());
}

World::~World() = default;

/* Per-frame camera / camera-path update for one world. Main-game frames the
   primary world, then the sky world with flag 0x40 so the motion pass is
   skipped there. Each controller owns its refresh position and rotation. */
void UpdateWorldCameraAndPaths(World* world, unsigned int flags)
{
    glm::vec3 camera_position;
    glm::vec3 navigator_position;
    glm::dvec3 render_position;
    glm::mat3 path_rotation;
    W8CameraPath* camera_path;
    KeyframeTrack* path;
    float yaw;
    float pitch;
    int camera_count;
    int index;

    RefreshDirtyAutomap();
    camera_position = cameraLocation();
    if (world->collisionIndex() != 0) {
        world->party_camera.motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        if ((flags & W8_CAMERA_MOTION_SKIP) == 0) {
            const auto orientation = world->party_camera.cameraRotation(
                flags, world->camera->transform.getRotation<glm::mat3>());
            world->camera->transform.setRotation(orientation.rotation);
            world->party_camera.saved_render_rotation = orientation.rotation;
            yaw = GetCameraYawInDegrees();
            pitch = GetCameraPitchInDegrees();
            PartyNavigator()->SetAngles(yaw);
            PartyNavigator()->SetPitch(pitch);
            const auto motion = world->party_camera.cameraMotion(
                *world->collisionIndex(), flags, camera_position, orientation.movement_basis);
            if (motion.moved) {
                camera_position += motion.displacement;
                navigator_position =
                    glm::vec3(camera_position.x, camera_position.y - g_default_world_height,
                              camera_position.z);
                if (world->camera_light != 0) {
                    render_position = glm::dvec3(camera_position);
                    world->camera_light->transform.setLocation(render_position);
                }
                render_position = glm::dvec3(camera_position);
                world->camera->transform.setLocation(render_position);
                PartyNavigator()->SetPositionInternal(&navigator_position);
                const auto refresh_offset =
                    camera_position - world->party_camera.last_automap_refresh_position;
                if (glm::dot(refresh_offset, refresh_offset) > kAutomapRefreshDistanceSquared) {
                    world->party_camera.last_automap_refresh_position = camera_position;
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
            world->camera->transform.setRotation(world->party_camera.saved_render_rotation);
        }
    } else {
        world->camera->transform.setRotation(world->party_camera.saved_render_rotation);
    }
    world->party_camera.applySway(*world->camera, CameraSwayMode::Reset);
    camera_count = static_cast<int>(world->plsCameras.size());
    if (camera_count != 0) {
        for (index = 0; index < camera_count; ++index) {
            camera_path = GetWorldCameraPath(world, index);
            if (camera_path != 0 && camera_path->active) {
                path = (camera_path->path ? &*camera_path->path : nullptr);
                PathAITick(path, 1);
                (world->camera)->transform =
                    sampleKeyframeTrack(*(path), (world->camera)->transform);
                {
                    glm::dvec3 location = world->camera->transform.getLocation();
                    glm::vec3 party_point;
                    party_point = glm::vec3(location);
                    world->party_camera.m_position = party_point;
                }
                path_rotation = world->camera->transform.getRotation<glm::mat3>();
                if (GetSecondaryWorld() != 0 && GetSecondaryWorld()->camera != 0) {
                    GetSecondaryWorld()->camera->transform.setRotation(path_rotation);
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

/* Resolve a particle by its runtime name from the current world's particle
   vector. Names are case-insensitive in the original registry-facing lookup. */
ParticleSystem* FindParticleByName(std::string_view name)
{
    if (GetWorld() != nullptr) {
        int count = static_cast<int>(GetWorld()->particles.size());

        for (int index = 0; index < count; ++index) {
            ParticleSystem* particle = GetWorld()->particles[index].get();

            if (CompareTextIgnoreAsciiCase(particle->getName(), name) == 0) {
                return particle;
            }
        }
    }
    return 0;
}

/* Read the camera position when one is attached to the world. The sole
   caller preserves this value across a level reload, so a world without a
   camera contributes the zero position. */
glm::vec3 cameraLocationOrZero(const World& world)
{
    return world.camera ? cameraLocation(world) : glm::vec3(0);
}

/* Set the view position after loading or traversing a portal. The camera light
   follows the camera, while only the camera move publishes the new game-space
   position. */
void SetWorldScenePosition(World* world, const glm::vec3* location)
{
    glm::vec3 position;
    glm::dvec3 render_position;

    if (!world) {
        Fail("pWorld");
    }

    position = *location;
    if (world->camera != 0) {
        render_position = glm::dvec3(position);
        world->camera->transform.setLocation(render_position);
        world->party_camera.m_position = position;
    }
    if (world->camera_light != 0) {
        render_position = glm::dvec3(position);
        world->camera_light->transform.setLocation(render_position);
    }
}

void RefreshWorldView()
{
    auto* world = GetWorld();
    if (!world || !world->camera) return;
    if (world->compiledIndex())
        UpdateWorldOctree(world);
    else if (world->quads)
        UpdateWorldMeshFromQuads(world);
    RepositionAmbientSounds();
    RequestRefreshPartyState();
}

/* The camera's rotation basis, read straight back out of the renderer node.
   Both assertions belong to this body: 3dapi.cpp:975 names the world and
   3dapi.cpp:976 names its camera member pWorld->psrCamera, which is what puts
   the camera at 0x44 rather than anywhere else. */
glm::mat3 cameraRotation(const World& world)
{
    if (!world.camera) {
        Fail("pWorld->psrCamera");
    }
    return world.camera->transform.getRotation<glm::mat3>();
}

/* Moves the camera and its camera light together. Only the camera's move
   notifies the level; the light simply follows the view. */
void WorldSetCameraLocation(const glm::vec3* location)
{
    glm::dvec3 position;

    if (!GetWorld()) {
        Fail("pWorld");
    }
    if (GetWorld()->camera != 0) {
        position = glm::dvec3((*location));
        GetWorld()->camera->transform.setLocation(position);
        PlacePartyAtPoint(location);
    }
    if (GetWorld()->camera_light != 0) {
        position = glm::dvec3((*location));
        GetWorld()->camera_light->transform.setLocation(position);
    }
}

/* Restore the owning world's camera, light and orientation, retaining the
   opaque retail fields for its next save snapshot. */
void SetWorldCameraState(World* world, CameraPose* state)
{
    if (world == nullptr) {
        Fail("pWorld");
    }
    if (state == nullptr) {
        Fail("CamPos");
    }
    if (world->camera == nullptr) {
        Fail("pWorld->pCamera");
    }

    glm::vec3 location = state->position;
    world->camera->transform.setLocation(glm::dvec3(location));
    world->party_camera.m_position = location;
    world->party_camera.saved_pose_bytes = state->retail;
    if (world->camera_light != nullptr) {
        world->camera_light->transform.setLocation(glm::dvec3(location));
    }

    world->party_camera.SetYaw(state->orientation.yaw);
    world->party_camera.SetPitch(state->orientation.pitch);
    state->orientation = {world->party_camera.m_yaw, world->party_camera.m_pitch};
    world->camera->transform.setRotation(world->party_camera.rotationMatrix());
    world->party_camera.saved_render_rotation = world->party_camera.rotationMatrix();
}

/* Reinstall a CamPos record, then copy the resulting camera pose onto the
   startup navigator in degrees with the default world height removed. */
void RestoreWorldCameraState(CameraPose* state)
{
    glm::vec3 navigator_position;

    if (GetWorld() == nullptr) {
        Fail("pWorld");
    }
    SetWorldCameraState(GetWorld(), state);
    navigator_position = glm::vec3(GetWorld()->camera->transform.getLocation());
    PartyNavigator()->SetAngles(GetCameraYawInDegrees());
    PartyNavigator()->SetPitch(GetCameraPitchInDegrees());
    navigator_position.y -= g_default_world_height;
    PartyNavigator()->SetPositionInternal(&navigator_position);
}

/* Snapshot the world's camera location and the live yaw/pitch into a CamPos
   record. A world without a camera contributes the zero position. */
CameraPose cameraState(const World& world)
{
    CameraPose state{};
    state.position = cameraLocationOrZero(world);
    state.orientation = {world.party_camera.m_yaw, world.party_camera.m_pitch};
    state.retail = world.party_camera.saved_pose_bytes;
    return state;
}

/* Read the camera node's double-precision renderer position back into the
   world's float position type. */
glm::vec3 cameraLocation(const World& world)
{
    return glm::vec3(world.camera->transform.getLocation());
}

/* Runtime/debug adjustment dispatcher for the active world's clipping and
   environment controls. Bits not handled here are intentionally ignored. */
void ApplyWorldUpdateFlags(World* world, unsigned int flags)
{
    float scale;

    if (world == 0) {
        Fail("pWorld");
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
        SetWorldEnvironmentIntensity(GetWorld(), GetWorld()->environment.intensity + 0.02f);
    }
    if ((flags & W8_WORLD_DECREASE_ENVIRONMENT) != 0) {
        SetWorldEnvironmentIntensity(GetWorld(), GetWorld()->environment.intensity - 0.02f);
    }
}

/* Resolve the PARTY alias or a named position stored with the current world. */
bool FindEntityByName(std::string_view name, glm::vec3* position, float* angle,
                      glm::vec3* direction)
{
    if (name.empty()) {
        return false;
    }
    if (CompareTextIgnoreAsciiCase("PARTY", name) == 0) {
        *position = cameraLocation();
        return true;
    }

    int count = static_cast<int>(GetWorld()->named_positions.size());
    for (int index = 0; index < count; ++index) {
        const W8NamedPosition* entry = &GetWorld()->named_positions[index];
        if (CompareTextIgnoreAsciiCase(name, entry->name) == 0) {
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

        if (const auto hit = GetWorldSpatialIndex()->traceSight(*position, first))
            first = hit->point;
        if (const auto hit = GetWorldSpatialIndex()->traceSight(*position, second))
            second = hit->point;

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
        for (const auto& item : CurrentLevelPopulation().world_items) {
            WorldItemEntity* owner = item->p3D;
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
        for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
            auto* info = monster_owner.get();
            Monster* monster = info->p3D;
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

        if (const auto hit = GetWorldSpatialIndex()->traceSight(*position, candidate))
            candidate = hit->point;
        if (candidate == unblocked &&
            AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
            *position = candidate;
            return 1;
        }
        angle += kQuarterTurn;
    } while (angle < g_camera_pi);
    return 0;
}

LevelSpatialIndex* World::compiledIndex() const
{
    return spatial && spatial->compiled_available ? spatial.get() : nullptr;
}
LevelSpatialIndex* World::collisionIndex() const
{
    return spatial && spatial->collision_available ? spatial.get() : nullptr;
}
