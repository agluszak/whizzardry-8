#include <cstdint>

#include "wiz8/local_code/Configuration.h"
#include "wiz8/ui_input.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

int g_ui_help_delay;
uint64_t g_ui_help_clock;

bool InitializeUiHelp(void)
{
    g_ui_help_delay = g_settings.tooltip_delay_ms;
    g_ui.ClearPointer();
    g_ui_help_forced = false;
    return true;
}
