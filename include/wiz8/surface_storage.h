#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include <cstddef>
#include <memory>

using SDLSurfaceOwner = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

void ConvertCpuPixels(void* destination, const void* source, size_t count, SDL_PixelFormat source_format,
                      SDL_PixelFormat destination_format);
SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format, void* pixels = nullptr, int pitch = -1);
SDLSurfaceOwner CopyCpuStorage(SDL_Surface& source);
bool ClipCpuBlit(SDL_Rect& destination, SDL_Rect& source, const SDL_Rect& destination_bounds,
                 const SDL_Rect& source_bounds);
