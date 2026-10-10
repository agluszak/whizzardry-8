#include "surrender/srPixelConvert.h"
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

void initPixelTables();
void writeRGB24(const srPixelConvert::ConversionInfo&);
void readRGB24(const srPixelConvert::ConversionInfo&);
void writeABGR(const srPixelConvert::ConversionInfo&);
void readABGR(const srPixelConvert::ConversionInfo&);
void writeRGB555(const srPixelConvert::ConversionInfo&);
void readRGB555(const srPixelConvert::ConversionInfo&);

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (0)

int main()
{
    try
    {
        constexpr unsigned count = 65539;
        std::vector<w8_ulong> input(count), rotated(count), output(count);
        for (unsigned i = 0; i < count; ++i)
            input[i] = (i * 0x9e3779b9u) ^ (i >> 8);
        srPixelConvert::ConversionInfo info{rotated.data(), input.data(), count, nullptr, nullptr};
        writeABGR(info);
        for (unsigned i = 0; i < count; ++i)
            CHECK(rotated[i] == (input[i] << 8 | input[i] >> 24));
        info.source = rotated.data();
        info.dest = output.data();
        readABGR(info);
        CHECK(input == output);
        info.source = output.data();
        writeABGR(info);
        CHECK(output == rotated);
        readABGR(info);
        CHECK(input == output);

        std::vector<unsigned char> rgb(count * 3 + 2, 0xa5);
        info.source = input.data();
        info.dest = rgb.data() + 1;
        writeRGB24(info);
        CHECK(rgb.front() == 0xa5 && rgb.back() == 0xa5);
        for (unsigned i = 0; i < count; ++i)
        {
            CHECK(rgb[1 + i * 3] == (input[i] >> 16 & 255));
            CHECK(rgb[2 + i * 3] == (input[i] >> 8 & 255));
            CHECK(rgb[3 + i * 3] == (input[i] & 255));
        }
        info.source = rgb.data() + 1;
        info.dest = output.data();
        readRGB24(info);
        for (unsigned i = 0; i < count; ++i)
            CHECK(output[i] == (input[i] | 0xff000000));
        output = input;
        info.source = output.data();
        info.dest = output.data();
        writeRGB24(info);
        CHECK(memcmp(output.data(), rgb.data() + 1, count * 3) == 0);
        readRGB24(info);
        for (unsigned i = 0; i < count; ++i)
            CHECK(output[i] == (input[i] | 0xff000000));
        // Zero length must not inspect pointers or SDL formats.
        info = {nullptr, nullptr, 0, nullptr, nullptr};
        writeRGB24(info);
        readRGB24(info);
        writeABGR(info);
        readABGR(info);

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
        for (unsigned i = 0; i < packed.size(); ++i)
        {
            const auto red = i < 32768 ? expand(i >> 10, 31) : expand((i >> 10) - 32, 63);
            const auto expected = 0xff000000u | red << 16 | expand(i >> 5 & 31, 31) << 8 |
                                  expand(i & 31, 31);
            CHECK(output[i] == expected);
        }
        info = {packed.data(), input.data(), 65536, nullptr, nullptr};
        writeRGB555(info);
        const auto reduce = [](unsigned value) { return unsigned(value * (1. / 255.) * 31. + .5); };
        for (unsigned i = 0; i < packed.size(); ++i)
            CHECK(packed[i] == (reduce(input[i] >> 16 & 255) << 10 |
                                reduce(input[i] >> 8 & 255) << 5 | reduce(input[i] & 255)));
        std::vector<w8_ulong> sdlOutput(65536);
        info = {output.data(), packed.data(), 65536, nullptr, nullptr};
        readRGB555(info);
        CHECK(SDL_ConvertPixels(65536, 1, SDL_PIXELFORMAT_XRGB1555, packed.data(), 65536 * 2,
                                SDL_PIXELFORMAT_ARGB8888, sdlOutput.data(), 65536 * 4));
        CHECK(output != sdlOutput); // Rounded channel expansion is not SDL's bit replication.
        puts("Pixel conversions: exact SDL byte channels, alpha, in-place rotation and exhaustive custom RGB555 passed");
        return 0;
    }
    catch (const std::exception& failure)
    {
        fprintf(stderr, "pixel conversion: %s\n", failure.what());
        return 1;
    }
}
