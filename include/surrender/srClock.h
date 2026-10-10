#pragma once

#include <chrono>
#include <cstdint>

/* Monotonic wall clock over std::chrono::steady_clock, counting time since
   construction (or reset()). Simulation-side pause, stepping and time scaling
   live in W8GameTimer, not here. */
class srClock {
public:
    srClock() : base_(std::chrono::steady_clock::now()) {}

    void reset()
    {
        base_ = std::chrono::steady_clock::now();
    }

    std::chrono::steady_clock::duration elapsed() const
    {
        return std::chrono::steady_clock::now() - base_;
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
        const std::uint64_t micros = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed()).count());
        return static_cast<w8_ulong>(micros * units_per_second / 1000000);
    }

private:
    std::chrono::steady_clock::time_point base_;
};
