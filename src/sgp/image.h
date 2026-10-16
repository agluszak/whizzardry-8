#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/surface_storage.h"

// Decoded bitmaps convert transparent pixels to this value and every other pixel to nonzero RGB.
inline constexpr std::uint32_t kBitmapTransparentPixel = 0;

struct ImageBlit {
    // Source pixels whose RGB matches this value are skipped; surface colour-key state is ignored.
    std::optional<std::uint32_t> color_key;
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
// Surfaces in kFramePixelFormat without blending.
SDLSurfaceOwner CreateFrameSurface(int width, int height);
SDLSurfaceOwner ConvertBitmapToFrameSurface(const SDL_Surface& bitmap);
void BlitImage(SDL_Surface& destination, int x, int y, SDL_Surface& source,
               const SDL_Rect* source_rect = nullptr, ImageBlit options = {});
void PixelateRect(SDL_Surface& surface, const SDL_Rect& area,
                  std::span<const std::uint8_t, 64> pattern, std::uint16_t color);
void ShadowRect(SDL_Surface& surface, const SDL_Rect& area);
void HatchRect(SDL_Surface& surface, const SDL_Rect& area);
