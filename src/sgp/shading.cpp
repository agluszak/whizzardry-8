#include <SDL3/SDL_pixels.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include "Types.h"
#include "himage.h"
#include "shading.h"

// GLOBAL: WIZ8 0x0069da80
UINT16 IntensityTable[65536];
// GLOBAL: WIZ8 0x006c0da0
UINT16 ShadeTable[65536];
// GLOBAL: WIZ8 0x006000ac
FLOAT guiShadePercent = (FLOAT)0.48;

/**********************************************************************************************
 BuildShadeTable

	Builds a 16-bit color shading table. This function should be called only after the current
	video adapter's pixel format is known (IE: GetRgbDistribution() has been called, and the
	globals for masks and shifts have been initialized by that function), and before any
	blitting is done.

	Using the table is a straight lookup. The pixel to be shaded down is used as the index into
	the table and the entry at that point will be a pixel that is 25% darker.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00413d60
void BuildShadeTable(void)
{
    UINT16 red, green, blue;
    UINT16 index;

    for (red = 0; red < 256; red += 4)
        for (green = 0; green < 256; green += 4)
            for (blue = 0; blue < 256; blue += 4) {
                index = Get16BPPColor(SDL_Color{static_cast<UINT8>(red), static_cast<UINT8>(green),
                                              static_cast<UINT8>(blue), SDL_ALPHA_OPAQUE});
                ShadeTable[index] = Get16BPPColor(SDL_Color{
                    static_cast<UINT8>(red * guiShadePercent),
                    static_cast<UINT8>(green * guiShadePercent),
                    static_cast<UINT8>(blue * guiShadePercent), SDL_ALPHA_OPAQUE});
            }

}

// FUNCTION: WIZ8 0x00413e80
void SetShadeTablePercent(FLOAT uiShadePercent)
{
    guiShadePercent = uiShadePercent;
    BuildShadeTable();
}
