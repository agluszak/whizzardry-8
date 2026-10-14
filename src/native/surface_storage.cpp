#include "wiz8/surface_storage.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

namespace {
void require_sdl(bool result)
{
    if (!result) throw std::runtime_error(SDL_GetError());
}
} // namespace

void ConvertCpuPixels(void* destination, const void* source, size_t count,
                      SDL_PixelFormat source_format, SDL_PixelFormat destination_format)
{
    if (count == 0) return;
    const unsigned source_bytes = SDL_BYTESPERPIXEL(source_format);
    const unsigned destination_bytes = SDL_BYTESPERPIXEL(destination_format);
    if (source_bytes == 0 || source_bytes > 4 || destination_bytes == 0 || destination_bytes > 4 ||
        SDL_ISPIXELFORMAT_FOURCC(source_format) || SDL_ISPIXELFORMAT_FOURCC(destination_format))
        throw std::runtime_error("Unsupported SDL pixel conversion format");
    if (count > size_t(std::numeric_limits<int>::max() / 4))
        throw std::length_error("Pixel conversion row is too large");
    const int source_pitch = int(count) * int(source_bytes);
    const int destination_pitch = int(count) * int(destination_bytes);
    std::vector<unsigned char> snapshot;
    const auto source_address = reinterpret_cast<uintptr_t>(source);
    const auto destination_address = reinterpret_cast<uintptr_t>(destination);
    if (destination_address < source_address + source_pitch &&
        source_address < destination_address + destination_pitch) {
        const auto* bytes = static_cast<const unsigned char*>(source);
        snapshot.assign(bytes, bytes + source_pitch);
        source = snapshot.data();
    }
    require_sdl(SDL_ConvertPixels(int(count), 1, source_format, source, source_pitch,
                                  destination_format, destination, destination_pitch));
}

SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format, void* pixels,
                                 int pitch)
{
    SDLSurfaceOwner result{pitch >= 0 ? SDL_CreateSurfaceFrom(width, height, format, pixels, pitch)
                                      : SDL_CreateSurface(width, height, format),
                           SDL_DestroySurface};
    require_sdl(bool(result));
    require_sdl(SDL_SetSurfaceBlendMode(result.get(), SDL_BLENDMODE_NONE));
    if (SDL_ISPIXELFORMAT_INDEXED(format))
        require_sdl(SDL_CreateSurfacePalette(result.get()) != nullptr);
    return result;
}

SDLSurfaceOwner CopyCpuStorage(SDL_Surface& source)
{
    auto result = CreateCpuStorage(source.w, source.h, source.format);
    if (auto* palette = SDL_GetSurfacePalette(&source))
        require_sdl(SDL_SetPaletteColors(SDL_GetSurfacePalette(result.get()), palette->colors, 0,
                                         palette->ncolors));
    require_sdl(SDL_BlitSurface(&source, nullptr, result.get(), nullptr));
    return result;
}

bool ClipCpuBlit(SDL_Rect& destination, SDL_Rect& source, const SDL_Rect& destination_bounds,
                 const SDL_Rect& source_bounds)
{
    const auto clip = [](SDL_Rect& rect, SDL_Rect& paired, const SDL_Rect& bounds) {
        SDL_Rect intersection;
        if (!SDL_GetRectIntersection(&rect, &bounds, &intersection)) return false;
        paired.x += intersection.x - rect.x;
        paired.y += intersection.y - rect.y;
        paired.w = intersection.w;
        paired.h = intersection.h;
        rect = intersection;
        return true;
    };
    return clip(source, destination, source_bounds) &&
           clip(destination, source, destination_bounds);
}
