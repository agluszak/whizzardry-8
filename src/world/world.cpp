#include "wiz8/world/world.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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
#include "wiz8/entities/animation.h"
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
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/camera_shake.h"
#include "wiz8/render/draw_inputs.h"
#include "wiz8/render/fog.h" // IWYU pragma: keep - completes world-owned fog nodes.
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/native_content.h" // IWYU pragma: keep - completes the VSG node inheritance for picking.
#include "wiz8/render/particle_system.h"
#include "wiz8/render/transform.h"
#include "wiz8/render/vsg_scene.h"
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
#include "wiz8/local_code/SpellEffect.h" // IWYU pragma: keep - completes world-owned casts.
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/monster_generators.h"
#include "wiz8/world/automap.h"

class MeshInstance;

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

// Keep the authored update points: sky particles first, then primary simulation;
// continuing casts resolve after level scripts, before the primary particle sweep.
void Game::advanceWorlds(std::chrono::milliseconds elapsed)
{
    g_navigator_vertical_enabled =
        !(((GetInputState().modifiers & CTRL_DOWN) != 0) && combat.has_value() &&
          (combat->round_active || party_movement_mode));
    if (level.sky) SweepFinishedParticles(level.sky.get());
    if (level.primary) {
        auto& world = *level.primary;
        world.updateMonsters();
        UpdateWorldMissiles(&world);
        UpdateWorldSpellVisuals(&world);
        UpdateAmbientSounds(&world);
        WorldUpdateLights(&world);
        RunMonsterGenerators(elapsed);
        UpdateWorldTriggers(&world);
        if (world.compiledIndex() && world.script) world.script->runMasters();
        advanceSpellEffects(world);
        SweepFinishedParticles(&world);
    }
    RefreshWorldView();
    UpdateTimedTriggerEvents();
    camera_shakes.advance(level.primary ? level.primary->party.eye() : glm::vec3(0));
    UpdateEnvironment();
    UpdateNpcEvents();
}

W8LevelPopulation& CurrentLevelPopulation()
{
    if (!GetWorld()) throw std::logic_error("no gameplay level is loaded");
    return *GetWorld()->population;
}

World::World(int level_id, std::string path, TimerClockView clocks, ShakeSystem* shakes,
             float reference_gravity)
    : clocks(clocks), shakes(shakes), level_id(level_id),
      source_path(path.empty() ? std::string{} : wiz8::normalize_asset_path(path).generic_string()),
      scene(std::make_unique<WorldScene>()), party(*this, reference_gravity),
      population(std::make_unique<W8LevelPopulation>())
{
    SetSceneAmbientLightWhite(scene.get());
}

World::~World() = default;

WorldParty::WorldParty(World& world, float reference_gravity)
    : camera(world.clocks, reference_gravity), navigator(world)
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

    world.automap.reveal(10);
    if (auto* collision = world.collisionIndex()) {
        party.motion.flags &= ~W8_LEVEL_FLAG_MOVED_THIS_UPDATE;
        const glm::mat3 movement_basis = party.turn(flags);
        const auto motion = party.move(*collision, flags, movement_basis);
        // A camera path flies the eye without moving the party.
        if (!g_camera_path_active) world.party.followCamera();
        world.automap.follow(navigationPosition(party.m_position));
        if (motion.moved) DispatchWorldCursorNodeCommand(0, 0, 0);
    }
    party.applySway(*world.camera, CameraSwayMode::Reset);
    for (const auto& camera_path : world.plsCameras) {
        if (!camera_path->active) continue;
        KeyframeTrack* path = camera_path->path ? &*camera_path->path : nullptr;
        PathAITick(path, 1, world.clocks.worldMilliseconds());
        Transform3D pose;
        pose.setLocation(glm::dvec3(party.m_position));
        pose.setRotation(party.rotationMatrix());
        pose = sampleKeyframeTrack(*path, pose);
        party.m_position = glm::vec3(pose.getLocation());
        glm::mat3 rotation = pose.getRotation<glm::mat3>();
        ApplyCameraRotation(&rotation);
        if (path->discrete_mode != 0) {
            if (path->position >= static_cast<int>(path->points().size()) - 1.0f) {
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

LevelSpatialIndex* World::compiledIndex() const
{
    return spatial && spatial->compiled_available ? spatial.get() : nullptr;
}
LevelSpatialIndex* World::collisionIndex() const
{
    return spatial && spatial->collision_available ? spatial.get() : nullptr;
}

void World::acceptPick(Pick intersection, bool in_combat)
{
    Selection result{std::move(intersection), std::monostate{}};
    if (auto* model = render::gameOwner(result.intersection)) {
        result.subject = model;
        for (const auto& owned : plsProps) {
            auto& prop = *owned;
            if (!prop.active) continue;
            bool matches;
            if (prop.currentAnimation()->positioned_parts) {
                const auto count = std::min<std::size_t>(prop.currentAnimation()->parts[2].size(),
                                                         prop.models.size());
                matches = std::ranges::any_of(
                    prop.models | std::views::take(count),
                    [model](const auto& instance) { return instance.get() == model; });
            } else {
                matches = prop.FrameModel(prop.subcycle) == model;
            }
            if (!matches) continue;
            // Rejected props still occlude what is behind them; never repick.
            result.subject = std::monostate{};
            const auto* trigger = prop.trigger;
            if (trigger && (trigger->flags & W8_TRIGGER_ENABLED) &&
                (!(trigger->flags & W8_TRIGGER_ONCE) || !(trigger->flags & W8_TRIGGER_FIRED)) &&
                (!in_combat || (trigger->doorData() && !trigger->doorData()->open))) {
                if (const auto bounds = prop.GetFrameBounds()) {
                    const float distance = glm::length(glm::vec3(bounds->center()) - party.eye());
                    if (trigger->range_minimum <= distance && distance <= trigger->range_maximum)
                        result.subject = &prop;
                }
            }
            break;
        }
    } else if (result.intersection.hit) {
        const auto& path = result.intersection.hit->nodePath;
        for (const auto& object : native_objects) {
            if (std::ranges::find(path, object->node.get()) != path.end()) {
                result.subject = object.get();
                break;
            }
        }
    }
    picked = std::move(result);
}

MeshInstance* World::selectedModel() const
{
    const auto* model = std::get_if<MeshInstance*>(&picked.subject);
    return model ? *model : nullptr;
}

Prop* World::selectedProp() const
{
    const auto* prop = std::get_if<Prop*>(&picked.subject);
    return prop ? *prop : nullptr;
}

NativeObject* World::selectedNativeObject() const
{
    const auto* object = std::get_if<NativeObject*>(&picked.subject);
    if (!object || !picked.intersection.hit) return nullptr;
    const auto point = picked.intersection.hit->worldIntersection;
    return glm::distance(glm::dvec3(point.x, point.y, point.z), glm::dvec3(party.eye())) <= 5000
               ? *object
               : nullptr;
}
