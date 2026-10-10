#pragma once

#include "surrender/srColorSurface.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <memory>

namespace srImage {
using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

srColorSurface* loadTga(SDL_IOStream* source);

inline bool copyRows(const SDL_Surface& source, srColorSurfaceIFace& destination,
                     unsigned bytes_per_pixel, bool mirror = false)
{
    const size_t row_bytes = size_t(source.w) * bytes_per_pixel;
    if (source.w <= 0 || source.h <= 0 || !source.pixels || !destination.getDataPtr() ||
        destination.getWidth() != source.w || destination.getHeight() != source.h ||
        source.pitch < 0 || size_t(source.pitch) < row_bytes ||
        size_t(destination.getPitch()) < row_bytes)
        return false;
    for (int y = 0; y < source.h; ++y) {
        const auto* src = static_cast<const unsigned char*>(source.pixels) + size_t(y) * source.pitch;
        auto* dst = static_cast<unsigned char*>(destination.getDataPtr()) + size_t(y) * destination.getPitch();
        if (!mirror) {
            SDL_memcpy(dst, src, row_bytes);
        } else {
            for (int x = 0; x < source.w; ++x)
                SDL_memcpy(dst + size_t(x) * bytes_per_pixel,
                           src + size_t(source.w - 1 - x) * bytes_per_pixel, bytes_per_pixel);
        }
    }
    return true;
}
} // namespace srImage
