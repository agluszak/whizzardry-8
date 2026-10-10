#include "surrender/srBinIStream.h"
#include "surrender/srBinOStream.h"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srExporter.h"
#include "surrender/srVectorProcessor.h"
#include "wiz8/sr_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
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
    auto* surface = srCore.getSurfaceIOManager()->importSurface(name, stream, {});
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

static void CheckJPEG()
{
    auto* source = new srColorSurface(srPixelConvert::SURFACE_RGB565, 16, 16);
    source->fill(0xfff82010);
    srBinOMStream output;
    srCore.getSurfaceIOManager()->exportSurface("screenshot.JPG", output, *source,
                                               {0, 0, "QUALITY=1.0"});
    source->release();
    CHECK(output.good() && output.getSize() > 2);
    CHECK(static_cast<unsigned char*>(output.getPtr())[0] == 255 &&
          static_cast<unsigned char*>(output.getPtr())[1] == 216);
    srBinIMStream input(output.getPtr(), output.getSize());
    auto* decoded = srCore.getSurfaceIOManager()->importSurface("screenshot.jpeg", input, {});
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

int main()
{
    CHECK(srExit());
    for (int cycle = 0; cycle < 3; ++cycle) {
        CHECK(srInit() && srCore.isInitialized());
        auto* manager = srCore.getSurfaceIOManager();
        CHECK(srInit() && manager == srCore.getSurfaceIOManager());
        CHECK(srVectorProcessor::getName());
        for (const char* extension : {"jpg", "jpeg", "tga", "bmp", "pcx"}) {
            char name[32];
            snprintf(name, sizeof(name), "test.%s", extension);
            CHECK(manager->getImporter(name));
        }
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
        unsigned char corrupt[] = {1, 2, 3};
        for (const char* name : {"bad.jpg", "bad.tga", "bad.bmp", "bad.pcx"}) {
            srBinIMStream stream(corrupt, sizeof(corrupt));
            CHECK(!manager->importSurface(name, stream, {}));
        }
        CHECK(srExit() && !srCore.isInitialized());
        CHECK(!srVectorProcessor::getName());
        CHECK(srExit());
    }
    puts("ok: static image handlers, JPEG export and SurRender reinitialization");
}
