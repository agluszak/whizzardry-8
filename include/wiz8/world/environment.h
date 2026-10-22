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
class Game;
struct Level;
struct World;

bool ReadEnvironmentColours(wiz8::File* file, std::array<glm::vec3, 256>& colours);
// The default day-phase table: a grey ramp that peaks at midday.
std::array<glm::vec3, 256> EnvironmentColourRamp();

/* The game's calendar drives the environment of its level: the primary world's day/night
   lighting and the sky world's Sun, Moon, gradients and clouds. */

/* Advance the game's calendar by `elapsed` milliseconds (0x00482A20), run the game clock
   while the party is in the world, and place the sky for the new time of day. */
void AdvanceEnvironmentTime(Game& game, int elapsed);
/* Turn the environment clock on or off; enabling drops the time it has not taken up. */
void SetEnvironmentTimeEnabled(Game& game, bool enabled);
/* One step's elapsed time reaches the environment clock while it runs; the clock takes it
   up as it next advances. */
void PassEnvironmentTime(Game& game, std::chrono::milliseconds elapsed);
void SetGameTimeMilliseconds(Game& game, int value);
/* Catch the clock up and publish the day phase's light direction if it moved on. */
void UpdateEnvironmentLight(Game& game);
/* Catch the clock up and apply the day phase's colour if it moved on. */
void RefreshEnvironment(Game& game);
/* The per-frame environment update: a running lighting transition, otherwise the clock
   and, in day/night mode, the day phase's light and colour. */
void UpdateEnvironment(Game& game);
/* Level-entry setup: find the sky's Sun and Moon, place the sky, and apply the current
   day phase's colour and light direction. */
void InitializeLevelEnvironment(Game& game);
// Create the level's fog and catch its light direction up.
void EnableSky(Game& game);
// Drop the level's fog and clear the fog colour.
void DisableSky(Level& level);
/* Arm the level's lighting transition. Duration is in milliseconds: negative fades the
   worlds out, positive fades them back in over abs(duration); zero applies the current
   base intensity immediately and returns to day/night mode. */
void BeginWorldLightingFade(Level& level, float duration);

/* Apply a colour and intensity to the world's static scene, its environment lights and
   its environment state. */
void ApplyEnvironmentColour(World& world, float intensity, glm::vec3 colour);
/* Set the world's environment intensity, clamped to the unit range, keeping its current
   colour. Returns the clamped intensity. */
float SetWorldEnvironmentIntensity(World& world, float value);
/* The ambient light the world contributes, or black without a static scene. */
glm::vec3 WorldLightValue(const World& world);
void RefreshFogRanges(World& world);
// Show or hide the world's camera light, if it has one (0x00483E50).
void SetCameraLightVisible(World& world, bool visible);
void SetCameraLightIntensity(World& world, float value);

/* Whether day/night lighting is active, fading, or faded out. */
enum class W8EnvironmentLightingMode : unsigned char { FadedOut, Transition, DayNight };
/* The game clock's multiplier, not a distance: every environment clock advance
   scales the elapsed milliseconds by it (12.0 normally, 2880.0 while the party
   rests) and the environment update is skipped while it holds another value.
   Sight.cpp and GameplayTime.cpp consume it through SetViewDistance and
   GetViewDistance, so the rename waits for those owners. */
extern float g_view_distance;
void SetViewDistance(float distance);
float GetViewDistance(void);

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
