// Round-trips data through system zlib and SGP's streaming decompressor.
#include "Compression.h"
#include "himage.h"

#include <algorithm>
#include <iterator>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <zlib.h>

int main()
{
    std::vector<unsigned char> original(100000);
    for (size_t index = 0; index < original.size(); ++index) {
        original[index] = static_cast<unsigned char>((index * 7) ^ (index >> 5));
    }
    uLongf packed_size = compressBound(original.size());
    std::vector<unsigned char> packed(packed_size);
    if (compress2(packed.data(), &packed_size, original.data(), original.size(), 9) != Z_OK) {
        fprintf(stderr, "compress2 failed\n");
        return 1;
    }

    auto stream = DecompressInit(packed.data(), static_cast<UINT32>(packed_size));
    if (stream == NULL) {
        fprintf(stderr, "DecompressInit failed\n");
        return 1;
    }
    /* Decompress in uneven chunks to exercise the streaming state. */
    std::vector<unsigned char> unpacked(original.size());
    UINT32 total = 0;
    while (total < unpacked.size()) {
        UINT32 chunk = static_cast<UINT32>(unpacked.size() - total);
        if (chunk > 7919) {
            chunk = 7919;
        }
        UINT32 produced = Decompress(stream.get(), unpacked.data() + total, chunk);
        if (produced == 0) {
            break;
        }
        total += produced;
    }
    stream.reset();

    if (total != original.size() || memcmp(unpacked.data(), original.data(), total) != 0) {
        fprintf(stderr, "round trip mismatch: %u of %zu bytes\n", total, original.size());
        return 1;
    }
    image_type image{};
    image.usWidth = 500;
    image.usHeight = 200;
    image.pImageData.resize(packed_size);
    std::copy_n(packed.data(), packed_size, image.pImageData.data());

    image.pui16BPPPalette = std::make_unique<UINT16[]>(256);
    for (unsigned i = 0; i < 256; ++i) {
        image.pui16BPPPalette[i] = static_cast<UINT16>(i * 0x203);
    }
    unsigned char destination8[200];
    std::fill_n(destination8, 200, 0xa5);
    UINT16 destination16[200];
    std::fill_n(destination16, 200, 0xdead);
    SGPRect rectangle{3, 2, 13, 7};
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        if (!Copy8BPPCompressedImageTo8BPPBuffer(&image, destination8, 20, 10, 1, 1, &rectangle) ||
            !Copy8BPPCompressedImageTo16BPPBuffer(&image, reinterpret_cast<BYTE*>(destination16),
                                                20, 10, 1, 1, &rectangle)) {
            fputs("compressed blitter cleanup failed\n", stderr);
            return 1;
        }
    }
    /* The 16-bit copy skips iTop rows then blits uiNumLines-1 rows of the
       rectangle's columns through the palette; the last row is never written. */
    {
        auto expected = DecompressInit(packed.data(), static_cast<UINT32>(packed_size));
        if (!expected) {
            fputs("expected-stream init failed\n", stderr);
            return 1;
        }
        std::vector<unsigned char> line(image.usWidth);
        for (long skip = 0; skip < rectangle.iTop; ++skip) {
            if (Decompress(expected.get(), line.data(), image.usWidth) != image.usWidth) {
                fputs("expected-stream skip failed\n", stderr);
                return 1;
            }
        }
        for (long row = rectangle.iTop; row < rectangle.iBottom - 1; ++row) {
            if (Decompress(expected.get(), line.data(), image.usWidth) != image.usWidth) {
                fputs("expected-stream read failed\n", stderr);
                return 1;
            }
            for (long col = rectangle.iLeft; col < rectangle.iRight; ++col) {
                const std::size_t at =
                    (1 + row - rectangle.iTop) * 20 + 1 + (col - rectangle.iLeft);
                if (destination16[at] != image.pui16BPPPalette[line[col]]) {
                    fputs("compressed 16-bit copy produced wrong palette value\n", stderr);
                    return 1;
                }
            }
        }
        if (destination16[5 * 20 + 1] != 0xdead || destination16[0] != 0xdead) {
            fputs("compressed 16-bit copy wrote outside its scanline window\n", stderr);
            return 1;
        }
    }
    /* The uncompressed variant writes the same LUT through the same path. */
    {
        image_type flat{};
        flat.usWidth = 24;
        flat.usHeight = 12;
        flat.pImageData.resize(24 * 12);
        for (std::size_t i = 0; i < flat.pImageData.size(); ++i) {
            flat.pImageData[i] = static_cast<UINT8>(i * 29 + 5);
        }
        flat.pui16BPPPalette = std::make_unique<UINT16[]>(256);
        for (unsigned i = 0; i < 256; ++i) {
            flat.pui16BPPPalette[i] = static_cast<UINT16>(i * 0x107);
        }
        UINT16 flatDest[40 * 24];
        std::fill_n(flatDest, 40 * 24, 0xdead);
        SGPRect flatRect{2, 1, 14, 9};
        if (!Copy8BPPImageTo16BPPBuffer(&flat, reinterpret_cast<BYTE*>(flatDest), 40, 24, 3, 2,
                                        &flatRect)) {
            fputs("uncompressed 16-bit copy failed\n", stderr);
            return 1;
        }
        for (long row = flatRect.iTop; row < flatRect.iBottom - 1; ++row) {
            for (long col = flatRect.iLeft; col < flatRect.iRight; ++col) {
                const std::size_t at =
                    (2 + row - flatRect.iTop) * 40 + 3 + (col - flatRect.iLeft);
                if (flatDest[at] != flat.pui16BPPPalette[flat.pImageData[row * 24 + col]]) {
                    fputs("uncompressed 16-bit copy produced wrong palette value\n", stderr);
                    return 1;
                }
            }
        }
        if (flatDest[9 * 40 + 3] != 0xdead || flatDest[0] != 0xdead) {
            fputs("uncompressed 16-bit copy wrote outside its scanline window\n", stderr);
            return 1;
        }
    }
    image.pImageData = {0};

    if (!Copy8BPPCompressedImageTo8BPPBuffer(&image, destination8, 20, 10, 1, 1, &rectangle) ||
        !std::all_of(std::begin(destination8), std::end(destination8),
                     [](unsigned char pixel) { return pixel == 0xa5; })) {
        fputs("8-bit compressed blitter must not decode or modify its destination\n", stderr);
        return 1;
    }
    if (Copy8BPPCompressedImageTo8BPPBuffer(&image, destination8, 20, 10, 20, 1, &rectangle)) {
        fputs("8-bit compressed blitter must still validate destination coordinates\n", stderr);
        return 1;
    }
    printf("ok: %u bytes via zlib %s\n", total, zlibVersion());
    return 0;
}
