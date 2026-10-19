/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include <cstdint>

void InitializeRandom();
// MSVC-compatible 32-bit state and 15-bit output, shared by all game randomness.
void SeedRandom(std::uint32_t seed);
std::uint32_t Random15();
// Returns [0, range); an empty range returns zero without consuming a draw.
std::uint32_t Random(std::uint32_t range);
// Still consumes a draw when chance >= 100, preserving the shared sequence.
bool Chance(std::uint32_t chance);
