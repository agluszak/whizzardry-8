#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "compat/compiler.h"
#include "glm/ext/vector_int2.hpp"
#include "surrender/pixel_math.h"
#include "surrender/srARGB.h"
#include "surrender/srColorSurface.h"
#include "surrender/srFilter.h"
#include "surrender/srPalette.h"
#include "surrender/srPixelConvert.h"
#include "surrender_test_fixture.h"
#include "wiz8/sr_api.h"

void initPixelTables();
void writeRGB24(const srPixelConvert::ConversionInfo&);
void readRGB24(const srPixelConvert::ConversionInfo&);
void writeABGR(const srPixelConvert::ConversionInfo&);
void readABGR(const srPixelConvert::ConversionInfo&);
void writeRGB555(const srPixelConvert::ConversionInfo&);
void readRGB555(const srPixelConvert::ConversionInfo&);
void writeBGRX(const srPixelConvert::ConversionInfo&);
void readBGRX(const srPixelConvert::ConversionInfo&);

TEST_CASE("pixel conversions")
{
    constexpr unsigned count = 65539;
    std::vector<w8_ulong> input(count), rotated(count), output(count);
    for (unsigned i = 0; i < count; ++i) input[i] = (i * 0x9e3779b9u) ^ (i >> 8);
    srPixelConvert::ConversionInfo info{rotated.data(), input.data(), count, nullptr, nullptr};
    writeABGR(info);
    for (unsigned i = 0; i < count; ++i) REQUIRE(rotated[i] == (input[i] << 8 | input[i] >> 24));
    info.source = rotated.data();
    info.dest = output.data();
    readABGR(info);
    REQUIRE(input == output);
    info.source = output.data();
    writeABGR(info);
    REQUIRE(output == rotated);
    readABGR(info);
    REQUIRE(input == output);

    std::vector<unsigned char> rgb(count * 3 + 2, 0xa5);
    info.source = input.data();
    info.dest = rgb.data() + 1;
    writeRGB24(info);
    REQUIRE((rgb.front() == 0xa5 && rgb.back() == 0xa5));
    for (unsigned i = 0; i < count; ++i) {
        REQUIRE(rgb[1 + i * 3] == (input[i] >> 16 & 255));
        REQUIRE(rgb[2 + i * 3] == (input[i] >> 8 & 255));
        REQUIRE(rgb[3 + i * 3] == (input[i] & 255));
    }
    info.source = rgb.data() + 1;
    info.dest = output.data();
    readRGB24(info);
    for (unsigned i = 0; i < count; ++i) REQUIRE(output[i] == (input[i] | 0xff000000));
    info = {output.data(), input.data(), count, nullptr, nullptr};
    writeBGRX(info);
    for (unsigned i = 0; i < count; ++i) REQUIRE(output[i] == (input[i] & 0x00ffffff));
    info.source = output.data();
    readBGRX(info);
    for (unsigned i = 0; i < count; ++i) REQUIRE(output[i] == (input[i] | 0xff000000));
        // BGRX records may start at odd addresses, including exact in-place conversion.
    std::vector<unsigned char> unaligned(count * sizeof(w8_ulong) + 2, 0xa5);
    std::vector<unsigned char> masked(unaligned.size(), 0xa5);
    if (count != 0) std::memcpy(unaligned.data() + 1, input.data(), count * sizeof(w8_ulong));
    info = {masked.data() + 1, unaligned.data() + 1, count, nullptr, nullptr};
    writeBGRX(info);
    REQUIRE((masked.front() == 0xa5 && masked.back() == 0xa5));
    for (unsigned i = 0; i < count; ++i) {
        w8_ulong pixel;
        std::memcpy(&pixel, masked.data() + 1 + i * sizeof(pixel), sizeof(pixel));
        REQUIRE(pixel == (input[i] & 0x00ffffff));
    }
    info = {unaligned.data() + 1, unaligned.data() + 1, count, nullptr, nullptr};
    writeBGRX(info);
    REQUIRE(std::memcmp(unaligned.data(), masked.data(), masked.size()) == 0);
    readBGRX(info);
    REQUIRE((unaligned.front() == 0xa5 && unaligned.back() == 0xa5));
    for (unsigned i = 0; i < count; ++i) {
        w8_ulong pixel;
        std::memcpy(&pixel, unaligned.data() + 1 + i * sizeof(pixel), sizeof(pixel));
        REQUIRE(pixel == (input[i] | 0xff000000));
    }
    info.source = masked.data() + 1;
    readBGRX(info);
    REQUIRE((count == 0 ||
             std::memcmp(unaligned.data() + 1, output.data(), count * sizeof(w8_ulong)) == 0));
    output = input;
    info.source = output.data();
    info.dest = output.data();
    writeRGB24(info);
    REQUIRE(memcmp(output.data(), rgb.data() + 1, count * 3) == 0);
    readRGB24(info);
    for (unsigned i = 0; i < count; ++i) REQUIRE(output[i] == (input[i] | 0xff000000));
        // Zero length must not inspect pointers or SDL formats.
    info = {nullptr, nullptr, 0, nullptr, nullptr};
    writeRGB24(info);
    readRGB24(info);
    writeABGR(info);
    readABGR(info);
    writeBGRX(info);
    readBGRX(info);

    initPixelTables();
    std::vector<unsigned short> packed(65536);
    output.resize(65536);
    for (unsigned i = 0; i < packed.size(); ++i) packed[i] = i;
    info = {output.data(), packed.data(), 65536, nullptr, nullptr};
    readRGB555(info);
    const auto expand = [](unsigned value, unsigned max) {
        return unsigned(value * 255.f * (1.f / max) + .5f);
    };
    for (unsigned i = 0; i < packed.size(); ++i) {
        const auto red = i < 32768 ? expand(i >> 10, 31) : expand((i >> 10) - 32, 63);
        const auto expected =
            0xff000000u | red << 16 | expand(i >> 5 & 31, 31) << 8 | expand(i & 31, 31);
        REQUIRE(output[i] == expected);
    }
    info = {packed.data(), input.data(), 65536, nullptr, nullptr};
    writeRGB555(info);
    const auto reduce = [](unsigned value) { return unsigned(value * (1. / 255.) * 31. + .5); };
    for (unsigned i = 0; i < packed.size(); ++i)
        REQUIRE(packed[i] == (reduce(input[i] >> 16 & 255) << 10 |
                              reduce(input[i] >> 8 & 255) << 5 | reduce(input[i] & 255)));
    std::vector<w8_ulong> sdlOutput(65536);
    info = {output.data(), packed.data(), 65536, nullptr, nullptr};
    readRGB555(info);
    REQUIRE(SDL_ConvertPixels(65536, 1, SDL_PIXELFORMAT_XRGB1555, packed.data(), 65536 * 2,
                              SDL_PIXELFORMAT_ARGB8888, sdlOutput.data(), 65536 * 4));
    REQUIRE(output != sdlOutput); // Rounded channel expansion is not SDL's bit replication.
    REQUIRE(srInit());
    for (auto format : {srPixelConvert::SURFACE_RGB332, srPixelConvert::SURFACE_ARGB1555,
                        srPixelConvert::SURFACE_RGB24, srPixelConvert::SURFACE_ARGB32}) {
        srColorSurface actual(format, 7, 5), expected(format, 7, 5);
        actual.fill(0xff123456);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x) expected.setPixel(x, y, 0xff123456);
        actual.setHLine(2, 9, -2, 0xffabcdef);
        for (int x = 0; x < 7; ++x) expected.setPixel(x, 2, 0xffabcdef);
        actual.setVLine(3, 8, 1, 0xff765432);
        for (int y = 1; y < 5; ++y) expected.setPixel(3, y, 0xff765432);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 7; ++x)
                REQUIRE(actual.getPixelRaw(x, y) == expected.getPixelRaw(x, y));
    }
    REQUIRE(srExit());
    puts("Pixel conversions: exact SDL byte channels, alpha, in-place rotation and exhaustive "
         "custom RGB555 passed");
}
TEST_CASE("SurRender uses shared packed surface operations")
{
    REQUIRE(srInit());
    for (auto format : {srPixelConvert::SURFACE_P8, srPixelConvert::SURFACE_RGB555,
                        srPixelConvert::SURFACE_RGB565, srPixelConvert::SURFACE_RGB24,
                        srPixelConvert::SURFACE_BGRA32}) {
        srColorSurface source(format, 7, 5);
        source.setFilter(nullptr);
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
                    REQUIRE(scaled.getPixelRaw(x, y) ==
                            source.getPixelRaw(x * 7 / width, y * 5 / 9));
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
        explicit VirtualPixels(srPixelConvert::e_surfaceType type = srPixelConvert::SURFACE_BGRA32)
            : srColorSurface(type, 2, 2)
        {
            fill(0xff000000);
        }
        void getPixelRow(w8_ulong* pixels, w8_long, w8_long begin, w8_long end) override
        {
            for (w8_long x = begin; x < end; ++x) pixels[x - begin] = 0xff123456;
        }
    } source;
    srColorSurface destination(srPixelConvert::SURFACE_BGRA32, 2, 2);

    // Both surfaces have identical backing formats. A subclass must still use
    // its virtual pixel interface rather than the base class's raw SDL blit.
    destination.copy(source);
    REQUIRE(destination.getPixel(0, 0) == 0xff123456);
    destination.fill(0xff000000);
    destination.blit(0, 0, source, 0, 0, 2, 2);
    REQUIRE(destination.getPixel(1, 1) == 0xff123456);
    VirtualPixels packed_source(srPixelConvert::SURFACE_RGB565);
    VirtualPixels converted;
    converted.copy(packed_source);
    REQUIRE(converted.getPixel(1, 1) == 0xff123456);
}

TEST_CASE("generic rounded RGB conversion remains exact")
{
    initPixelTables();
    for (auto type : {srPixelConvert::SURFACE_RGB332, srPixelConvert::SURFACE_RGB565,
                      srPixelConvert::SURFACE_ARGB1555, srPixelConvert::SURFACE_RGB444,
                      srPixelConvert::SURFACE_ARGB4444, srPixelConvert::SURFACE_BGR565,
                      srPixelConvert::SURFACE_BGR555, srPixelConvert::SURFACE_BGR24,
                      srPixelConvert::SURFACE_ARGB32, srPixelConvert::SURFACE_RGBA32}) {
        const auto f = srPixelConvert::pixelFormat(type);
        const auto functions = srPixelConvert::selectFuncs(f);
        for (unsigned value = 0; value < 256; ++value) {
            const w8_ulong input = value * 0x01010101u;
            unsigned char storage[6]{};
            srPixelConvert::ConversionInfo info{storage + 1, &input, 1, nullptr, &f};
            functions.write(info);
            const auto reduce = [value](unsigned bits) {
                return unsigned((value * (1. / 255.)) * ((1u << bits) - 1) + .5);
            };
            const w8_ulong expected =
                reduce(f.red_bits) << f.red_shift | reduce(f.green_bits) << f.green_shift |
                reduce(f.blue_bits) << f.blue_shift | reduce(f.alpha_bits) << f.alpha_shift;
            w8_ulong packed = 0, output = 0;
            std::memcpy(&packed, storage + 1, f.pixel_size + 1);
            REQUIRE(packed == expected);
            info.source = storage + 1;
            info.dest = &output;
            functions.read(info);
            const auto expand = [packed](unsigned bits, unsigned shift) {
                if (!bits) return 255u;
                const unsigned max = (1u << bits) - 1;
                return unsigned(((packed >> shift) & max) * 255.f * (1.f / max) + .5f);
            };
            REQUIRE(output ==
                    (expand(f.red_bits, f.red_shift) << 16 |
                     expand(f.green_bits, f.green_shift) << 8 | expand(f.blue_bits, f.blue_shift) |
                     expand(f.alpha_bits, f.alpha_shift) << 24));
        }
    }
}

TEST_CASE("filtered copies preserve SurRender sampling")
{
    REQUIRE(srInit());
    // Golden output from the previous horizontal/vertical implementations.
    uint64_t checksum = 14695981039346656037ull;
    const srPixelConvert::e_surfaceType formats[]{srPixelConvert::SURFACE_BGRA32,
                                                  srPixelConvert::SURFACE_RGB565};
    srFilter* filters[]{&srBellFilter, &srBSplineFilter, &srTriangleFilter};
    for (auto format : formats)
        for (auto* filter : filters)
            for (int clamp = 0; clamp < 4; ++clamp) {
                srColorSurface source(format, 7, 5);
                source.setFilter(filter);
                source.setHClampMode(clamp & 1);
                source.setVClampMode(clamp & 2);
                for (int y = 0; y < 5; ++y)
                    for (int x = 0; x < 7; ++x) source.setPixel(x, y, (x + y * 7) * 0x9e3779b9u);
                for (int width : {1, 3, 7, 9, 14})
                    for (int height : {1, 3, 5, 8, 10}) {
                        srColorSurface output(format, width, height);
                        output.copy(source);
                        for (int y = 0; y < height; ++y)
                            for (int x = 0; x < width; ++x) {
                                const auto pixel = output.getPixelRaw(x, y);
                                for (unsigned shift = 0; shift < 32; shift += 8)
                                    checksum =
                                        (checksum ^ ((pixel >> shift) & 255)) * 1099511628211ull;
                            }
                    }
            }
    REQUIRE(checksum == 16051822824179383716ull);
    REQUIRE(srExit());
}

TEST_CASE_METHOD(SurRenderTestFixture,
                 "Intensity and indexed decoding support unaligned packed storage", "[pixels]")
{
    initPixelTables();
    std::array<srARGB, 256> colors;
    for (unsigned index = 0; index < colors.size(); ++index) {
        colors[index].red = index;
        colors[index].green = 255 - index;
        colors[index].blue = index ^ 0x55;
        colors[index].alpha = index ^ 0xa5;
    }
    srPalette palette(colors.data(), colors.size());
    REQUIRE(palette.matchPalette(colors.data(), colors.size()));
    auto different_alpha = colors;
    different_alpha[17].alpha ^= 1;
    REQUIRE_FALSE(palette.matchPalette(different_alpha.data(), different_alpha.size()));
    REQUIRE_FALSE(palette.matchPalette(colors.data(), colors.size() - 1));
    for (auto type :
         {srPixelConvert::SURFACE_AL44, srPixelConvert::SURFACE_AL88, srPixelConvert::SURFACE_L8,
          srPixelConvert::SURFACE_AP44, srPixelConvert::SURFACE_AP88}) {
        const auto format = srPixelConvert::pixelFormat(type);
        const auto functions = srPixelConvert::selectFuncs(format);
        for (unsigned value = 0; value < 256; ++value) {
            std::array<unsigned char, 6> input{}, output;
            output.fill(0x6a);
            const w8_ulong packed = value * 0x010101u;
            std::memcpy(input.data() + 1, &packed, format.pixel_size + 1);
            srPixelConvert::ConversionInfo info{output.data() + 1, input.data() + 1, 1, &palette,
                                                &format};
            functions.read(info);
            const unsigned mask = (1u << format.red_bits) - 1;
            const unsigned component = (packed >> format.red_shift) & mask;
            const unsigned alpha = format.alpha_bits == 4
                                       ? ((packed >> format.alpha_shift) & 15) * 17
                                       : (packed >> format.alpha_shift) & 255;
            const unsigned gray = format.red_bits == 4 ? component * 17 : component;
            const auto& color = colors[component];
            const w8_ulong rgb =
                format.color_model == srPixelConvert::COLOR_INDEXED
                    ? (w8_ulong(color.red) << 16 | w8_ulong(color.green) << 8 | color.blue)
                    : gray * 0x010101u;
            w8_ulong actual;
            std::memcpy(&actual, output.data() + 1, sizeof(actual));
            REQUIRE(actual == (rgb | (format.alpha_bits ? alpha : 255u) << 24));
            REQUIRE(output.front() == 0x6a);
            REQUIRE(output.back() == 0x6a);
        }
    }
}

TEST_CASE("Mipmap averages retain per-input floor and four-sample ceiling")
{
    REQUIRE(srPixels::average(0xffffffffu, 0x01010101u) == 0x7f7f7f7fu);
    REQUIRE(srPixels::average(0u, 0u, 0u, 0xfefefefeu) == 0x40404040u);
    REQUIRE(srPixels::average(0x0004080cu, 0x10182028u, 0x30384048u, 0x50586068u) == 0x242b3239u);
}

TEST_CASE("Inherited surface lines dispatch through clipped rectangle filling")
{
    REQUIRE(srInit());
    {
        struct RecordingSurface : srColorSurface {
            RecordingSurface() : srColorSurface(srPixelConvert::SURFACE_ARGB32, 7, 5) {}
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
        srColorSurface surface(srPixelConvert::SURFACE_RGB24, 3, 2);
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
