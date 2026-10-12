#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_surface.h>

#include <cstdint>

#include "image.h"
#include "wiz8/surface_storage.h"

namespace {
using Surface = SDLSurfaceOwner;

Surface surface(std::int32_t width, std::int32_t height)
{
    return {SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB1555), SDL_DestroySurface};
}

std::uint16_t& pixel(SDL_Surface& image, std::int32_t x, std::int32_t y)
{
    return static_cast<std::uint16_t*>(image.pixels)[y * image.pitch / 2 + x];
}
}

TEST_CASE("SDL image blits use color keys, mirroring, and destination clip rectangles", "[surface]")
{
    auto source = surface(3, 1);
    auto destination = surface(5, 1);
    REQUIRE(source);
    REQUIRE(destination);
    pixel(*source, 0, 0) = 0;
    pixel(*source, 1, 0) = 0x1234;
    pixel(*source, 2, 0) = 0x2345;
    for (std::int32_t x = 0; x < 5; ++x)
        pixel(*destination, x, 0) = 0x4567;
    BlitImage(*destination, 1, 0, *source, nullptr, {.color_key = 0});
    REQUIRE(pixel(*destination, 0, 0) == 0x4567);
    REQUIRE(pixel(*destination, 1, 0) == 0x4567);
    REQUIRE(pixel(*destination, 2, 0) == 0x1234);
    REQUIRE(pixel(*destination, 3, 0) == 0x2345);
    REQUIRE(pixel(*destination, 4, 0) == 0x4567);

    const SDL_Rect clip{1, 0, 2, 1};
    {
        ScopedClipRect scoped_clip(*destination, clip);
        BlitImage(*destination, 0, 0, *source, nullptr, {.color_key = 0, .mirror = true});
        SDL_Rect active{};
        REQUIRE(SDL_GetSurfaceClipRect(destination.get(), &active));
        REQUIRE((active.x == clip.x && active.y == clip.y && active.w == clip.w &&
                 active.h == clip.h));
    }
    SDL_Rect restored{};
    REQUIRE(SDL_GetSurfaceClipRect(destination.get(), &restored));
    REQUIRE((restored.x == 0 && restored.y == 0 && restored.w == 5 && restored.h == 1));
    REQUIRE(pixel(*destination, 0, 0) == 0x4567);
    REQUIRE(pixel(*destination, 1, 0) == 0x1234);
    REQUIRE(pixel(*destination, 2, 0) == 0x1234);
    REQUIRE(pixel(*destination, 3, 0) == 0x2345);
    REQUIRE(pixel(*destination, 4, 0) == 0x4567);
}
