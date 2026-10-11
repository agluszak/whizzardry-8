#include <SDL3/SDL_pixels.h>
#include <array>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-11.
   Distributed under the accompanying SFI Source Code license agreement. */

#include "Types.h"
#include "himage.h"
#include "shading.h"

// GLOBAL: WIZ8 0x006c0da0
std::array<UINT16, 65536> ShadeTable{};

// FUNCTION: WIZ8 0x00413d60
// FUNCTION: WIZ8 0x00413e80
void SetShadeTablePercent(FLOAT percent)
{
    // Steps of 4 reach every 5- and 6-bit channel level of the packed formats.
    for (unsigned red = 0; red < 256; red += 4)
        for (unsigned green = 0; green < 256; green += 4)
            for (unsigned blue = 0; blue < 256; blue += 4) {
                const UINT16 index = Get16BPPColor(SDL_Color{static_cast<UINT8>(red),
                    static_cast<UINT8>(green), static_cast<UINT8>(blue), SDL_ALPHA_OPAQUE});
                ShadeTable[index] = Get16BPPColor(SDL_Color{static_cast<UINT8>(red * percent),
                    static_cast<UINT8>(green * percent), static_cast<UINT8>(blue * percent),
                    SDL_ALPHA_OPAQUE});
            }
}
