#include "surrender/srBinIStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srImageIO.h"
#include "wiz8/sr_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <stdexcept>
#include <vector>

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression); \
            exit(1); \
        } \
    } while (0)

static const w8_ulong colors[] = {0xff123456, 0xffabcdef, 0xff2468ac, 0xfffedcba};

class ClientImageStream : public srBinIMStream {
public:
    using srBinIMStream::srBinIMStream;
};

static void CheckImage(const char* name, const std::vector<unsigned char>& bytes,
                       const w8_ulong* expected = colors)
{
    ClientImageStream stream(bytes.data(), static_cast<w8_ulong>(bytes.size()));
    auto* surface = srImage::load(name, stream);
    CHECK(surface && surface->getWidth() == 2 && surface->getHeight() == 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            CHECK(surface->getPixel(x, y) == expected[y * 2 + x]);
    surface->release();
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
    bytes[0] = 'B'; bytes[1] = 'M'; bytes[2] = 70; bytes[10] = 54;
    bytes[14] = 40; bytes[18] = bytes[22] = 2; bytes[26] = 1; bytes[28] = 24;
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
    bytes[0] = 10; bytes[1] = 5; bytes[2] = 1; bytes[3] = 8;
    bytes[8] = bytes[10] = 1;
    bytes[65] = indexed ? 1 : 3;
    bytes[66] = 2;
    for (int y = 0; y < 2; ++y)
        for (int channel = 0; channel < (indexed ? 1 : 3); ++channel)
            for (int x = 0; x < 2; ++x) {
                auto value = indexed ? static_cast<unsigned char>(y * 2 + x) :
                    static_cast<unsigned char>(colors[y * 2 + x] >> ((2 - channel) * 8));
                if (value >= 192) bytes.push_back(193);
                bytes.push_back(value);
            }
    if (indexed) {
        bytes.push_back(12);
        for (int entry = 0; entry < 256; ++entry)
            for (int channel = 0; channel < 3; ++channel)
                bytes.push_back(entry < 4 ?
                    static_cast<unsigned char>(colors[entry] >> ((2 - channel) * 8)) : 0);
    }
    return bytes;
}

class PixelOnlySurface : public srColorSurface {
public:
    PixelOnlySurface() : srColorSurface(srPixelConvert::SURFACE_RGB565, nullptr, 16, 16, 32) {}

    void getPixelRow(w8_ulong* pixels, w8_long, w8_long begin, w8_long end) override
    {
        for (w8_long x = begin; x < end; ++x) pixels[x - begin] = 0xfff82010;
    }
};

static void CheckJPEG()
{
    auto* source = new PixelOnlySurface;
    CHECK(!source->getDataPtr());
    srBinOMStream output;
    srImage::save("screenshot.JPG", output, *source);
    source->release();
    CHECK(output.good() && output.getSize() > 2);
    CHECK(static_cast<unsigned char*>(output.getPtr())[0] == 255 &&
          static_cast<unsigned char*>(output.getPtr())[1] == 216);
    srBinIMStream input(output.getPtr(), output.getSize());
    auto* decoded = srImage::load("screenshot.jpeg", input);
    CHECK(decoded && decoded->getWidth() == 16 && decoded->getHeight() == 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            auto pixel = decoded->getPixel(x, y);
            CHECK(abs(int((pixel >> 16) & 255) - 248) <= 4);
            CHECK(abs(int((pixel >> 8) & 255) - 32) <= 4);
            CHECK(abs(int(pixel & 255) - 16) <= 4);
        }
    decoded->release();
}

static void CheckLossless(const char* name)
{
    srColorSurface source(srPixelConvert::SURFACE_BGRA32, 2, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            source.setPixel(x, y, colors[y * 2 + x]);
    srBinOMStream output;
    srImage::save(name, output, source);
    CHECK(output.good() && output.getSize());
    const auto* bytes = static_cast<const unsigned char*>(output.getPtr());
    CheckImage(name, {bytes, bytes + output.getSize()});
    srBinIMStream input(output.getPtr(), output.getSize());
    input.seek(7);
    srColorSurfaceIFace::SurfaceDesc description;
    CHECK(srImage::describe(description, name, input));
    CHECK(description.width == 2 && description.height == 2 && input.tell() == 7);
    unsigned char signature = 0;
    input.seek(0);
    input.read(&signature, 1);
    CHECK(input.good());
    if (strstr(name, "PNG")) {
        CHECK(signature == 0x89);
        CheckImage("without-extension", {bytes, bytes + output.getSize()});
    } else {
        CHECK(signature == 'B');
    }
}

static void CheckErrors()
{
    srColorSurface source(srPixelConvert::SURFACE_BGR24, 2, 2);
    for (const char* path : {static_cast<const char*>(nullptr), "", "image.pcx", "image", "dir.png/image", "dir\\name.jpg\\image"}) {
        srBinOMStream output;
        bool rejected = false;
        try { srImage::save(path, output, source); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected && output.tell() == 0 && output.getSize() == 0);
    }
    for (const char* path : {static_cast<const char*>(nullptr), ""}) {
        auto bytes = Bitmap();
        srBinIMStream input(bytes.data(), bytes.size());
        bool rejected = false;
        try { srImage::load(path, input); }
        catch (const std::runtime_error&) { rejected = true; }
        CHECK(rejected && input.tell() == 0);
    }
    unsigned char corrupt[] = {1, 2, 3};
    srBinIMStream input(corrupt, sizeof(corrupt));
    input.seek(1);
    srColorSurfaceIFace::SurfaceDesc description;
    CHECK(!srImage::describe(description, "bad.jpg", input) && input.tell() == 1);
}

int main()
{
    CHECK(srExit());
    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(srInit() && srCore.isInitialized());
        CHECK(srInit());
        for (bool top : {false, true})
            for (bool rle : {false, true})
                CheckImage("test.TGA", Targa(top, rle, false));
        auto alpha = Targa(true, false, true);
        alpha[21] = 64;
        w8_ulong transparent[] = {0x40123456, colors[1], colors[2], colors[3]};
        CheckImage("alpha.tga", alpha, transparent);
        CheckImage("test.bmp", Bitmap());
        CheckImage("test.pcx", PCX(false));
        CheckImage("palette.pcx", PCX(true));
        CheckJPEG();
        CheckLossless("dir.jpg/lossless.PNG");
        CheckLossless("lossless.BMP");
        CheckErrors();
        unsigned char corrupt[] = {1, 2, 3};
        for (const char* name : {"bad.jpg", "bad.tga", "bad.bmp", "bad.pcx"}) {
            srBinIMStream stream(corrupt, sizeof(corrupt));
            CHECK(!srImage::load(name, stream));
        }
        CHECK(srExit() && !srCore.isInitialized());
        CHECK(srExit());
    }
    puts("ok: direct image loading, JPEG export and SurRender reinitialization");
}
