#pragma once

#include "surrender/srColorSurface.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>

namespace srImage {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

std::unique_ptr<srColorSurface> loadTga(SDL_IOStream* source);

bool copyRows(const SDL_Surface& source, srColorSurfaceIFace& destination,
                     unsigned bytes_per_pixel, bool mirror = false);
} // namespace srImage
