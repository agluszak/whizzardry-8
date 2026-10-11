#pragma once

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstddef>
#include <cstdint>

#include "himage.h"
#include "wiz8/surface_storage.h"

enum class SpriteBlend {
    Transparent,
    Mono,
};

struct SpriteDraw {
    SpriteBlend blend = SpriteBlend::Transparent;
    std::uint16_t foreground = 0;
    std::uint16_t background = 0;
    std::uint16_t shadow = 0;
    bool mirror = false;
    const Palette16* palette = nullptr;
    bool color_key = true;
};

void DrawSprite(SDL_Surface& target, const SpriteSheet& sprites, std::size_t frame,
                int x, int y, SpriteDraw options = {});
bool SpriteIsClipped(const SpriteSheet& sprites, std::size_t frame, int x, int y,
                     const SDL_Rect& clip);
void TileSprite(SDL_Surface& target, const SDL_Rect& area, const SpriteSheet& sprites,
                std::size_t frame, SDL_Point offset);
SDLSurfaceOwner RenderSpriteToImage(const SpriteSheet& sprites, std::size_t frame);
