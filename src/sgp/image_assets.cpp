#include "image_assets.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <memory>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "himage.h"
#include "image.h"

namespace {
std::string cache_key(std::string_view path)
{
    std::string key;
    key.reserve(path.size());
    for (unsigned char character : path) {
        if (character == '/')
            key.push_back('\\');
        else if (character >= 'A' && character <= 'Z')
            key.push_back(static_cast<char>(character + ('a' - 'A')));
        else
            key.push_back(static_cast<char>(character));
    }
    return key;
}
}

ImageAssets g_image_assets;

std::shared_ptr<const SpriteSheet> ImageAssets::sprites(std::string_view path)
{
    const auto key = cache_key(path);
    if (const auto found = sprites_.find(key); found != sprites_.end())
        return found->second;

    auto decoded = LoadImage(path);
    auto* sheet = decoded ? std::get_if<SpriteSheet>(&*decoded) : nullptr;
    if (!sheet)
        return {};

    sheet->colors = Create16BPPPalette(sheet->palette);
    auto sprites = std::make_shared<const SpriteSheet>(std::move(*sheet));
    sprites_.emplace(key, sprites);
    return sprites;
}

std::shared_ptr<SDL_Surface> ImageAssets::bitmap(std::string_view path)
{
    const auto key = cache_key(path);
    if (const auto found = bitmaps_.find(key); found != bitmaps_.end())
        return found->second;

    auto decoded = LoadImage(path);
    auto* image = decoded ? std::get_if<BitmapImage>(&*decoded) : nullptr;
    if (!image)
        return {};

    auto bitmap_owner = CreateFrameSurface(image->width, image->height);
    auto& framebuffer = GetFrameBufferSurface();
    if (image->bitDepth == 8) {
        if (!image->palette)
            throw std::runtime_error("Indexed bitmap has no palette");
        const auto palette = Create16BPPPalette(*image->palette);
        for (int y = 0; y < image->height; ++y) {
            const auto* input = image->pixels.data() + y * image->width;
            auto* output = reinterpret_cast<std::uint16_t*>(
                static_cast<std::uint8_t*>(bitmap_owner->pixels) + y * bitmap_owner->pitch);
            for (int x = 0; x < image->width; ++x)
                output[x] = palette[input[x]];
        }
    } else {
        if (image->format == SDL_PIXELFORMAT_UNKNOWN ||
            !SDL_ConvertPixels(image->width, image->height, image->format,
                               image->pixels.data(), image->width * SDL_BYTESPERPIXEL(image->format),
                               framebuffer.format, bitmap_owner->pixels, bitmap_owner->pitch))
            throw std::runtime_error(SDL_GetError());
    }
    if (!SDL_SetSurfaceColorKey(bitmap_owner.get(), true, 0))
        throw std::runtime_error(SDL_GetError());
    auto bitmap = std::shared_ptr<SDL_Surface>(bitmap_owner.release(), SDL_DestroySurface);
    bitmaps_.emplace(key, bitmap);
    return bitmap;
}

void ImageAssets::clear()
{
    sprites_.clear();
    bitmaps_.clear();
}
