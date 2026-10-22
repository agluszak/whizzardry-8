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
#include <SDL3_image/SDL_image.h>
#include <catch2/catch_test_macros.hpp>

#include "surface_pixels.h"
#include "wiz8/platform/sdl_io.h"
#include "wiz8/render/image_io.h"
#include "wiz8/surface_storage.h"

static const std::uint32_t colors[] = {0xff123456, 0xffabcdef, 0xff2468ac, 0xfffedcba};

static void CheckPixels(const SDLSurfaceOwner& surface, const std::uint32_t* expected = colors)
{
    REQUIRE((surface && surface->w == 2 && surface->h == 2));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) REQUIRE(ReadARGB(*surface, x, y) == expected[y * 2 + x]);
}

static SDLSurfaceOwner LoadTga(const std::vector<unsigned char>& bytes)
{
    auto stream = sdlIO::own(SDL_IOFromConstMem(bytes.data(), bytes.size()));
    return imageIO::loadTga(stream.get());
}

// Production only writes screenshots; decode them with SDL_image to check the output.
static SDLSurfaceOwner DecodeWritten(SDL_IOStream* output)
{
    auto input = sdlIO::own(SDL_IOFromConstMem(
        SDL_GetPointerProperty(SDL_GetIOProperties(output),
                               SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
        SDL_GetIOSize(output)));
    SDLSurfaceOwner decoded(IMG_Load_IO(input.get(), false), SDL_DestroySurface);
    if (!decoded) return decoded;
    return {SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32), SDL_DestroySurface};
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
    auto decoded = DecodeWritten(output.get());
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
    CheckPixels(DecodeWritten(output.get()));
    auto input = sdlIO::own(SDL_IOFromConstMem(
        SDL_GetPointerProperty(SDL_GetIOProperties(output.get()),
                               SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr),
        SDL_GetIOSize(output.get())));
    unsigned char signature = 0;
    SDL_SeekIO(input.get(), 0, SDL_IO_SEEK_SET);
    sdlIO::readExact(input.get(), &signature, 1);
    REQUIRE((SDL_GetIOStatus(input.get()) == SDL_IO_STATUS_READY));
    REQUIRE(signature == (strstr(name, "PNG") ? 0x89 : 'B'));
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
}

TEST_CASE("image_io_test")
{
    {
        for (bool top : {false, true})
            for (bool rle : {false, true}) CheckPixels(LoadTga(Targa(top, rle, false)));
        auto alpha = Targa(true, false, true);
        alpha[21] = 64;
        std::uint32_t transparent[] = {0x40123456, colors[1], colors[2], colors[3]};
        CheckPixels(LoadTga(alpha), transparent);
        // Indexed TGA files without a palette use the retail color cube and gray ramp.
        // The decoded surface must retain that palette after the decoder returns.
        std::vector<unsigned char> indexed(18);
        indexed[2] = 1;
        indexed[12] = indexed[14] = 2;
        indexed[16] = 8;
        indexed[17] = 0x20;
        indexed.insert(indexed.end(), {0, 209, 210, 255});
        const std::uint32_t indexed_colors[]{0xff000033, 0xffffffcc, 0xff000000, 0xffffffff};
        CheckPixels(LoadTga(indexed), indexed_colors);
        CheckJPEG();
        CheckLossless("dir.jpg/lossless.PNG");
        CheckLossless("lossless.BMP");
        CheckErrors();
        REQUIRE(!LoadTga({1, 2, 3}));
    }
    puts("ok: TGA decoding and screenshot export without renderer initialization");
}
