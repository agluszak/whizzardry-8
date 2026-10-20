/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include <chrono>
#include <cstdint>
#include <utility>

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

/* Measures the time between laps on the monotonic clock: a screen that steps the game
   passes it the time since its previous step. */
class LapTimer {
public:
    // Begin the next lap now, dropping the time since the previous one.
    void restart() { last_ = MonotonicMilliseconds(); }
    // The time since the previous lap began; the next lap begins now.
    std::chrono::milliseconds lap()
    {
        const std::uint64_t now = MonotonicMilliseconds();
        return std::chrono::milliseconds(
            static_cast<std::chrono::milliseconds::rep>(now - std::exchange(last_, now)));
    }

private:
    std::uint64_t last_ = MonotonicMilliseconds();
};
