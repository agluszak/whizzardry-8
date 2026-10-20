#include <cstdint>
#include <initializer_list>
#include <memory>
#include <utility>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>
#include <vsg/app/Camera.h>
#include <vsg/app/ProjectionMatrix.h>
#include <vsg/app/View.h>
#include <vsg/app/ViewMatrix.h>
#include <vsg/core/observer_ptr.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/maths/vec3.h>
#include <vsg/maths/vec4.h>
#include <vsg/state/ViewportState.h>
#include <vsg/utils/Builder.h>
#include <vulkan/vulkan_core.h>

#include "surface_pixels.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/renderer.h"
#include "wiz8/surface_storage.h"

TEST_CASE("CPU canvas publication preserves alpha holes and unpublished painting")
{
    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
            SDL_CreateWindow("surface rendering", 640, 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN),
            SDL_DestroyWindow};
        REQUIRE(window);
        Renderer renderer(window.get());
        auto storage = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_ARGB1555);
        for (int y = 0; y < 480; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(
                static_cast<unsigned char*>(storage->pixels) + y * storage->pitch);
            for (int x = 0; x < 640; ++x) row[x] = (x + y) % 3 == 0 ? 0 : (x % 2 ? 0xffff : 0x8000);
        }
        auto pixels = std::shared_ptr<SDL_Surface>(std::move(storage));
        UiCanvasTexture canvas(pixels);
        canvas.setAlphaTestEnabled(true);
        const auto draw = [&] {
            renderer.beginFrame({0, 0, 1, 1});
            canvas.draw(&renderer);
            renderer.endFrame(false, true);
            auto* frame = renderer.readback();
            REQUIRE(frame);
            return frame;
        };
        auto* frame = draw();
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                const auto expected = (x + y) % 3 == 0 ? 0x0000ffu : (x % 2 ? 0xffffffu : 0u);
                REQUIRE((ReadARGB(*frame, x, y) & 0xffffff) == expected);
            }

        // Refresh publishes these two regions. Ordinary Draw calls may also
        // paint elsewhere; uploading a full live surface would publish them early.
        const SDL_Rect update{125, 125, 8, 8}, edge{635, 475, 15, 15};
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), nullptr, 0xfc00));
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), &update, 0x83e0));
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), &edge, 0x83e0));
        canvas.updateRectangle(&renderer, 125, 125, 133, 133);
        canvas.updateRectangle(&renderer, 635, 475, 650, 490);
        frame = draw();
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                const bool updated =
                    (x >= 125 && x < 133 && y >= 125 && y < 133) || (x >= 635 && y >= 475);
                const auto expected = updated            ? 0x00ff00u
                                      : (x + y) % 3 == 0 ? 0x0000ffu
                                      : x % 2            ? 0xffffffu
                                                         : 0u;
                REQUIRE((ReadARGB(*frame, x, y) & 0xffffff) == expected);
            }
        // Movie canvases draw all pixels, including transparent source black.
        canvas.setAlphaTestEnabled(false);
        frame = draw();
        REQUIRE((ReadARGB(*frame, 0, 0) & 0xffffff) == 0);
        renderer.close();
        REQUIRE_FALSE(renderer.isOpen());
        renderer.reopen();
        REQUIRE(renderer.isOpen());
        frame = draw();
        REQUIRE((ReadARGB(*frame, 125, 125) & 0xffffff) == 0x00ff00);
    }
    SDL_Quit();
}

TEST_CASE("VSG presentation and offscreen targets cover resized windows")
{
    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
            SDL_CreateWindow("resized surface rendering", 640, 480,
                             SDL_WINDOW_HIDDEN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow};
        REQUIRE(window);
        Renderer renderer(window.get());
        OverlayQuad quad;
        quad.size = {640, 480};
        quad.color = {0, 1, 0, 1};
        auto builder = vsg::Builder::create();
        vsg::GeometryInfo geometry;
        geometry.dx = {2, 0, 0};
        geometry.dy = {0, 2, 0};
        geometry.color = vsg::vec4(1, 0, 0, 1);
        vsg::StateInfo state;
        state.lighting = false;
        state.two_sided = true;
        auto view = vsg::View::create(
            vsg::Camera::create(
                vsg::Orthographic::create(-1, 1, -1, 1, .1, 10),
                vsg::LookAt::create(vsg::dvec3(0, 0, 2), vsg::dvec3(0, 0, 0), vsg::dvec3(0, 1, 0)),
                vsg::ViewportState::create(VkExtent2D{640, 480})),
            builder->createQuad(geometry, state));
        const auto draw = [&](bool present, bool ordinary) {
            renderer.beginFrame({0, 0, 1, 1});
            if (ordinary)
                renderer.addView(view);
            else
                renderer.drawOverlayQuad(quad);
            renderer.endFrame(present, true);
            auto* frame = renderer.readback();
            REQUIRE(frame);
            REQUIRE(frame->w == static_cast<int>(renderer.width()));
            REQUIRE(frame->h == static_cast<int>(renderer.height()));
            // Check the new edge: matching dimensions alone can hide an old
            // framebuffer, render area, scissor or cached camera viewport.
            for (const int x : {0, frame->w - 1})
                for (const int y : {0, frame->h - 1})
                    if (ordinary) {
                        const auto color = ReadARGB(*frame, x, y);
                        REQUIRE(((color >> 16) & 255) > 200);
                        REQUIRE((color & 0xffff) == 0);
                    } else {
                        REQUIRE((ReadARGB(*frame, x, y) & 0xffffff) == 0x00ff00);
                    }
        };
        draw(false, false);
        draw(false, true);
        // A cached camera still needs resizing when another view supplied
        // the most recent frame (for example, a menu or movie overlay).
        draw(false, false);
        REQUIRE(SDL_SetWindowSize(window.get(), 800, 600));
        REQUIRE(SDL_SyncWindow(window.get()));
        REQUIRE(renderer.reopen());
        REQUIRE(renderer.width() > 640);
        REQUIRE(renderer.height() > 480);
        draw(false, true);
        draw(true, true);
        draw(false, false);
        draw(true, false);

        // Changing present mode can consume a pending resize before the SDL
        // event handler calls reopen. Both targets must follow that extent.
        REQUIRE(SDL_SetWindowSize(window.get(), 1000, 750));
        REQUIRE(SDL_SyncWindow(window.get()));
        renderer.setVsync(false);
        REQUIRE(renderer.width() > 800);
        REQUIRE(renderer.height() > 600);
        draw(false, true);
        draw(true, true);
        draw(false, false);
        draw(true, false);
        renderer.close();
        REQUIRE(SDL_SetWindowSize(window.get(), 640, 480));
        REQUIRE(SDL_SyncWindow(window.get()));
        REQUIRE(renderer.reopen());
        renderer.setVsync(true);
        draw(false, true);
        draw(true, true);
        draw(false, false);
        draw(true, false);
        const vsg::observer_ptr<vsg::View> weak_view(view);
        view = {};
        REQUIRE_FALSE(weak_view.valid());
        // A later resize safely prunes expired camera observers.
        REQUIRE(SDL_SetWindowSize(window.get(), 800, 600));
        REQUIRE(SDL_SyncWindow(window.get()));
        REQUIRE(renderer.reopen());
        draw(false, false);
    }
    SDL_Quit();
}
