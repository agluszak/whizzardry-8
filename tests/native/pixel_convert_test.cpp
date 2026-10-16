#include <array>
#include <limits>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <catch2/catch_test_macros.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_int2.hpp"
#include "surrender/srColorSurface.h"
#include "surrender_test_fixture.h"
#include "wiz8/sr_api.h"

TEST_CASE("SurRender uses shared packed surface operations")
{
    REQUIRE(srInit());
    for (auto format : {SDL_PIXELFORMAT_INDEX8, SDL_PIXELFORMAT_XRGB1555, SDL_PIXELFORMAT_RGB565,
                        SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_BGRA32}) {
        srColorSurface source(format, 7, 5);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) source.setPixelRaw(x, y, (x + y * 7) * 0x1234567u);
        srColorSurface clone(source);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                REQUIRE(clone.getPixelRaw(x, y) == source.getPixelRaw(x, y));
        clone.flipHorizontal();
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                REQUIRE(clone.getPixelRaw(x, y) == source.getPixelRaw(6 - x, y));
        clone = source;
        clone.flipRectangle({5, 4, 1, 1});
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) {
                const bool inside = x >= 1 && x < 5 && y >= 1 && y < 4;
                REQUIRE(clone.getPixelRaw(x, y) ==
                        source.getPixelRaw(inside ? 5 - x : x, inside ? 4 - y : y));
            }
        for (int width : {3, 7, 14, 17}) {
            srColorSurface scaled(format, width, 9);
            scaled.copy(source);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < width; ++x)
                    REQUIRE(
                        scaled.getPixelRaw(x, y) ==
                        source.getPixelRaw((2 * x + 1) * 7 / (2 * width), (2 * y + 1) * 5 / 18));
        }
        for (int dx : {-2, 0, 2})
            for (int dy : {-2, 0, 2}) {
                clone = source;
                clone.blit(dx, dy, clone, 0, 0, 7, 5);
                for (int y = 0; y < 5; ++y)
                    for (int x = 0; x < 7; ++x) {
                        const int sx = x - dx, sy = y - dy;
                        const bool copied = sx >= 0 && sx < 7 && sy >= 0 && sy < 5;
                        REQUIRE(clone.getPixelRaw(x, y) ==
                                source.getPixelRaw(copied ? sx : x, copied ? sy : y));
                    }
            }
    }
    REQUIRE(srExit());
}

TEST_CASE("Surface copies and blits respect virtual pixels across dynamic types")
{
    SurRenderTestFixture runtime;
    struct VirtualPixels : srColorSurface {
        explicit VirtualPixels(SDL_PixelFormat type = SDL_PIXELFORMAT_BGRA32)
            : srColorSurface(type, 2, 2)
        {
            fill(0xff000000);
        }
        void getPixelRow(w8_ulong* pixels, w8_long, w8_long begin, w8_long end) override
        {
            for (w8_long x = begin; x < end; ++x) pixels[x - begin] = 0xff123456;
        }
    } source;
    srColorSurface destination(SDL_PIXELFORMAT_BGRA32, 2, 2);

    // Both surfaces have identical backing formats. A subclass must still use
    // its virtual pixel interface rather than the base class's raw SDL blit.
    destination.copy(source);
    REQUIRE(destination.getPixel(0, 0) == 0xff123456);
    destination.fill(0xff000000);
    destination.blit(0, 0, source, 0, 0, 2, 2);
    REQUIRE(destination.getPixel(1, 1) == 0xff123456);
    VirtualPixels packed_source(SDL_PIXELFORMAT_RGB565);
    VirtualPixels converted;
    converted.copy(packed_source);
    REQUIRE(converted.getPixelRaw(1, 1) == 0xff123456);
    VirtualPixels converted_back(SDL_PIXELFORMAT_RGB565);
    converted_back.copy(source);
    srColorSurface expected(SDL_PIXELFORMAT_RGB565, 2, 2);
    expected.fill(0xff123456);
    REQUIRE(converted_back.getPixelRaw(1, 1) == expected.getPixelRaw(1, 1));
}

TEST_CASE("Inherited surface lines dispatch through clipped rectangle filling")
{
    REQUIRE(srInit());
    {
        struct RecordingSurface : srColorSurface {
            RecordingSurface() : srColorSurface(SDL_PIXELFORMAT_ARGB32, 7, 5) {}
            void* getDataPtr() override { return nullptr; }
            void setPixel(w8_long x, w8_long y, w8_ulong) override { writes.emplace_back(x, y); }
            std::vector<glm::ivec2> writes;
        } surface;
        surface.setHLine(1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 0);
        REQUIRE(surface.writes.size() == 7);
        for (int x = 0; x < 7; ++x) REQUIRE(surface.writes[x] == glm::ivec2(x, 1));
        surface.writes.clear();
        surface.setVLine(2, std::numeric_limits<int>::max(), std::numeric_limits<int>::min(), 0);
        REQUIRE(surface.writes.size() == 5);
        for (int y = 0; y < 5; ++y) REQUIRE(surface.writes[y] == glm::ivec2(2, y));
        surface.writes.clear();
        surface.setHLine(5, 0, 7, 0);
        surface.setVLine(-1, 0, 5, 0);
        surface.setHLine(2, 3, 3, 0);
        surface.setVLine(1, -5, -2, 0);
        REQUIRE(surface.writes.empty());
        surface.fillRectangle({-2, -1, 5, 3}, 0);
        const std::vector<glm::ivec2> expected{{0, 0}, {1, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}};
        REQUIRE(surface.writes == expected);
    }
    REQUIRE(srExit());
}

TEST_CASE("Raw pixel row views expose packed bytes without pitch padding")
{
    REQUIRE(srInit());
    {
        srColorSurface surface(SDL_PIXELFORMAT_RGB24, 3, 2);
        surface.fill(0xff123456);
        auto row = surface.rawPixelRow(1, 1, 3);
        REQUIRE(row.size() == 6);
        row[0] ^= 0x11;
        std::array<unsigned char, 6> copied;
        surface.getPixelRowRaw(copied.data(), 1, 1, 3);
        REQUIRE(copied[0] == row[0]);
        copied[5] ^= 0x22;
        surface.setPixelRowRaw(copied.data(), 1, 1, 3);
        REQUIRE(copied[5] == row[5]);
        REQUIRE(surface.rawPixelRow(-1, 0, 1).empty());
        REQUIRE(surface.rawPixelRow(0, -1, 1).empty());
        REQUIRE(surface.rawPixelRow(0, 2, 4).empty());
        REQUIRE(surface.rawPixelRow(0, 2, 2).empty());
    }
    REQUIRE(srExit());
}
