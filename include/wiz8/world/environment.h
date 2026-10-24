#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
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

/* ABS 0x0065AD78: the three light-direction words as one colour triple.
   Produced from the day-phase colour table and consumed as fog-vector
   floats; the word copies below move it without reinterpreting it. */
glm::vec3 EnvironmentLightDirection();

class Light;

bool ReadEnvironmentColours(wiz8::File* file, std::array<glm::vec3, 256>& colours);
// The default day-phase table: a grey ramp that peaks at midday.
std::array<glm::vec3, 256> EnvironmentColourRamp();
void UpdateEnvironmentLight(void);
void RefreshEnvironment(void);
void SetWorldEnvironmentColour(glm::vec3 colour);

void SetSkyEnabled(bool enabled);
void EnableSky(void);
void DisableSky(void);
/* The ambient light the world contributes, or black without a static scene. */
void GetWorldLightValue(const World* world, glm::vec3* pLightValue);
void SetLightDirection(const glm::vec3* direction);
bool IsSkyEnabled();
void ResetEnvironment(void);
void InitializeLevelEnvironment(void);
/* Reset sky presentation and the current world's fog nodes. */
/* Apply one day-phase colour and intensity to the world, its environment
   lights, and the animated cloud material. */
void ApplyEnvironmentColour(World* world, float intensity, const glm::vec3* colour);

/* Turn the environment clock on or off; enabling drops the time it has not taken up. */
/* One step's elapsed time reaches the environment clock while it runs; the clock takes it
   up as it next advances. */
float GetViewDistance(void);
void SetGameTimeMilliseconds(int value);
/* The per-frame environment update, and the alternate lighting transition it
   hands off to when the bypass value is set. */
void UpdateEnvironment(void);
void UpdateEnvironmentLighting(void);
/* Show or hide the world's camera light: 0x00483E50 loads GetWorld()->camera_light
   and updates its visibility.
   Nothing here is a sky node; the exported name is still the placeholder and one
   caller lives in a TU owned elsewhere, so the rename waits for it. */
/* Camera light actions used by SetCameraLightMode. */
enum class W8CameraLightMode : unsigned char { Increase, Decrease, Disable, Enable };
void SetCameraLightMode(W8CameraLightMode mode);
void RefreshFogRanges(World& world);

void SetViewDistance(float distance);
bool InitializeEnvironmentColours(void);

/* Whether day/night lighting is active, fading, or faded out. */
enum class W8EnvironmentLightingMode : unsigned char { FadedOut, Transition, DayNight };
W8EnvironmentLightingMode EnvironmentLightingMode();
/* The game clock's multiplier, not a distance: every environment clock advance
   scales the elapsed milliseconds by it (12.0 normally, 2880.0 while the party
   rests) and the environment update is skipped while it holds another value.
   Sight.cpp and GameplayTime.cpp consume it through SetViewDistance and
   GetViewDistance, so the rename waits for those owners. */
extern float g_view_distance;
void AdvanceEnvironmentTime(int elapsed); /* 0x00482A20 */
void SetWorldEnvironmentIntensity(World* world, float value);
/* Arm the lighting transition. Duration is in milliseconds: negative fades the
   world out, positive fades it back in over abs(duration); zero applies the
   current base intensity immediately and returns to day/night mode. */
void BeginWorldLightingFade(float duration);

// Authored level lighting and its live sky/fade state. References borrow only
// from this world's resources, so an unpublished world cannot change the sky.
struct EnvironmentState {
    EnvironmentState();
    ~EnvironmentState();
    EnvironmentState(const EnvironmentState&) = delete;
    EnvironmentState& operator=(const EnvironmentState&) = delete;
    std::array<glm::vec3, 256> colours = EnvironmentColourRamp();
    std::array<glm::vec3, 256> light_colours = EnvironmentColourRamp();
    std::vector<Light*> lights;
    std::shared_ptr<Material> cloud_material;
    std::unique_ptr<FogParams> static_fog, dynamic_fog;
    glm::vec3 offset{0.0f};
    glm::vec3 colour{0.0f};
    glm::vec3 light_direction{1.0f};
    float range_start = 0.75f, range_end = 1.0f;
    float view_distance = 0.0f, intensity = 0.0f, base_intensity = 0.0f;
    int last_light_phase = -1, last_colour_phase = -1;
    bool fog_enabled = false, sky_enabled = false;
    W8EnvironmentLightingMode lighting_mode = W8EnvironmentLightingMode::DayNight;
    float transition_rate = 0.0f;
    std::uint64_t transition_tick = 0;
    // Step time that passed in the world and the day clock has not taken up yet.
    std::chrono::milliseconds untaken_time{};
    // Sun and Moon are owned by this sky world's props collection.
    Prop* sun = nullptr;
    Prop* moon = nullptr;
    glm::vec3 celestial_origin{0.0f};
    float orbit_radius = -1.0f;
    std::array<std::weak_ptr<Texture>, 3> gradients;
};

// Publish the environment colour to the primary and sky scenes.
void PublishLightDirection(const glm::vec3* direction);
