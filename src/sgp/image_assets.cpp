#include "image_assets.h"

#include <SDL3/SDL_surface.h>
#include <memory>
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
        if (character == '/') {
            key.push_back('\\');
        } else if (character >= 'A' && character <= 'Z') {
            key.push_back(static_cast<char>(character + ('a' - 'A')));
        } else {
            key.push_back(static_cast<char>(character));
        }
    }
    return key;
}
} // namespace

ImageAssets g_image_assets;

void ImageAssets::useSessionFormat()
{
    if (format_ != g_sgp_pixel_format) {
        clear();
        format_ = g_sgp_pixel_format;
    }
}

std::shared_ptr<const SpriteSheet> ImageAssets::sprites(std::string_view path)
{
    useSessionFormat();
    const auto key = cache_key(path);
    if (const auto found = sprites_.find(key); found != sprites_.end()) {
        return found->second;
    }

    auto decoded = LoadImage(path);
    auto* sheet = decoded ? std::get_if<SpriteSheet>(&*decoded) : nullptr;
    if (!sheet) {
        return {};
    }

    sheet->colors = Create16BPPPalette(sheet->palette);
    auto sprites = std::make_shared<const SpriteSheet>(std::move(*sheet));
    sprites_.emplace(key, sprites);
    return sprites;
}

std::shared_ptr<SDL_Surface> ImageAssets::bitmap(std::string_view path)
{
    useSessionFormat();
    const auto key = cache_key(path);
    if (const auto found = bitmaps_.find(key); found != bitmaps_.end()) {
        return found->second;
    }

    auto decoded = LoadImage(path);
    auto* image = decoded ? std::get_if<BitmapImage>(&*decoded) : nullptr;
    if (!image) {
        return {};
    }

    auto bitmap_owner = ConvertBitmapToFrameSurface(**image);
    auto bitmap = std::shared_ptr<SDL_Surface>(bitmap_owner.release(), SDL_DestroySurface);
    bitmaps_.emplace(key, bitmap);
    return bitmap;
}

void ImageAssets::clear()
{
    sprites_.clear();
    bitmaps_.clear();
}
