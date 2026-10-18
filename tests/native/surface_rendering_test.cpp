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
#include "surrender/SDLGPUDevice.h"
#include "surrender/srGERD.h"
#include "surrender/srTexture.h"
#include "wiz8/surface2d.h"
#include "wiz8/surface_storage.h"

TEST_CASE("Native UI texture preserves pixels, alpha holes, and dirty rectangles")
{
    SDL_SetHint(SDL_HINT_SHUTDOWN_DBUS_ON_QUIT, "1");
    REQUIRE(SDL_Init(SDL_INIT_VIDEO));
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
            SDL_CreateWindow("surface rendering", 640, 480, SDL_WINDOW_HIDDEN), SDL_DestroyWindow};
        REQUIRE(window);
        srGERD renderer;
        REQUIRE(renderer.createContext(window.get()) == srGERD::ERROR_NONE);
        REQUIRE(renderer.openWindow(640, 480) == srGERD::ERROR_NONE);
        renderer.setTextureReduction(1);
        renderer.setTextureDefaultMagFilter(srTexture::FILTER_BEST);
        renderer.setTextureDefaultMinFilter(srTexture::FILTER_BEST);
        auto storage = CreateCpuStorage(640, 480, SDL_PIXELFORMAT_ARGB1555);
        for (int y = 0; y < 480; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(
                static_cast<unsigned char*>(storage->pixels) + y * storage->pitch);
            for (int x = 0; x < 640; ++x) row[x] = (x + y) % 3 == 0 ? 0 : (x % 2 ? 0xffff : 0x8000);
        }
        auto pixels = std::shared_ptr<SDL_Surface>(std::move(storage));
        stSurface2D surface{pixels, nullptr};
        surface.setAlphaTestEnabled(true);
        const auto draw = [&] {
            renderer.setClearColor(0, 0, 1, 1);
            renderer.clear(SDLGPUDevice::BUFFER_COLOR | SDLGPUDevice::BUFFER_DEPTH);
            renderer.matrixMode(srGERD::MATRIX_MODELVIEW);
            renderer.loadIdentity();
            renderer.resetStatistics();
            surface.draw(&renderer);
            REQUIRE(renderer.getStatistics().draw_calls == 1);
        };
        const auto initial_memory = renderer.getResidentTextureMemUsed();
        draw();
        REQUIRE(renderer.getStatistics().textures_created == 1);
        // 640x480 BGRA, without a power-of-two resize, texture reduction or mip chain.
        REQUIRE(renderer.getResidentTextureMemUsed() - initial_memory == 640 * 480 * 4);
        auto* frame = renderer.lockBuffer();
        REQUIRE(frame);
        for (int y = 0; y < 480; ++y)
            for (int x = 0; x < 640; ++x) {
                const auto expected = (x + y) % 3 == 0 ? 0x0000ffu : (x % 2 ? 0xffffffu : 0u);
                REQUIRE((ReadARGB(*frame, x, y) & 0xffffff) == expected);
            }
        renderer.unlockBuffer();
        // Exercise an update crossing both former tile axes and clipping at the canvas edge.
        const SDL_Rect update{125, 125, 8, 8}, edge{635, 475, 15, 15};
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), &update, 0x83e0));
        REQUIRE(SDL_FillSurfaceRect(pixels.get(), &edge, 0x83e0));
        surface.updateRectangle(&renderer, 125, 125, 133, 133);
        surface.updateRectangle(&renderer, 635, 475, 650, 490);
        draw();
        frame = renderer.lockBuffer();
        REQUIRE(frame);
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
        renderer.unlockBuffer();
        // Movie presentation disables alpha testing: transparent source pixels become black.
        surface.setAlphaTestEnabled(false);
        draw();
        frame = renderer.lockBuffer();
        REQUIRE(frame);
        REQUIRE((ReadARGB(*frame, 0, 0) & 0xffffff) == 0);
        renderer.unlockBuffer();
    }
    SDL_Quit();
}
