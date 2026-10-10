/* Actual SDL_image decoding and byte-exact transfer. gray.jpg/cmyk.jpg are
   synthetic 3x2, quality-100 JPEGs with samples 20 + 9*i. */
#include "surrender/srImageIO.h"
#include "image_stream.h"
#include <SDL3_image/SDL_image.h>
#include "wiz8/sr_api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

#define CHECK(e) do { if (!(e)) { fprintf(stderr, "line %d: %s (%s)\n", \
    __LINE__, #e, SDL_GetError()); throw std::runtime_error(#e); } } while (0)
using Bytes = std::vector<Uint8>;
using OwnedSurface = std::unique_ptr<srColorSurfaceIFace, void(*)(srColorSurfaceIFace*)>;
static OwnedSurface owned(srColorSurfaceIFace* p)
{
    return {p, [](srColorSurfaceIFace* s) { if (s) s->release(); }};
}
static Bytes fixture(const char* name)
{
    std::ifstream input(std::filesystem::path(WIZ8_IMAGE_IMPORT_FIXTURE_DIR) / name,
                        std::ios::binary);
    CHECK(input.good());
    return {std::istreambuf_iterator<char>(input), {}};
}
static void checkFormat(const srColorSurfaceIFace& surface, srPixelConvert::e_surfaceType type)
{
    srPixelConvert::PixelFormat expected;
    srPixelConvert::mapPixelFormat(type, expected);
    srPixelConvert::PixelFormat actual;
    surface.getPixelFormat(actual);
    CHECK(actual == expected);
}
static OwnedSurface loadJpeg(const Bytes& data)
{
    srBinIMStream input(data.data(), data.size());
    auto result = owned(srImage::load("sample.jpg", input));
    CHECK(input.getSize() == data.size());
    return result;
}
static Bytes encode(srColorSurfaceIFace& surface, int quality = 100)
{
    srBinOMStream output;
    srImage::save("sample.jpg", output, surface, quality);
    CHECK(output.good() && output.getSize() > 2);
    auto* data = static_cast<const Uint8*>(output.getPtr());
    return {data, data + output.getSize()};
}
static void jpegTests()
{
    const auto gray = fixture("gray.jpg"), cmyk = fixture("cmyk.jpg");
    Bytes pixels(2 * 16, 0xee);
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 3; ++x) {
            auto* p = pixels.data() + y * 16 + x * 4;
            p[0] = 20 + 40 * x + 60 * y;
            p[1] = p[0] + 20; p[2] = p[0] + 60; p[3] = 40;
        }
    auto source = owned(SR_NEW(srColorSurface)(srPixelConvert::SURFACE_BGRA32, pixels.data(), 3, 2, 16));
    const auto rgb = encode(*source);
    CHECK(rgb == encode(*source, 100));
    CHECK(rgb == encode(*source, 200));
    CHECK(encode(*source, -1) == encode(*source, 0));
    CHECK(rgb != encode(*source, 20));
    CHECK(pixels[12] == 0xee && pixels[28] == 0xee);
    for (const auto* data : {&gray, &rgb, &cmyk}) {
        const bool monochrome = data == &gray;
        const bool four_channels = data == &cmyk;
        const unsigned pixel_bytes = four_channels ? 4 : 3;
        srBinIMStream input(data->data(), data->size());
        input.seek(7);
        srColorSurfaceIFace::SurfaceDesc description;
        CHECK(srImage::describe(description, "sample.JPEG", input));
        CHECK(input.tell() == 7 && input.good());
        CHECK(description.width == 3 && description.height == 2);
        auto surface = loadJpeg(*data);
        CHECK(surface && surface->getWidth() == 3 && surface->getHeight() == 2);
        checkFormat(*surface, monochrome ? srPixelConvert::SURFACE_L8 :
                              four_channels ? srPixelConvert::SURFACE_BGRA32 : srPixelConvert::SURFACE_BGR24);
        srImage::IO io(SDL_IOFromConstMem(data->data(), data->size()), SDL_CloseIO);
        srImage::Surface decoded(IMG_LoadJPG_IO(io.get()), SDL_DestroySurface);
        CHECK(decoded);
        srImage::Surface reference(SDL_ConvertSurface(decoded.get(), four_channels ? SDL_PIXELFORMAT_BGRA32 :
                                                                                  SDL_PIXELFORMAT_BGR24), SDL_DestroySurface);
        CHECK(reference);
        for (unsigned y = 0; y < 2; ++y) {
            const auto* expected = static_cast<const Uint8*>(reference->pixels) + y * reference->pitch;
            const auto* row = static_cast<const Uint8*>(surface->getDataPtr()) + y * surface->getPitch();
            for (unsigned x = 0; x < 3; ++x) {
                if (monochrome) {
                    CHECK(row[x] == expected[x * 3]);
                    CHECK(abs(int(row[x]) - int(20 + 9 * (3 * y + x))) <= 1);
                } else {
                    CHECK(memcmp(row + x * pixel_bytes, expected + x * pixel_bytes, pixel_bytes) == 0);
                    if (four_channels) CHECK(row[x * 4 + 3] == 255);
                    if (data == &rgb) {
                        const auto* original = pixels.data() + y * 16 + x * 4;
                        for (unsigned component = 0; component < 3; ++component)
                            CHECK(abs(int(row[x * 3 + component]) - int(original[component])) <= 3);
                    }
                }
            }
        }
        CHECK(!encode(*surface).empty());
    }
    // SDL/libjpeg tolerates missing EOI; our stream bridge must not accept it.
    for (size_t length : {size_t(0), size_t(1), size_t(2), size_t(18), rgb.size() / 2, rgb.size() - 2})
        CHECK(!loadJpeg(Bytes(rgb.begin(), rgb.begin() + length)));
    auto invalid = rgb;
    invalid[0] = 0;
    CHECK(!loadJpeg(invalid));
    for (size_t i = 0; i + 9 < invalid.size(); ++i)
        if (rgb[i] == 0xff && rgb[i + 1] == 0xc0) {
            invalid = rgb;
            invalid[i + 5] = invalid[i + 6] = invalid[i + 7] = invalid[i + 8] = 0xff;
            CHECK(!loadJpeg(invalid));
            invalid = rgb;
            invalid[i + 5] = invalid[i + 6] = 0;
            CHECK(!loadJpeg(invalid));
            break;
        }
}

static void pitchTests()
{
    struct Format { SDL_PixelFormat sdl; srPixelConvert::e_surfaceType sr; unsigned bytes; };
    for (const auto format : {Format{SDL_PIXELFORMAT_INDEX8, srPixelConvert::SURFACE_L8, 1},
                              Format{SDL_PIXELFORMAT_ARGB1555, srPixelConvert::SURFACE_ARGB1555, 2},
                              Format{SDL_PIXELFORMAT_BGR24, srPixelConvert::SURFACE_BGR24, 3},
                              Format{SDL_PIXELFORMAT_BGRA32, srPixelConvert::SURFACE_BGRA32, 4}}) {
        const unsigned row_bytes = 3 * format.bytes, src_pitch = row_bytes + 5, dst_pitch = row_bytes + 7;
        Bytes src(2 * src_pitch, 0xac), dst(2 * dst_pitch, 0xcd);
        for (unsigned y = 0; y < 2; ++y)
            for (unsigned x = 0; x < row_bytes; ++x) src[y * src_pitch + x] = 20 * y + x;
        srImage::Surface view(SDL_CreateSurfaceFrom(3, 2, format.sdl, src.data(), src_pitch), SDL_DestroySurface);
        auto destination = owned(SR_NEW(srColorSurface)(format.sr, dst.data(), 3, 2, dst_pitch));
        CHECK(view && destination);
        for (bool mirror : {false, true}) {
            CHECK(srImage::copyRows(*view, *destination, format.bytes, mirror));
            for (unsigned y = 0; y < 2; ++y) {
                for (unsigned x = 0; x < 3; ++x)
                    CHECK(memcmp(dst.data() + y * dst_pitch + x * format.bytes,
                                 src.data() + y * src_pitch + (mirror ? 2 - x : x) * format.bytes,
                                 format.bytes) == 0);
                for (unsigned x = row_bytes; x < dst_pitch; ++x) CHECK(dst[y * dst_pitch + x] == 0xcd);
                for (unsigned x = row_bytes; x < src_pitch; ++x) CHECK(src[y * src_pitch + x] == 0xac);
            }
        }
        view->pitch = row_bytes - 1;
        CHECK(!srImage::copyRows(*view, *destination, format.bytes));
    }
}

struct TgaFixture {
    Bytes encoded, expected;
    unsigned bytes;
    srPixelConvert::e_surfaceType format;
};
static TgaFixture tgaFixture(unsigned depth, bool indexed, bool rle, unsigned origin,
                             bool alpha, unsigned palette_bits = 24, bool with_palette = true)
{
    TgaFixture fixture;
    fixture.bytes = depth / 8;
    fixture.format = indexed ? srPixelConvert::SURFACE_P8 : depth == 8 ? srPixelConvert::SURFACE_L8 :
        depth == 16 ? (alpha ? srPixelConvert::SURFACE_ARGB1555 : srPixelConvert::SURFACE_RGB555) :
        depth == 24 ? srPixelConvert::SURFACE_BGR24 :
        alpha ? srPixelConvert::SURFACE_BGRA32 : srPixelConvert::SURFACE_BGRX32;
    Bytes& file = fixture.encoded;
    file.resize(18);
    file[0] = 3; file[1] = indexed && with_palette;
    file[2] = (indexed ? 1 : depth == 8 ? 3 : 2) + (rle ? 8 : 0);
    file[3] = 7; file[5] = 3; file[7] = palette_bits;
    file[12] = 3; file[14] = 2; file[16] = depth;
    file[17] = origin | (alpha ? (depth == 16 ? 1 : 8) : 0);
    file.insert(file.end(), {'i', 'd', '!'});
    if (indexed && with_palette)
        for (unsigned i = 0; i < 3; ++i) {
            if (palette_bits == 16) file.insert(file.end(), {Uint8(i + 1), 0x7c});
            else {
                file.insert(file.end(), {Uint8(10 + i), Uint8(30 + i), Uint8(90 + i)});
                if (palette_bits == 32) file.push_back(40 + i);
            }
        }
    // Repeated middle pixels exercise RLE packets crossing row boundaries.
    for (unsigned p = 0; p < 6; ++p) {
        const unsigned value = p == 0 ? 0 : p == 5 ? 2 : 1;
        if (indexed) fixture.expected.push_back(7 + value);
        else if (depth == 8) fixture.expected.push_back(20 + 50 * value);
        else if (depth == 16) {
            const unsigned word = value == 0 ? 0 : value == 1 ? 0xfc03 : 0x03e0;
            fixture.expected.insert(fixture.expected.end(), {Uint8(word), Uint8(word >> 8)});
        } else {
            fixture.expected.insert(fixture.expected.end(), {Uint8(10 + value * 50),
                                       Uint8(70 + value * 30), Uint8(210 - value * 40)});
            if (depth == 32) fixture.expected.push_back(value * 100);
        }
    }
    Bytes serialized;
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 3; ++x) {
            const unsigned canonical = ((origin & 0x20) ? y : 1 - y) * 3 + ((origin & 0x10) ? 2 - x : x);
            serialized.insert(serialized.end(), fixture.expected.begin() + canonical * fixture.bytes,
                              fixture.expected.begin() + (canonical + 1) * fixture.bytes);
        }
    if (!rle) file.insert(file.end(), serialized.begin(), serialized.end());
    else
        for (unsigned p = 0; p < 6;) {
            unsigned run = 1;
            while (p + run < 6 && memcmp(serialized.data() + p * fixture.bytes,
                                         serialized.data() + (p + run) * fixture.bytes, fixture.bytes) == 0)
                ++run;
            file.push_back(run > 1 ? 0x80 | (run - 1) : 0);
            file.insert(file.end(), serialized.begin() + p * fixture.bytes, serialized.begin() + (p + 1) * fixture.bytes);
            p += run;
        }
    return fixture;
}
static OwnedSurface loadTga(const Bytes& data)
{
    srBinIMStream input(data.data(), data.size());
    auto result = owned(srImage::loadTga(input));
    CHECK(input.getSize() == data.size());
    return result;
}
static void checkTga(const TgaFixture& fixture, srColorSurfaceIFace& surface)
{
    CHECK(surface.getWidth() == 3 && surface.getHeight() == 2);
    checkFormat(surface, fixture.format);
    for (unsigned y = 0; y < 2; ++y)
        CHECK(memcmp(static_cast<const Uint8*>(surface.getDataPtr()) + y * surface.getPitch(),
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
                    if (!surface) fprintf(stderr, "TGA depth=%u rle=%d origin=%u alpha=%d\n", depth, rle, origin, alpha);
                    CHECK(surface);
                    checkTga(fixture, *surface);
                    auto truncated = fixture.encoded;
                    truncated.pop_back();
                    CHECK(!loadTga(truncated));
                }
    for (unsigned origin : {0u, 0x10u, 0x20u, 0x30u})
        for (bool rle : {false, true})
            for (unsigned palette_bits : {16u, 24u, 32u}) {
                const auto fixture = tgaFixture(8, true, rle, origin, false, palette_bits);
                auto surface = loadTga(fixture.encoded);
                CHECK(surface);
                checkTga(fixture, *surface);
                const auto* palette = surface->getPalette();
                CHECK(palette && palette->getPaletteSize() == 10);
                CHECK(palette->getColor(0).red == 0 && palette->getColor(6).blue == 0);
                for (unsigned i = 0; i < 3; ++i) {
                    const auto color = palette->getColor(7 + i);
                    CHECK(color.alpha == (palette_bits == 32 ? 40 + i : 255));
                    if (palette_bits != 16)
                        CHECK(color.blue == 10 + i && color.green == 30 + i && color.red == 90 + i);
                    else CHECK(color.red >= 248 && color.blue != 0);
                }
            }
    const auto fallback = tgaFixture(8, true, true, 0x30, false, 24, false);
    auto core_palette = loadTga(fallback.encoded);
    CHECK(core_palette && core_palette->getPalette() == srCore.getPalette());
    checkTga(fallback, *core_palette);
    auto palette_only = tgaFixture(8, true, false, 0x20, false, 32).encoded;
    palette_only[2] = 0; palette_only[12] = palette_only[14] = 0;
    palette_only.resize(18 + 3 + 3 * 4);
    auto palette_surface = loadTga(palette_only);
    CHECK(palette_surface && palette_surface->getWidth() == 1 && palette_surface->getPalette());
    CHECK(*static_cast<const Uint8*>(palette_surface->getDataPtr()) == 7);
    const auto raw = tgaFixture(24, false, false, 0, false).encoded;
    for (size_t size : {size_t(0), size_t(17), size_t(18), raw.size() - 1})
        CHECK(!loadTga(Bytes(raw.begin(), raw.begin() + size)));
    for (const auto change : {std::pair{2u, 7u}, {12u, 0u}, {16u, 15u}, {17u, 0x40u}, {1u, 2u}}) {
        auto invalid = raw; invalid[change.first] = change.second;
        CHECK(!loadTga(invalid));
    }
    auto invalid = raw;
    invalid[12] = invalid[13] = invalid[14] = invalid[15] = 0xff;
    CHECK(!loadTga(invalid));
    invalid = tgaFixture(24, false, true, 0, false).encoded;
    invalid[21] = 0xff;
    CHECK(!loadTga(invalid));
    invalid = tgaFixture(8, true, false, 0, false).encoded;
    invalid[3] = 255;
    CHECK(!loadTga(invalid));
    invalid = tgaFixture(8, true, false, 0, false).encoded;
    invalid.back() = 6;
    CHECK(!loadTga(invalid));
}

class FaultInput : public srBinIMStream {
public:
    FaultInput(const Bytes& data, int fault) : srBinIMStream(data.data(), data.size()), fault(fault) {}
    w8_ulong getSize() override { if (fault == 1) throw 0; return srBinIMStream::getSize(); }
    srBinStream& seek(w8_ulong position) override
    {
        if (fault == 2) { setState(SR_STREAM_ERROR); return *this; }
        return srBinIMStream::seek(position);
    }
private:
    w8_ulong vread(void*, w8_ulong) override { if (fault == 3) throw 0; return 0; }
    int fault;
};
class FaultOutput : public srBinOMStream {
public:
    explicit FaultOutput(bool throws) : throws(throws) {}
private:
    w8_ulong vwrite(const void*, w8_ulong bytes) override
    {
        if (throws) throw 0;
        return bytes - 1;
    }
    bool throws;
};
static void streamTests()
{
    const Bytes data{1, 2, 3, 4};
    srBinIMStream source(data.data(), data.size());
    {
        srImage::Stream bridge{source, &source};
        auto io = bridge.open();
        CHECK(io && SDL_GetIOSize(io.get()) == 4);
        CHECK(SDL_SeekIO(io.get(), -1, SDL_IO_SEEK_END) == 3);
        Uint8 bytes[8]{};
        CHECK(SDL_ReadIO(io.get(), bytes, 8) == 1 && bytes[0] == 4);
        CHECK(SDL_ReadIO(io.get(), bytes, 8) == 0 && bridge.eof);
        CHECK(SDL_GetIOStatus(io.get()) == SDL_IO_STATUS_EOF && !bridge.failed);
    }
    CHECK(source.good() && source.getSize() == data.size());
    source.seek(0);
    {
        srImage::Stream bridge{source, &source};
        auto io = bridge.open();
        CHECK(SDL_SeekIO(io.get(), std::numeric_limits<Sint64>::max(), SDL_IO_SEEK_CUR) == -1);
        CHECK(bridge.failed && source.tell() == 0);
    }
    {
        srImage::Stream bridge{source, &source};
        auto io = bridge.open();
        CHECK(SDL_SeekIO(io.get(), std::numeric_limits<Sint64>::min(), SDL_IO_SEEK_END) == -1);
        CHECK(bridge.failed);
    }
    const auto jpeg = fixture("gray.jpg");
    for (int fault : {0, 1, 2, 3}) {
        FaultInput input(jpeg, fault);
        CHECK(!srImage::load("sample.jpg", input));
        CHECK(input.tell() <= jpeg.size());
        FaultInput description_input(jpeg, fault);
        srColorSurfaceIFace::SurfaceDesc description;
        CHECK(!srImage::describe(description, "sample.jpg", description_input));
    }
    auto surface = loadJpeg(jpeg);
    CHECK(surface);
    for (bool throws : {false, true}) {
        FaultOutput output(throws);
        bool rejected = false;
        try { srImage::save("sample.jpg", output, *surface); }
        catch (...) { rejected = true; }
        CHECK(rejected);
    }
}
int main() try
{
    CHECK(srInit());
    jpegTests();
    pitchTests();
    tgaTests();
    streamTests();
    srExit();
    puts("SDL_image JPEG/TGA formats, transfer, origins/RLE/palettes, pitch and checked streams passed");
    return 0;
}
catch (...) { srExit(); return 1; }
