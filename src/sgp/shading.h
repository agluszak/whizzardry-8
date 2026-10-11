#ifndef _SHADING_H_
#define _SHADING_H_

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include "Types.h"
#ifdef __cplusplus
extern "C" {
#endif

void BuildShadeTable(void);
void SetShadeTablePercent(FLOAT uiShadePercent);

extern UINT16 IntensityTable[65536];
extern UINT16 ShadeTable[65536];
extern FLOAT guiShadePercent;

#ifdef __cplusplus
}
#endif

#endif
