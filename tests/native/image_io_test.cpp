#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_properties.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <memory>

#include "surrender/srIO.h"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srImageIO.h"
#include "wiz8/sr_api.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srPixelConvert.h"


static const std::uint32_t colors[] = {0xff123456, 0xffabcdef, 0xff2468ac, 0xfffedcba};

static void CheckImage(const char* name, const std::vector<std::uint8_t>& bytes,
                       const std::uint32_t* expected = colors)
{
    auto stream = srIO::own(SDL_IOFromConstMem(bytes.data(), static_cast<std::uint32_t>(bytes.size())));
    auto surface = srImage::load(name, stream.get());
    REQUIRE((surface && surface->getWidth() == 2 && surface->getHeight() == 2));
    for (std::int32_t y = 0; y < 2; ++y)
        for (std::int32_t x = 0; x < 2; ++x)
            REQUIRE(surface->getPixel(x, y) == expected[y * 2 + x]);
}

static std::vector<std::uint8_t> Targa(bool top, bool rle, bool alpha)
{
    std::vector<std::uint8_t> bytes(18);
    bytes[2] = rle ? 10 : 2;
    bytes[12] = bytes[14] = 2;
    bytes[16] = alpha ? 32 : 24;
    bytes[17] = (top ? 32 : 0) | (alpha ? 8 : 0);
    if (rle) bytes.push_back(3); // Four raw pixels in one RLE packet.
    for (std::int32_t y = 0; y < 2; ++y)
        for (std::int32_t x = 0; x < 2; ++x) {
            auto color = colors[(top ? y : 1 - y) * 2 + x];
            for (std::int32_t channel = 0; channel < (alpha ? 4 : 3); ++channel)
                bytes.push_back(static_cast<std::uint8_t>(color >> (channel * 8)));
        }
    return bytes;
}

static std::vector<std::uint8_t> Bitmap()
{
    std::vector<std::uint8_t> bytes(70);
    bytes[0] = 'B'; bytes[1] = 'M'; bytes[2] = 70; bytes[10] = 54;
    bytes[14] = 40; bytes[18] = bytes[22] = 2; bytes[26] = 1; bytes[28] = 24;
    for (std::int32_t y = 0; y < 2; ++y)
        for (std::int32_t x = 0; x < 2; ++x)
            for (std::int32_t channel = 0; channel < 3; ++channel)
                bytes[54 + y * 8 + x * 3 + channel] =
                    static_cast<std::uint8_t>(colors[(1 - y) * 2 + x] >> (channel * 8));
    return bytes;
}

static std::vector<std::uint8_t> PCX(bool indexed)
{
    std::vector<std::uint8_t> bytes(128);
    bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = 8;
    bytes[8] = bytes[10] = 1;
    bytes[65] = indexed ? 1 : 3;
    bytes[66] = 2;
    for (std::int32_t y = 0; y < 2; ++y)
        for (std::int32_t channel = 0; channel < (indexed ? 1 : 3); ++channel)
            for (std::int32_t x = 0; x < 2; ++x) {
                auto value = indexed ? static_cast<std::uint8_t>(y * 2 + x) :
                    static_cast<std::uint8_t>(colors[y * 2 + x] >> ((2 - channel) * 8));
                if (value >= 192) bytes.push_back(193);
                bytes.push_back(value);
            }
    if (indexed) {
        bytes.push_back(12);
        for (std::int32_t entry = 0; entry < 256; ++entry)
            for (std::int32_t channel = 0; channel < 3; ++channel)
                bytes.push_back(entry < 4 ?
                    static_cast<std::uint8_t>(colors[entry] >> ((2 - channel) * 8)) : 0);
    }
    return bytes;
}

class PixelOnlySurface : public srColorSurface {
public:
    PixelOnlySurface() : srColorSurface(srPixelConvert::SURFACE_RGB565, nullptr, 16, 16, 32) {}

    void getPixelRow(std::uint32_t* pixels, std::int32_t, std::int32_t begin, std::int32_t end) override
    {
        for (std::int32_t x = begin; x < end; ++x) pixels[x - begin] = 0xfff82010;
    }
};

static void CheckJPEG()
{
    auto source = std::make_unique<PixelOnlySurface>();
    REQUIRE(!source->getDataPtr());
    auto output = srIO::own(SDL_IOFromDynamicMem());
    srImage::save("screenshot.JPG", output.get(), *source);
    REQUIRE(((SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY) && SDL_GetIOSize(output.get()) > 2));
    REQUIRE((static_cast<std::uint8_t*>(SDL_GetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr))[0] == 255 &&
          static_cast<std::uint8_t*>(SDL_GetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr))[1] == 216));
    auto input = srIO::own(SDL_IOFromConstMem(SDL_GetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr), SDL_GetIOSize(output.get())));
    auto decoded = srImage::load("screenshot.jpeg", input.get());
    REQUIRE((decoded && decoded->getWidth() == 16 && decoded->getHeight() == 16));
    for (std::int32_t y = 0; y < 16; ++y)
        for (std::int32_t x = 0; x < 16; ++x) {
            auto pixel = decoded->getPixel(x, y);
            REQUIRE(abs(std::int32_t((pixel >> 16) & 255) - 248) <= 4);
            REQUIRE(abs(std::int32_t((pixel >> 8) & 255) - 32) <= 4);
            REQUIRE(abs(std::int32_t(pixel & 255) - 16) <= 4);
        }
}

static void CheckLossless(const char* name)
{
    srColorSurface source(srPixelConvert::SURFACE_BGRA32, 2, 2);
    for (std::int32_t y = 0; y < 2; ++y)
        for (std::int32_t x = 0; x < 2; ++x)
            source.setPixel(x, y, colors[y * 2 + x]);
    auto output = srIO::own(SDL_IOFromDynamicMem());
    srImage::save(name, output.get(), source);
    REQUIRE(((SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY) && SDL_GetIOSize(output.get())));
    const auto* bytes = static_cast<const std::uint8_t*>(SDL_GetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    CheckImage(name, {bytes, bytes + SDL_GetIOSize(output.get())});
    auto input = srIO::own(SDL_IOFromConstMem(SDL_GetPointerProperty(SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr), SDL_GetIOSize(output.get())));
    SDL_SeekIO(input.get(), 7, SDL_IO_SEEK_SET);
    srColorSurfaceIFace::SurfaceDesc description;
    REQUIRE(srImage::describe(description, name, input.get()));
    REQUIRE((description.width == 2 && description.height == 2 && SDL_TellIO(input.get()) == 7));
    std::uint8_t signature = 0;
    SDL_SeekIO(input.get(), 0, SDL_IO_SEEK_SET);
    srIO::readExact(input.get(), &signature, 1);
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
    srColorSurface source(srPixelConvert::SURFACE_BGR24, 2, 2);
    for (const char* path : {static_cast<const char*>(nullptr), "", "image.pcx", "image", "dir.png/image", "dir\\name.jpg\\image"}) {
        auto output = srIO::own(SDL_IOFromDynamicMem());
        bool rejected = false;
        try { srImage::save(path, output.get(), source); }
        catch (const std::runtime_error&) { rejected = true; }
        REQUIRE((rejected && SDL_TellIO(output.get()) == 0 && SDL_GetIOSize(output.get()) == 0));
    }
    for (const char* path : {static_cast<const char*>(nullptr), ""}) {
        auto bytes = Bitmap();
        auto input = srIO::own(SDL_IOFromConstMem(bytes.data(), bytes.size()));
        bool rejected = false;
        try { srImage::load(path, input.get()); }
        catch (const std::runtime_error&) { rejected = true; }
        REQUIRE((rejected && SDL_TellIO(input.get()) == 0));
    }
    std::uint8_t corrupt[] = {1, 2, 3};
    auto input = srIO::own(SDL_IOFromConstMem(corrupt, sizeof(corrupt)));
    SDL_SeekIO(input.get(), 1, SDL_IO_SEEK_SET);
    srColorSurfaceIFace::SurfaceDesc description;
    REQUIRE((!srImage::describe(description, "bad.jpg", input.get()) && SDL_TellIO(input.get()) == 1));
}

TEST_CASE("image_io_test")
{
    REQUIRE(srExit());
    for (std::int32_t cycle = 0; cycle < 3; ++cycle) {
        REQUIRE((srInit() && srCore.isInitialized()));
        REQUIRE(srInit());
        for (bool top : {false, true})
            for (bool rle : {false, true})
                CheckImage("test.TGA", Targa(top, rle, false));
        auto alpha = Targa(true, false, true);
        alpha[21] = 64;
        std::uint32_t transparent[] = {0x40123456, colors[1], colors[2], colors[3]};
        CheckImage("alpha.tga", alpha, transparent);
        CheckImage("test.bmp", Bitmap());
        CheckImage("test.pcx", PCX(false));
        CheckImage("palette.pcx", PCX(true));
        CheckJPEG();
        CheckLossless("dir.jpg/lossless.PNG");
        CheckLossless("lossless.BMP");
        CheckErrors();
        std::uint8_t corrupt[] = {1, 2, 3};
        for (const char* name : {"bad.jpg", "bad.tga", "bad.bmp", "bad.pcx"}) {
            auto stream = srIO::own(SDL_IOFromConstMem(corrupt, sizeof(corrupt)));
            REQUIRE(!srImage::load(name, stream.get()));
        }
        REQUIRE((srExit() && !srCore.isInitialized()));
        REQUIRE(srExit());
    }
    puts("ok: direct image loading, JPEG export and SurRender reinitialization");

}
