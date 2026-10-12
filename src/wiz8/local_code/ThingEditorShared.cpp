

#include <cstdint>
#include "wiz8/local_code/ThingEditorShared.h"

#include <cmath>

/* Scale total surviving hostile experience to the level used by the combat
   difficulty evaluator. The signed byte clamp is visible in retail. */
// FUNCTION: WIZ8 0x00556000
std::uint8_t EstimateCombatThreatLevel(std::uint32_t experience)
{
    std::int8_t level =
        static_cast<std::int8_t>((pow(static_cast<double>(experience), 0.29f) - 3.5) * 0.5);
    if (level < 0) {
        return 0;
    }
    if (level > 50) {
        return 50;
    }
    return level;
}
