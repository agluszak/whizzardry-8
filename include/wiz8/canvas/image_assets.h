#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/canvas/himage.h"

// Path-keyed decoded images packed in the fixed ARGB1555 framebuffer format.
class ImageAssets {
public:
    std::shared_ptr<const SpriteSheet> sprites(std::string_view path);
    std::shared_ptr<SDL_Surface> bitmap(std::string_view path);
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<const SpriteSheet>> sprites_;
    std::unordered_map<std::string, std::shared_ptr<SDL_Surface>> bitmaps_;
};

extern ImageAssets g_image_assets;
