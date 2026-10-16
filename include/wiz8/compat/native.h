#pragma once

#include <cstdint>

// The game's one monotonic clock; runtime tests can substitute a virtual one.
std::uint64_t w8_clock_us();
// Milliseconds on the same clock, 64-bit so deadlines never wrap.
std::uint64_t w8_get_ticks();
