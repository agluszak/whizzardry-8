/* Modified for the Wizardry 8 reconstruction: 2026-10-06, 2026-10-10.
   Distributed under the accompanying SFI Source Code license agreement. */
#pragma once

#include "Types.h"

/* Countdowns are stored as UINT32 w8_get_ticks deadline stamps. Signed
   subtraction keeps TimeUntilDeadline correct across the 32-bit tick
   wraparound. */
inline UINT32 TimeUntilDeadline(UINT32 deadline)
{
    const INT32 remaining = INT32(deadline - w8_get_ticks());
    return remaining > 0 ? UINT32(remaining) : 0;
}
