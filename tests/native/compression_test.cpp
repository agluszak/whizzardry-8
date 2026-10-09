// Round-trips data through system zlib and SGP's streaming decompressor.
#include "Compression.h"

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

    PTR stream = DecompressInit(packed.data(), static_cast<UINT32>(packed_size));
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
        UINT32 produced = Decompress(stream, unpacked.data() + total, chunk);
        if (produced == 0) {
            break;
        }
        total += produced;
    }
    DecompressFini(stream);

    if (total != original.size() || memcmp(unpacked.data(), original.data(), total) != 0) {
        fprintf(stderr, "round trip mismatch: %u of %zu bytes\n", total, original.size());
        return 1;
    }
    printf("ok: %u bytes via zlib %s\n", total, zlibVersion());
    return 0;
}
