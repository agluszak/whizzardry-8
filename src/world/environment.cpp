#include "wiz8/world/environment.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>

#include <glm/common.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/native.h"
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
#include "wiz8/platform/assertion.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_shading.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

static std::uint64_t g_tick;
/* 0x00659AB4: the world being rendered. Its sky node is the one field these
   two bodies reach, and it is the same World the 3d code walks. */

/* Camera-light intensity ceiling for SetCameraLightMode; the linker
   folds it into the shared 50.0f constant Octree.cpp emits at 0x005EC02C. */
static const float CAMERA_LIGHT_MAXIMUM_INTENSITY = 50.0f;

/* 1/duration while the transition body at 0x00484300 runs, zero when idle.
   BeginWorldLightingFade stores 1/duration and sets the lighting mode to 1;
   UpdateEnvironmentLighting stores zero again once it completes. */

/* Tick baseline for the active lighting transition; rewritten every frame the
   transition body advances the light scale. */

/* The mapper starts its scroll rate at 0.002 texture units per second on x
   only and reseeds the shared frame clock, so the first scrolled frame uses
   the interval since this object's construction rather than since startup. */

/* Add the frame-scaled rate to each axis and keep only the fractional part,
   then shift the first texture coordinate set of every vertex by it. */

static void RefreshCloudMaterial(World& world)
{
    if (!world.environment.cloud_material) return;
    auto& material = *world.environment.cloud_material;
    const double brightness =
        1.0 - std::abs(CurrentGame().status.game_time_ms - 43200000.0) / 43200000.0;
    material.parms.ambient = glm::vec4(static_cast<float>(brightness));
    material.parms.diffuse.w = static_cast<float>(brightness * 0.75 + 0.25);
    material.dirty = 1;
}

/* Drop every per-level environment object: the sky gradients, the Sun/Moon
   props and the orbit radius reset, then release the two fog objects and
   empty the registered-light list without freeing its storage. */
void ClearEnvironmentObjects(void)
{
    ResetEnvironment();
    if (GetWorld()) {
        GetWorld()->environment.static_fog.reset();
        GetWorld()->environment.dynamic_fog.reset();
    }
}

/* Advance the authoritative game clock and place the two celestial props on
   opposite sides of the world's recovered sky origin: the day's prop sits one
   orbit radius away in the rotated direction, the other rests on the origin.
   The three animated sky gradients consume the same 8-bit day phase, so the
   clock, prop placement, and gradient animation remain one update rather than
   parallel timers. */
void AdvanceEnvironmentTime(int elapsed)
{
    unsigned int time = CurrentGame().status.game_time_ms + elapsed;
    if (time > 86399999U) {
        ++CurrentGame().status.game_time_days;
    }
    CurrentGame().status.game_time_ms = time % 86400000U;

    if (CurrentGame().activity.in_world()) {
        UpdateGameClock(elapsed);
    }
    g_tick = w8_get_ticks();

    if (!GetSecondaryWorld()) return;
    auto& sky = GetSecondaryWorld()->environment;
    RefreshCloudMaterial(*GetSecondaryWorld());
    const double arc = 3.141592653589793 * (1.0f / 180.0f) * 80.0;
    bool day;
    double angle;
    if (CurrentGame().status.game_time_ms < 18000001U) {
        day = false;
        angle = (CurrentGame().status.game_time_ms + 7200000) * arc * 3.9682539682539686e-08;
    } else if (CurrentGame().status.game_time_ms < 79200001U) {
        day = true;
        angle = (CurrentGame().status.game_time_ms - 18000000) * arc * 1.633986928104575e-08;
    } else {
        day = false;
        angle = (CurrentGame().status.game_time_ms - 79200000) * arc * 3.9682539682539686e-08;
    }

    glm::vec3 direction;
    direction = glm::vec3(0.0, sky.orbit_radius, 0.0);

    glm::mat3 rotation;
    rotation = glm::mat3(1);

    angle -= 3.141592653589793 * (1.0f / 180.0f) * 40.0;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 0, 1)));

    glm::vec3 position = rotation * direction + sky.celestial_origin;

    Prop* moving = day ? sky.sun : sky.moon;
    Prop* opposite = day ? sky.moon : sky.sun;
    if (moving != 0) {
        moving->SetLocation(&position);
    }
    if (opposite != 0) {
        opposite->SetLocation(&sky.celestial_origin);
    }

    unsigned int phase = ((CurrentGame().status.game_time_ms / 1000U) << 8) / 86400U;
    for (const auto& sky_gradient_animation : sky.gradients) {
        if (auto animation = sky_gradient_animation.lock()) {
            animation->playback->frame = static_cast<int>(phase);
        }
    }
}

/* Move the world clock on by the game-clock multiplier's share of the ticks
   since the baseline. Every caller expands this body, including its enable
   test, which SetEnvironmentTimeEnabled repeats right after setting the flag. */
static void AdvanceEnvironmentClock(void)
{
    if (g_environment_time_enabled) {
        std::uint64_t now = w8_get_ticks();
        std::uint64_t elapsed = now - g_tick;
        if (elapsed != 0) {
            AdvanceEnvironmentTime(
                static_cast<int>(static_cast<double>(elapsed) * g_view_distance));
        }
    }
}

/* Turn environment time progression on or off. Enabling it resets its
   elapsed-tick baseline and moves the world clock on by the game-clock
   multiplier's share of the time since that baseline. */
void SetEnvironmentTimeEnabled(bool enabled)
{
    if (!enabled) {
        g_environment_time_enabled = false;
        return;
    }

    g_environment_time_enabled = true;
    g_tick = w8_get_ticks();
    AdvanceEnvironmentClock();
}

/* The environment's per-frame update. A running transition short-circuits to
   the transition body. Otherwise, in day/night mode the sky consumes the day
   phase to pick the light direction, and the world colour table is refreshed
   from the same phase whenever the colour-refresh flag is set; any other mode
   just advances the clock. Every path that advances the clock scales the
   elapsed ticks by the game-clock multiplier. */
void UpdateEnvironment(void)
{
    if (!GetWorld()) return;
    if (GetWorld()->environment.transition_rate != 0.0f) {
        UpdateEnvironmentLighting();
        return;
    }
    if (!g_environment_time_enabled) {
        return;
    }
    if (GetWorld()->environment.lighting_mode == W8EnvironmentLightingMode::DayNight) {
        if (GetWorld()->environment.sky_enabled) {
            UpdateEnvironmentLight();
        }
        {
            RefreshEnvironment();
        }
    } else {
        AdvanceEnvironmentClock();
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
    unsigned char components[256 * 3];
    memset(components, 0xff, sizeof(components));
    if (!file || file->read(components, sizeof(components)).bytes != sizeof(components))
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
void UpdateEnvironmentLight(void)
{
    if (!GetWorld()) return;
    AdvanceEnvironmentClock();
    unsigned int phase = ((CurrentGame().status.game_time_ms / 1000U) << 8) / 86400U;
    if (phase != static_cast<unsigned int>(GetWorld()->environment.last_light_phase)) {
        GetWorld()->environment.light_direction = GetWorld()->environment.light_colours[phase];
        PublishLightDirection(&GetWorld()->environment.light_colours[phase]);
        GetWorld()->environment.last_light_phase = static_cast<int>(phase);
    }
}

void SetSkyEnabled(bool enabled)
{
    if (!GetWorld()) return;
    if (enabled) {
        if (GetWorld() == 0 || !GetWorld()->scene || GetWorld()->environment.static_fog != 0) {
            return;
        }

        GetWorld()->environment.static_fog = std::make_unique<FogParams>(GetWorld()->scene.get());
        GetWorld()->environment.dynamic_fog = std::make_unique<FogParams>(GetWorld()->scene.get());
        GetWorld()->environment.static_fog->density = 1.0f;
        GetWorld()->environment.dynamic_fog->density = 1.0f;

        RefreshFogRanges(*GetWorld());

        PublishLightDirection(&GetWorld()->environment.light_direction);
        if (GetWorld() == 0 || GetWorld()->camera == 0) {
            return;
        }
        GetWorld()->camera->setEnvironmentRange(
            static_cast<float>(WorldGetFarClip(GetWorld())) * GetWorld()->environment.range_start,
            static_cast<float>(WorldGetFarClip(GetWorld())) * GetWorld()->environment.range_end);
        return;
    }

    GetWorld()->environment.static_fog.reset();
    GetWorld()->environment.dynamic_fog.reset();

    glm::vec3 direction(0);
    PublishLightDirection(&direction);
    if (GetWorld() == 0 || GetWorld()->camera == 0) {
        return;
    }
    GetWorld()->camera->setEnvironmentRange(0.0f, static_cast<float>(WorldGetFarClip(GetWorld())));
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
void EnableSky(void)
{
    if (!GetWorld()) return;
    SetSkyEnabled(true);
    GetWorld()->environment.sky_enabled = true;
    UpdateEnvironmentLight();
}

/* Advance the clock-driven sky and refresh the world's environment colour
   from the day-phase table when the phase turns over. */
void RefreshEnvironment(void)
{
    if (!GetWorld()) return;
    AdvanceEnvironmentClock();
    unsigned int phase = ((CurrentGame().status.game_time_ms / 1000U) << 8) / 86400U;
    if (phase != static_cast<unsigned int>(GetWorld()->environment.last_colour_phase)) {
        glm::vec3 colour = GetWorld()->environment.colours[phase];
        SetWorldEnvironmentColour(colour);
        GetWorld()->environment.last_colour_phase = static_cast<int>(phase);
    }
}

/* Set the world's environment intensity, clamped to the unit range, keeping
   its current colour unless the world has no static scene to take it from. */
void SetWorldEnvironmentIntensity(World* world, float value)
{
    glm::vec3 colour{1.0f};

    if (world == 0) {
        Fail("pWorld");
    }
    if (1.0 <= value || 0.0 < value) {
        if (1.0 <= value) {
            value = static_cast<float>(1.0);
        }
    } else {
        value = static_cast<float>(0.0);
    }
    if (world->scene == 0) {
        colour = glm::vec3(0);
        ApplyEnvironmentColour(world, value, &colour);
        return;
    }
    ApplyEnvironmentColour(world, value, &world->environment.colour);
}

static float ApplyWorldLightingIntensity(World* world, float intensity)
{
    glm::vec3 colour{1.0f};
    if (1.0 <= intensity || 0.0 < intensity) {
        if (1.0 <= intensity) {
            intensity = static_cast<float>(1.0);
        }
    } else {
        intensity = static_cast<float>(0.0);
    }
    if (world->scene == 0) {
        colour = glm::vec3(0);
        ApplyEnvironmentColour(world, intensity, &colour);
    } else {
        ApplyEnvironmentColour(world, intensity, &world->environment.colour);
    }
    return intensity;
}

/* Arm or complete a lighting fade. A zero duration snaps back to day/night
   lighting with the current base intensity; any other duration stores
   1/duration as the per-millisecond rate and, for a fade-out, snapshots the
   live intensity into the world's base field. */
void BeginWorldLightingFade(float duration)
{
    if (!GetWorld()) return;
    World* world;
    float intensity;
    glm::vec3 direction{1.0f};

    if (duration == 0.0f) {
        GetWorld()->environment.lighting_mode = W8EnvironmentLightingMode::DayNight;
        GetWorld()->environment.transition_rate = 0.0f;
        g_world_render_enabled = 1;
        g_monster_shadow_updates_enabled = 1;
        g_world_blacked_out = false;
        g_monster_light_scale = 1.0f;

        world = GetWorld();
        if (world == 0) {
            Fail("pWorld");
        }
        intensity = world->environment.base_intensity;
        intensity = ApplyWorldLightingIntensity(world, intensity);

        world = GetSecondaryWorld();
        if (world != 0) {
            intensity = world->environment.base_intensity;
            intensity = ApplyWorldLightingIntensity(world, intensity);
        }

        direction = GetWorld()->environment.light_direction;
        direction = glm::clamp(direction, 0.0f, 1.0f);
        PublishLightDirection(&direction);
        return;
    }

    GetWorld()->environment.transition_rate = 1.0f / duration;
    GetWorld()->environment.transition_tick = w8_get_ticks();
    GetWorld()->environment.lighting_mode = W8EnvironmentLightingMode::Transition;
    if (duration < 0.0f) {
        GetWorld()->environment.base_intensity = GetWorld()->environment.intensity;
        if (GetSecondaryWorld() != 0) {
            GetSecondaryWorld()->environment.base_intensity =
                GetSecondaryWorld()->environment.intensity;
        }
    }
}

/* Advance an armed lighting transition by the milliseconds since its tick
   baseline: update the shared light scale, reapply both worlds' ambient from
   scale times each world's base intensity, and publish a scale-modulated light
   direction. Completing a fade-out leaves mode 0; completing a fade-in returns
   to day/night mode 2. */
void UpdateEnvironmentLighting(void)
{
    if (!GetWorld()) return;
    std::uint64_t now = w8_get_ticks();
    std::uint64_t elapsed = now - GetWorld()->environment.transition_tick;
    World* world;
    float scale;
    float intensity;
    glm::vec3 direction{1.0f};

    if (elapsed == 0) {
        return;
    }

    scale = elapsed * GetWorld()->environment.transition_rate + g_monster_light_scale;
    if (1.0f < scale) {
        scale = 1.0f;
    } else if (scale < 0.0f) {
        scale = 0.0f;
    }
    g_monster_light_scale = scale;

    world = GetWorld();
    if (world == 0) {
        Fail("pWorld");
    }
    intensity = scale * world->environment.base_intensity;
    intensity = ApplyWorldLightingIntensity(world, intensity);

    world = GetSecondaryWorld();
    if (world != 0) {
        float secondary = scale * world->environment.base_intensity;
        secondary = ApplyWorldLightingIntensity(world, secondary);
    }

    direction = GetWorld()->environment.light_direction;
    direction *= scale;
    direction = glm::clamp(direction, 0.0f, 1.0f);
    PublishLightDirection(&direction);

    if (intensity == 0.0f) {
        GetWorld()->environment.lighting_mode = W8EnvironmentLightingMode::FadedOut;
        g_world_render_enabled = 0;
        g_monster_shadow_updates_enabled = 0;
        g_world_blacked_out = true;
        GetWorld()->environment.transition_rate = 0.0f;
        GetWorld()->environment.transition_tick = now;
        return;
    }
    if (scale == 1.0f) {
        GetWorld()->environment.lighting_mode = W8EnvironmentLightingMode::DayNight;
        g_world_render_enabled = 1;
        g_monster_shadow_updates_enabled = 1;
        g_world_blacked_out = false;
        GetWorld()->environment.transition_rate = 0.0f;
        GetWorld()->environment.transition_tick = now;
        return;
    }
    g_world_blacked_out = false;
    GetWorld()->environment.transition_tick = now;
    GetWorld()->environment.lighting_mode = W8EnvironmentLightingMode::Transition;
    g_world_render_enabled = 1;
    g_monster_shadow_updates_enabled = 1;
}

void DisableSky(void)
{
    if (!GetWorld()) return;
    SetSkyEnabled(false);
    GetWorld()->environment.sky_enabled = false;
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

/* Clear the whole ambient block. The six stores together are what makes them one
   group; the orbit radius last, at minus one, which is the "not measured yet"
   value InitializeLevelEnvironment tests before re-deriving the sky. */
void ResetEnvironment(void)
{
    if (!GetSecondaryWorld()) return;
    auto& sky = GetSecondaryWorld()->environment;
    sky.gradients[0].reset();
    sky.gradients[1].reset();
    sky.gradients[2].reset();
    sky.sun = 0;
    sky.moon = 0;
    sky.orbit_radius = -1.0f;
}

/* The direction light comes from. Setting it also hands the new direction to
   the renderer, so the two are not a plain field pair. */
void SetLightDirection(const glm::vec3* direction)
{
    if (GetWorld()) GetWorld()->environment.light_direction = *direction;
    PublishLightDirection(direction);
}

/* The ambient light the world contributes, or nothing at all when the world's
   own gate at 0x3c is clear. Both assertions belong to this body: line 616
   names the world and line 617 names the out-parameter pLightValue. */
void GetWorldLightValue(const World* world, glm::vec3* pLightValue)
{
    if (world == 0) {
        Fail("pWorld");
    }
    if (pLightValue == 0) {
        Fail("pLightValue");
    }
    if (world->scene != 0) {
        *pLightValue = world->environment.colour;
    } else {
        *pLightValue = glm::vec3(0);
    }
}

/* Drops the two renderer objects the environment holds and clears the count
   that goes with them. Both releases run through one loaded import address,
   which is what makes the two globals the same class rather than two. */

/* Retain the world's current intensity while replacing its environment
   colour. Retail takes the colour triple by value and forwards its address to
   SetWorldEnvironment. */
void SetWorldEnvironmentColour(glm::vec3 colour)
{
    if (GetWorld() == nullptr) {
        Fail("pWorld");
    }
    ApplyEnvironmentColour(GetWorld(), GetWorld()->environment.intensity, &colour);
}

/* Push one day-phase colour and intensity into the world's static scene, every
   registered environment light, and the animated cloud material. */
void ApplyEnvironmentColour(World* world, float intensity, const glm::vec3* colour)
{
    if (world == 0) {
        Fail("pWorld");
    }
    if (world->scene != 0) {
        const auto ambient = glm::clamp(*colour * intensity, 0.0f, 1.0f);
        world->scene->setAmbientLight(ambient);
        world->environment.colour = *colour;
        world->environment.intensity = intensity;
    }
    for (Light* light : world->environment.lights) {
        glm::vec3 scaled(glm::dvec3(*colour) * static_cast<double>(intensity));
        light->ambient = glm::clamp(scaled, 0.0f, 1.0f);
    }
    RefreshCloudMaterial(*world);
}

/* Write the camera light's intensity, if the world has one. */
void SetCameraLightIntensity(float value)
{
    if (GetWorld()->camera_light != 0) {
        GetWorld()->camera_light->intensity = value;
    }
}

/* Show or hide the world's camera light, which is the renderer's flag zero the
   other way round: showing it clears the flag. 0x00483E50 loads the light from
   GetWorld() at offset 0x54 and reaches the same two imported flag thunks, so the
   node here is the camera light the world setup creates, never a sky node. */
void SetSkyNodeVisible(bool visible)
{
    Light* camera_light = GetWorld()->camera_light;

    if (camera_light != 0) {
        if (visible) {
            camera_light->visible = true;
        } else {
            camera_light->visible = false;
        }
    }
}

/* Raise or lower the camera light by one step, disable it, or enable it. */
// FUNCTION: WIZ8 0x00483E80
void SetCameraLightMode(W8CameraLightMode mode)
{
    Light* camera_light;
    float intensity;

    if (GetWorld() != 0 && (camera_light = GetWorld()->camera_light) != 0) {
        switch (mode) {
        case W8CameraLightMode::Increase:
            camera_light->visible = true;
            camera_light = GetWorld()->camera_light;
            intensity = 0.0f;
            if (camera_light != 0) {
                intensity = camera_light->intensity;
            }
            intensity += 0.5f;
            if (CAMERA_LIGHT_MAXIMUM_INTENSITY < intensity) {
                intensity = CAMERA_LIGHT_MAXIMUM_INTENSITY;
            }
            if (camera_light != 0) {
                camera_light->intensity = intensity;
            }
            return;
        case W8CameraLightMode::Decrease:
            intensity = camera_light->intensity - 0.5f;
            if (0.0f < intensity) {
                camera_light->intensity = intensity;
                return;
            }
            [[fallthrough]];
        case W8CameraLightMode::Disable:
            camera_light->visible = false;
            return;
        case W8CameraLightMode::Enable:
            camera_light->visible = true;
            return;
        }
    }
}

void SetGameTimeMilliseconds(int value)
{
    CurrentGame().status.game_time_ms = value;
    g_tick = w8_get_ticks();
}

const char* g_sky_gradient_names[3] = {"SkyGrad0000.ifl", "Skytop0000.ifl", "Horizon0000.ifl"};

/* Level-entry environment setup: locate the level's Sun and Moon props, derive
   the celestial origin and orbit radius from the distance between them, register
   the three sky gradient textures, advance the day clock and publish the current
   day phase's colour and light direction. */
void InitializeLevelEnvironment(void)
{
    if (!GetWorld()) return;
    if (GetSecondaryWorld()) {
        auto& sky = GetSecondaryWorld()->environment;
        sky.sun = FindPropByName(GetSecondaryWorld(), "Sun");
        sky.moon = FindPropByName(GetSecondaryWorld(), "Moon");
        if (sky.orbit_radius < 0.0f) {
            if (sky.sun == 0 || sky.moon == 0) {
                sky.sun = 0;
                sky.moon = 0;
            } else {
                glm::vec3 sun;
                glm::vec3 moon;

                sun = sky.sun->GetLocation();
                moon = sky.moon->GetLocation();
                glm::vec3 delta = sun - moon;
                glm::vec3 midpoint = (sun + moon) * 0.5f;
                sky.orbit_radius = glm::length(delta) * 0.5f;
                sky.celestial_origin = midpoint;
                for (int index = 0; index < 3; ++index) {
                    const char* name = g_sky_gradient_names[index];
                    std::shared_ptr<Texture> animation = g_texture_animations.find(name);
                    sky.gradients[index] = animation;
                    if (animation != 0) {
                        animation->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                    }
                }
            }
        }
    }
    AdvanceEnvironmentClock();
    unsigned int phase = ((CurrentGame().status.game_time_ms / 1000U) << 8) / 86400U;
    SetWorldEnvironmentColour(GetWorld()->environment.colours[phase]);
    GetWorld()->environment.light_direction = GetWorld()->environment.light_colours[phase];
    PublishLightDirection(&GetWorld()->environment.light_colours[phase]);
}

EnvironmentState::EnvironmentState() = default;
EnvironmentState::~EnvironmentState() = default;

glm::vec3 EnvironmentLightDirection()
{
    return GetWorld() ? GetWorld()->environment.light_direction : glm::vec3(1.0f);
}
bool IsSkyEnabled()
{
    return GetWorld() && GetWorld()->environment.sky_enabled;
}
W8EnvironmentLightingMode EnvironmentLightingMode()
{
    return GetWorld() ? GetWorld()->environment.lighting_mode : W8EnvironmentLightingMode::DayNight;
}
