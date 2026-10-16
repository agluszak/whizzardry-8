#pragma once

#include <cstdint>

#include <SDL3/SDL_surface.h>

inline std::uint32_t ReadARGB(SDL_Surface& surface, int x, int y)
{
    Uint8 red, green, blue, alpha;
    SDL_ReadSurfacePixel(&surface, x, y, &red, &green, &blue, &alpha);
    return (std::uint32_t(alpha) << 24) | (std::uint32_t(red) << 16) | (std::uint32_t(green) << 8) |
           blue;
}
inline void WriteARGB(SDL_Surface& surface, int x, int y, std::uint32_t color)
{
    SDL_WriteSurfacePixel(&surface, x, y, (color >> 16) & 255, (color >> 8) & 255, color & 255,
                          color >> 24);
}
