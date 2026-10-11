#include "image.h"

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <stdexcept>

#include "shading.h"
#include "wiz8/surface_storage.h"

namespace {

void require_sdl(bool result)
{
    if (!result)
        throw std::runtime_error(SDL_GetError());
}

SDL_Rect surface_bounds(const SDL_Surface& surface)
{
    return {0, 0, surface.w, surface.h};
}

}

ScopedClipRect::ScopedClipRect(SDL_Surface& surface, const SDL_Rect& clip)
    : surface_(&surface)
{
    SDL_GetSurfaceClipRect(&surface, &previous_);
    require_sdl(SDL_SetSurfaceClipRect(&surface, &clip));
}

ScopedClipRect::~ScopedClipRect()
{
    SDL_SetSurfaceClipRect(surface_, &previous_);
}

SDLSurfaceOwner CreateFrameSurface(int width, int height)
{
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Frame surface dimensions");
    SDL_Surface& framebuffer = GetFrameBufferSurface();
    SDLSurfaceOwner result{SDL_CreateSurface(width, height, framebuffer.format), SDL_DestroySurface};
    if (!result)
        throw std::runtime_error(SDL_GetError());
    require_sdl(SDL_SetSurfaceBlendMode(result.get(), SDL_BLENDMODE_NONE));
    return result;
}

void BlitImage(SDL_Surface& destination, int x, int y, SDL_Surface& source,
               const SDL_Rect* source_rect, ImageBlit options)
{
    const SDL_Rect source_area = source_rect ? *source_rect : surface_bounds(source);
    if (SDL_RectEmpty(&source_area))
        return;
    SDL_Rect destination_area{x, y, source_area.w, source_area.h};
    Uint32 color_key = 0;
    const bool has_color_key = SDL_GetSurfaceColorKey(&source, &color_key);
    const unsigned bytes_per_pixel = SDL_BYTESPERPIXEL(destination.format);
    if (!options.mirror && destination.pixels == source.pixels &&
        destination.format == source.format && (bytes_per_pixel == 1 || bytes_per_pixel == 2)) {
        SDL_Rect visible{};
        SDL_Rect clip{};
        SDL_GetSurfaceClipRect(&destination, &clip);
        if (!SDL_GetRectIntersection(&destination_area, &clip, &visible))
            return;
        const int source_x = source_area.x + visible.x - x;
        if (source_x >= 0 && source_x + visible.w <= source.w) {
            for (int row = 0; row < visible.h; ++row) {
                const int destination_y = visible.y + row;
                const int source_y = source_area.y + destination_y - y;
                if (source_y < 0 || source_y >= source.h)
                    continue;
                const auto* input = static_cast<const std::uint8_t*>(source.pixels) +
                    source_y * source.pitch + source_x * bytes_per_pixel;
                auto* output = static_cast<std::uint8_t*>(destination.pixels) +
                    destination_y * destination.pitch + visible.x * bytes_per_pixel;
                if (options.color_key && has_color_key) {
                    for (int column = 0; column < visible.w; ++column) {
                        Uint32 pixel = 0;
                        std::memcpy(&pixel, input + column * bytes_per_pixel, bytes_per_pixel);
                        if (pixel != color_key)
                            std::memcpy(output + column * bytes_per_pixel, &pixel, bytes_per_pixel);
                    }
                } else {
                    const unsigned row_bytes = visible.w * bytes_per_pixel;
                    unsigned offset = 0;
                    for (unsigned chunk : {1u, 2u, 4u}) {
                        const unsigned end = chunk == 4 ? row_bytes : offset + (row_bytes & chunk);
                        for (; offset < end; offset += chunk) {
                            std::uint32_t value = 0;
                            std::memcpy(&value, input + offset, chunk);
                            std::memcpy(output + offset, &value, chunk);
                        }
                    }
                }
            }
            return;
        }
    }
    if (!options.mirror && (options.color_key || !has_color_key)) {
        require_sdl(SDL_BlitSurface(&source, &source_area, &destination, &destination_area));
        return;
    }

    if (destination.format != source.format || SDL_BYTESPERPIXEL(destination.format) != 2)
        throw std::invalid_argument("Image blit requires matching 16-bit surfaces");

    SDL_Rect visible{};
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&destination, &clip);
    if (!SDL_GetRectIntersection(&destination_area, &clip, &visible))
        return;

    for (int row = 0; row < visible.h; ++row) {
        const int destination_y = visible.y + row;
        const int source_y = source_area.y + destination_y - y;
        if (source_y < 0 || source_y >= source.h)
            continue;
        const auto* input = reinterpret_cast<const std::uint16_t*>(
            static_cast<const std::uint8_t*>(source.pixels) + source_y * source.pitch);
        auto* output = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(destination.pixels) + destination_y * destination.pitch);
        for (int column = 0; column < visible.w; ++column) {
            const int destination_x = visible.x + column;
            const int relative_x = destination_x - x;
            const int source_x = options.mirror
                ? source_area.x + source_area.w - 1 - relative_x
                : source_area.x + relative_x;
            if (source_x < 0 || source_x >= source.w)
                continue;
            const std::uint16_t pixel = input[source_x];
            if (options.color_key && has_color_key && pixel == color_key)
                continue;
            output[destination_x] = pixel;
        }
    }
}

// FUNCTION: WIZ8 0x004124a0
void ShadowRect(SDL_Surface& surface, const SDL_Rect& area)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2)
        throw std::invalid_argument("Shadow rect requires a 16-bit surface");
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible))
        return;

    for (int y = visible.y; y < visible.y + visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(surface.pixels) + y * surface.pitch);
        for (int x = visible.x; x < visible.x + visible.w; ++x)
            row[x] = ShadeTable[row[x]];
    }
}

// FUNCTION: WIZ8 0x00411850
void PixelateRect(SDL_Surface& surface, const SDL_Rect& area,
                  std::span<const std::uint8_t, 64> pattern, std::uint16_t color)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2)
        throw std::invalid_argument("Pixelate rect requires a 16-bit surface");
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible))
        return;

    for (int y = 0; y < visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(surface.pixels) + (visible.y + y) * surface.pitch);
        for (int x = 0; x < visible.w; ++x) {
            const std::uint8_t mask = x == 0 ? pattern[0] : pattern[(y & 7) * 8 + (x & 7)];
            if (mask != 0)
                row[visible.x + x] = color;
        }
    }
}

// FUNCTION: WIZ8 0x00411930
void HatchRect(SDL_Surface& surface, const SDL_Rect& area)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2)
        throw std::invalid_argument("Hatch rect requires a 16-bit surface");
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible))
        return;

    for (int y = 0; y < visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(surface.pixels) + (visible.y + y) * surface.pitch);
        for (int x = 0; x < visible.w; ++x)
            if (x == 0 || ((x + y) & 1) == 0)
                row[visible.x + x] = 0;
    }
}
