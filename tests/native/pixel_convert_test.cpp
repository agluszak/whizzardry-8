#include "surrender/pixel_math.h"
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_pixels.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <array>
#include "surrender/srPalette.h"
#include "surrender/srARGB.h"
#include "surrender_test_fixture.h"
#include <cstdint>

#include "surrender/srPixelConvert.h"
#include "surrender/srColorSurface.h"
#include "wiz8/sr_api.h"
#include "compat/compiler.h"
#include "surrender/srFilter.h"

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
        for (unsigned i = 0; i < count; ++i)
            input[i] = (i * 0x9e3779b9u) ^ (i >> 8);
        srPixelConvert::ConversionInfo info{rotated.data(), input.data(), count, nullptr, nullptr};
        writeABGR(info);
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(rotated[i] == (input[i] << 8 | input[i] >> 24));
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
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(output[i] == (input[i] | 0xff000000));
        info = {output.data(), input.data(), count, nullptr, nullptr};
        writeBGRX(info);
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(output[i] == (input[i] & 0x00ffffff));
        info.source = output.data();
        readBGRX(info);
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(output[i] == (input[i] | 0xff000000));
        // BGRX records may start at odd addresses, including exact in-place conversion.
        std::vector<unsigned char> unaligned(count * sizeof(w8_ulong) + 2, 0xa5);
        std::vector<unsigned char> masked(unaligned.size(), 0xa5);
        if (count != 0)
            std::memcpy(unaligned.data() + 1, input.data(), count * sizeof(w8_ulong));
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
        for (unsigned i = 0; i < count; ++i)
            REQUIRE(output[i] == (input[i] | 0xff000000));
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
        for (unsigned i = 0; i < packed.size(); ++i)
            packed[i] = i;
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
                for (int x = 0; x < 7; ++x)
                    expected.setPixel(x, y, 0xff123456);
            actual.setHLine(2, 9, -2, 0xffabcdef);
            for (int x = 0; x < 7; ++x)
                expected.setPixel(x, 2, 0xffabcdef);
            actual.setVLine(3, 8, 1, 0xff765432);
            for (int y = 1; y < 5; ++y)
                expected.setPixel(3, y, 0xff765432);
            for (int y = 0; y < 5; ++y)
                for (int x = 0; x < 7; ++x)
                    REQUIRE(actual.getPixelRaw(x, y) == expected.getPixelRaw(x, y));
        }
        REQUIRE(srExit());
        puts("Pixel conversions: exact SDL byte channels, alpha, in-place rotation and exhaustive "
             "custom RGB555 passed");
        }

TEST_CASE("pixel format names are owned values")
{
    auto format = srPixelConvert::pixelFormat(srPixelConvert::SURFACE_RGB565);
    REQUIRE(format.getName() == "RGB565/16");

    format.fourcc = 0x34333231;
    REQUIRE(format.getName() == "1234");
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
            for (int x = 0; x < 7; ++x)
                source.setPixelRaw(x, y, (x + y * 7) * 0x1234567u);
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
                REQUIRE(clone.getPixelRaw(x, y) == source.getPixelRaw(inside ? 5 - x : x,
                                                                    inside ? 4 - y : y));
            }
        for (int width : {3, 7, 14, 17}) {
            srColorSurface scaled(format, width, 9);
            scaled.copy(source);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < width; ++x)
                    REQUIRE(scaled.getPixelRaw(x, y) == source.getPixelRaw(x * 7 / width, y * 5 / 9));
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
            const w8_ulong expected = reduce(f.red_bits) << f.red_shift |
                reduce(f.green_bits) << f.green_shift | reduce(f.blue_bits) << f.blue_shift |
                reduce(f.alpha_bits) << f.alpha_shift;
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
            REQUIRE(output == (expand(f.red_bits, f.red_shift) << 16 |
                               expand(f.green_bits, f.green_shift) << 8 |
                               expand(f.blue_bits, f.blue_shift) |
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
                    for (int x = 0; x < 7; ++x)
                        source.setPixel(x, y, (x + y * 7) * 0x9e3779b9u);
                for (int width : {1, 3, 7, 9, 14})
                    for (int height : {1, 3, 5, 8, 10}) {
                        srColorSurface output(format, width, height);
                        output.copy(source);
                        for (int y = 0; y < height; ++y)
                            for (int x = 0; x < width; ++x) {
                                const auto pixel = output.getPixelRaw(x, y);
                                for (unsigned shift = 0; shift < 32; shift += 8)
                                    checksum = (checksum ^ ((pixel >> shift) & 255)) * 1099511628211ull;
                            }
                    }
            }
    REQUIRE(checksum == 16051822824179383716ull);
    REQUIRE(srExit());
}

TEST_CASE("Pixel format lookup returns complete independent values")
{
    for (int index = 0; index < 25; ++index) {
        const auto type = static_cast<srPixelConvert::e_surfaceType>(index);
        auto format = srPixelConvert::pixelFormat(type);
        REQUIRE(srPixelConvert::mapPixelFormat(format) == type);
        format.fourcc = 0x34333231;
        REQUIRE(srPixelConvert::pixelFormat(type).fourcc == 0);
    }
    REQUIRE(srPixelConvert::pixelFormat(srPixelConvert::SURFACE_INVALID) ==
            srPixelConvert::pixelFormat(srPixelConvert::SURFACE_BGRA32));
}

TEST_CASE("YUV conversion preserves scatter destinations and untouched storage")
{
    initPixelTables();
    const std::array<w8_ulong, 8> input{0, 0x00ff0000, 0x0000ff00, 0x000000ff,
                                       0xffffffff, 0x7f123456, 0x80010203, 0x01000000};
    const std::array formats{srPixelConvert::SURFACE_Y4U2V2, srPixelConvert::SURFACE_A8Y4U2V2};
    // Full-buffer checksums from the previous conversion bodies, including untouched bytes.
    const std::array<uint64_t, 2> expected{8152149035273351469ull, 13038364476061159287ull};
    for (unsigned index = 0; index < formats.size(); ++index) {
        const auto format = srPixelConvert::pixelFormat(formats[index]);
        const auto functions = srPixelConvert::selectFuncs(format);
        std::array<unsigned char, 2048> encoded, decoded;
        encoded.fill(0x6a);
        decoded.fill(0x6a);
        srPixelConvert::ConversionInfo info{encoded.data(), input.data(), input.size(), nullptr, &format};
        functions.write(info);
        info = {decoded.data(), encoded.data(), input.size(), nullptr, &format};
        functions.read(info);
        uint64_t checksum = 14695981039346656037ull;
        for (auto byte : encoded) checksum = (checksum ^ byte) * 1099511628211ull;
        for (auto byte : decoded) checksum = (checksum ^ byte) * 1099511628211ull;
        REQUIRE(checksum == expected[index]);
    }
}

TEST_CASE_METHOD(SurRenderTestFixture, "Intensity and indexed decoding support unaligned packed storage", "[pixels]")
{
    initPixelTables();
    std::array<srARGB, 256> colors;
    for (unsigned index = 0; index < colors.size(); ++index) {
        colors[index].red = index;
        colors[index].green = 255 - index;
        colors[index].blue = index ^ 0x55;
        colors[index].alpha = 0;
    }
    srPalette palette(colors.data(), colors.size());
    for (auto type : {srPixelConvert::SURFACE_AL44, srPixelConvert::SURFACE_AL88,
                      srPixelConvert::SURFACE_L8, srPixelConvert::SURFACE_AP44,
                      srPixelConvert::SURFACE_AP88}) {
        const auto format = srPixelConvert::pixelFormat(type);
        const auto functions = srPixelConvert::selectFuncs(format);
        for (unsigned value = 0; value < 256; ++value) {
            std::array<unsigned char, 6> input{}, output;
            output.fill(0x6a);
            const w8_ulong packed = value * 0x010101u;
            std::memcpy(input.data() + 1, &packed, format.pixel_size + 1);
            srPixelConvert::ConversionInfo info{output.data() + 1, input.data() + 1, 1, &palette, &format};
            functions.read(info);
            const unsigned mask = (1u << format.red_bits) - 1;
            const unsigned component = (packed >> format.red_shift) & mask;
            const unsigned alpha = format.alpha_bits == 4 ? ((packed >> format.alpha_shift) & 15) * 17
                                                          : (packed >> format.alpha_shift) & 255;
            const unsigned gray = format.red_bits == 4 ? component * 17 : component;
            const auto& color = colors[component];
            const w8_ulong rgb = format.color_model == srPixelConvert::COLOR_INDEXED
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
    REQUIRE(srPixels::average(0x0004080cu, 0x10182028u, 0x30384048u, 0x50586068u) ==
            0x242b3239u);
}

TEST_CASE("Pixel format matching retains shortfall, channel and fallback policies")
{
    using Converter = srPixelConvert;
    const auto rgb565 = Converter::pixelFormat(Converter::SURFACE_RGB565);
    const auto rgb555 = Converter::pixelFormat(Converter::SURFACE_RGB555);
    const auto rgb24 = Converter::pixelFormat(Converter::SURFACE_RGB24);
    const auto rgba32 = Converter::pixelFormat(Converter::SURFACE_RGBA32);
    REQUIRE_FALSE(rgb565.match({}));
    std::array formats{rgb555, rgb24, rgb565};
    REQUIRE(rgb565.match(formats) == 2); // extra bits are free, fewer bytes win the tie
    formats = {rgb555, rgb24, rgb24};
    REQUIRE(rgb565.match(formats) == 1); // equal scores retain the first candidate
    formats = {rgb555, rgb565, rgb555};
    REQUIRE(rgb24.match(formats) == 1); // smaller channel shortfall wins
    const std::array with_alpha{rgb24, rgba32};
    REQUIRE(rgba32.match(with_alpha) == 1);
    const std::array without_alpha{rgb565, rgb24};
    REQUIRE(rgba32.match(without_alpha) == 1); // no complete channels: widest fallback
    auto indexed = rgb24;
    indexed.color_model = Converter::COLOR_INDEXED;
    const std::array wrong_class{indexed, rgb565};
    REQUIRE(rgb565.match(wrong_class) == 1);
    auto fourcc = rgba32;
    fourcc.fourcc = 123;
    auto exact_fourcc = indexed;
    exact_fourcc.fourcc = 123;
    const std::array compressed{rgba32, exact_fourcc};
    REQUIRE(fourcc.match(compressed) == 1); // FOURCC match precedes class/channel scoring
    const std::array single{indexed};
    REQUIRE(rgb565.match(single) == 0); // retain the sole candidate policy
}
