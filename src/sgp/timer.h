/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include "Types.h"

/* Countdowns are stored as 64-bit w8_get_ticks deadline stamps. */
inline uint64_t TimeUntilDeadline(uint64_t deadline)
{
    const int64_t remaining = int64_t(deadline - w8_get_ticks());
    return remaining > 0 ? uint64_t(remaining) : 0;
}
