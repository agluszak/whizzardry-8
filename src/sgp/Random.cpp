/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include "random.h"

namespace
{
// Retail used MSVC rand(), whose RAND_MAX is 0x7fff. Host rand() ranges are
// wider (glibc: 2^31 - 1), which overflowed the 32-bit scaling and confined
// Random() to 0..2, so reduce every roll to the retail 15-bit range.
constexpr std::uint64_t kRetailRandMax = 0x7fff;

std::uint32_t ScaleRoll(std::int32_t roll, std::uint32_t range)
{
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(roll) & kRetailRandMax) * range /
                                      kRetailRandMax % range);
}
} // namespace

#ifdef PRERANDOM_GENERATOR

std::uint32_t guiPreRandomIndex = 0;
std::uint32_t guiPreRandomNums[MAX_PREGENERATED_NUMS];

#endif

// FUNCTION: WIZ8 0x0040ef80
void InitializeRandom()
{
    // Seed the random-number generator with current time so that
    // the numbers will be different every time we run.
    srand((std::uint32_t)time(nullptr));
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
    return ScaleRoll(rand(), uiRange);
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
    uiNum = ScaleRoll(static_cast<std::int32_t>(guiPreRandomNums[guiPreRandomIndex]), uiRange);
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
