#include <cstring>

#include "surrender/srClock.h"
#include "surrender/srCore.h"
#include "surrender/srStatisticsManager.h"

// FUNCTION: SURRENDER 0x100148A0
void srStatisticsManager::reset()
{
    memset(&statistics, 0, sizeof(Statistics));
    statistics.elapsed_time = srCore.getTimer()->seconds();
}

// FUNCTION: SURRENDER 0x10014920
srStatisticsManager::Statistics srStatisticsManager::getStatistics() const
{
    const double now = srCore.getTimer()->seconds();
    Statistics result = statistics;
    result.elapsed_time = now - result.elapsed_time;
    return result;
}
