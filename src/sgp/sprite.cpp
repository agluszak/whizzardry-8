#include "sprite.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "himage.h"
#include "image.h"
#include "wiz8/surface_storage.h"

namespace {

SDL_Rect sprite_rect(const SpriteFrame& frame, int x, int y, bool mirror)
{
    return {mirror ? x - frame.offset.x : x + frame.offset.x, y + frame.offset.y, frame.width,
            frame.height};
}

}

void DrawSprite(SDL_Surface& target, const SpriteSheet& sprites, std::size_t frame_index, int x,
                int y, SpriteDraw options)
{
    if (frame_index >= sprites.frames.size()) {
        return;
    }
    if (SDL_BYTESPERPIXEL(target.format) != 2) {
        throw std::invalid_argument("Sprite target must be 16-bit");
    }

    const auto& frame = sprites.frames[frame_index];
    if (frame.indices.empty()) {
        return;
    }
    const SDL_Rect requested = sprite_rect(frame, x, y, options.mirror);
    SDL_Rect clip{};
    SDL_GetSurfaceClipRect(&target, &clip);
    SDL_Rect visible{};
    if (!SDL_GetRectIntersection(&requested, &clip, &visible)) {
        return;
    }

    const auto& palette = options.palette ? *options.palette : sprites.colors;
    for (int destination_y = visible.y; destination_y < visible.y + visible.h; ++destination_y) {
        const int source_y = destination_y - requested.y;
        const std::uint16_t* input = frame.indices.data() + std::size_t(source_y) * frame.width;
        auto* output = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(target.pixels) +
                                                        destination_y * target.pitch);
        for (int destination_x = visible.x; destination_x < visible.x + visible.w;
             ++destination_x) {
            const int relative_x = destination_x - requested.x;
            const int source_x = options.mirror ? frame.width - 1 - relative_x : relative_x;
            const std::uint16_t index = input[source_x];
            if (options.blend == SpriteBlend::Mono) {
                if (index == 1) {
                    if (options.shadow != 0) {
                        output[destination_x] = options.shadow;
                    }
                } else if (index != 0 && index != SpriteFrame::kTransparent) {
                    output[destination_x] = options.foreground;
                } else if (options.background != 0) {
                    output[destination_x] = options.background;
                }
                continue;
            }
            if (index == SpriteFrame::kTransparent) {
                continue;
            }
            output[destination_x] = palette[index];
        }
    }
}

bool SpriteIsClipped(const SpriteSheet& sprites, std::size_t frame_index, int x, int y,
                     const SDL_Rect& clip)
{
    if (frame_index >= sprites.frames.size()) {
        return false;
    }
    const auto& frame = sprites.frames[frame_index];
    if (frame.indices.empty()) {
        return false;
    }
    const SDL_Rect requested = sprite_rect(frame, x, y, false);
    SDL_Rect visible{};
    return !SDL_GetRectIntersection(&requested, &clip, &visible) ||
           !SDL_RectsEqual(&requested, &visible);
}

void TileSprite(SDL_Surface& target, const SDL_Rect& area, const SpriteSheet& sprites,
                std::size_t frame_index, SDL_Point offset)
{
    if (frame_index >= sprites.frames.size() || SDL_RectEmpty(&area)) {
        return;
    }
    const auto& frame = sprites.frames[frame_index];
    const int tile_width = frame.width + frame.offset.x;
    const int tile_height = frame.height + frame.offset.y;
    if (frame.indices.empty() || tile_width <= 0 || tile_height <= 0) {
        return;
    }

    ScopedClipRect clip(target, area);
    const auto modulo = [](int value, int period) { return (value % period + period) % period; };
    const int first_x = area.x + modulo(offset.x, tile_width) - tile_width;
    const int first_y = area.y + modulo(offset.y, tile_height) - tile_height;
    for (int y = first_y; y < area.y + area.h; y += tile_height) {
        for (int x = first_x; x < area.x + area.w; x += tile_width) {
            DrawSprite(target, sprites, frame_index, x, y);
        }
    }
}

SDLSurfaceOwner RenderSpriteToImage(const SpriteSheet& sprites, std::size_t frame_index)
{
    if (frame_index >= sprites.frames.size()) {
        return {nullptr, SDL_DestroySurface};
    }
    const auto& frame = sprites.frames[frame_index];
    if (frame.indices.empty()) {
        return {nullptr, SDL_DestroySurface};
    }
    auto image = CreateFrameSurface(frame.width, frame.height);
    // SDL color keys compare RGB only. A zero key aliases opaque black
    // (0x8000 in ARGB1555), so reserve an RGB value absent from the palette.
    const auto* format = SDL_GetPixelFormatDetails(image->format);
    const auto rgb_mask = format->Rmask | format->Gmask | format->Bmask;
    std::uint16_t key = 0;
    while (std::any_of(sprites.colors.begin(), sprites.colors.end(), [key, rgb_mask](auto color) {
        return (color & rgb_mask) == (key & rgb_mask);
    })) {
        ++key;
    }
    if (!SDL_FillSurfaceRect(image.get(), nullptr, key)) {
        throw std::runtime_error(SDL_GetError());
    }
    DrawSprite(*image, sprites, frame_index, 0, 0);
    if (!SDL_SetSurfaceColorKey(image.get(), true, key)) {
        throw std::runtime_error(SDL_GetError());
    }
    return image;
}
