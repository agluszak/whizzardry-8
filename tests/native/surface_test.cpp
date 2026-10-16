#include <array>
#include <cstdint>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "image.h"
#include "wiz8/surface_storage.h"

namespace {
using Surface = SDLSurfaceOwner;

Surface surface(int width, int height)
{
    return {SDL_CreateSurface(width, height, SDL_PIXELFORMAT_XRGB1555), SDL_DestroySurface};
}

std::uint16_t& pixel(SDL_Surface& image, int x, int y)
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
    for (int x = 0; x < 5; ++x) pixel(*destination, x, 0) = 0x4567;
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
        REQUIRE(
            (active.x == clip.x && active.y == clip.y && active.w == clip.w && active.h == clip.h));
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

TEST_CASE("Stretched CPU surfaces preserve aliased pixels and fill the complete image", "[surface]")
{
    std::array<std::uint32_t, 10> pixels{0, 0, 1, 2, 3, 4};
    auto input = CreateCpuStorage(4, 1, SDL_PIXELFORMAT_ARGB8888, pixels.data() + 2, 16);
    auto output = CreateCpuStorage(8, 1, SDL_PIXELFORMAT_ARGB8888, pixels.data(), 32);
    StretchCpuSurface(*input, *output, SDL_SCALEMODE_NEAREST);
    for (unsigned i = 0; i < 8; ++i) REQUIRE(pixels[i] == i / 2 + 1);

    input = CreateCpuStorage(3, 3, SDL_PIXELFORMAT_ARGB8888);
    output = CreateCpuStorage(6, 6, SDL_PIXELFORMAT_ARGB8888);
    REQUIRE(SDL_FillSurfaceRect(input.get(), nullptr, 0xff4488cc));
    REQUIRE(SDL_FillSurfaceRect(output.get(), nullptr, 0));
    StretchCpuSurface(*input, *output, SDL_SCALEMODE_LINEAR);
    for (int y = 0; y < output->h; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const unsigned char*>(output->pixels) + y * output->pitch);
        for (int x = 0; x < output->w; ++x) REQUIRE(row[x] == 0xff4488cc);
    }
}
