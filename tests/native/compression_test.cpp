// Round-trips data through system zlib and SGP's streaming decompressor.
#include "Compression.h"
#include "himage.h"

#include <algorithm>
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
    image.pImageData = std::make_unique<UINT8[]>(packed_size);
    std::copy_n(packed.data(), packed_size, image.pImageData.get());
    image.uiSizePixData = packed_size;
    image.pui16BPPPalette = std::make_unique<UINT16[]>(256);
    unsigned char destination8[200]{};
    UINT16 destination16[200]{};
    SGPRect rectangle{3, 2, 13, 7};
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
        if (!Copy8BPPCompressedImageTo8BPPBuffer(&image, destination8, 20, 10, 1, 1, &rectangle) ||
            !Copy8BPPCompressedImageTo16BPPBuffer(&image, reinterpret_cast<BYTE*>(destination16),
                                                20, 10, 1, 1, &rectangle)) {
            fputs("compressed blitter cleanup failed\n", stderr);
            return 1;
        }
    }
    printf("ok: %u bytes via zlib %s\n", total, zlibVersion());
    return 0;
}
