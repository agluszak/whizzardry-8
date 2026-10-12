#include <cstdint>
#include "wiz8/local_code/HealthStaminaMana.h"

// FUNCTION: WIZ8 0x0052A780
std::int32_t CalculateMonsterFatigueBand(std::int32_t current, std::int32_t maximum)
{
    std::int32_t percentage_lost = 100 - current * 100 / static_cast<std::uint32_t>(maximum);
    if (percentage_lost < 50)
        return 0;
    if (percentage_lost < 70)
        return 1;
    if (percentage_lost < 85)
        return 2;
    if (percentage_lost < 95)
        return 3;
    return 4;
}
