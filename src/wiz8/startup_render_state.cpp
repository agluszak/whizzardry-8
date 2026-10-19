#include <cstdint>
#include <memory>

#include "compat/native.h"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/engine_code/Environment.h"

class W8Prop;
class stTextureAnim;

uint64_t g_frame_tick;
float g_frame_elapsed;

/* The renderer's shared millisecond delta. The constructor immediately before
   this body seeds the same clock; every consumer reads the single scaled
   elapsed value rather than maintaining a parallel frame timer. */
void UpdateRenderElapsedTime(void)
{
    uint64_t now = w8_get_ticks();
    uint64_t elapsed = now - g_frame_tick;
    g_frame_tick = now;
    g_frame_elapsed = elapsed * 0.001f;
}

std::weak_ptr<stTextureAnim> g_sky_gradient_animations[3];
bool g_environment_time_enabled = true;
W8Prop* g_sun_prop;
W8Prop* g_moon_prop;
glm::vec3 g_celestial_origin;
float g_view_distance = 12.0f;
float g_celestial_orbit_radius = -1.0f;
glm::vec3 g_light_direction{1.0f};

unsigned char InitializeEnvironmentColours(void)
{
    g_sky_gradient_animations[0].reset();
    g_sky_gradient_animations[1].reset();
    g_environment_time_enabled = false;
    g_sun_prop = 0;
    g_moon_prop = 0;
    g_view_distance = 12.0f;
    g_sky_gradient_animations[2].reset();
    return 1;
}
