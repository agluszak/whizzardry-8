#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/render/vertex_channels.h"

class Texture;
class FogParams;
class Light;
class Material;
class Prop;
namespace wiz8 {
class File;
}
struct World;

bool ReadEnvironmentColours(wiz8::File* file, std::array<glm::vec3, 256>& colours);
// The default day-phase table: a grey ramp that peaks at midday.
std::array<glm::vec3, 256> EnvironmentColourRamp();

/* Whether day/night lighting is active, fading, or faded out. */
enum class W8EnvironmentLightingMode : unsigned char { FadedOut, Transition, DayNight };

// A level's authored lighting, as its .LVL file describes it.
struct EnvironmentSettings {
    // Day-phase tables: the ambient colour, and the light direction that is also the fog colour.
    std::array<glm::vec3, 256> colours = EnvironmentColourRamp();
    std::array<glm::vec3, 256> light_colours = EnvironmentColourRamp();
    float intensity = 0.0f;
    float view_distance = 0.0f;
    // The fog's extent as fractions of the far clip.
    float range_start = 0.75f, range_end = 1.0f;
    bool fog = false;
};

/* One world's lighting, fog, fade and sky. It is built for the world that owns it and
   changes only that world; `Level` coordinates the primary world with its sky, and
   `Game` supplies the calendar. What it borrows (lights, props, materials, gradients)
   belongs to the same world, so an unpublished world cannot change the installed one. */
class Environment {
public:
    explicit Environment(World& world);
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;

    // Take up the level's authored lighting: white at its intensity.
    void configure(const EnvironmentSettings& settings);
    void setOffset(glm::vec3 offset) { offset_ = offset; }
    glm::vec3 offset() const { return offset_; }
    float viewDistance() const { return settings_.view_distance; }
    // The level asks for fog and a day/night sky.
    bool fogConfigured() const { return settings_.fog; }
    // The sky's lights take the environment's ambient; they belong to this world.
    void addLight(Light* light) { lights_.push_back(light); }
    void removeLight(Light* light);
    // The sky's clouds brighten towards midday; the material belongs to this world.
    void setCloudMaterial(std::shared_ptr<Material> material);

    // Apply a colour and intensity to the world's static scene and environment lights.
    void applyColour(float intensity, glm::vec3 colour);
    /* Apply `value`, clamped to the unit range (NaN becomes zero), with the current
       colour, or black without a static scene. Returns the clamped intensity. */
    float setIntensity(float value);
    float intensity() const { return intensity_; }
    glm::vec3 colour() const { return colour_; }
    // The ambient light the world contributes, or black without a static scene.
    glm::vec3 ambient() const;

    /* Show a time of day's phase: its light direction or its colour, each only when the
       phase moved on since it last showed one. Answers whether the light direction moved. */
    bool showLightPhase(unsigned int time_of_day);
    void showColourPhase(unsigned int time_of_day);
    // Show a time of day's colour and light direction whatever was shown before.
    void showDayPhase(unsigned int time_of_day);
    glm::vec3 lightDirection() const { return light_direction_; }
    // The static scene takes the light direction as its fog colour.
    void setFogColour(glm::vec3 colour);

    // Create the fog and range it and the camera's environment by the far clip.
    void enableFog();
    void disableFog();
    bool fogEnabled() const { return static_fog_ != nullptr; }
    // Range the fog by the far clip; runs whenever the camera's clip range moves.
    void refreshFogRanges();
    // Whether the fog is on and the light direction follows the day.
    bool skyEnabled() const { return sky_enabled_; }
    void setSkyEnabled(bool enabled) { sky_enabled_ = enabled; }

    W8EnvironmentLightingMode lightingMode() const { return lighting_mode_; }
    bool fading() const { return transition_rate_ != 0.0f; }
    /* Arm the lighting transition. Duration is in milliseconds: negative fades out from
       the present intensity, positive fades back in over abs(duration); zero returns to
       day/night lighting at the held intensity. */
    void beginFade(float duration);
    // Hold the present intensity as the one fades scale.
    void holdIntensity() { base_intensity_ = intensity_; }
    // Apply `scale` times the held intensity; returns the intensity applied.
    float fadeTo(float scale) { return setIntensity(scale * base_intensity_); }
    /* Advance an armed transition to the world clock's present from `scale`, the light
       scale it last reached, and apply it. Answers the new scale, or nothing when no time
       has passed. A faded-out or fully restored world stops fading. */
    std::optional<float> advanceFade(float scale);

    // Step time reaches the world while the calendar runs; the calendar takes it up.
    void passTime(std::chrono::milliseconds elapsed) { untaken_time_ += elapsed; }
    std::chrono::milliseconds takeUntakenTime() { return std::exchange(untaken_time_, {}); }

    /* For a sky world: find its Sun and Moon and, the first time, measure their orbit
       and claim the animated sky gradients. */
    void locateCelestialBodies();
    /* For a sky world: place the Sun and Moon, show the gradients' day phase and
       brighten the clouds towards midday for a time of day. */
    void placeSky(unsigned int time_of_day);

private:
    World& world_;
    EnvironmentSettings settings_;
    std::vector<Light*> lights_;
    std::shared_ptr<Material> cloud_material_;
    std::unique_ptr<FogParams> static_fog_, dynamic_fog_;
    glm::vec3 offset_{0.0f};
    glm::vec3 colour_{0.0f};
    glm::vec3 light_direction_{1.0f};
    float intensity_ = 0.0f, base_intensity_ = 0.0f;
    int last_light_phase_ = -1, last_colour_phase_ = -1;
    bool sky_enabled_ = false;
    W8EnvironmentLightingMode lighting_mode_ = W8EnvironmentLightingMode::DayNight;
    float transition_rate_ = 0.0f;
    std::uint64_t transition_tick_ = 0;
    std::chrono::milliseconds untaken_time_{};
    // Sun and Moon are owned by this sky world's props collection.
    Prop* sun_ = nullptr;
    Prop* moon_ = nullptr;
    glm::vec3 celestial_origin_{0.0f};
    float orbit_radius_ = -1.0f;
    std::array<std::weak_ptr<Texture>, 3> gradients_;
};
