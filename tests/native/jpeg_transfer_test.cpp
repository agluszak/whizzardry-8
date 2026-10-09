/* Run the recovered transfer loop with controlled decoded rows. The codec
   adapters are fixtures; this does not claim end-to-end JPEG decoding. */
#include "codec_adapter.h"
#include "plugin_classes.h"
#include "surrender/srBinIStream.h"
#include "wiz8/sr_api.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

srBinIStream* srJPEG_active_input_stream;
srBinOStream* srJPEG_active_output_stream;
static int components;
static unsigned char pixels[3 * 2 * 4];

void srJPEG_read_header_adapter(JpegCodecState* state)
{
    state->width = 3;
    state->height = 2;
    state->components = components;
}
void srJPEG_decode_adapter(JpegCodecState* state)
{
    memcpy(state->pixels, pixels, state->width * state->height * components);
}
void srJPEG_encode_adapter(JpegCodecState*)
{
}

#define CHECK(expression)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if (!(expression))                                                                         \
        {                                                                                          \
            fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                               \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int main()
{
    CHECK(srInit());
    {
        srJPEGImporter importer;
        for (int count : {1, 3, 4})
        {
            components = count;
            for (unsigned int i = 0; i < sizeof(pixels); ++i)
            {
                pixels[i] = static_cast<unsigned char>(i + 1);
            }
            srBinIMStream stream(pixels, sizeof(pixels));
            srSurfaceIOManager::ImportInfo options = {};
            srColorSurfaceIFace::SurfaceDesc description;
            CHECK(importer.getSurfaceDesc(description, stream, options));
            CHECK(description.width == 3 && description.height == 2 &&
                  description.pitch == 3 * count);
            srColorSurfaceIFace* surface = importer.importSurface(stream, options);
            CHECK(surface && surface->getWidth() == 3 && surface->getHeight() == 2);
            for (unsigned int y = 0; y < 2; ++y)
            {
                const unsigned char* row =
                    static_cast<const unsigned char*>(surface->getDataPtr()) +
                    y * surface->getPitch();
                for (unsigned int x = 0; x < 3; ++x)
                {
                    for (int component = 0; component < count; ++component)
                    {
                        CHECK(row[x * count + component] ==
                              pixels[(y * 3 + x) * count + count - 1 - component]);
                    }
                }
            }
            surface->release();
        }
    }
    srExit();
    puts("recovered JPEG transfer preserves L8/BGR24/BGRA32 row bytes");
}
