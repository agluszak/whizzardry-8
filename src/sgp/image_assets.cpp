#include "image_assets.h"

#include <string>
#include <utility>

#include "himage.h"

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

std::shared_ptr<SGPVObject> ImageAssets::sprites(std::string_view path)
{
    const auto key = cache_key(path);
    if (const auto found = sprites_.find(key); found != sprites_.end())
        return found->second;

    auto loaded = LoadVideoObject(path);
    if (!loaded)
        return {};

    auto sprites = std::shared_ptr<SGPVObject>(std::move(loaded));
    sprites_.emplace(key, sprites);
    return sprites;
}

std::shared_ptr<SGPVSurface> ImageAssets::bitmap(std::string_view path)
{
    const auto key = cache_key(path);
    if (const auto found = bitmaps_.find(key); found != bitmaps_.end())
        return found->second;

    auto loaded = LoadVideoSurface(path);
    if (!loaded)
        return {};

    auto bitmap = std::shared_ptr<SGPVSurface>(std::move(loaded));
    bitmaps_.emplace(key, bitmap);
    return bitmap;
}

void ImageAssets::clear()
{
    sprites_.clear();
    bitmaps_.clear();
}
