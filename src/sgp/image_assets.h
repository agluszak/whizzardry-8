#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "vobject.h"
#include "vsurface.h"

class ImageAssets {
public:
    std::shared_ptr<SGPVObject> sprites(std::string_view path);
    std::shared_ptr<SGPVSurface> bitmap(std::string_view path);
    void clear();

private:
    std::unordered_map<std::string, std::shared_ptr<SGPVObject>> sprites_;
    std::unordered_map<std::string, std::shared_ptr<SGPVSurface>> bitmaps_;
};

extern ImageAssets g_image_assets;
