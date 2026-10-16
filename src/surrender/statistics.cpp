#include <cstring>

#include "surrender/srStatisticsManager.h"
#include "wiz8/compat/native.h"

// FUNCTION: SURRENDER 0x100148A0
void srStatisticsManager::reset()
{
    memset(&statistics, 0, sizeof(Statistics));
    statistics.elapsed_time = static_cast<double>(w8_clock_us()) / 1'000'000;
}

// FUNCTION: SURRENDER 0x10014920
srStatisticsManager::Statistics srStatisticsManager::getStatistics() const
{
    const double now = static_cast<double>(w8_clock_us()) / 1'000'000;
    Statistics result = statistics;
    result.elapsed_time = now - result.elapsed_time;
    return result;
}
