#include <fmt/format.h>
#include <string>

#include "wiz8/version.h"

/* Version-banner formatting. Live query: 0x004E3620 is a gap between
   Local Code\Gameloop.cpp (upper 0x004E34B0) and Local Code\MonsterManager.cpp
   (lower 0x004E3820). */

// FUNCTION: WIZ8 0x004E3620
std::string FormatVersionBanner(bool with_title, bool with_build, bool with_date)
{
    std::string text = with_title ? "Wizardry 8 " : "";
    text += fmt::format("v{:d}.{:d}.{:d}", 1, 2, 4);
    if (with_build) {
        text += fmt::format(" (build {:d})", 0xdb);
    }
    if (with_date) {
        text += " 2001/12/24 15:36";
    }
    return text;
}
