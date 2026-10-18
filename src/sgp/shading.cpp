#include <array>
#include <cstdint>

#include <SDL3/SDL_pixels.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-11.
   Distributed under the accompanying SFI Source Code license agreement. */

#include "himage.h"
#include "shading.h"

std::array<std::uint16_t, 65536> ShadeTable{};

void SetShadeTablePercent(float percent)
{
    // Preserve the existing quantized 5-bit shade levels, sampled in steps of 4.
    for (unsigned red = 0; red < 256; red += 4)
        for (unsigned green = 0; green < 256; green += 4)
            for (unsigned blue = 0; blue < 256; blue += 4) {
                const std::uint16_t index = Get16BPPColor(
                    SDL_Color{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                              static_cast<std::uint8_t>(blue), SDL_ALPHA_OPAQUE});
                ShadeTable[index] = Get16BPPColor(
                    SDL_Color{static_cast<std::uint8_t>(red * percent),
                              static_cast<std::uint8_t>(green * percent),
                              static_cast<std::uint8_t>(blue * percent), SDL_ALPHA_OPAQUE});
            }
}
