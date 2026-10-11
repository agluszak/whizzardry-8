#pragma once

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "himage.h"

// Path-keyed decoded images packed in g_sgp_pixel_format; a format change discards them.
class ImageAssets {
public:
    std::shared_ptr<const SpriteSheet> sprites(std::string_view path);
    std::shared_ptr<SDL_Surface> bitmap(std::string_view path);
    void clear();

private:
    void useSessionFormat();

    SDL_PixelFormat format_ = SDL_PIXELFORMAT_UNKNOWN;
    std::unordered_map<std::string, std::shared_ptr<const SpriteSheet>> sprites_;
    std::unordered_map<std::string, std::shared_ptr<SDL_Surface>> bitmaps_;
};

extern ImageAssets g_image_assets;
