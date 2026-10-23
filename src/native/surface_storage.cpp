#include "wiz8/surface_storage.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

namespace {
void require_sdl(bool result)
{
    if (!result) throw std::runtime_error(SDL_GetError());
}

SDLSurfaceOwner ConfigureCpuStorage(SDL_Surface* surface)
{
    SDLSurfaceOwner result{surface, SDL_DestroySurface};
    require_sdl(bool(result));
    require_sdl(SDL_SetSurfaceBlendMode(result.get(), SDL_BLENDMODE_NONE));
    if (SDL_ISPIXELFORMAT_INDEXED(result->format))
        require_sdl(SDL_CreateSurfacePalette(result.get()) != nullptr);
    return result;
}
} // namespace

SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format)
{
    return ConfigureCpuStorage(SDL_CreateSurface(width, height, format));
}

SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format, void* pixels,
                                 int pitch)
{
    return ConfigureCpuStorage(SDL_CreateSurfaceFrom(width, height, format, pixels, pitch));
}

void StretchCpuSurface(SDL_Surface& source, SDL_Surface& destination, SDL_ScaleMode mode)
{
    SDL_Surface* input = &source;
    SDLSurfaceOwner snapshot{nullptr, SDL_DestroySurface};
    const auto src = reinterpret_cast<std::uintptr_t>(source.pixels);
    const auto dst = reinterpret_cast<std::uintptr_t>(destination.pixels);
    if (dst < src + std::size_t(source.pitch) * source.h &&
        src < dst + std::size_t(destination.pitch) * destination.h) {
        snapshot.reset(SDL_DuplicateSurface(&source));
        require_sdl(bool(snapshot));
        input = snapshot.get();
    }
    if (input->format != destination.format ||
        (mode == SDL_SCALEMODE_LINEAR && SDL_BYTESPERPIXEL(input->format) != 4) ||
        SDL_ISPIXELFORMAT_INDEXED(input->format)) {
        SDLSurfaceOwner converted{nullptr, SDL_DestroySurface};
        if (input->format != SDL_PIXELFORMAT_BGRA32) {
            converted.reset(SDL_ConvertSurface(input, SDL_PIXELFORMAT_BGRA32));
            require_sdl(bool(converted));
            require_sdl(SDL_SetSurfaceBlendMode(converted.get(), SDL_BLENDMODE_NONE));
            input = converted.get();
        }
        if (destination.format == SDL_PIXELFORMAT_BGRA32) {
            require_sdl(SDL_StretchSurface(input, nullptr, &destination, nullptr, mode));
        } else {
            auto scaled = CreateCpuStorage(destination.w, destination.h, SDL_PIXELFORMAT_BGRA32);
            require_sdl(SDL_StretchSurface(input, nullptr, scaled.get(), nullptr, mode));
            require_sdl(SDL_BlitSurface(scaled.get(), nullptr, &destination, nullptr));
        }
        return;
    }
    require_sdl(SDL_StretchSurface(input, nullptr, &destination, nullptr, mode));
}
