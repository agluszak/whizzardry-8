#include "wiz8/world/world.h"

#include <algorithm>
#include <cassert>
#include <chrono>
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
#include <glm/trigonometric.hpp>

#include "asset_paths.h"
#include "camera_motion.h"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "named_position.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/combat/combat.h"
#include "wiz8/entities/cursor_nodes.h"
#include "wiz8/entities/cycle_player.h"
#include "wiz8/entities/keyframe_track.h"
#include "wiz8/entities/missile.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/native_object.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/spell_visual.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/fog.h" // IWYU pragma: keep - completes world-owned fog nodes.
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/render/transform.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/spatial/bounding_sphere.h"
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

/* Build scene resources under a local owner. No active world is required;
   malformed resources destroy only this unpublished result. */
static void UpdateWorld(World& world, std::chrono::milliseconds elapsed)
{
    if (&world != GetSecondaryWorld()) {
        UpdateWorldMonsters(&world);
        UpdateWorldMissiles(&world);
        UpdateWorldSpellVisuals(&world);
        UpdateAmbientSounds(&world);
        WorldUpdateLights(&world);
        RunMonsterGenerators(elapsed);
        UpdateWorldTriggers(&world);
        if (world.compiledIndex() != 0 && world.script) {
            world.script->runMasters();
        }
        UpdateSpellEffects();
    }

    SweepFinishedParticles(&world);
}

void UpdateWorlds(std::chrono::milliseconds elapsed)
{
    g_navigator_vertical_enabled =
        !(((GetInputState().modifiers & CTRL_DOWN) != 0) && CurrentGame().combat.has_value() &&
          (CurrentGame().combat->round_active || CurrentGame().party_movement_mode));

    // Loading registered the sky first; preserve that update order.
    for (World* world : {GetSecondaryWorld(), GetWorld()}) {
        if (world) UpdateWorld(*world, elapsed);
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

WorldParty::WorldParty(World& world) : navigator(world)
{
    navigator.configureStartupRange(500.0f);
    navigator.configureStartupDepth(std::max(kStartupNearLimit, g_default_world_height),
                                    g_default_world_height);
}

/* Retail handed the proxy the camera's angles in degrees, and nothing reads them back as
   radians. */
void WorldParty::followCamera()
{
    navigator.SetAngles(glm::degrees(camera.m_yaw));
    navigator.SetPitch(glm::degrees(camera.m_pitch));
    const glm::vec3 position = navigationPosition(camera.m_position);
    navigator.SetPositionInternal(&position);
}

CameraPose WorldParty::pose() const
{
    return {camera.m_position, {camera.m_yaw, camera.m_pitch}, camera.saved_pose_bytes};
}

BoundingSphere WorldParty::collisionSphere() const
{
    return {navigator.movement.position, navigator.movement.alternate_radius};
}

void WorldParty::place(const glm::vec3& position)
{
    camera.m_position = position;
    followCamera();
}

void WorldParty::restore(const CameraPose& pose)
{
    camera.m_position = pose.position;
    camera.SetYaw(pose.orientation.yaw);
    camera.SetPitch(pose.orientation.pitch);
    camera.saved_pose_bytes = pose.retail;
    followCamera();
}

/* One frame of party movement in the primary world: the turn and look input, the motion
   against the collision index, the automap's visited cells and the camera paths. The
   rendered views derive from the result (`placeWorldCameras`); nothing here writes them. */
void updatePartyMovement(World& world, unsigned int flags)
{
    auto& party = world.party.camera;

    RefreshDirtyAutomap();
    if (auto* collision = world.collisionIndex()) {
        party.motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        const glm::mat3 movement_basis = party.turn(flags);
        const auto motion = party.move(*collision, flags, movement_basis);
        // A camera path flies the eye without moving the party.
        if (!g_camera_path_active) world.party.followCamera();
        if (motion.moved) {
            const auto refresh_offset = party.m_position - party.last_automap_refresh_position;
            if (glm::dot(refresh_offset, refresh_offset) > kAutomapRefreshDistanceSquared) {
                party.last_automap_refresh_position = party.m_position;
                const glm::vec3 automap_position = navigationPosition(party.m_position);
                if (AutomapHasCellAt(&automap_position)) UpdateAutomapBounds();
            }
            DispatchWorldCursorNodeCommand(0, 0, 0);
        }
    }
    party.applySway(*world.camera, CameraSwayMode::Reset);
    for (const auto& camera_path : world.plsCameras) {
        if (!camera_path->active) continue;
        KeyframeTrack* path = camera_path->path ? &*camera_path->path : nullptr;
        PathAITick(path, 1);
        Transform3D pose;
        pose.setLocation(glm::dvec3(party.m_position));
        pose.setRotation(party.rotationMatrix());
        pose = sampleKeyframeTrack(*path, pose);
        party.m_position = glm::vec3(pose.getLocation());
        glm::mat3 rotation = pose.getRotation<glm::mat3>();
        ApplyCameraRotation(&rotation);
        if (path->discrete_mode != 0) {
            if (path->position >= static_cast<int>(path->nodes.size()) - 1.0f) {
                UpdateCameraPathState(&world, camera_path.get(), 0);
            }
        } else if (path->position >= 1.0) {
            UpdateCameraPathState(&world, camera_path.get(), 0);
        }
    }
}

void placeWorldCameras(World& world, World* sky, const glm::vec3& view_offset)
{
    if (!world.camera) return;
    const auto& party = world.party.camera;
    const glm::dvec3 eye(party.m_position + view_offset);
    const glm::mat3 rotation = party.rotationMatrix();
    world.camera->transform.setLocation(eye);
    world.camera->transform.setRotation(rotation);
    if (world.camera_light) world.camera_light->transform.setLocation(eye);
    if (sky && sky->camera) {
        // The sky stays at its origin, turning and swaying with the party's view.
        sky->camera->transform.setRotation(rotation);
        const auto [view_plane, distance] = world.camera->getViewPlane();
        sky->camera->setViewPlane(view_plane, distance);
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

void RefreshWorldView()
{
    auto* world = GetWorld();
    if (!world || !world->camera) return;
    if (world->compiledIndex()) world->compiledIndex()->UpdateCameraVisibility();
    RepositionAmbientSounds(world);
    RequestRefreshPartyState();
}

/* Runtime/debug adjustment dispatcher for the active world's clipping and
   environment controls. Bits not handled here are intentionally ignored. */
void ApplyWorldUpdateFlags(World* world, unsigned int flags)
{
    float scale;

    assert(world != 0);
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
bool AdjustWorldCollisionPosition(float radius, glm::vec3* position, bool check_items,
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
            return false;
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
            WorldItemEntity* owner = item->entity.find();
            if (owner != 0) {
                glm::vec3 center;
                center = owner->GetSearchPosition();
                const auto other_radius = owner->GetBoundsRadius();
                glm::vec3 delta = center - *position;
                if (glm::length(delta) < other_radius + radius) {
                    return false;
                }
            }
        }
    }

    if (check_monsters) {
        for (const auto& monster_owner : CurrentLevelPopulation().monster_roster.live()) {
            auto* info = monster_owner.get();
            Monster* monster = info->entity.find();
            if (monster != 0) {
                const auto center = monster->GetAnimationCenter();
                float other_radius;
                if (center && monster->GetAnimationRadius(&other_radius) &&
                    glm::length((*center - *position)) < other_radius + radius) {
                    return false;
                }
            }
        }
    }
    return true;
}

/* Try the requested point first, then the two retail half-turn probe points
   around it; only an unobstructed probe is handed to the overlap resolver. */
bool FindNearbyFreePosition(float radius, glm::vec3* position, bool check_items,
                            bool check_monsters)
{
    glm::vec3 candidate = *position;
    if (AdjustWorldCollisionPosition(radius, &candidate, check_items, check_monsters)) {
        *position = candidate;
        return true;
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
            return true;
        }
        angle += kQuarterTurn;
    } while (angle < g_camera_pi);
    return false;
}

LevelSpatialIndex* World::compiledIndex() const
{
    return spatial && spatial->compiled_available ? spatial.get() : nullptr;
}
LevelSpatialIndex* World::collisionIndex() const
{
    return spatial && spatial->collision_available ? spatial.get() : nullptr;
}
