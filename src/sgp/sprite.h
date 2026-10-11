#pragma once

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstddef>

#include "himage.h"
#include "wiz8/surface_storage.h"

enum class SpriteBlend {
    Transparent,
    Shadow,
};

struct SpriteDraw {
    SpriteBlend blend = SpriteBlend::Transparent;
    bool mirror = false;
    const Palette16* palette = nullptr;
};

void DrawSprite(SDL_Surface& target, const SpriteSheet& sprites, std::size_t frame,
                int x, int y, SpriteDraw options = {});
bool SpriteIsClipped(const SpriteSheet& sprites, std::size_t frame, int x, int y,
                     const SDL_Rect& clip);
void TileSprite(SDL_Surface& target, const SDL_Rect& area, const SpriteSheet& sprites,
                std::size_t frame, SDL_Point origin);
SDLSurfaceOwner RenderSpriteToImage(const SpriteSheet& sprites, std::size_t frame);
