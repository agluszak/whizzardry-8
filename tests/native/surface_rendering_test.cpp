#include <cstdint>
#include <memory>
#include <utility>

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <catch2/catch_test_macros.hpp>

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
