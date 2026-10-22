#include "wiz8/world/environment.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "resource_cache.h"
#include "texture_animation.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

// The 8-bit day phase the colour tables and sky gradients are indexed by.
static unsigned int DayPhase(unsigned int time_of_day)
{
    return ((time_of_day / 1000U) << 8) / 86400U;
}

/* Both static scenes take the light direction as their fog colour. */
static void PublishLightDirection(World& world, World* sky, glm::vec3 direction)
{
    world.scene->setFogColor(direction);
    if (sky) sky->scene->setFogColor(direction);
}

/* Place the sky for a time of day: the day's celestial prop sits one orbit radius from
   the recovered sky origin in the rotated direction and the other rests on the origin,
   the three animated gradients show the day phase, and the clouds brighten towards
   midday. */
static void PlaceSky(World& sky, unsigned int time_of_day)
{
    auto& environment = sky.environment;
    if (environment.cloud_material) {
        auto& material = *environment.cloud_material;
        const double brightness = 1.0 - std::abs(time_of_day - 43200000.0) / 43200000.0;
        material.parms.ambient = glm::vec4(static_cast<float>(brightness));
        material.parms.diffuse.w = static_cast<float>(brightness * 0.75 + 0.25);
        material.dirty = 1;
    }

    const double arc = 3.141592653589793 * (1.0f / 180.0f) * 80.0;
    bool day;
    double angle;
    if (time_of_day < 18000001U) {
        day = false;
        angle = (time_of_day + 7200000) * arc * 3.9682539682539686e-08;
    } else if (time_of_day < 79200001U) {
        day = true;
        angle = (time_of_day - 18000000) * arc * 1.633986928104575e-08;
    } else {
        day = false;
        angle = (time_of_day - 79200000) * arc * 3.9682539682539686e-08;
    }
    angle -= 3.141592653589793 * (1.0f / 180.0f) * 40.0;
    const glm::mat3 rotation = glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 0, 1)));
    glm::vec3 position =
        rotation * glm::vec3(0.0, environment.orbit_radius, 0.0) + environment.celestial_origin;

    Prop* moving = day ? environment.sun : environment.moon;
    Prop* opposite = day ? environment.moon : environment.sun;
    if (moving) moving->SetLocation(&position);
    if (opposite) opposite->SetLocation(&environment.celestial_origin);

    const auto phase = static_cast<int>(DayPhase(time_of_day));
    for (const auto& gradient : environment.gradients) {
        if (auto animation = gradient.lock()) animation->playback->frame = phase;
    }
}

/* Advance the authoritative game calendar and place the sky for the new time of day.
   The three animated sky gradients consume the same 8-bit day phase, so the calendar,
   prop placement, and gradient animation remain one update rather than parallel
   timers. */
void AdvanceEnvironmentTime(Game& game, int elapsed)
{
    unsigned int time = game.status.game_time_ms + elapsed;
    if (time > 86399999U) {
        ++game.status.game_time_days;
    }
    game.status.game_time_ms = time % 86400000U;

    /* The game clock (turns, aging, recharges, fatigue, stamina, camping) runs only while
       the party is in the world. Time reaches it from the environment clock while nothing
       holds the world and no fight is on, and from the rounds of a fight there. */
    if (game.activity.in_world()) {
        UpdateGameClock(game, elapsed);
    }
    if (World* world = game.level.primary.get()) world->environment.untaken_time = {};
    if (World* sky = game.level.sky.get()) PlaceSky(*sky, game.status.game_time_ms);
}

/* Move the calendar on by the game-clock multiplier's share of the step time the level
   has not taken up yet. */
static void AdvanceEnvironmentClock(Game& game)
{
    World* world = game.level.primary.get();
    if (!game.advancement.time_passes || !world) return;
    const auto elapsed = std::exchange(world->environment.untaken_time, {});
    if (elapsed.count() != 0) {
        AdvanceEnvironmentTime(
            game, static_cast<int>(static_cast<double>(elapsed.count()) * g_view_distance));
    }
}

/* Turn environment time progression on or off. While it is off, no step time reaches
   it, so it resumes from the moment it is enabled. */
void SetEnvironmentTimeEnabled(Game& game, bool enabled)
{
    game.advancement.time_passes = enabled;
    if (World* world = game.level.primary.get(); enabled && world)
        world->environment.untaken_time = {};
}

void PassEnvironmentTime(Game& game, std::chrono::milliseconds elapsed)
{
    if (World* world = game.level.primary.get(); game.advancement.time_passes && world)
        world->environment.untaken_time += elapsed;
}

void SetGameTimeMilliseconds(Game& game, int value)
{
    game.status.game_time_ms = value;
    if (World* world = game.level.primary.get()) world->environment.untaken_time = {};
}

static void AdvanceLightingFade(World& world, World* sky);

/* The environment's per-frame update. A running transition short-circuits to
   the transition body. Otherwise, in day/night mode the sky consumes the day
   phase to pick the light direction, and the world colour table is refreshed
   from the same phase; any other mode just advances the clock. Every path that
   advances the clock scales the elapsed ticks by the game-clock multiplier. */
void UpdateEnvironment(Game& game)
{
    World* world = game.level.primary.get();
    if (!world) return;
    if (world->environment.transition_rate != 0.0f) {
        AdvanceLightingFade(*world, game.level.sky.get());
        return;
    }
    if (!game.advancement.time_passes) {
        return;
    }
    if (world->environment.lighting_mode == W8EnvironmentLightingMode::DayNight) {
        if (world->environment.sky_enabled) {
            UpdateEnvironmentLight(game);
        }
        RefreshEnvironment(game);
    } else {
        AdvanceEnvironmentClock(game);
    }
}

/* Repeated component clamps share one helper. Preserve the comparison order:
   a NaN component follows the nonpositive branch and becomes zero. */
static void ClampEnvironmentComponent(float& component)
{
    if (0.0f < component) {
        if (1.0f <= component) {
            component = 1.0f;
        }
    } else {
        component = 0.0f;
    }
}

/* The two binary colour tables differ only in their destination array. */
bool ReadEnvironmentColours(wiz8::File* file, std::array<glm::vec3, 256>& colours)
try {
    std::array<unsigned char, 256 * 3> components{};
    if (!file || file->read(components.data(), components.size()).bytes != components.size())
        return false;

    for (int index = 0; index < 256; ++index) {
        colours[index].x = components[index * 3] * (1.0f / 255.0f);
        colours[index].y = components[index * 3 + 1] * (1.0f / 255.0f);
        colours[index].z = components[index * 3 + 2] * (1.0f / 255.0f);
        ClampEnvironmentComponent(colours[index].x);
        ClampEnvironmentComponent(colours[index].y);
        ClampEnvironmentComponent(colours[index].z);
    }
    return true;
} catch (const std::exception&) {
    return false;
}

std::array<glm::vec3, 256> EnvironmentColourRamp()
{
    std::array<glm::vec3, 256> colours;
    for (int index = 0; index < 256; ++index) {
        const float value = (index < 128 ? index : 255 - index) * (1.0f / 127.0f);
        colours[index] = glm::vec3(value);
    }
    return colours;
}

/* Refresh the light direction from the day-phase table when the phase turns
   over; used after level load once fog is on. */
void UpdateEnvironmentLight(Game& game)
{
    World* world = game.level.primary.get();
    if (!world) return;
    AdvanceEnvironmentClock(game);
    auto& environment = world->environment;
    const unsigned int phase = DayPhase(game.status.game_time_ms);
    if (phase != static_cast<unsigned int>(environment.last_light_phase)) {
        environment.light_direction = environment.light_colours[phase];
        PublishLightDirection(*world, game.level.sky.get(), environment.light_direction);
        environment.last_light_phase = static_cast<int>(phase);
    }
}

/* The far plane the world is drawn to. */
void SetViewDistance(float distance)
{
    g_view_distance = distance;
}

float GetViewDistance(void)
{
    return g_view_distance;
}

/* Turn the sky on, then catch the light direction up: advance the clock by the
   elapsed ticks' share and publish the day phase's light direction if it moved
   on. The phase tracker is the same one the per-frame update keeps, so enabling
   the sky does not restart the cycle. */
void EnableSky(Game& game)
{
    World* world = game.level.primary.get();
    if (!world) return;
    auto& environment = world->environment;
    if (world->scene && !environment.static_fog) {
        environment.static_fog = std::make_unique<FogParams>(world->scene.get());
        environment.dynamic_fog = std::make_unique<FogParams>(world->scene.get());
        environment.static_fog->density = 1.0f;
        environment.dynamic_fog->density = 1.0f;
        RefreshFogRanges(*world);
        PublishLightDirection(*world, game.level.sky.get(), environment.light_direction);
        if (world->camera) {
            const auto far_clip = static_cast<float>(WorldGetFarClip(world));
            world->camera->setEnvironmentRange(far_clip * environment.range_start,
                                               far_clip * environment.range_end);
        }
    }
    environment.sky_enabled = true;
    UpdateEnvironmentLight(game);
}

void DisableSky(Level& level)
{
    World* world = level.primary.get();
    if (!world) return;
    world->environment.static_fog.reset();
    world->environment.dynamic_fog.reset();
    PublishLightDirection(*world, level.sky.get(), glm::vec3(0));
    if (world->camera)
        world->camera->setEnvironmentRange(0.0f, static_cast<float>(WorldGetFarClip(world)));
    world->environment.sky_enabled = false;
}

/* Advance the clock-driven sky and refresh the world's environment colour
   from the day-phase table when the phase turns over. */
void RefreshEnvironment(Game& game)
{
    World* world = game.level.primary.get();
    if (!world) return;
    AdvanceEnvironmentClock(game);
    auto& environment = world->environment;
    const unsigned int phase = DayPhase(game.status.game_time_ms);
    if (phase != static_cast<unsigned int>(environment.last_colour_phase)) {
        ApplyEnvironmentColour(*world, environment.intensity, environment.colours[phase]);
        environment.last_colour_phase = static_cast<int>(phase);
    }
}

/* Set the world's environment intensity, clamped to the unit range (NaN becomes zero),
   keeping its current colour unless the world has no static scene to take it from. */
float SetWorldEnvironmentIntensity(World& world, float value)
{
    value = value >= 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
    ApplyEnvironmentColour(world, value, world.scene ? world.environment.colour : glm::vec3(0));
    return value;
}

/* Arm or complete a lighting fade. A zero duration snaps back to day/night
   lighting with the current base intensity; any other duration stores
   1/duration as the per-millisecond rate and, for a fade-out, snapshots the
   live intensity into the worlds' base fields. */
void BeginWorldLightingFade(Level& level, float duration)
{
    World* world = level.primary.get();
    if (!world) return;
    World* sky = level.sky.get();
    auto& environment = world->environment;

    if (duration == 0.0f) {
        environment.lighting_mode = W8EnvironmentLightingMode::DayNight;
        environment.transition_rate = 0.0f;
        g_world_render_enabled = 1;
        g_monster_shadow_updates_enabled = 1;
        g_world_blacked_out = false;
        g_monster_light_scale = 1.0f;
        SetWorldEnvironmentIntensity(*world, environment.base_intensity);
        if (sky) SetWorldEnvironmentIntensity(*sky, sky->environment.base_intensity);
        PublishLightDirection(*world, sky, glm::clamp(environment.light_direction, 0.0f, 1.0f));
        return;
    }

    environment.transition_rate = 1.0f / duration;
    environment.transition_tick = WorldClockMilliseconds();
    environment.lighting_mode = W8EnvironmentLightingMode::Transition;
    if (duration < 0.0f) {
        environment.base_intensity = environment.intensity;
        if (sky) sky->environment.base_intensity = sky->environment.intensity;
    }
}

/* Advance an armed lighting transition by the milliseconds since its tick
   baseline: update the shared light scale, reapply both worlds' ambient from
   scale times each world's base intensity, and publish a scale-modulated light
   direction. Completing a fade-out leaves mode 0; completing a fade-in returns
   to day/night mode 2. */
static void AdvanceLightingFade(World& world, World* sky)
{
    auto& environment = world.environment;
    const std::uint64_t now = WorldClockMilliseconds();
    const std::uint64_t elapsed = now - environment.transition_tick;
    if (elapsed == 0) {
        return;
    }

    float scale = elapsed * environment.transition_rate + g_monster_light_scale;
    if (1.0f < scale) {
        scale = 1.0f;
    } else if (scale < 0.0f) {
        scale = 0.0f;
    }
    g_monster_light_scale = scale;

    const float intensity = SetWorldEnvironmentIntensity(world, scale * environment.base_intensity);
    if (sky) SetWorldEnvironmentIntensity(*sky, scale * sky->environment.base_intensity);
    PublishLightDirection(world, sky, glm::clamp(environment.light_direction * scale, 0.0f, 1.0f));

    environment.transition_tick = now;
    if (intensity == 0.0f) {
        environment.lighting_mode = W8EnvironmentLightingMode::FadedOut;
        g_world_render_enabled = 0;
        g_monster_shadow_updates_enabled = 0;
        g_world_blacked_out = true;
        environment.transition_rate = 0.0f;
        return;
    }
    g_world_render_enabled = 1;
    g_monster_shadow_updates_enabled = 1;
    g_world_blacked_out = false;
    if (scale == 1.0f) {
        environment.lighting_mode = W8EnvironmentLightingMode::DayNight;
        environment.transition_rate = 0.0f;
        return;
    }
    environment.lighting_mode = W8EnvironmentLightingMode::Transition;
}

/* Refresh the two fog objects' ranges from the world's far clip. Runs after
   the camera's clip range moves, so the fog tracks the same plane. */
void RefreshFogRanges(World& world)
{
    if (world.environment.static_fog != nullptr) {
        world.environment.static_fog->fog_end =
            WorldGetFarClip(&world) * world.environment.range_end;
        world.environment.static_fog->fog_start =
            WorldGetFarClip(&world) * world.environment.range_start;
        world.environment.dynamic_fog->fog_start =
            WorldGetFarClip(&world) * world.environment.range_start;
        world.environment.dynamic_fog->fog_end =
            WorldGetFarClip(&world) * world.environment.range_end;
    }
}

glm::vec3 WorldLightValue(const World& world)
{
    return world.scene ? world.environment.colour : glm::vec3(0);
}

/* Push a colour and intensity into the world's static scene and every registered
   environment light. */
void ApplyEnvironmentColour(World& world, float intensity, glm::vec3 colour)
{
    if (world.scene) {
        world.scene->setAmbientLight(glm::clamp(colour * intensity, 0.0f, 1.0f));
        world.environment.colour = colour;
        world.environment.intensity = intensity;
    }
    for (Light* light : world.environment.lights) {
        glm::vec3 scaled(glm::dvec3(colour) * static_cast<double>(intensity));
        light->ambient = glm::clamp(scaled, 0.0f, 1.0f);
    }
}

void SetCameraLightIntensity(World& world, float value)
{
    if (world.camera_light) world.camera_light->intensity = value;
}

void SetCameraLightVisible(World& world, bool visible)
{
    if (world.camera_light) world.camera_light->visible = visible;
}

const char* g_sky_gradient_names[3] = {"SkyGrad0000.ifl", "Skytop0000.ifl", "Horizon0000.ifl"};

/* Level-entry environment setup: locate the level's Sun and Moon props, derive
   the celestial origin and orbit radius from the distance between them, register
   the three sky gradient textures, advance the day clock, place the sky and
   publish the current day phase's colour and light direction. */
void InitializeLevelEnvironment(Game& game)
{
    World* world = game.level.primary.get();
    if (!world) return;
    World* sky_world = game.level.sky.get();
    if (sky_world) {
        auto& sky = sky_world->environment;
        sky.sun = FindPropByName(sky_world, "Sun");
        sky.moon = FindPropByName(sky_world, "Moon");
        if (sky.orbit_radius < 0.0f) {
            if (sky.sun == 0 || sky.moon == 0) {
                sky.sun = 0;
                sky.moon = 0;
            } else {
                const glm::vec3 sun = sky.sun->GetLocation();
                const glm::vec3 moon = sky.moon->GetLocation();
                sky.orbit_radius = glm::length(sun - moon) * 0.5f;
                sky.celestial_origin = (sun + moon) * 0.5f;
                for (int index = 0; index < 3; ++index) {
                    std::shared_ptr<Texture> animation =
                        g_texture_animations.find(g_sky_gradient_names[index]);
                    sky.gradients[index] = animation;
                    if (animation != 0) {
                        animation->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                    }
                }
            }
        }
    }
    AdvanceEnvironmentClock(game);
    if (sky_world) PlaceSky(*sky_world, game.status.game_time_ms);
    auto& environment = world->environment;
    const unsigned int phase = DayPhase(game.status.game_time_ms);
    ApplyEnvironmentColour(*world, environment.intensity, environment.colours[phase]);
    environment.light_direction = environment.light_colours[phase];
    PublishLightDirection(*world, sky_world, environment.light_direction);
}

EnvironmentState::EnvironmentState() = default;
EnvironmentState::~EnvironmentState() = default;
