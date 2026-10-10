#include "himage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <zlib.h>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(#expression); } while (0)

int main()
try
{
    image_type image{};
    image.usWidth = 500;
    image.usHeight = 200;
    image.ubBitDepth = 8;
    image.fFlags = IMAGE_BITMAPDATA | IMAGE_COMPRESSED;
    std::vector<UINT8> original(std::size_t(image.usWidth) * image.usHeight);
    for (std::size_t i = 0; i < original.size(); ++i)
        original[i] = (i * 7) ^ (i >> 5);
    uLongf packed_size = compressBound(original.size());
    std::vector<UINT8> packed(packed_size);
    CHECK(compress2(packed.data(), &packed_size, original.data(), original.size(), 9) == Z_OK);
    packed.resize(packed_size);
    image.pImageData = packed;
    image.pui16BPPPalette = std::make_unique<UINT16[]>(256);
    for (unsigned i = 0; i < 256; ++i)
        image.pui16BPPPalette[i] = 0x8000 | i;
    const SGPRect rect{3, 2, 13, 7};
    std::array<UINT8, 200> destination8;
    std::array<UINT8, 401> destination16;
    auto output16 = std::span(destination16).subspan(1);
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        destination8.fill(0xa5);
        destination16.fill(0xa5);
        CHECK(CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 1, 1, rect));
        CHECK(std::all_of(destination8.begin(), destination8.end(), [](UINT8 value) { return value == 0xa5; }));
        CHECK(CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, rect));
        for (std::size_t y = 0; y < 10; ++y) {
            for (std::size_t x = 0; x < 20; ++x) {
                UINT16 pixel;
                std::memcpy(&pixel, output16.data() + (y * 20 + x) * 2, sizeof(pixel));
                const auto expected = x >= 1 && x < 11 && y >= 1 && y < 6 ?
                    image.pui16BPPPalette[original[(y + 1) * image.usWidth + x + 2]] : 0xa5a5;
                CHECK(pixel == expected);
            }
        }
        CHECK(destination16.front() == 0xa5);
    }
    for (unsigned corrupt = 0; corrupt < 5; ++corrupt) {
        image.pImageData = packed;
        if (corrupt == 0) image.pImageData = {0};
        if (corrupt == 1) image.pImageData.pop_back();
        if (corrupt == 2) image.pImageData.back() ^= 1;
        if (corrupt == 3) image.pImageData.push_back(0);
        if (corrupt == 4) ++image.usHeight;
        destination16.fill(0xa5);
        CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, rect));
        CHECK(std::all_of(destination16.begin(), destination16.end(), [](UINT8 value) { return value == 0xa5; }));
        CHECK(CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 1, 1, rect));
    }
    CHECK(!CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 20, 1, rect));
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16.first(10), 20, 10, 1, 1, rect));
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 19, 1, rect));
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 9, rect));
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, {-1, 2, 3, 7}));
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, {0, 0, 501, 1}));
    image.pImageData = original;
    --image.usHeight;
    image.fFlags = IMAGE_BITMAPDATA;
    destination16.fill(0xa5);
    CHECK(CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, rect));
    CHECK(CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 1, 1, rect));
    CHECK(destination8[5 * 20 + 10] == original[6 * image.usWidth + 12]);
    image.pui16BPPPalette.reset();
    CHECK(!CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 1, 1, rect));
    image.pImageData.clear();
    CHECK(!CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 1, 1, rect));

    image.usWidth = image.usHeight = 2;
    image.ubBitDepth = 16;
    image.pImageData = {1, 2, 3, 4, 5, 6, 7, 8};
    destination16.fill(0xa5);
    CHECK(CopyImageToBuffer(image, BUFFER_16BPP, output16, 20, 10, 17, 8, {0, 0, 2, 2}));
    CHECK(!std::memcmp(output16.data() + (8 * 20 + 17) * 2, image.pImageData.data(), 4));
    CHECK(!std::memcmp(output16.data() + (9 * 20 + 17) * 2, image.pImageData.data() + 4, 4));
    CHECK(!CopyImageToBuffer(image, BUFFER_8BPP, destination8, 20, 10, 1, 1, {0, 0, 2, 2}));
    puts("bounded image copies and zlib failure handling passed");
    return 0;
}
catch (const std::exception& error)
{
    fprintf(stderr, "%s\n", error.what());
    return 1;
}
