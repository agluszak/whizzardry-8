/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "random.h"

#include <cstdint>
#include <cstdlib>
#include <ctime>

namespace {
// Retail used MSVC rand(), whose RAND_MAX is 0x7fff. Host rand() ranges are
// wider (glibc: 2^31 - 1), which overflowed the 32-bit scaling and confined
// Random() to 0..2, so reduce every roll to the retail 15-bit range.
constexpr std::uint64_t kRetailRandMax = 0x7fff;

std::uint32_t ScaleRoll(int roll, std::uint32_t range)
{
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(roll) & kRetailRandMax) * range /
                                      kRetailRandMax % range);
}
} // namespace

// FUNCTION: WIZ8 0x0040ef80
void InitializeRandom()
{
    // Seed the random-number generator with current time so that
    // the numbers will be different every time we run.
    srand((unsigned)time(nullptr));
}

// Returns a pseudo-random integer between 0 and uiRange
// FUNCTION: WIZ8 0x0040efa0
std::uint32_t Random(std::uint32_t uiRange)
{
    // Always return 0, if no range given (it's not an error)

    if (uiRange == 0) {
        return (0);
    }
    return ScaleRoll(rand(), uiRange);
}

// FUNCTION: WIZ8 0x0040efe0
bool Chance(std::uint32_t uiChance)
{
    return (bool)(Random(100) < uiChance);
}
