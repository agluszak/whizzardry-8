#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>
#include <catch2/catch_test_macros.hpp>

#include "surrender/srIO.h"
/* Actual SDL_image decoding and byte-exact transfer. */
#include "surrender/srImageIO.h"
#include "wiz8/surface_storage.h"

using Bytes = std::vector<Uint8>;
using OwnedSurface = SDLSurfaceOwner;
static Bytes fixture(const char* name)
{
    std::ifstream input(std::filesystem::path(WIZ8_IMAGE_IMPORT_FIXTURE_DIR) / name,
                        std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
static void checkFormat(const SDL_Surface& surface, SDL_PixelFormat type)
{
    const auto expected = type;
    REQUIRE(surface.format == expected);
}
static OwnedSurface loadJpeg(const Bytes& data)
{
    if (data.empty()) return {nullptr, SDL_DestroySurface};
    auto input = srIO::own(SDL_IOFromConstMem(data.data(), data.size()));
    auto result = srImage::load("sample.jpg", input.get());
    REQUIRE(SDL_GetIOSize(input.get()) == Sint64(data.size()));
    return result;
}
static Bytes encode(SDL_Surface& surface, int quality = 100)
{
    auto output = srIO::own(SDL_IOFromDynamicMem());
    srImage::save("sample.jpg", output.get(), surface, quality);
    REQUIRE(((SDL_GetIOStatus(output.get()) == SDL_IO_STATUS_READY) &&
             SDL_GetIOSize(output.get()) > 2));
    auto* data = static_cast<const Uint8*>(SDL_GetPointerProperty(
        SDL_GetIOProperties(output.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
    return {data, data + SDL_GetIOSize(output.get())};
}
static void jpegTests()
{
    const auto gray = fixture("gray.jpg"), cmyk = fixture("cmyk.jpg");
    Bytes pixels(2 * 16, 0xee);
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 3; ++x) {
            auto* p = pixels.data() + y * 16 + x * 4;
            p[0] = 20 + 40 * x + 60 * y;
            p[1] = p[0] + 20;
            p[2] = p[0] + 60;
            p[3] = 40;
        }
    auto source = CreateCpuStorage(3, 2, SDL_PIXELFORMAT_BGRA32, pixels.data(), 16);
    const auto rgb = encode(*source);
    REQUIRE(rgb == encode(*source, 100));
    REQUIRE(rgb == encode(*source, 200));
    REQUIRE(encode(*source, -1) == encode(*source, 0));
    REQUIRE(rgb != encode(*source, 20));
    REQUIRE((pixels[12] == 0xee && pixels[28] == 0xee));
    for (const auto* data : {&gray, &rgb, &cmyk}) {
        auto surface = loadJpeg(*data);
        REQUIRE((surface && surface->w == 3 && surface->h == 2));
        checkFormat(*surface, SDL_PIXELFORMAT_BGRA32);
        auto io = srIO::own(SDL_IOFromConstMem(data->data(), data->size()));
        SDLSurfaceOwner decoded(IMG_LoadJPG_IO(io.get()), SDL_DestroySurface);
        REQUIRE(decoded);
        SDLSurfaceOwner reference(SDL_ConvertSurface(decoded.get(), SDL_PIXELFORMAT_BGRA32),
                                  SDL_DestroySurface);
        REQUIRE(reference);
        for (unsigned y = 0; y < 2; ++y) {
            const auto* expected =
                static_cast<const Uint8*>(reference->pixels) + y * reference->pitch;
            const auto* row = static_cast<const Uint8*>(surface->pixels) + y * surface->pitch;
            for (unsigned x = 0; x < 3; ++x) {
                REQUIRE(memcmp(row + x * 4, expected + x * 4, 4) == 0);
                if (data == &rgb) {
                    const auto* original = pixels.data() + y * 16 + x * 4;
                    for (unsigned component = 0; component < 3; ++component)
                        REQUIRE(abs(int(row[x * 4 + component]) - int(original[component])) <= 3);
                }
            }
        }
        REQUIRE(!encode(*surface).empty());
    }
    auto invalid = rgb;
    invalid[0] = 0;
    REQUIRE(!loadJpeg(invalid));
}

struct TgaFixture {
    Bytes encoded, expected;
    unsigned bytes;
    SDL_PixelFormat format;
};
static TgaFixture tgaFixture(unsigned depth, bool indexed, bool rle, unsigned origin, bool alpha,
                             unsigned palette_bits = 24, bool with_palette = true)
{
    TgaFixture fixture;
    fixture.bytes = depth / 8;
    fixture.format = indexed       ? SDL_PIXELFORMAT_INDEX8
                     : depth == 8  ? SDL_PIXELFORMAT_INDEX8
                     : depth == 16 ? (alpha ? SDL_PIXELFORMAT_ARGB1555 : SDL_PIXELFORMAT_XRGB1555)
                     : depth == 24 ? SDL_PIXELFORMAT_BGR24
                     : alpha       ? SDL_PIXELFORMAT_BGRA32
                                   : SDL_PIXELFORMAT_BGRX32;
    Bytes& file = fixture.encoded;
    file.resize(18);
    file[0] = 3;
    file[1] = indexed && with_palette;
    file[2] = (indexed ? 1 : depth == 8 ? 3 : 2) + (rle ? 8 : 0);
    file[3] = 7;
    file[5] = 3;
    file[7] = palette_bits;
    file[12] = 3;
    file[14] = 2;
    file[16] = depth;
    file[17] = origin | (alpha ? (depth == 16 ? 1 : 8) : 0);
    file.insert(file.end(), {'i', 'd', '!'});
    if (indexed && with_palette)
        for (unsigned i = 0; i < 3; ++i) {
            if (palette_bits == 16)
                file.insert(file.end(), {Uint8(i + 1), 0x7c});
            else {
                file.insert(file.end(), {Uint8(10 + i), Uint8(30 + i), Uint8(90 + i)});
                if (palette_bits == 32) file.push_back(40 + i);
            }
        }
    // Repeated middle pixels exercise RLE packets crossing row boundaries.
    for (unsigned p = 0; p < 6; ++p) {
        const unsigned value = p == 0 ? 0 : p == 5 ? 2 : 1;
        if (indexed)
            fixture.expected.push_back(7 + value);
        else if (depth == 8)
            fixture.expected.push_back(20 + 50 * value);
        else if (depth == 16) {
            const unsigned word = value == 0 ? 0 : value == 1 ? 0xfc03 : 0x03e0;
            fixture.expected.insert(fixture.expected.end(), {Uint8(word), Uint8(word >> 8)});
        } else {
            fixture.expected.insert(
                fixture.expected.end(),
                {Uint8(10 + value * 50), Uint8(70 + value * 30), Uint8(210 - value * 40)});
            if (depth == 32) fixture.expected.push_back(value * 100);
        }
    }
    Bytes serialized;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 3; ++x) {
            const unsigned canonical =
                ((origin & 0x20) ? y : 1 - y) * 3 + ((origin & 0x10) ? 2 - x : x);
            serialized.insert(serialized.end(),
                              fixture.expected.begin() + canonical * fixture.bytes,
                              fixture.expected.begin() + (canonical + 1) * fixture.bytes);
        }
    if (!rle)
        file.insert(file.end(), serialized.begin(), serialized.end());
    else
        for (unsigned p = 0; p < 6;) {
            unsigned run = 1;
            while (p + run < 6 &&
                   memcmp(serialized.data() + p * fixture.bytes,
                          serialized.data() + (p + run) * fixture.bytes, fixture.bytes) == 0)
                ++run;
            file.push_back(run > 1 ? 0x80 | (run - 1) : 0);
            file.insert(file.end(), serialized.begin() + p * fixture.bytes,
                        serialized.begin() + (p + 1) * fixture.bytes);
            p += run;
        }
    return fixture;
}
static OwnedSurface loadTga(const Bytes& data)
{
    auto input = srIO::own(SDL_IOFromConstMem(data.data(), data.size()));
    auto result = srImage::loadTga(input.get());
    REQUIRE(SDL_GetIOSize(input.get()) == Sint64(data.size()));
    return result;
}
static void checkTga(const TgaFixture& fixture, SDL_Surface& surface)
{
    REQUIRE((surface.w == 3 && surface.h == 2));
    checkFormat(surface, fixture.format);
    for (unsigned y = 0; y < 2; ++y)
        REQUIRE(memcmp(static_cast<const Uint8*>(surface.pixels) + y * surface.pitch,
                       fixture.expected.data() + y * 3 * fixture.bytes, 3 * fixture.bytes) == 0);
}
static void tgaTests()
{
    for (unsigned origin : {0u, 0x10u, 0x20u, 0x30u})
        for (bool rle : {false, true})
            for (unsigned depth : {8u, 16u, 24u, 32u})
                for (bool alpha : {false, true}) {
                    const auto fixture = tgaFixture(depth, false, rle, origin, alpha);
                    auto surface = loadTga(fixture.encoded);
                    if (!surface)
                        fprintf(stderr, "TGA depth=%u rle=%d origin=%u alpha=%d\n", depth, rle,
                                origin, alpha);
                    REQUIRE(surface);
                    checkTga(fixture, *surface);
                    auto truncated = fixture.encoded;
                    truncated.pop_back();
                    REQUIRE(!loadTga(truncated));
                }
    for (unsigned origin : {0u, 0x10u, 0x20u, 0x30u})
        for (bool rle : {false, true})
            for (unsigned palette_bits : {16u, 24u, 32u}) {
                const auto fixture = tgaFixture(8, true, rle, origin, false, palette_bits);
                auto surface = loadTga(fixture.encoded);
                REQUIRE(surface);
                checkTga(fixture, *surface);
                const auto palette = SDL_GetSurfacePalette(surface.get());
                REQUIRE((palette && palette->ncolors >= 10));
                REQUIRE((palette->colors[0].r == 0 && palette->colors[6].b == 0));
                for (unsigned i = 0; i < 3; ++i) {
                    const auto color = palette->colors[7 + i];
                    REQUIRE(color.a == (palette_bits == 32 ? 40 + i : 255));
                    if (palette_bits != 16)
                        REQUIRE((color.b == 10 + i && color.g == 30 + i && color.r == 90 + i));
                    else
                        REQUIRE((color.r >= 248 && color.b != 0));
                }
            }
    const auto fallback = tgaFixture(8, true, true, 0x30, false, 24, false);
    auto core_palette = loadTga(fallback.encoded);
    REQUIRE((core_palette && SDL_GetSurfacePalette(core_palette.get()) != nullptr));
    checkTga(fallback, *core_palette);
    auto palette_only = tgaFixture(8, true, false, 0x20, false, 32).encoded;
    palette_only[2] = 0;
    palette_only[12] = palette_only[14] = 0;
    palette_only.resize(18 + 3 + 3 * 4);
    auto palette_surface = loadTga(palette_only);
    REQUIRE((palette_surface && palette_surface->w == 1 &&
             SDL_GetSurfacePalette(palette_surface.get())));
    REQUIRE(*static_cast<const Uint8*>(palette_surface->pixels) == 7);
    const auto raw = tgaFixture(24, false, false, 0, false).encoded;
    for (size_t size : {size_t(0), size_t(17), size_t(18), raw.size() - 1})
        REQUIRE(!loadTga(Bytes(raw.begin(), raw.begin() + size)));
    for (const auto change : {std::pair{2u, 7u}, {12u, 0u}, {16u, 15u}, {17u, 0x40u}, {1u, 2u}}) {
        auto invalid = raw;
        invalid[change.first] = change.second;
        REQUIRE(!loadTga(invalid));
    }
    auto invalid = raw;
    invalid[12] = invalid[13] = invalid[14] = invalid[15] = 0xff;
    REQUIRE(!loadTga(invalid));
    invalid = tgaFixture(24, false, true, 0, false).encoded;
    invalid[21] = 0xff;
    REQUIRE(!loadTga(invalid));
    invalid = tgaFixture(8, true, false, 0, false).encoded;
    invalid[3] = 255;
    REQUIRE(!loadTga(invalid));
    invalid = tgaFixture(8, true, false, 0, false).encoded;
    invalid.back() = 6;
    REQUIRE(!loadTga(invalid));
}

// Faults are supplied at SDL's backend boundary, where production callers
// receive short transfers and SDL status rather than C++ virtual methods.
struct FaultIO {
    const Bytes& data;
    int fault;
    Sint64 position = 0;
    static Sint64 SDLCALL size(void* cookie)
    {
        auto& self = *static_cast<FaultIO*>(cookie);
        return self.fault == 1 ? -1 : Sint64(self.data.size());
    }
    static Sint64 SDLCALL seek(void* cookie, Sint64 offset, SDL_IOWhence whence)
    {
        auto& self = *static_cast<FaultIO*>(cookie);
        if (self.fault == 2) return -1;
        const Sint64 base = whence == SDL_IO_SEEK_SET   ? 0
                            : whence == SDL_IO_SEEK_CUR ? self.position
                                                        : Sint64(self.data.size());
        if (offset < -base || offset > Sint64(self.data.size()) - base) return -1;
        return self.position = base + offset;
    }
    static size_t SDLCALL read(void* cookie, void*, size_t, SDL_IOStatus* status)
    {
        *status =
            static_cast<FaultIO*>(cookie)->fault == 3 ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF;
        return 0;
    }
    static size_t SDLCALL write(void* cookie, const void*, size_t bytes, SDL_IOStatus* status)
    {
        if (static_cast<FaultIO*>(cookie)->fault == 5) {
            *status = SDL_IO_STATUS_ERROR;
            return 0;
        }
        return bytes ? bytes - 1 : 0;
    }
    srIO::IO open()
    {
        SDL_IOStreamInterface interface;
        SDL_INIT_INTERFACE(&interface);
        interface.size = size;
        interface.seek = seek;
        interface.read = read;
        interface.write = write;
        return srIO::own(SDL_OpenIO(&interface, this));
    }
};
static void streamTests()
{
    const Bytes data{1, 2, 3, 4};
    auto source = srIO::own(SDL_IOFromConstMem(data.data(), data.size()));
    REQUIRE(SDL_GetIOSize(source.get()) == 4);
    REQUIRE(SDL_SeekIO(source.get(), -1, SDL_IO_SEEK_END) == 3);
    Uint8 bytes[8]{};
    REQUIRE((SDL_ReadIO(source.get(), bytes, 8) == 1 && bytes[0] == 4));
    REQUIRE(SDL_ReadIO(source.get(), bytes, 8) == 0);
    REQUIRE(SDL_GetIOStatus(source.get()) == SDL_IO_STATUS_EOF);
    REQUIRE(SDL_SeekIO(source.get(), 0, SDL_IO_SEEK_SET) == 0);
    // SDL's const-memory backend clamps out-of-range seeks. Archive entry
    // backends reject them; those bounds are tested in virtual_transfer_test.
    const auto jpeg = fixture("gray.jpg");
    for (int fault : {0, 1, 2, 3}) {
        FaultIO backend{jpeg, fault};
        auto input = backend.open();
        REQUIRE(!srImage::load("sample.jpg", input.get()));
        REQUIRE(SDL_TellIO(input.get()) <= Sint64(jpeg.size()));
    }
    auto surface = loadJpeg(jpeg);
    REQUIRE(surface);
    for (int fault : {4, 5}) {
        FaultIO backend{jpeg, fault};
        auto output = backend.open();
        bool rejected = false;
        try {
            srImage::save("sample.jpg", output.get(), *surface);
        } catch (...) {
            rejected = true;
        }
        REQUIRE(rejected);
    }
}
TEST_CASE("JPEG decoding and transfer", "[image][transfer]")
{
    jpegTests();
}

TEST_CASE("JPEG validation", "[image][transfer]")
{
    jpegValidationTests();
}

TEST_CASE("Pixel transfer pitch", "[image][transfer]") {}

TEST_CASE("TGA orientation and RLE", "[image][transfer]")
{
    tgaTests();
}

TEST_CASE("Image stream boundaries", "[image][transfer]")
{
    streamTests();
}
