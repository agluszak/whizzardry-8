#include <cstdint>
#include <string>

#include "wiz8/local_code/Configuration.h"
#include "wiz8/regions.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// GLOBAL: WIZ8 0x00689b48
std::int32_t g_region_help_delay;
// GLOBAL: WIZ8 0x00689b38
std::uint64_t g_region_help_clock;

// FUNCTION: WIZ8 0x004f11d0
bool InitializeRegionHelpState(void)
{
    g_region_help_delay = g_settings.tooltip_delay_ms;
    g_current_region_index = 0;
    g_captured_region_index = 0;
    g_hover_region_index = 0;
    g_region_help_force_enabled = false;
    g_default_help_text.clear();
    return true;
}
