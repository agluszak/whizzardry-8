#include <memory>
#include <utility>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>
#include <vsg/maths/vec4.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/Builder.h>

#include "surface_pixels.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/renderer.h"
#include "wiz8/render/texture.h"
#include "wiz8/render/world_scene.h"
#include "wiz8/surface_storage.h"

namespace {
struct Fixture {
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<Renderer> renderer;
    Fixture()
    {
        SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
        REQUIRE(SDL_Init(SDL_INIT_VIDEO));
        window.reset(
            SDL_CreateWindow("VSG rendering", 64, 64, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN));
        REQUIRE(window);
        renderer = std::make_unique<Renderer>(window.get());
    }
    ~Fixture()
    {
        renderer.reset();
        window.reset();
        SDL_Quit();
    }
    unsigned pixel(int x, int y)
    {
        auto* surface = renderer->readback();
        REQUIRE(surface);
        return ReadARGB(*surface, x, y) & 0xffffff;
    }
};
std::shared_ptr<Texture> solidTexture(unsigned color)
{
    auto pixels = CreateCpuStorage(1, 1, SDL_PIXELFORMAT_BGRA32);
    REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr, color));
    return std::make_shared<Texture>(std::shared_ptr<SDL_Surface>(std::move(pixels)));
}
}

TEST_CASE("VSG frame owns submitted overlay resources through completion")
{
    Fixture fixture;
    auto texture = solidTexture(0xffff0000);
    OverlayQuad quad;
    quad.size = {320, 480};
    quad.texture = texture;
    fixture.renderer->beginFrame({0, 0, 1, 1});
    fixture.renderer->drawOverlayQuad(quad);
    texture = solidTexture(0xff00ff00);
    quad.texture = texture;
    quad.position.x = 320;
    fixture.renderer->drawOverlayQuad(quad);
    texture.reset();
    quad.texture.reset();
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0xff0000);
    REQUIRE(fixture.pixel(48, 32) == 0x00ff00);
    fixture.renderer->beginFrame({0, 0, 1, 1});
    fixture.renderer->endFrame(false, true);
    REQUIRE(fixture.pixel(16, 32) == 0x0000ff);
}

TEST_CASE("Two VSG views render an ordinary subgraph from one world")
{
    Fixture fixture;
    WorldScene scene;
    auto builder = vsg::Builder::create();
    vsg::GeometryInfo geometry;
    geometry.position = {0, 0, 3};
    geometry.dx = {-2, 0, 0};
    geometry.dy = {0, 2, 0};
    geometry.color = vsg::vec4(1, 0, 0, 1);
    vsg::StateInfo material;
    material.lighting = false;
    material.two_sided = true;
    auto ordinary = builder->createQuad(geometry, material);
    scene.root->addChild(ordinary);
    scene.camera.setClipRange(1, 10);
    scene.camera.setViewPlane(Camera3D::Rect{-1, -1, 1, 1}, 1);
    fixture.renderer->beginFrame({0, 0, 1, 1});
    const auto number = fixture.renderer->frameNumber();
    auto left = scene.createView(fixture.renderer->context(), &scene.camera, {},
                                 vsg::ViewportState::create(0, 0, 32, 64), number);
    auto right = scene.createView(fixture.renderer->context(), &scene.camera, {},
                                  vsg::ViewportState::create(32, 0, 32, 64), number);
    fixture.renderer->addView(left);
    fixture.renderer->addView(right);
    fixture.renderer->endFrame(false, true);
    const auto left_pixel = fixture.pixel(16, 32);
    REQUIRE((left_pixel >> 16) >= 200);
    REQUIRE((left_pixel & 0xffff) == 0);
    REQUIRE(fixture.pixel(48, 32) == left_pixel);
    REQUIRE(scene.root->children.back() == ordinary);
}
