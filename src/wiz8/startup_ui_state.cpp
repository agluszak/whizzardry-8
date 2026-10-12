#include <cstdint>
#include <string>

#include "wiz8/local_code/Configuration.h"
#include "wiz8/ui_input.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

// GLOBAL: WIZ8 0x00689b48
int g_ui_help_delay;
// GLOBAL: WIZ8 0x00689b38
uint64_t g_ui_help_clock;

// FUNCTION: WIZ8 0x004f11d0
unsigned char InitializeUiHelp(void)
{
    g_ui_help_delay = g_settings.tooltip_delay_ms;
    g_ui.ClearPointer();
    g_ui_help_forced = false;
    return 1;
}
