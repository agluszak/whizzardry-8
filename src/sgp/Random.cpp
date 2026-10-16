/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "random.h"

#include <cstdint>
#include <ctime>

namespace {
std::uint32_t state = 1;
} // namespace

// FUNCTION: WIZ8 0x0040ef80
void InitializeRandom()
{
    // Seed the random-number generator with current time so that
    // the numbers will be different every time we run.
    SeedRandom(static_cast<std::uint32_t>(std::time(nullptr)));
}

void SeedRandom(std::uint32_t seed)
{
    state = seed;
}

std::uint32_t Random15()
{
    state = state * 214013u + 2531011u;
    return (state >> 16) & 0x7fff;
}

// Returns a pseudo-random integer between 0 and uiRange
// FUNCTION: WIZ8 0x0040efa0
std::uint32_t Random(std::uint32_t uiRange)
{
    if (uiRange == 0) return 0;
    return static_cast<std::uint32_t>(std::uint64_t{Random15()} * uiRange / 0x7fff % uiRange);
}

// FUNCTION: WIZ8 0x0040efe0
bool Chance(std::uint32_t uiChance)
{
    return Random(100) < uiChance;
}
