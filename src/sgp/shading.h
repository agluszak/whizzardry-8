#ifndef _SHADING_H_
#define _SHADING_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#ifdef __cplusplus
extern "C" {
#endif

void BuildShadeTable(void);
void SetShadeTablePercent(float uiShadePercent);

extern std::uint16_t IntensityTable[65536];
extern std::uint16_t ShadeTable[65536];
extern std::uint16_t White16BPPPalette[256];
extern float guiShadePercent;
extern float guiBrightPercent;

#ifdef __cplusplus
}
#endif

#endif
