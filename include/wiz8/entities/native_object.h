#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <vsg/core/ref_ptr.h>

class Game;
class Material;
struct World;
namespace render {
class NativeContent;
}

// A game object that owns imported VSG content, without a retail mesh wrapper.
class NativeObject {
public:
    NativeObject(World& world, const std::filesystem::path& file);
    ~NativeObject();
    NativeObject(const NativeObject&) = delete;
    NativeObject& operator=(const NativeObject&) = delete;

    vsg::ref_ptr<render::NativeContent> node;
    std::string name;
    bool illuminated = false;
    void activate();

private:
    World& world;
    std::shared_ptr<Material> material;
};

// Resolve the existing VSG pick only; this does not run another intersection test.
void LoadNativeLevelObject(Game& game);
