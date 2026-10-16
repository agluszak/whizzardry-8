/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include <cstdint>

void InitializeRandom();
std::uint32_t Random(std::uint32_t range);
// Chance(74) succeeds 74% of the time; values >= 100 always succeed.
bool Chance(std::uint32_t chance);
