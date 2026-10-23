#include <cstdint>

#include "wiz8/platform/timer.h"
#include "wiz8/render/frame.h"

class Prop;
class Texture;

static uint64_t previous_render_tick;
static double render_clock_seconds;

/* The render clock advances once per rendered frame; every scene pass reads
   the same value rather than maintaining a parallel frame timer. */
void UpdateRenderElapsedTime(void)
{
    uint64_t now = MonotonicMilliseconds();
    uint64_t elapsed = previous_render_tick ? now - previous_render_tick : 0;
    previous_render_tick = now;
    render_clock_seconds += elapsed * .001;
}

double RenderClockSeconds()
{
    return render_clock_seconds;
}
