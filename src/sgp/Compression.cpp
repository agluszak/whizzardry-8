#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "Compression.h"
#include "WCheck.h"
#include "zlib.h"

// FUNCTION: WIZ8 0x00415850
DecompressionStream DecompressInit(BYTE* pCompressedData, UINT32 uiDataSize)
{
    DecompressionStream stream(new z_stream{}, DecompressFini);
    if (inflateInit(stream.get()) != Z_OK)
        return DecompressionStream(nullptr, DecompressFini);
    stream->next_in = pCompressedData;
    stream->avail_in = uiDataSize;
    return stream;
}

// FUNCTION: WIZ8 0x004158b0
UINT32 Decompress(z_stream* pDecompPtr, BYTE* pBuffer, UINT32 uiBufferLen)
{
    z_stream* pZStream = (z_stream*)pDecompPtr;

    // these assertions is in here to ensure that we get passed a proper z_stream pointer
    Assert(pZStream != nullptr);

    if (pZStream->avail_in == 0) { // There is nothing left to decompress!
        return (0);
    }

    // set up the z_stream with our parameters
    pZStream->next_out = pBuffer;
    pZStream->avail_out = uiBufferLen;

    // decompress!
    inflate(pZStream, Z_PARTIAL_FLUSH);

    return (uiBufferLen - pZStream->avail_out);
}

// FUNCTION: WIZ8 0x004158f0
void DecompressFini(z_stream* pDecompPtr)
{
    z_stream* pZStream = (z_stream*)pDecompPtr;

    // these assertions is in here to ensure that we get passed a proper z_stream pointer
    Assert(pZStream != nullptr);

    inflateEnd(pZStream);
    delete pZStream;
}
