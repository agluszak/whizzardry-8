#include "wiz8/engine_code/Environment.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>

#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include "compat/compiler.h"
#include "compat/native.h"
#include "glm/common.hpp"
#include "glm/ext/matrix_float3x3.hpp"
#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "resource_cache.h"
#include "surrender/srCamera.h"
#include "surrender/srDebug.h"
#include "surrender/srFog.h"
#include "surrender/srNode.h"
#include "surrender/srScene.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"
#include "texture_animation.h"
#include "wiz8/engine_code/3d.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/materials.h"
#include "wiz8/engine_code/stLight.hpp"
#include "wiz8/engine_code/stTextureAnim.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayTime.h"

#define ENVIRONMENT_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\Environment.cpp"

// GLOBAL: WIZ8 0x0060a3a8
W8EnvironmentLightingMode g_environment_lighting_mode = W8EnvironmentLightingMode::DayNight;

// GLOBAL: WIZ8 0x0065b9ae
bool g_sky_enabled;
// GLOBAL: WIZ8 0x0065B9A8
static std::uint64_t g_tick;
/* 0x00659AB4: the world being rendered. Its sky node is the one field these
   two bodies reach, and it is the same W8World the 3d code walks. */

/* Camera-light intensity ceiling for SetCameraLightMode; the linker
   folds it into the shared 50.0f constant Octree.cpp emits at 0x005EC02C. */
static const float CAMERA_LIGHT_MAXIMUM_INTENSITY = 50.0f;

/* 1/duration while the transition body at 0x00484300 runs, zero when idle.
   BeginWorldLightingFade stores 1/duration and sets the lighting mode to 1;
   UpdateEnvironmentLighting stores zero again once it completes. */
// GLOBAL: WIZ8 0x0065b9b8
float g_environment_transition_rate;
/* Tick baseline for the active lighting transition; rewritten every frame the
   transition body advances the light scale. */
// GLOBAL: WIZ8 0x0065b9bc
std::uint64_t g_environment_transition_tick;
// GLOBAL: WIZ8 0x0060a3ac

// GLOBAL: WIZ8 0x0060a395
bool g_environment_colour_refresh = true;

/* The mapper starts its scroll rate at 0.002 texture units per second on x
   only and reseeds the shared frame clock, so the first scrolled frame uses
   the interval since this object's construction rather than since startup. */
// FUNCTION: WIZ8 0x00482010
W8MaterialMapper::W8MaterialMapper()
{
    scroll_rate_u = 0.002f;
    scroll_rate_v = 0.0f;
    g_frame_tick = w8_get_ticks();
}

// FUNCTION: WIZ8 0x00482040
int W8MaterialMapper::isActive(srVertexPipe&)
{
    return 1;
}

/* Add the frame-scaled rate to each axis and keep only the fractional part,
   then shift the first texture coordinate set of every vertex by it. */
// FUNCTION: WIZ8 0x00482050
void W8MaterialMapper::process(srVertexPipe& pipe)
{
    float offset_x;
    float offset_y;

    if (!pipe.isChannelAvailable(srVertexProcessor::CHANNEL_ST0)) {
        return;
    }
    w8_ulong count = pipe.getVertexCount();
    glm::vec2* coordinates = pipe.getST(0, 0);

    offset_x = scroll_rate_u * g_frame_elapsed + scroll_u;
    scroll_u = offset_x - static_cast<float>(floor(offset_x));
    offset_y = scroll_rate_v * g_frame_elapsed + scroll_v;
    scroll_v = offset_y - static_cast<float>(floor(offset_y));

    for (w8_ulong index = 0; index < count; ++index) {
        coordinates[index].x += scroll_u;
        coordinates[index].y += scroll_v;
    }
}

/* Drop every per-level environment object: the sky gradients, the Sun/Moon
   props and the orbit radius reset, then release the two fog objects and
   empty the registered-light list without freeing its storage. */
// FUNCTION: WIZ8 0x004823B0
void ClearEnvironmentObjects(void)
{
    ResetEnvironment();
    if (GetWorld()) {
        GetWorld()->static_fog.reset();
        GetWorld()->dynamic_fog.reset();
    }
}

/* Advance the authoritative game clock and place the two celestial props on
   opposite sides of the world's recovered sky origin: the day's prop sits one
   orbit radius away in the rotated direction, the other rests on the origin.
   The three animated sky gradients consume the same 8-bit day phase, so the
   clock, prop placement, and gradient animation remain one update rather than
   parallel timers. */
// FUNCTION: WIZ8 0x00482a20
void AdvanceEnvironmentTime(int elapsed)
{
    unsigned int time = g_status.game_time_ms + elapsed;
    if (time > 86399999U) {
        ++g_status.game_time_days;
    }
    g_status.game_time_ms = time % 86400000U;

    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        UpdateGameClock(elapsed);
    }
    g_tick = w8_get_ticks();

    const double arc = 3.141592653589793 * (1.0f / 180.0f) * 80.0;
    bool day;
    double angle;
    if (g_status.game_time_ms < 18000001U) {
        day = false;
        angle = (g_status.game_time_ms + 7200000) * arc * 3.9682539682539686e-08;
    } else if (g_status.game_time_ms < 79200001U) {
        day = true;
        angle = (g_status.game_time_ms - 18000000) * arc * 1.633986928104575e-08;
    } else {
        day = false;
        angle = (g_status.game_time_ms - 79200000) * arc * 3.9682539682539686e-08;
    }

    glm::vec3 direction;
    direction = glm::vec3(0.0, g_celestial_orbit_radius, 0.0);

    glm::mat3 rotation;
    rotation = glm::mat3(1);

    angle -= 3.141592653589793 * (1.0f / 180.0f) * 40.0;
    rotation *= glm::mat3_cast(glm::angleAxis(float(angle), glm::vec3(0, 0, 1)));

    glm::vec3 position = rotation * direction + g_celestial_origin;

    W8Prop* moving = day ? g_sun_prop : g_moon_prop;
    W8Prop* opposite = day ? g_moon_prop : g_sun_prop;
    if (moving != 0) {
        moving->Rep()->SetLocation(&position);
    }
    if (opposite != 0) {
        opposite->Rep()->SetLocation(&g_celestial_origin);
    }

    unsigned int phase = ((g_status.game_time_ms / 1000U) << 8) / 86400U;
    for (const auto& sky_gradient_animation : g_sky_gradient_animations) {
        if (auto animation = sky_gradient_animation.lock()) {
            animation->frame = static_cast<int>(phase);
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
// FUNCTION: WIZ8 0x00482990
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
// FUNCTION: WIZ8 0x00482770
void UpdateEnvironment(void)
{
    if (g_environment_transition_rate != 0.0f) {
        UpdateEnvironmentLighting();
        return;
    }
    if (!g_environment_time_enabled) {
        return;
    }
    if (g_environment_lighting_mode == W8EnvironmentLightingMode::DayNight) {
        if (g_sky_enabled) {
            UpdateEnvironmentLight();
        }
        if (g_environment_colour_refresh) {
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
// FUNCTION: WIZ8 0x004834B0
void UpdateEnvironmentLight(void)
{
    if (!GetWorld()) return;
    AdvanceEnvironmentClock();
    unsigned int phase = ((g_status.game_time_ms / 1000U) << 8) / 86400U;
    if (phase != static_cast<unsigned int>(GetWorld()->last_light_phase)) {
        g_light_direction = GetWorld()->light_colours[phase];
        PublishLightDirection(&GetWorld()->light_colours[phase]);
        GetWorld()->last_light_phase = static_cast<int>(phase);
    }
}

// FUNCTION: WIZ8 0x00483750
void SetSkyEnabled(bool enabled)
{
    if (!GetWorld()) return;
    if (enabled) {
        if (GetWorld() == 0 || GetWorld()->dynamic_scene == 0 || GetWorld()->static_fog != 0) {
            return;
        }

        GetWorld()->static_fog = std::make_unique<srFog>(GetWorld()->static_scene.get());
        GetWorld()->dynamic_fog = std::make_unique<srFog>(GetWorld()->dynamic_scene);
        GetWorld()->static_fog->density = 1.0f;
        GetWorld()->dynamic_fog->density = 1.0f;

        RefreshFogRanges(*GetWorld());

        PublishLightDirection(&g_light_direction);
        if (GetWorld() == 0 || GetWorld()->camera == 0) {
            return;
        }
        GetWorld()->camera->setEnvironmentRange(
            static_cast<float>(WorldGetFarClip(GetWorld())) * GetWorld()->environment_range_start,
            static_cast<float>(WorldGetFarClip(GetWorld())) * GetWorld()->environment_range_end);
        return;
    }

    GetWorld()->static_fog.reset();
    GetWorld()->dynamic_fog.reset();

    glm::vec3 direction(0);
    PublishLightDirection(&direction);
    if (GetWorld() == 0 || GetWorld()->camera == 0) {
        return;
    }
    GetWorld()->camera->setEnvironmentRange(0.0f, static_cast<float>(WorldGetFarClip(GetWorld())));
}

/* The far plane the world is drawn to. */
// FUNCTION: WIZ8 0x00482750
void SetViewDistance(float distance)
{
    g_view_distance = distance;
}

// FUNCTION: WIZ8 0x00482760
float GetViewDistance(void)
{
    return g_view_distance;
}

/* Turn the sky on, then catch the light direction up: advance the clock by the
   elapsed ticks' share and publish the day phase's light direction if it moved
   on. The phase tracker is the same one the per-frame update keeps, so enabling
   the sky does not restart the cycle. */
// FUNCTION: WIZ8 0x00482EA0
void EnableSky(void)
{
    SetSkyEnabled(true);
    g_sky_enabled = true;
    UpdateEnvironmentLight();
}

// GLOBAL: WIZ8 0x0060a3b0

/* Advance the clock-driven sky and refresh the world's environment colour
   from the day-phase table when the phase turns over. */
// FUNCTION: WIZ8 0x00483560
void RefreshEnvironment(void)
{
    if (!GetWorld()) return;
    AdvanceEnvironmentClock();
    unsigned int phase = ((g_status.game_time_ms / 1000U) << 8) / 86400U;
    if (phase != static_cast<unsigned int>(GetWorld()->last_environment_colour_phase)) {
        glm::vec3 colour = GetWorld()->environment_colours[phase];
        SetWorldEnvironmentColour(colour);
        GetWorld()->last_environment_colour_phase = static_cast<int>(phase);
    }
}

/* Set the world's environment intensity, clamped to the unit range, keeping
   its current colour unless the world has no static scene to take it from. */
// FUNCTION: WIZ8 0x00483AE0
void SetWorldEnvironmentIntensity(W8World* world, float value)
{
    glm::vec3 colour{1.0f};

    if (world == 0) {
        srAssertFail("pWorld", ENVIRONMENT_CPP, 0x298, 0);
    }
    if (1.0 <= value || 0.0 < value) {
        if (1.0 <= value) {
            value = static_cast<float>(1.0);
        }
    } else {
        value = static_cast<float>(0.0);
    }
    if (world->static_scene == 0) {
        colour = glm::vec3(0);
        ApplyEnvironmentColour(world, value, &colour);
        return;
    }
    ApplyEnvironmentColour(world, value, &world->environment_colour);
}

static float ApplyWorldLightingIntensity(W8World* world, float intensity)
{
    glm::vec3 colour{1.0f};
    if (1.0 <= intensity || 0.0 < intensity) {
        if (1.0 <= intensity) {
            intensity = static_cast<float>(1.0);
        }
    } else {
        intensity = static_cast<float>(0.0);
    }
    if (world->static_scene == 0) {
        colour = glm::vec3(0);
        ApplyEnvironmentColour(world, intensity, &colour);
    } else {
        ApplyEnvironmentColour(world, intensity, &world->environment_colour);
    }
    return intensity;
}

/* Arm or complete a lighting fade. A zero duration snaps back to day/night
   lighting with the current base intensity; any other duration stores
   1/duration as the per-millisecond rate and, for a fade-out, snapshots the
   live intensity into the world's base field. */
// FUNCTION: WIZ8 0x00483FD0
void BeginWorldLightingFade(float duration)
{
    W8World* world;
    float intensity;
    glm::vec3 direction{1.0f};

    if (duration == 0.0f) {
        g_environment_lighting_mode = W8EnvironmentLightingMode::DayNight;
        g_environment_transition_rate = 0.0f;
        g_world_render_enabled = 1;
        g_monster_shadow_updates_enabled = 1;
        g_world_blacked_out = false;
        g_monster_light_scale = 1.0f;

        world = GetWorld();
        intensity = world->environment_base_intensity;
        if (world == 0) {
            srAssertFail("pWorld", ENVIRONMENT_CPP, 0x298, 0);
        }
        intensity = ApplyWorldLightingIntensity(world, intensity);

        world = GetSecondaryWorld();
        if (world != 0) {
            intensity = world->environment_base_intensity;
            intensity = ApplyWorldLightingIntensity(world, intensity);
        }

        direction = g_light_direction;
        direction = glm::clamp(direction, 0.0f, 1.0f);
        PublishLightDirection(&direction);
        return;
    }

    g_environment_transition_rate = 1.0f / duration;
    g_environment_transition_tick = w8_get_ticks();
    g_environment_lighting_mode = W8EnvironmentLightingMode::Transition;
    if (duration < 0.0f) {
        GetWorld()->environment_base_intensity = GetWorld()->environment_intensity;
        if (GetSecondaryWorld() != 0) {
            GetSecondaryWorld()->environment_base_intensity =
                GetSecondaryWorld()->environment_intensity;
        }
    }
}

/* Advance an armed lighting transition by the milliseconds since its tick
   baseline: update the shared light scale, reapply both worlds' ambient from
   scale times each world's base intensity, and publish a scale-modulated light
   direction. Completing a fade-out leaves mode 0; completing a fade-in returns
   to day/night mode 2. */
// FUNCTION: WIZ8 0x00484300
void UpdateEnvironmentLighting(void)
{
    std::uint64_t now = w8_get_ticks();
    std::uint64_t elapsed = now - g_environment_transition_tick;
    W8World* world;
    float scale;
    float intensity;
    glm::vec3 direction{1.0f};

    if (elapsed == 0) {
        return;
    }

    scale = elapsed * g_environment_transition_rate + g_monster_light_scale;
    if (1.0f < scale) {
        scale = 1.0f;
    } else if (scale < 0.0f) {
        scale = 0.0f;
    }
    g_monster_light_scale = scale;

    world = GetWorld();
    intensity = scale * world->environment_base_intensity;
    if (world == 0) {
        srAssertFail("pWorld", ENVIRONMENT_CPP, 0x298, 0);
    }
    intensity = ApplyWorldLightingIntensity(world, intensity);

    world = GetSecondaryWorld();
    if (world != 0) {
        float secondary = scale * world->environment_base_intensity;
        secondary = ApplyWorldLightingIntensity(world, secondary);
    }

    direction = g_light_direction;
    direction *= scale;
    direction = glm::clamp(direction, 0.0f, 1.0f);
    PublishLightDirection(&direction);

    if (intensity == 0.0f) {
        g_environment_lighting_mode = W8EnvironmentLightingMode::FadedOut;
        g_world_render_enabled = 0;
        g_monster_shadow_updates_enabled = 0;
        g_world_blacked_out = true;
        g_environment_transition_rate = 0.0f;
        g_environment_transition_tick = now;
        return;
    }
    if (scale == 1.0f) {
        g_environment_lighting_mode = W8EnvironmentLightingMode::DayNight;
        g_world_render_enabled = 1;
        g_monster_shadow_updates_enabled = 1;
        g_world_blacked_out = false;
        g_environment_transition_rate = 0.0f;
        g_environment_transition_tick = now;
        return;
    }
    g_world_blacked_out = false;
    g_environment_transition_tick = now;
    g_environment_lighting_mode = W8EnvironmentLightingMode::Transition;
    g_world_render_enabled = 1;
    g_monster_shadow_updates_enabled = 1;
}

// FUNCTION: WIZ8 0x00482F60
void DisableSky(void)
{
    SetSkyEnabled(false);
    g_sky_enabled = false;
}

/* Refresh the two fog objects' ranges from the world's far clip. Runs after
   the camera's clip range moves, so the fog tracks the same plane. */
// FUNCTION: WIZ8 0x004836A0
void RefreshFogRanges(W8World& world)
{
    if (world.static_fog != nullptr) {
        world.static_fog->fog_end = WorldGetFarClip(&world) * world.environment_range_end;
        world.static_fog->fog_start = WorldGetFarClip(&world) * world.environment_range_start;
        world.dynamic_fog->fog_start = WorldGetFarClip(&world) * world.environment_range_start;
        world.dynamic_fog->fog_end = WorldGetFarClip(&world) * world.environment_range_end;
    }
}

/* Clear the whole ambient block. The six stores together are what makes them one
   group; the orbit radius last, at minus one, which is the "not measured yet"
   value InitializeLevelEnvironment tests before re-deriving the sky. */
// FUNCTION: WIZ8 0x004826b0
void ResetEnvironment(void)
{
    g_sky_gradient_animations[0].reset();
    g_sky_gradient_animations[1].reset();
    g_sky_gradient_animations[2].reset();
    g_sun_prop = 0;
    g_moon_prop = 0;
    g_celestial_orbit_radius = -1.0f;
}

/* The direction light comes from. Setting it also hands the new direction to
   the renderer, so the two are not a plain field pair. */
// FUNCTION: WIZ8 0x00483650
void SetLightDirection(const glm::vec3* direction)
{
    g_light_direction = *direction;
    PublishLightDirection(direction);
}

/* The ambient light the world contributes, or nothing at all when the world's
   own gate at 0x3c is clear. Both assertions belong to this body: line 616
   names the world and line 617 names the out-parameter pLightValue. */
// FUNCTION: WIZ8 0x004839e0
void GetWorldLightValue(const W8World* world, glm::vec3* pLightValue)
{
    if (world == 0) {
        srAssertFail("pWorld", ENVIRONMENT_CPP, 616, 0);
    }
    if (pLightValue == 0) {
        srAssertFail("pLightValue", ENVIRONMENT_CPP, 617, 0);
    }
    if (world->static_scene != 0) {
        *pLightValue = world->environment_colour;
    } else {
        *pLightValue = glm::vec3(0);
    }
}

/* Drops the two renderer objects the environment holds and clears the count
   that goes with them. Both releases run through one loaded import address,
   which is what makes the two globals the same class rather than two. */
// FUNCTION: WIZ8 0x004826e0

// FUNCTION: WIZ8 0x00483F30

/* Retain the world's current intensity while replacing its environment
   colour. Retail takes the colour triple by value and forwards its address to
   SetWorldEnvironment. */
// FUNCTION: WIZ8 0x00483a60
void SetWorldEnvironmentColour(glm::vec3 colour)
{
    if (GetWorld() == nullptr) {
        srAssertFail("pWorld", ENVIRONMENT_CPP, 634, 0);
    }
    ApplyEnvironmentColour(GetWorld(), GetWorld()->environment_intensity, &colour);
}

// GLOBAL: WIZ8 0x005ec988
const double g_inverse_half_day_ms = 2.3148148148148148e-08;
// GLOBAL: WIZ8 0x005ec990
const double g_half_day_ms = 43200000.0;

/* Push one day-phase colour and intensity into the world's static scene, every
   registered environment light, and the animated cloud material. */
// FUNCTION: WIZ8 0x00483ba0
void ApplyEnvironmentColour(W8World* world, float intensity, const glm::vec3* colour)
{
    if (world == 0) {
        srAssertFail("pWorld", ENVIRONMENT_CPP, 0x2b0, 0);
    }
    if (world->static_scene != 0) {
        const auto ambient = glm::clamp(*colour * intensity, 0.0f, 1.0f);
        world->static_scene->setAmbientLight(ambient);
        world->environment_colour = *colour;
        world->environment_intensity = intensity;
    }
    for (stLight* light : world->environment_lights) {
        glm::vec3 scaled(glm::dvec3(*colour) * static_cast<double>(intensity));
        light->ambient = glm::clamp(scaled, 0.0f, 1.0f);
    }
    {
        if (GetSecondaryWorld() && GetSecondaryWorld()->cloud_material) {
            auto& material = GetSecondaryWorld()->cloud_material;
            double brightness =
                1.0 - fabs(g_status.game_time_ms - g_half_day_ms) * g_inverse_half_day_ms;

            material->parms.ambient.x = static_cast<float>(brightness);
            material->parms.ambient.y = static_cast<float>(brightness);
            material->parms.ambient.z = static_cast<float>(brightness);
            material->parms.ambient.w = static_cast<float>(brightness);
            material->parms.diffuse.w = static_cast<float>(brightness * 0.75 + 0.25);

            material->dirty = 1;
        }
    }
}

/* Write the camera light's intensity, if the world has one. */
// FUNCTION: WIZ8 0x00483e30
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
// FUNCTION: WIZ8 0x00483e50
void SetSkyNodeVisible(bool visible)
{
    stLight* camera_light = GetWorld()->camera_light;

    if (camera_light != 0) {
        if (visible) {
            camera_light->clearFlag(srNode::FLAG_DISABLE);
        } else {
            camera_light->setFlag(srNode::FLAG_DISABLE);
        }
    }
}

/* Raise or lower the camera light by one step, disable it, or enable it. */
// FUNCTION: WIZ8 0x00483E80
void SetCameraLightMode(W8CameraLightMode mode)
{
    stLight* camera_light;
    float intensity;

    if (GetWorld() != 0 && (camera_light = GetWorld()->camera_light) != 0) {
        switch (mode) {
        case W8CameraLightMode::Increase:
            camera_light->clearFlag(srNode::FLAG_DISABLE);
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
            camera_light->setFlag(srNode::FLAG_DISABLE);
            return;
        case W8CameraLightMode::Enable:
            camera_light->clearFlag(srNode::FLAG_DISABLE);
            return;
        }
    }
}

// FUNCTION: WIZ8 0x00482720
void SetGameTimeMilliseconds(int value)
{
    g_status.game_time_ms = value;
    g_tick = w8_get_ticks();
}

// GLOBAL: WIZ8 0x0060a398
const char* g_sky_gradient_names[3] = {"SkyGrad0000.ifl", "Skytop0000.ifl", "Horizon0000.ifl"};

/* Level-entry environment setup: locate the level's Sun and Moon props, derive
   the celestial origin and orbit radius from the distance between them, register
   the three sky gradient textures, advance the day clock and publish the current
   day phase's colour and light direction. */
// FUNCTION: WIZ8 0x00482410
void InitializeLevelEnvironment(void)
{
    if (GetSecondaryWorld() != 0) {
        g_sun_prop = FindPropByName(GetSecondaryWorld(), "Sun");
        g_moon_prop = FindPropByName(GetSecondaryWorld(), "Moon");
    }
    if (g_celestial_orbit_radius < 0.0f) {
        if (g_sun_prop == 0 || g_moon_prop == 0) {
            g_sun_prop = 0;
            g_moon_prop = 0;
        } else {
            glm::vec3 sun;
            glm::vec3 moon;

            sun = g_sun_prop->Rep()->GetLocation();
            moon = g_moon_prop->Rep()->GetLocation();
            glm::vec3 delta = sun - moon;
            glm::vec3 midpoint = (sun + moon) * 0.5f;
            g_celestial_orbit_radius = glm::length(delta) * 0.5f;
            g_celestial_origin = midpoint;
            for (int index = 0; index < 3; ++index) {
                const char* name = g_sky_gradient_names[index];
                std::shared_ptr<stTextureAnim> animation = g_texture_animations.find(name);
                g_sky_gradient_animations[index] = animation;
                if (animation != 0) {
                    animation->animation_mode = W8_TEXTURE_ANIM_MANUAL;
                }
            }
        }
    }
    AdvanceEnvironmentClock();
    unsigned int phase = ((g_status.game_time_ms / 1000U) << 8) / 86400U;
    SetWorldEnvironmentColour(GetWorld()->environment_colours[phase]);
    g_light_direction = GetWorld()->light_colours[phase];
    PublishLightDirection(&GetWorld()->light_colours[phase]);
}
