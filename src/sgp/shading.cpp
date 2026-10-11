#include <SDL3/SDL_pixels.h>
#include <cstdint>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include "himage.h"
#include "shading.h"

// GLOBAL: WIZ8 0x0069da80
std::uint16_t IntensityTable[65536];
// GLOBAL: WIZ8 0x006c0da0
std::uint16_t ShadeTable[65536];
// GLOBAL: WIZ8 0x006000ac
float guiShadePercent = (float)0.48;

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
    std::uint16_t red, green, blue;
    std::uint16_t index;

    for (red = 0; red < 256; red += 4)
        for (green = 0; green < 256; green += 4)
            for (blue = 0; blue < 256; blue += 4) {
                index = Get16BPPColor(SDL_Color{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                              static_cast<std::uint8_t>(blue), SDL_ALPHA_OPAQUE});
                ShadeTable[index] = Get16BPPColor(SDL_Color{
                    static_cast<std::uint8_t>(red * guiShadePercent),
                    static_cast<std::uint8_t>(green * guiShadePercent),
                    static_cast<std::uint8_t>(blue * guiShadePercent), SDL_ALPHA_OPAQUE});
            }

}

// FUNCTION: WIZ8 0x00413e80
void SetShadeTablePercent(float uiShadePercent)
{
    guiShadePercent = uiShadePercent;
    BuildShadeTable();
}
