/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include "random.h"

#ifdef PRERANDOM_GENERATOR

std::uint32_t guiPreRandomIndex = 0;
std::uint32_t guiPreRandomNums[MAX_PREGENERATED_NUMS];

#endif

// FUNCTION: WIZ8 0x0040ef80
void InitializeRandom()
{
    // Seed the random-number generator with current time so that
    // the numbers will be different every time we run.
    srand((unsigned)time(nullptr));
#ifdef PRERANDOM_GENERATOR
    //Pregenerate all of the random numbers.
    for (guiPreRandomIndex = 0; guiPreRandomIndex < MAX_PREGENERATED_NUMS; guiPreRandomIndex++) {
        guiPreRandomNums[guiPreRandomIndex] = rand();
    }
    guiPreRandomIndex = 0;
#endif
}

// Returns a pseudo-random integer between 0 and uiRange
// FUNCTION: WIZ8 0x0040efa0
std::uint32_t Random(std::uint32_t uiRange)
{
    // Always return 0, if no range given (it's not an error)

    if (uiRange == 0)
        return (0);
    return rand() * uiRange / RAND_MAX % uiRange;
}

// FUNCTION: WIZ8 0x0040efe0
bool Chance(std::uint32_t uiChance)
{
    return (bool)(Random(100) < uiChance);
}

#ifdef PRERANDOM_GENERATOR

std::uint32_t PreRandom(std::uint32_t uiRange)
{
    std::uint32_t uiNum;
    if (!uiRange)
        return 0;
    //Extract the current pregenerated number
    uiNum = guiPreRandomNums[guiPreRandomIndex] * uiRange / RAND_MAX % uiRange;
    //Replace the current pregenerated number with a new one.

    //This was removed in the name of optimization.  Uncomment if you hate recycling.
    //guiPreRandomNums[ guiPreRandomIndex ] = rand();

    //Go to the next index.
    guiPreRandomIndex++;
    if (guiPreRandomIndex >= (std::uint32_t)MAX_PREGENERATED_NUMS)
        guiPreRandomIndex = 0;
    return uiNum;
}

bool PreChance(std::uint32_t uiChance)
{
    return (bool)(PreRandom(100) < uiChance);
}

#endif
