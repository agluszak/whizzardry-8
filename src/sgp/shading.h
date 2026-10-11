#ifndef _SHADING_H_
#define _SHADING_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-11.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <array>

#include "Types.h"

// Maps every packed g_sgp_pixel_format pixel to its darkened pixel.
extern std::array<UINT16, 65536> ShadeTable;

// Rebuilds ShadeTable for the current pixel format; call after it is selected.
void SetShadeTablePercent(FLOAT percent);

#endif
