#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <span>

#include "wiz8/surface_storage.h"

struct BitmapImage;

struct ImageBlit {
    bool color_key = false;
    bool mirror = false;
};

class ScopedClipRect {
public:
    ScopedClipRect(SDL_Surface& surface, const SDL_Rect& clip);
    ~ScopedClipRect();
    ScopedClipRect(const ScopedClipRect&) = delete;
    ScopedClipRect& operator=(const ScopedClipRect&) = delete;

private:
    SDL_Surface* surface_;
    SDL_Rect previous_;
};

SDL_Surface& GetFrameBufferSurface();
SDLSurfaceOwner CreateFrameSurface(int width, int height);
SDLSurfaceOwner ConvertIndexedBitmapToSurface(const BitmapImage& bitmap, SDL_PixelFormat format);
void BlitImage(SDL_Surface& destination, int x, int y, SDL_Surface& source,
               const SDL_Rect* source_rect = nullptr, ImageBlit options = {});
void PixelateRect(SDL_Surface& surface, const SDL_Rect& area,
                  std::span<const std::uint8_t, 64> pattern, std::uint16_t color);
void ShadowRect(SDL_Surface& surface, const SDL_Rect& area);
void HatchRect(SDL_Surface& surface, const SDL_Rect& area);
