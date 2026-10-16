#pragma once

#include <memory>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

using SDLSurfaceOwner = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format,
                                 void* pixels = nullptr, int pitch = -1);
// Snapshot aliased storage before SDL's stretched copy.
void StretchCpuSurface(SDL_Surface& source, SDL_Surface& destination, SDL_ScaleMode mode);
