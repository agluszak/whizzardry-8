#pragma once

#include <chrono>
#include <cstdint>

/* Monotonic clock over the game's clock (w8_clock_us), counting time since
   construction (or reset()). Simulation-side pause, stepping and time scaling
   live in W8GameTimer, not here. */
class srClock {
public:
    srClock() : base_(w8_clock_us()) {}

    void reset()
    {
        base_ = w8_clock_us();
    }

    std::chrono::microseconds elapsed() const
    {
        return std::chrono::microseconds(w8_clock_us() - base_);
    }

    /* Elapsed seconds. */
    double seconds() const
    {
        return std::chrono::duration<double>(elapsed()).count();
    }

    /* Elapsed milliseconds, truncated to 32 bits like the retail reader. */
    w8_ulong milliseconds() const
    {
        return static_cast<w8_ulong>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed()).count());
    }

    /* Elapsed time in units of `units_per_second`, truncated to 32 bits. */
    w8_ulong ticks(w8_ulong units_per_second) const
    {
        const std::uint64_t micros = static_cast<std::uint64_t>(elapsed().count());
        return static_cast<w8_ulong>(micros * units_per_second / 1000000);
    }

private:
    std::uint64_t base_;
};
