#include "wiz8/world/environment.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
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
#include "wiz8/gameplay_constants.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material_loader.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

// The 8-bit day phase the colour tables and sky gradients are indexed by.
static unsigned int DayPhase(unsigned int time_of_day)
{
    return ((time_of_day / 1000U) << 8) / 86400U;
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

Environment::Environment(World& world) : world_(world) {}
Environment::~Environment() = default;

void Environment::configure(const EnvironmentSettings& settings)
{
    settings_ = settings;
    intensity_ = settings.intensity;
    colour_ = glm::vec3(1.0f);
    world_.scene->setAmbientLight(glm::clamp(colour_ * intensity_, 0.0f, 1.0f));
}

void Environment::removeLight(Light* light)
{
    std::erase(lights_, light);
}

void Environment::setCloudMaterial(std::shared_ptr<Material> material)
{
    cloud_material_ = std::move(material);
}

void Environment::applyColour(float value, glm::vec3 value_colour)
{
    if (world_.scene) {
        world_.scene->setAmbientLight(glm::clamp(value_colour * value, 0.0f, 1.0f));
        colour_ = value_colour;
        intensity_ = value;
    }
    for (Light* light : lights_) {
        glm::vec3 scaled(glm::dvec3(value_colour) * static_cast<double>(value));
        light->ambient = glm::clamp(scaled, 0.0f, 1.0f);
    }
}

float Environment::setIntensity(float value)
{
    value = value >= 1.0f ? 1.0f : (value > 0.0f ? value : 0.0f);
    applyColour(value, ambient());
    return value;
}

glm::vec3 Environment::ambient() const
{
    return world_.scene ? colour_ : glm::vec3(0);
}

bool Environment::showLightPhase(unsigned int time_of_day)
{
    const unsigned int phase = DayPhase(time_of_day);
    if (std::exchange(last_light_phase_, static_cast<int>(phase)) == static_cast<int>(phase))
        return false;
    light_direction_ = settings_.light_colours[phase];
    return true;
}

void Environment::showColourPhase(unsigned int time_of_day)
{
    const unsigned int phase = DayPhase(time_of_day);
    if (std::exchange(last_colour_phase_, static_cast<int>(phase)) != static_cast<int>(phase))
        applyColour(intensity_, settings_.colours[phase]);
}

void Environment::showDayPhase(unsigned int time_of_day)
{
    const unsigned int phase = DayPhase(time_of_day);
    applyColour(intensity_, settings_.colours[phase]);
    light_direction_ = settings_.light_colours[phase];
}

void Environment::setFogColour(glm::vec3 value)
{
    world_.scene->setFogColor(value);
}

void Environment::enableFog()
{
    if (!world_.scene || static_fog_) return;
    static_fog_ = std::make_unique<FogParams>(world_.scene.get());
    dynamic_fog_ = std::make_unique<FogParams>(world_.scene.get());
    static_fog_->density = 1.0f;
    dynamic_fog_->density = 1.0f;
    refreshFogRanges();
    if (world_.camera) {
        const auto far_clip = static_cast<float>(WorldGetFarClip(&world_));
        world_.camera->setEnvironmentRange(far_clip * settings_.range_start,
                                           far_clip * settings_.range_end);
    }
}

void Environment::disableFog()
{
    static_fog_.reset();
    dynamic_fog_.reset();
    if (world_.camera)
        world_.camera->setEnvironmentRange(0.0f, static_cast<float>(WorldGetFarClip(&world_)));
}

void Environment::refreshFogRanges()
{
    if (!static_fog_) return;
    const auto far_clip = WorldGetFarClip(&world_);
    static_fog_->fog_start = dynamic_fog_->fog_start = far_clip * settings_.range_start;
    static_fog_->fog_end = dynamic_fog_->fog_end = far_clip * settings_.range_end;
}

void Environment::beginFade(float duration)
{
    if (duration == 0.0f) {
        lighting_mode_ = W8EnvironmentLightingMode::DayNight;
        transition_rate_ = 0.0f;
        fadeTo(1.0f);
        return;
    }
    transition_rate_ = 1.0f / duration;
    transition_tick_ = WorldClockMilliseconds();
    lighting_mode_ = W8EnvironmentLightingMode::Transition;
    if (duration < 0.0f) holdIntensity();
}

std::optional<float> Environment::advanceFade(float scale)
{
    const std::uint64_t now = WorldClockMilliseconds();
    const std::uint64_t elapsed = now - transition_tick_;
    if (elapsed == 0) return std::nullopt;
    transition_tick_ = now;

    scale += elapsed * transition_rate_;
    if (1.0f < scale) {
        scale = 1.0f;
    } else if (scale < 0.0f) {
        scale = 0.0f;
    }
    if (fadeTo(scale) == 0.0f) {
        lighting_mode_ = W8EnvironmentLightingMode::FadedOut;
        transition_rate_ = 0.0f;
    } else if (scale == 1.0f) {
        lighting_mode_ = W8EnvironmentLightingMode::DayNight;
        transition_rate_ = 0.0f;
    } else {
        lighting_mode_ = W8EnvironmentLightingMode::Transition;
    }
    return scale;
}

static const char* const g_sky_gradient_names[3] = {"SkyGrad0000.ifl", "Skytop0000.ifl",
                                                    "Horizon0000.ifl"};

/* The orbit is measured once per sky world from the authored distance between the Sun
   and the Moon; a sky kept across levels keeps it. */
void Environment::locateCelestialBodies()
{
    sun_ = FindPropByName(&world_, "Sun");
    moon_ = FindPropByName(&world_, "Moon");
    if (orbit_radius_ >= 0.0f) return;
    if (!sun_ || !moon_) {
        sun_ = nullptr;
        moon_ = nullptr;
        return;
    }
    const glm::vec3 sun_position = sun_->GetLocation();
    const glm::vec3 moon_position = moon_->GetLocation();
    orbit_radius_ = glm::length(sun_position - moon_position) * 0.5f;
    celestial_origin_ = (sun_position + moon_position) * 0.5f;
    for (int index = 0; index < 3; ++index) {
        std::shared_ptr<Texture> animation = g_texture_animations.find(g_sky_gradient_names[index]);
        gradients_[index] = animation;
        if (animation) animation->playback->animation_mode = W8_TEXTURE_ANIM_MANUAL;
    }
}

/* The day's celestial prop sits one orbit radius from the sky origin in the rotated
   direction and the other rests on the origin. The three animated gradients consume the
   same 8-bit day phase, so prop placement and gradient animation stay one update. */
void Environment::placeSky(unsigned int time_of_day)
{
    if (cloud_material_) {
        auto& material = *cloud_material_;
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
    glm::vec3 position = rotation * glm::vec3(0.0, orbit_radius_, 0.0) + celestial_origin_;

    Prop* moving = day ? sun_ : moon_;
    Prop* opposite = day ? moon_ : sun_;
    if (moving) moving->SetLocation(&position);
    if (opposite) opposite->SetLocation(&celestial_origin_);

    const auto phase = static_cast<int>(DayPhase(time_of_day));
    for (const auto& gradient : gradients_) {
        if (auto animation = gradient.lock()) animation->playback->frame = phase;
    }
}

// Both static scenes take the light direction as their fog colour.
static void ShowLightDirection(Level& level, glm::vec3 direction)
{
    level.primary->environment.setFogColour(direction);
    if (level.sky) level.sky->environment.setFogColour(direction);
}

static void ShowDayPhaseLight(Level& level, unsigned int time_of_day)
{
    auto& environment = level.primary->environment;
    if (environment.showLightPhase(time_of_day))
        ShowLightDirection(level, environment.lightDirection());
}

void Level::placeSky(unsigned int time_of_day)
{
    if (sky) sky->environment.placeSky(time_of_day);
}

void Level::enterEnvironment(unsigned int time_of_day)
{
    if (sky) sky->environment.locateCelestialBodies();
    primary->environment.showDayPhase(time_of_day);
    ShowLightDirection(*this, primary->environment.lightDirection());
}

void Level::showDayPhase(unsigned int time_of_day)
{
    ShowDayPhaseLight(*this, time_of_day);
    primary->environment.showColourPhase(time_of_day);
}

void Level::showDayNight(unsigned int time_of_day)
{
    auto& environment = primary->environment;
    if (environment.lightingMode() != W8EnvironmentLightingMode::DayNight) return;
    if (environment.skyEnabled()) ShowDayPhaseLight(*this, time_of_day);
    environment.showColourPhase(time_of_day);
}

bool Level::fading() const
{
    return primary->environment.fading();
}

void Level::enableSky(unsigned int time_of_day)
{
    auto& environment = primary->environment;
    if (primary->scene && !environment.fogEnabled()) {
        environment.enableFog();
        ShowLightDirection(*this, environment.lightDirection());
    }
    environment.setSkyEnabled(true);
    ShowDayPhaseLight(*this, time_of_day);
}

void Level::disableSky()
{
    if (!primary) return;
    primary->environment.disableFog();
    ShowLightDirection(*this, glm::vec3(0));
    primary->environment.setSkyEnabled(false);
}

void Level::showPartyLight(int light)
{
    if (!primary || !primary->camera_light) return;
    primary->camera_light->visible = light != 0;
    if (light != 0) primary->camera_light->intensity = light + kEnvironmentNearScale;
}

/* The primary world's transition drives both worlds; the shared light scale also
   brightens monsters, and a faded-out level stops drawing. */
static void ShowFadeState(W8EnvironmentLightingMode mode)
{
    const bool faded_out = mode == W8EnvironmentLightingMode::FadedOut;
    g_world_render_enabled = !faded_out;
    g_monster_shadow_updates_enabled = !faded_out;
    g_world_blacked_out = faded_out;
}

void Level::beginLightingFade(float duration)
{
    auto& environment = primary->environment;
    if (duration == 0.0f) {
        environment.beginFade(0.0f);
        ShowFadeState(environment.lightingMode());
        g_monster_light_scale = 1.0f;
        if (sky) sky->environment.fadeTo(1.0f);
        ShowLightDirection(*this, glm::clamp(environment.lightDirection(), 0.0f, 1.0f));
        return;
    }
    environment.beginFade(duration);
    if (duration < 0.0f && sky) sky->environment.holdIntensity();
}

void Level::advanceLightingFade()
{
    auto& environment = primary->environment;
    const auto scale = environment.advanceFade(g_monster_light_scale);
    if (!scale) return;
    g_monster_light_scale = *scale;
    if (sky) sky->environment.fadeTo(*scale);
    ShowLightDirection(*this, glm::clamp(environment.lightDirection() * *scale, 0.0f, 1.0f));
    ShowFadeState(environment.lightingMode());
}
