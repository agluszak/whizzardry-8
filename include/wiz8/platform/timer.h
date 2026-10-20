/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include <cstdint>

// The game's one monotonic clock; runtime tests can substitute a virtual one.
std::uint64_t MonotonicMicroseconds();
// Milliseconds on the same clock, 64-bit so deadlines never wrap.
std::uint64_t MonotonicMilliseconds();

/* Countdowns are stored as 64-bit MonotonicMilliseconds deadline stamps. */
inline std::uint64_t TimeUntilDeadline(std::uint64_t deadline)
{
    const auto remaining = static_cast<std::int64_t>(deadline - MonotonicMilliseconds());
    return remaining > 0 ? static_cast<std::uint64_t>(remaining) : 0;
}
