#include "image.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL_blendmode.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "himage.h"
#include "shading.h"
#include "wiz8/surface_storage.h"

namespace {

void require_sdl(bool result)
{
    if (!result) {
        throw std::runtime_error(SDL_GetError());
    }
}

SDL_Rect surface_bounds(const SDL_Surface& surface)
{
    return {0, 0, surface.w, surface.h};
}

}

ScopedClipRect::ScopedClipRect(SDL_Surface& surface, const SDL_Rect& clip) : surface_(&surface)
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
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("Frame surface dimensions");
    }
    SDLSurfaceOwner result{SDL_CreateSurface(width, height, g_sgp_pixel_format),
                           SDL_DestroySurface};
    if (!result) {
        throw std::runtime_error(SDL_GetError());
    }
    require_sdl(SDL_SetSurfaceBlendMode(result.get(), SDL_BLENDMODE_NONE));
    return result;
}

SDLSurfaceOwner ConvertBitmapToFrameSurface(const SDL_Surface& bitmap)
{
    auto result = CreateFrameSurface(bitmap.w, bitmap.h);
    if (SDL_BYTESPERPIXEL(result->format) != 2) {
        throw std::invalid_argument("Frame surfaces must be 16-bit");
    }
    if (SDL_ISPIXELFORMAT_INDEXED(bitmap.format)) {
        const auto* colors = SDL_GetSurfacePalette(const_cast<SDL_Surface*>(&bitmap));
        if (bitmap.format != SDL_PIXELFORMAT_INDEX8 || !colors) {
            throw std::invalid_argument("Indexed bitmap format");
        }
        Palette8 palette{};
        std::copy_n(colors->colors, std::min(colors->ncolors, int(palette.size())),
                    palette.begin());
        const Palette16 packed = Create16BPPPalette(palette);
        for (int y = 0; y < bitmap.h; ++y) {
            const auto* input = static_cast<const std::uint8_t*>(bitmap.pixels) + y * bitmap.pitch;
            auto* output = reinterpret_cast<std::uint16_t*>(
                static_cast<std::uint8_t*>(result->pixels) + y * result->pitch);
            for (int x = 0; x < bitmap.w; ++x) {
                output[x] = packed[input[x]];
            }
        }
        return result;
    }
    require_sdl(SDL_ConvertPixels(bitmap.w, bitmap.h, bitmap.format, bitmap.pixels, bitmap.pitch,
                                  result->format, result->pixels, result->pitch));
    const auto* details = SDL_GetPixelFormatDetails(result->format);
    const auto rgb_mask = details->Rmask | details->Gmask | details->Bmask;
    for (int y = 0; y < result->h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(result->pixels) +
                                                     y * result->pitch);
        for (int x = 0; x < result->w; ++x) {
            row[x] = (row[x] & rgb_mask) ? std::uint16_t(row[x] | details->Amask)
                                         : std::uint16_t(kBitmapTransparentPixel);
        }
    }
    return result;
}

void BlitImage(SDL_Surface& destination, int x, int y, SDL_Surface& source,
               const SDL_Rect* source_rect, ImageBlit options)
{
    const SDL_Rect source_area = source_rect ? *source_rect : surface_bounds(source);
    if (SDL_RectEmpty(&source_area)) {
        return;
    }
    SDL_Rect destination_area{x, y, source_area.w, source_area.h};
    const unsigned bytes_per_pixel = SDL_BYTESPERPIXEL(destination.format);
    const bool shares_pixels = destination.pixels == source.pixels;

    if (shares_pixels && !options.mirror && !options.color_key &&
        destination.format == source.format && bytes_per_pixel != 0) {
        SDL_Rect visible{};
        SDL_Rect clip{};
        SDL_GetSurfaceClipRect(&destination, &clip);
        if (!SDL_GetRectIntersection(&destination_area, &clip, &visible)) {
            return;
        }
        const SDL_Rect bounds = surface_bounds(source);
        const SDL_Rect source_visible{source_area.x + visible.x - x, source_area.y + visible.y - y,
                                      visible.w, visible.h};
        SDL_Rect clipped_source{};
        if (!SDL_GetRectIntersection(&source_visible, &bounds, &clipped_source)) {
            return;
        }
        visible.x += clipped_source.x - source_visible.x;
        visible.y += clipped_source.y - source_visible.y;
        visible.w = clipped_source.w;
        visible.h = clipped_source.h;

        const int first_source_y = source_area.y + visible.y - y;
        const bool copy_bottom_up = visible.y > first_source_y;
        const std::size_t row_bytes = static_cast<std::size_t>(visible.w) * bytes_per_pixel;
        for (int row = 0; row < visible.h; ++row) {
            const int offset = copy_bottom_up ? visible.h - 1 - row : row;
            const int destination_y = visible.y + offset;
            const int source_y = source_area.y + destination_y - y;
            const auto* input = static_cast<const std::uint8_t*>(source.pixels) +
                                source_y * source.pitch + clipped_source.x * bytes_per_pixel;
            auto* output = static_cast<std::uint8_t*>(destination.pixels) +
                           destination_y * destination.pitch + visible.x * bytes_per_pixel;
            std::memmove(output, input, row_bytes);
        }
        return;
    }

    if (!options.mirror && !shares_pixels) {
        SDLSurfaceOwner view{
            SDL_CreateSurfaceFrom(source.w, source.h, source.format, source.pixels, source.pitch),
            SDL_DestroySurface};
        if (!view) {
            throw std::runtime_error(SDL_GetError());
        }
        if (auto* palette = SDL_GetSurfacePalette(&source)) {
            require_sdl(SDL_SetSurfacePalette(view.get(), palette));
        }
        SDL_BlendMode blend = SDL_BLENDMODE_NONE;
        require_sdl(SDL_GetSurfaceBlendMode(&source, &blend) &&
                    SDL_SetSurfaceBlendMode(view.get(), blend));
        if (options.color_key) {
            require_sdl(SDL_SetSurfaceColorKey(view.get(), true, *options.color_key));
        }
        require_sdl(SDL_BlitSurface(view.get(), &source_area, &destination, &destination_area));
        return;
    }

    if (destination.format != source.format || SDL_BYTESPERPIXEL(destination.format) != 2) {
        throw std::invalid_argument("Image blit requires matching 16-bit surfaces");
    }

    SDL_Rect visible{};
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&destination, &clip);
    if (!SDL_GetRectIntersection(&destination_area, &clip, &visible)) {
        return;
    }

    const auto* details = SDL_GetPixelFormatDetails(source.format);
    const std::uint32_t rgb_mask = details->Rmask | details->Gmask | details->Bmask;
    std::vector<std::uint16_t> source_row;
    std::vector<std::uint8_t> source_valid;
    if (shares_pixels) {
        source_row.resize(visible.w);
        source_valid.resize(visible.w);
    }
    const int first_source_y = source_area.y + visible.y - y;
    const bool copy_bottom_up = shares_pixels && visible.y > first_source_y;
    for (int row = 0; row < visible.h; ++row) {
        const int offset = copy_bottom_up ? visible.h - 1 - row : row;
        const int destination_y = visible.y + offset;
        const int source_y = source_area.y + destination_y - y;
        if (source_y < 0 || source_y >= source.h) {
            continue;
        }
        const auto* input = reinterpret_cast<const std::uint16_t*>(
            static_cast<const std::uint8_t*>(source.pixels) + source_y * source.pitch);
        auto* output = reinterpret_cast<std::uint16_t*>(
            static_cast<std::uint8_t*>(destination.pixels) + destination_y * destination.pitch);
        if (shares_pixels) {
            std::fill(source_valid.begin(), source_valid.end(), 0);
            for (int column = 0; column < visible.w; ++column) {
                const int destination_x = visible.x + column;
                const int relative_x = destination_x - x;
                const int source_x = options.mirror ? source_area.x + source_area.w - 1 - relative_x
                                                    : source_area.x + relative_x;
                if (source_x >= 0 && source_x < source.w) {
                    source_row[column] = input[source_x];
                    source_valid[column] = 1;
                }
            }
        }
        for (int column = 0; column < visible.w; ++column) {
            const int destination_x = visible.x + column;
            const int relative_x = destination_x - x;
            const int source_x = options.mirror ? source_area.x + source_area.w - 1 - relative_x
                                                : source_area.x + relative_x;
            if (shares_pixels ? !source_valid[column] : source_x < 0 || source_x >= source.w) {
                continue;
            }
            const std::uint16_t pixel = shares_pixels ? source_row[column] : input[source_x];
            if (options.color_key && (pixel & rgb_mask) == (*options.color_key & rgb_mask)) {
                continue;
            }
            output[destination_x] = pixel;
        }
    }
}

void ShadowRect(SDL_Surface& surface, const SDL_Rect& area)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2) {
        throw std::invalid_argument("Shadow rect requires a 16-bit surface");
    }
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible)) {
        return;
    }

    for (int y = visible.y; y < visible.y + visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(surface.pixels) +
                                                     y * surface.pitch);
        for (int x = visible.x; x < visible.x + visible.w; ++x) {
            row[x] = ShadeTable[row[x]];
        }
    }
}

void PixelateRect(SDL_Surface& surface, const SDL_Rect& area,
                  std::span<const std::uint8_t, 64> pattern, std::uint16_t color)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2) {
        throw std::invalid_argument("Pixelate rect requires a 16-bit surface");
    }
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible)) {
        return;
    }

    for (int y = 0; y < visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(surface.pixels) +
                                                     (visible.y + y) * surface.pitch);
        for (int x = 0; x < visible.w; ++x) {
            const std::uint8_t mask = x == 0 ? pattern[0] : pattern[(y & 7) * 8 + (x & 7)];
            if (mask != 0) {
                row[visible.x + x] = color;
            }
        }
    }
}

void HatchRect(SDL_Surface& surface, const SDL_Rect& area)
{
    if (SDL_BYTESPERPIXEL(surface.format) != 2) {
        throw std::invalid_argument("Hatch rect requires a 16-bit surface");
    }
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&surface, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&area, &clip, &visible)) {
        return;
    }

    for (int y = 0; y < visible.h; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(surface.pixels) +
                                                     (visible.y + y) * surface.pitch);
        for (int x = 0; x < visible.w; ++x) {
            if (x == 0 || ((x + y) & 1) == 0) {
                row[visible.x + x] = 0;
            }
        }
    }
}
