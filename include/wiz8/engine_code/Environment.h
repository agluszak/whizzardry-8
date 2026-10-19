#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/vector_float3.hpp"
#include "surrender/srVertexProcessor.h"

class stTextureAnim;
class W8Prop;
namespace wiz8 {
class File;
}
struct W8World;

/* Environment.cpp. The vertex processor that scrolls the first texture
   coordinate set of the sky's AnimatedCloudMaterial by a fixed per-frame
   rate. No authored name survives, so the constructor address names the
   class; the global instance 0x00659738 is defined by the level loader. */
class W8MaterialMapper : public srVertexProcessor {
public:
    W8MaterialMapper();
    /* Retail ICF folds this onto W8NormalTexcoordMapper's deleting
       destructor at 0x004B8A50. */

    virtual int isActive(srVertexPipe& pipe) override;
    virtual void process(srVertexPipe& pipe) override;

    float scroll_rate_u;         /* 0x04 */
    float scroll_rate_v;         /* 0x08 */
    float scroll_u;              /* 0x14 */
    float scroll_v;              /* 0x18 */
};

/* ABS 0x0065AD78: the three light-direction words as one colour triple.
   Produced from the day-phase colour table and consumed as fog-vector
   floats; the word copies below move it without reinterpreting it. */
extern glm::vec3 g_light_direction;

class stLight;

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
void GetWorldLightValue(const W8World* world, glm::vec3* pLightValue);
void SetLightDirection(const glm::vec3* direction);
extern bool g_sky_enabled;
void ResetEnvironment(void);
void InitializeLevelEnvironment(void);
/* Reset sky presentation and the current world's fog nodes. */
void ClearEnvironmentObjects(void);
/* Apply one day-phase colour and intensity to the world, its environment
   lights, and the animated cloud material. */
void ApplyEnvironmentColour(W8World* world, float intensity, const glm::vec3* colour);

/* Turn the environment clock on or off; enabling resets its tick baseline. */
void SetEnvironmentTimeEnabled(bool enabled);
float GetViewDistance(void);
void SetGameTimeMilliseconds(int value);
/* The per-frame environment update, and the alternate lighting transition it
   hands off to when the bypass value is set. */
void UpdateEnvironment(void);
void UpdateEnvironmentLighting(void);
/* Show or hide the world's camera light: 0x00483E50 loads GetWorld()->camera_light
   (offset 0x54) and clears srNode::FLAG_DISABLE when visible, sets it when not.
   Nothing here is a sky node; the exported name is still the placeholder and one
   caller lives in a TU owned elsewhere, so the rename waits for it. */
void SetSkyNodeVisible(bool visible);
void SetCameraLightIntensity(float value);
/* Camera light actions used by SetCameraLightMode. */
enum class W8CameraLightMode : unsigned char { Increase, Decrease, Disable, Enable };
void SetCameraLightMode(W8CameraLightMode mode);
void RefreshFogRanges(W8World& world);

void SetViewDistance(float distance);
unsigned char InitializeEnvironmentColours(void);
extern uint64_t g_frame_tick;
extern float g_frame_elapsed;

/* Whether day/night lighting is active, fading, or faded out. */
enum class W8EnvironmentLightingMode : unsigned char { FadedOut, Transition, DayNight };
extern W8EnvironmentLightingMode g_environment_lighting_mode;
/* 1/duration while the lighting transition body at 0x00484300 runs, zero when
   idle: UpdateEnvironment hands off to that body while it is not zero. */
extern float g_environment_transition_rate;
extern std::uint64_t g_environment_transition_tick;
/* Gates the per-frame world-colour refresh. Retail writes it nowhere and
   initialises it to 1, so that refresh always runs; the only reference is the
   read inside UpdateEnvironment. */
extern bool g_environment_colour_refresh;
/* The game clock's multiplier, not a distance: every environment clock advance
   scales the elapsed milliseconds by it (12.0 normally, 2880.0 while the party
   rests) and the environment update is skipped while it holds another value.
   Sight.cpp and GameplayTime.cpp consume it through SetViewDistance and
   GetViewDistance, so the rename waits for those owners. */
extern float g_view_distance;
extern bool g_environment_time_enabled;
/* Half the Sun-to-Moon distance, which is the radius of the circle the active
   celestial prop travels; -1 until InitializeLevelEnvironment measures it. */
extern float g_celestial_orbit_radius;
/* The level's "Moon" prop. */
extern W8Prop* g_moon_prop;
/* The level's "Sun" prop. */
extern W8Prop* g_sun_prop;
/* The three day-phase sky gradients the level setup registers:
   SkyGrad0000.ifl, Skytop0000.ifl and Horizon0000.ifl. The registration loop
   and the per-frame animation both walk one contiguous base, so they are one
   array rather than three globals. */
extern std::weak_ptr<stTextureAnim> g_sky_gradient_animations[3];
/* Midpoint of the Sun and Moon, which the active prop orbits and the inactive
   one rests at. */
extern glm::vec3 g_celestial_origin;

void AdvanceEnvironmentTime(int elapsed); /* 0x00482A20 */
void SetWorldEnvironmentIntensity(W8World* world, float value);
/* Arm the lighting transition. Duration is in milliseconds: negative fades the
   world out, positive fades it back in over abs(duration); zero applies the
   current base intensity immediately and returns to day/night mode. */
void BeginWorldLightingFade(float duration);
