#include <cstdint>
#include <memory>

#include "compat/native.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/gpu_renderer.h"
#include "wiz8/world/environment.h"

class Prop;
class Texture;

static uint64_t previous_render_tick;

/* The renderer's shared millisecond delta. The constructor immediately before
   this body seeds the same clock; every consumer reads the single scaled
   elapsed value rather than maintaining a parallel frame timer. */
void UpdateRenderElapsedTime(void)
{
    uint64_t now = w8_get_ticks();
    uint64_t elapsed = previous_render_tick ? now - previous_render_tick : 0;
    previous_render_tick = now;
    if (g_gerd) g_gerd->frame_time += elapsed * .001;
}

float g_view_distance = 12.0f;

unsigned char InitializeEnvironmentColours(void)
{
    ResetEnvironment();
    g_view_distance = 12.0f;
    return 1;
}
