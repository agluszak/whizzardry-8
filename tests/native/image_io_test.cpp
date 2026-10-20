#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_surface.h>
#include <catch2/catch_test_macros.hpp>

#include "surface_pixels.h"
#include "wiz8/platform/sdl_io.h"
#include "wiz8/render/image_io.h"
#include "wiz8/surface_storage.h"

static const std::uint32_t colors[] = {0xff123456, 0xffabcdef, 0xff2468ac, 0xfffedcba};

static void CheckImage(const char* name, const std::vector<unsigned char>& bytes,
                       const std::uint32_t* expected = colors)
{
    auto stream =
        sdlIO::own(SDL_IOFromConstMem(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    auto surface = imageIO::load(name, stream.get());
    REQUIRE((surface && surface->w == 2 && surface->h == 2));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) REQUIRE(ReadARGB(*surface, x, y) == expected[y * 2 + x]);
}

static std::vector<unsigned char> Targa(bool top, bool rle, bool alpha)
{
    std::vector<unsigned char> bytes(18);
    bytes[2] = rle ? 10 : 2;
    bytes[12] = bytes[14] = 2;
    bytes[16] = alpha ? 32 : 24;
    bytes[17] = (top ? 32 : 0) | (alpha ? 8 : 0);
    if (rle) bytes.push_back(3); // Four raw pixels in one RLE packet.
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            auto color = colors[(top ? y : 1 - y) * 2 + x];
            for (int channel = 0; channel < (alpha ? 4 : 3); ++channel)
                bytes.push_back(static_cast<unsigned char>(color >> (channel * 8)));
        }
    return bytes;
}

static std::vector<unsigned char> Bitmap()
{
    std::vector<unsigned char> bytes(70);
    bytes[0] = 'B';
    bytes[1] = 'M';
    bytes[2] = 70;
    bytes[10] = 54;
    bytes[14] = 40;
    bytes[18] = bytes[22] = 2;
    bytes[26] = 1;
    bytes[28] = 24;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            for (int channel = 0; channel < 3; ++channel)
                bytes[54 + y * 8 + x * 3 + channel] =
                    static_cast<unsigned char>(colors[(1 - y) * 2 + x] >> (channel * 8));
    return bytes;
}

static std::vector<unsigned char> PCX(bool indexed)
{
    std::vector<unsigned char> bytes(128);
    bytes[0] = 10;
    bytes[1] = 5;
    bytes[2] = 1;
    bytes[3] = 8;
    bytes[8] = bytes[10] = 1;
    bytes[65] = indexed ? 1 : 3;
    bytes[66] = 2;
    for (int y = 0; y < 2; ++y)
        for (int channel = 0; channel < (indexed ? 1 : 3); ++channel)
            for (int x = 0; x < 2; ++x) {
                auto value =
                    indexed ? static_cast<unsigned char>(y * 2 + x)
                            : static_cast<unsigned char>(colors[y * 2 + x] >> ((2 - channel) * 8));
                if (value >= 192) bytes.push_back(193);
                bytes.push_back(value);
            }
    if (indexed) {
        bytes.push_back(12);
        for (int entry = 0; entry < 256; ++entry)
            for (int channel = 0; channel < 3; ++channel)
                bytes.push_back(
                    entry < 4 ? static_cast<unsigned char>(colors[entry] >> ((2 - channel) * 8))
                              : 0);
    }
    return bytes;
}

static void CheckJPEG()
{
    auto source = CreateCpuStorage(16, 16, SDL_PIXELFORMAT_RGB565);
    SDL_FillSurfaceRect(source.get(), nullptr, SDL_MapSurfaceRGB(source.get(), 248, 32, 16));
    const auto original = ReadARGB(*source, 0, 0);
    auto output = sdlIO::own(SDL_IOFromDynamicMem());
    imageIO::save("screenshot.JPG", output.get(), *source);
    REQUIRE(((SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY) &&
             SDL_GetIOSize(output.get()) > 2));
    REQUIRE((static_cast<unsigned char*>(SDL_GetPointerProperty(
                 SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER,
                 nullptr))[0] == 255 &&
             static_cast<unsigned char*>(SDL_GetPointerProperty(
                 SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER,
                 nullptr))[1] == 216));
    auto input = sdlIO::own(SDL_IOFromConstMem(
        SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                               SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
        SDL_GetIOSize(output.get())));
    auto decoded = imageIO::load("screenshot.jpeg", input.get());
    REQUIRE((decoded && decoded->w == 16 && decoded->h == 16));
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            auto pixel = ReadARGB(*decoded, x, y);
            REQUIRE(abs(int((pixel >> 16) & 255) - int((original >> 16) & 255)) <= 4);
            REQUIRE(abs(int((pixel >> 8) & 255) - int((original >> 8) & 255)) <= 4);
            REQUIRE(abs(int(pixel & 255) - int(original & 255)) <= 4);
        }
}

static void CheckLossless(const char* name)
{
    auto source = CreateCpuStorage(2, 2, SDL_PIXELFORMAT_BGRA32);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) WriteARGB(*source, x, y, colors[y * 2 + x]);
    auto output = sdlIO::own(SDL_IOFromDynamicMem());
    imageIO::save(name, output.get(), *source);
    REQUIRE(
        ((SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY) && SDL_GetIOSize(output.get())));
    const auto* bytes = static_cast<const unsigned char*>(SDL_GetPointerProperty(
        SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    CheckImage(name, {bytes, bytes + SDL_GetIOSize(output.get())});
    auto input = sdlIO::own(SDL_IOFromConstMem(
        SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                               SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
        SDL_GetIOSize(output.get())));
    unsigned char signature = 0;
    SDL_SeekIO(input.get(), 0, SDL_IO_SEEK_SET);
    sdlIO::readExact(input.get(), &signature, 1);
    REQUIRE((SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_READY));
    if (strstr(name, "PNG")) {
        REQUIRE(signature == 0x89);
        CheckImage("without-extension", {bytes, bytes + SDL_GetIOSize(output.get())});
    } else {
        REQUIRE(signature == 'B');
    }
}

static void CheckErrors()
{
    auto source = CreateCpuStorage(2, 2, SDL_PIXELFORMAT_BGR24);
    for (const char* path : {static_cast<const char*>(nullptr), "", "image.pcx", "image",
                             "dir.png/image", "dir\\name.jpg\\image"}) {
        auto output = sdlIO::own(SDL_IOFromDynamicMem());
        bool rejected = false;
        try {
            imageIO::save(path, output.get(), *source);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        REQUIRE((rejected && SDL_TellIO(output.get()) == 0 && SDL_GetIOSize(output.get()) == 0));
    }
    for (const char* path : {static_cast<const char*>(nullptr), ""}) {
        auto bytes = Bitmap();
        auto input = sdlIO::own(SDL_IOFromConstMem(bytes.data(), bytes.size()));
        bool rejected = false;
        try {
            imageIO::load(path, input.get());
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        REQUIRE((rejected && SDL_TellIO(input.get()) == 0));
    }
}

TEST_CASE("image_io_test")
{
    {
        for (bool top : {false, true})
            for (bool rle : {false, true}) CheckImage("test.TGA", Targa(top, rle, false));
        auto alpha = Targa(true, false, true);
        alpha[21] = 64;
        std::uint32_t transparent[] = {0x40123456, colors[1], colors[2], colors[3]};
        CheckImage("alpha.tga", alpha, transparent);
        CheckImage("test.bmp", Bitmap());
        CheckImage("test.pcx", PCX(false));
        CheckImage("palette.pcx", PCX(true));
        // Indexed TGA files without a palette use the retail color cube and gray ramp.
        // The decoded surface must retain that palette after the decoder returns.
        std::vector<unsigned char> indexed(18);
        indexed[2] = 1;
        indexed[12] = indexed[14] = 2;
        indexed[16] = 8;
        indexed[17] = 0x20;
        indexed.insert(indexed.end(), {0, 209, 210, 255});
        const std::uint32_t indexed_colors[]{0xff000033, 0xffffffcc, 0xff000000, 0xffffffff};
        CheckImage("default-palette.tga", indexed, indexed_colors);
        CheckJPEG();
        CheckLossless("dir.jpg/lossless.PNG");
        CheckLossless("lossless.BMP");
        CheckErrors();
        unsigned char corrupt[] = {1, 2, 3};
        for (const char* name : {"bad.jpg", "bad.tga", "bad.bmp", "bad.pcx"}) {
            auto stream = sdlIO::own(SDL_IOFromConstMem(corrupt, sizeof(corrupt)));
            REQUIRE(!imageIO::load(name, stream.get()));
        }
    }
    puts("ok: direct image loading and JPEG export without renderer initialization");
}
