/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once



#include <cstdint>
/* Countdowns are stored as 64-bit w8_get_ticks deadline stamps. */
inline std::uint64_t TimeUntilDeadline(std::uint64_t deadline)
{
    const std::int64_t remaining = std::int64_t(deadline - w8_get_ticks());
    return remaining > 0 ? std::uint64_t(remaining) : 0;
}
