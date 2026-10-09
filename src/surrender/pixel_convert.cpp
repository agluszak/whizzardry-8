#include "surrender/srPixelConvert.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "surrender/srARGB.h"
#include "surrender/srCore.h"
#include "surrender/srMath.h"
#include "surrender/srPalette.h"
#include "surrender/srVariableTimer.h"
#include "surrender/srVectorProcessor.h"

/* RGB24 rows contain a word at every third byte, including odd addresses. */
static inline unsigned short readPackedWord(const unsigned char* bytes)
{
    unsigned short value;
    memcpy(&value, bytes, sizeof(value));
    return value;
}

static inline void writePackedWord(unsigned char* bytes, unsigned short value)
{
    memcpy(bytes, &value, sizeof(value));
}

/* Conversion routines stored in the format table. The generic pair is
   selected by PixelFormat::color_model; the per-entry overrides cover
   formats whose converter does not fit a generic kernel. The MMX workers
   are installed by initFormats() when the CPU reports the feature bit. */
void __cdecl writeRGB(const srPixelConvert::ConversionInfo& info);
void __cdecl readRGB(const srPixelConvert::ConversionInfo& info);
void __cdecl writeYUV(const srPixelConvert::ConversionInfo& info);
void __cdecl readYUV(const srPixelConvert::ConversionInfo& info);
void __cdecl writeIntensity(const srPixelConvert::ConversionInfo& info);
void __cdecl readIntensity(const srPixelConvert::ConversionInfo& info);
void __cdecl writeIndexed(const srPixelConvert::ConversionInfo& info);
void __cdecl readIndexed(const srPixelConvert::ConversionInfo& info);
void __cdecl writeRGB555(const srPixelConvert::ConversionInfo& info);
void __cdecl readRGB555(const srPixelConvert::ConversionInfo& info);
void __cdecl writeBGRX(const srPixelConvert::ConversionInfo& info);
void __cdecl readBGRX(const srPixelConvert::ConversionInfo& info);
void __cdecl writeBGRA(const srPixelConvert::ConversionInfo& info);
void __cdecl readBGRA(const srPixelConvert::ConversionInfo& info);
void __cdecl writeABGR(const srPixelConvert::ConversionInfo& info);
void __cdecl readABGR(const srPixelConvert::ConversionInfo& info);
void __cdecl writeRGB24(const srPixelConvert::ConversionInfo& info);
void __cdecl readRGB24(const srPixelConvert::ConversionInfo& info);
void __cdecl writeL8MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl readL8MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl writeRGB565MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl readRGB565MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl writeARGB1555MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl readARGB1555MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl writeARGB4444MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl readARGB4444MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl writeBGR24MMX(const srPixelConvert::ConversionInfo& info);
void __cdecl readBGR24MMX(const srPixelConvert::ConversionInfo& info);

/* Shared conversion kernels the generic dispatchers route through, by
   destination/source byte width: pack writes BGRA source pixels through
   the reduction tables, unpack expands packed source records through the
   expansion tables, packIntensity routes the luma ramps plus optional
   alpha. */
static void packIntensity16(unsigned short* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha);
static void packIntensity24(unsigned char* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha);
static void packIntensity32(w8_ulong* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha);
static void pack8(unsigned char* dest, const srARGB* source, const unsigned char* const* luts,
                  const unsigned char* shifts, w8_ulong count, int has_alpha);
static void pack16(unsigned short* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha);
static void pack24(unsigned char* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha);
static void pack32(w8_ulong* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha);
static void unpack16(w8_ulong* dest, const unsigned short* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count);
static void unpack24(w8_ulong* dest, const unsigned char* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count);
static void unpack32(w8_ulong* dest, const w8_ulong* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count);

namespace {

struct FormatEntry {
    srPixelConvert::PixelFormat format;
    srPixelConvert::ConversionFunc write;
    srPixelConvert::ConversionFunc read;
    FormatEntry* next;
};

W8_ABI_ASSERT(sizeof(FormatEntry) == 0x20, "srPixelConvert_FormatEntry_must_be_0x20");

/* Lookup tables built by initPixelTables(): n-bit channel expansion (round(i * 255 / (2^n - 1))),
   8-bit channel reduction, the ordered-dither bias cube, the packed-chroma decode table and the
   fixed-point channel weight ramps. */
// GLOBAL: SURRENDER 0x100A1AB0
unsigned char lutExpand1[1];
// GLOBAL: SURRENDER 0x100A1AB4
unsigned char lutExpand2[2];
// GLOBAL: SURRENDER 0x100A1AB8
unsigned char lutExpand4[4];
// GLOBAL: SURRENDER 0x100A1ABC
unsigned char lutExpand16[16];
/* readRGB555 indexes the 5-bit table with the unmasked pixel >> 10 (retail 0x1000AED3), so a pixel
   with the unused bit 15 set reads indices 32..63, which in retail are the first half of the 6-bit
   table placed directly after it (0x100A1ACC + 0x20 = 0x100A1AEC). The two tables share one object
   so that adjacency is part of the layout rather than an accident of BSS ordering. */
struct ExpandTables5And6 {
    unsigned char expand32[32];
    unsigned char expand64[64];
};
static_assert(sizeof(ExpandTables5And6) == 0x60, "ExpandTables5And6_must_be_0x60");
// GLOBAL: SURRENDER 0x100A1ACC
ExpandTables5And6 lutExpand5And6;
// GLOBAL: SURRENDER 0x100A1B2C
unsigned char lutExpand8[8];
// GLOBAL: SURRENDER 0x100A1B34
unsigned char lutZero[256];
// GLOBAL: SURRENDER 0x100A1C34
unsigned char lutIdentity[256];
// GLOBAL: SURRENDER 0x100A1D34
unsigned char lutReduce128[256];
// GLOBAL: SURRENDER 0x100A1E34
unsigned char lutReduce64[256];
// GLOBAL: SURRENDER 0x100A1F34
unsigned char lutReduce32[256];
// GLOBAL: SURRENDER 0x100A2034
unsigned char lutReduce16[256];
// GLOBAL: SURRENDER 0x100A2134
unsigned char lutReduce8[256];
// GLOBAL: SURRENDER 0x100A2234
unsigned char lutReduce4[256];
// GLOBAL: SURRENDER 0x100A2334
unsigned char lutReduce2[256];

// GLOBAL: SURRENDER 0x100A2438
FormatEntry format_table[25];

/* YUV conversion matrices: rgbToYUV rows are the Y, U and V weights applied to the source pixel's
   float channels; yuvToRGB rows decode the expanded Y, U and V back to R, G and B. */
// GLOBAL: SURRENDER 0x100A2758
srVector3T<float> yuvToRGB[3] = {
    srVector3T<float>(1.0f, 0.956f, 0.620f),
    srVector3T<float>(1.0f, -0.272f, -0.647f),
    srVector3T<float>(1.0f, -1.108f, 1.705f),
};

// GLOBAL: SURRENDER 0x100A277C
unsigned char lutDecode[256][4];

// GLOBAL: SURRENDER 0x100A2B7C
FormatEntry* format_hash[32];

// GLOBAL: SURRENDER 0x100A2BFC
unsigned char lutExpand128[128];
// GLOBAL: SURRENDER 0x100A2C7C
w8_long lutRamp18[256];
// GLOBAL: SURRENDER 0x100A307C
int lutDither[9][4][4][4];
// GLOBAL: SURRENDER 0x100A397C
w8_long lutRamp54[256];
// GLOBAL: SURRENDER 0x100A3D80
srVector3T<float> rgbToYUV[3] = {
    srVector3T<float>(0.299f, 0.587f, 0.114f),
    srVector3T<float>(0.596f, -0.275f, -0.321f),
    srVector3T<float>(0.212f, -0.528f, 0.311f),
};
// GLOBAL: SURRENDER 0x100A3DA4
unsigned char lutGray[256][4];
// GLOBAL: SURRENDER 0x100A41A4
w8_long lutRamp183[256];

// GLOBAL: SURRENDER 0x100A45A4
int formats_initialized;

/* Channel lookup-table selectors indexed by channel bit count: the write
   dispatchers reduce each 8-bit source channel, the read dispatchers
   expand each packed channel back to 8 bits. Bit count 8 selects the
   identity table. */
// GLOBAL: SURRENDER 0x1009832C
const unsigned char* const channel_expand[] = {
    lutExpand1,
    lutExpand2,
    lutExpand4,
    lutExpand8,
    lutExpand16,
    lutExpand5And6.expand32,
    lutExpand5And6.expand64,
    lutExpand128,
    lutIdentity,
};
// GLOBAL: SURRENDER 0x10098350
const unsigned char* const channel_reduce[] = {
    lutZero,     lutReduce2,  lutReduce4,   lutReduce8,  lutReduce16,
    lutReduce32, lutReduce64, lutReduce128, lutIdentity,
};

// FUNCTION: SURRENDER 0x100077A0
void initFormat(w8_ulong index, unsigned char red_bits, unsigned char red_shift,
                unsigned char green_bits, unsigned char green_shift, unsigned char blue_bits,
                unsigned char blue_shift, unsigned char alpha_bits, unsigned char alpha_shift,
                srPixelConvert::e_colorModel color_model, srPixelConvert::e_pixelSize pixel_size,
                w8_ulong fourcc)
{
    srPixelConvert::PixelFormat& format = format_table[index].format;
    format.red_bits = red_bits;
    format.red_shift = red_shift;
    format.green_bits = green_bits;
    format.green_shift = green_shift;
    format.blue_bits = blue_bits;
    format.blue_shift = blue_shift;
    format.alpha_bits = alpha_bits;
    format.alpha_shift = alpha_shift;
    format.color_model = color_model;
    format.pixel_size = pixel_size;
    format.fourcc = fourcc;
    format_table[index].write = 0;
    format_table[index].read = 0;
    format_table[index].next = 0;
}

} // namespace

/* Library-init table builder: channel expansion/reduction ramps, the
   ordered-dither bias cube, fixed-point channel weights and the packed
   decode/grayscale palettes. */
// FUNCTION: SURRENDER 0x10007850
void __cdecl initPixelTables(void)
{
    int i;
    int ditherKernel[4][4][4] = {
        {{0, 7, 2, 7}, {4, 5, 2, 5}, {6, 1, 7, 1}, {6, 3, 4, 3}},
        {{0, 6, 3, 7}, {4, 6, 1, 5}, {5, 1, 7, 2}, {7, 3, 4, 2}},
        {{3, 4, 3, 6}, {1, 7, 1, 6}, {5, 2, 5, 4}, {7, 2, 7, 0}},
        {{6, 1, 5, 3}, {6, 2, 6, 1}, {2, 5, 2, 7}, {2, 5, 2, 3}},
    };
    for (int level = 0; level < 9; ++level) {
        for (int a = 0; a < 4; ++a) {
            for (int b = 0; b < 4; ++b) {
                for (int c = 0; c < 4; ++c) {
                    lutDither[level][a][b][c] = static_cast<w8_long>(
                        (ditherKernel[a][b][c] - 3.5f) * (255.0f / 7.0f) / (1 << level) + 0.5f);
                }
            }
        }
    }

    lutExpand1[0] = 0xff;
    for (i = 0; i < 2; ++i) {
        lutExpand2[i] = static_cast<unsigned char>(i * 255.0f + 0.5f);
    }
    for (i = 0; i < 4; ++i) {
        lutExpand4[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 3.0f) + 0.5f);
    }
    for (i = 0; i < 8; ++i) {
        lutExpand8[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 7.0f) + 0.5f);
    }
    for (i = 0; i < 16; ++i) {
        lutExpand16[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 15.0f) + 0.5f);
    }
    for (i = 0; i < 32; ++i) {
        lutExpand5And6.expand32[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 31.0f) + 0.5f);
    }
    for (i = 0; i < 64; ++i) {
        lutExpand5And6.expand64[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 63.0f) + 0.5f);
    }
    for (i = 0; i < 128; ++i) {
        lutExpand128[i] = static_cast<unsigned char>(i * 255.0f * (1.0f / 127.0f) + 0.5f);
    }

    for (i = 0; i < 256; ++i) {
        lutRamp54[i] = static_cast<w8_long>(i * 54.4 + 0.5);
        lutRamp183[i] = static_cast<w8_long>(i * 183.1424 + 0.5);
        lutRamp18[i] = static_cast<w8_long>(i * 18.4576 + 0.5);
        lutGray[i][0] = static_cast<unsigned char>(i);
        lutGray[i][1] = static_cast<unsigned char>(i);
        lutGray[i][2] = static_cast<unsigned char>(i);
        lutGray[i][3] = 0;
    }

    memset(lutZero, 0, sizeof(lutZero));

    for (i = 0; i < 256; ++i) {
        lutIdentity[i] = static_cast<unsigned char>(i);
        double value = i * (1.0 / 255.0);
        lutReduce128[i] = static_cast<unsigned char>(value * 127.0 + 0.5);
        lutReduce64[i] = static_cast<unsigned char>(value * 63.0 + 0.5);
        lutReduce32[i] = static_cast<unsigned char>(value * 31.0 + 0.5);
        lutReduce16[i] = static_cast<unsigned char>(value * 15.0 + 0.5);
        lutReduce8[i] = static_cast<unsigned char>(value * 7.0 + 0.5);
        lutReduce4[i] = static_cast<unsigned char>(value * 3.0 + 0.5);
        lutReduce2[i] = static_cast<unsigned char>(value * 1.0 + 0.5);
    }

    for (i = 0; i < 256; ++i) {
        int luma = lutExpand16[(i >> 4) & 0xf];
        int chroma1 = lutExpand4[(i >> 2) & 3];
        int chroma2 = lutExpand4[i & 3];
        lutDecode[i][3] = 0xff;
        int value = static_cast<int>(luma + chroma1 * 0.956f + chroma2 * 0.620f);
        if (value < 0) {
            value = 0;
        } else if (value > 0xff) {
            value = 0xff;
        }
        lutDecode[i][2] = static_cast<unsigned char>(value);
        value = static_cast<int>(luma - chroma1 * 0.272f - chroma2 * 0.647f);
        if (value < 0) {
            value = 0;
        } else if (value > 0xff) {
            value = 0xff;
        }
        lutDecode[i][1] = static_cast<unsigned char>(value);
        value = static_cast<int>(luma - chroma1 * 1.108f + chroma2 * 1.705f);
        if (value < 0) {
            value = 0;
        } else if (value > 0xff) {
            value = 0xff;
        }
        lutDecode[i][0] = static_cast<unsigned char>(value);
    }
}

namespace {

// FUNCTION: SURRENDER 0x100082C0
void initFormats()
{
    if (formats_initialized != 0) {
        return;
    }
    initFormat(srPixelConvert::SURFACE_AP44, 4, 0, 0, 0, 0, 0, 4, 4, srPixelConvert::COLOR_INDEXED,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_AL44, 4, 0, 0, 0, 0, 0, 4, 4,
               srPixelConvert::COLOR_INTENSITY, srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_L8, 8, 0, 0, 0, 0, 0, 0, 0, srPixelConvert::COLOR_INTENSITY,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_A8, 0, 0, 0, 0, 0, 0, 8, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_P8, 8, 0, 0, 0, 0, 0, 0, 0, srPixelConvert::COLOR_INDEXED,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_AP88, 8, 0, 0, 0, 0, 0, 8, 8, srPixelConvert::COLOR_INDEXED,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_AL88, 8, 0, 0, 0, 0, 0, 8, 8,
               srPixelConvert::COLOR_INTENSITY, srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_RGB565, 5, 0xb, 6, 5, 5, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_RGB555, 5, 10, 5, 5, 5, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_ARGB1555, 5, 10, 5, 5, 5, 0, 1, 0xf,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_RGB444, 4, 8, 4, 4, 4, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_ARGB4444, 4, 8, 4, 4, 4, 0, 4, 0xc,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_BGR24, 8, 0x10, 8, 8, 8, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_24, 0);
    initFormat(srPixelConvert::SURFACE_BGRX32, 8, 0x10, 8, 8, 8, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_32, 0);
    initFormat(srPixelConvert::SURFACE_BGRA32, 8, 0x10, 8, 8, 8, 0, 8, 0x18,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_32, 0);
    initFormat(srPixelConvert::SURFACE_Y4U2V2, 4, 4, 2, 2, 2, 0, 0, 0, srPixelConvert::COLOR_YUV,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(srPixelConvert::SURFACE_A8Y4U2V2, 4, 4, 2, 2, 2, 0, 8, 8, srPixelConvert::COLOR_YUV,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_RGB332, 3, 5, 3, 2, 2, 0, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_8, 0);
    initFormat(0x12, 3, 5, 3, 2, 2, 0, 8, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_BGR565, 5, 0, 6, 5, 5, 0xb, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_ARGB32, 8, 8, 8, 0x10, 8, 0x18, 8, 0,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_32, 0);
    initFormat(srPixelConvert::SURFACE_BGR555, 5, 0, 5, 5, 5, 10, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_16, 0);
    initFormat(srPixelConvert::SURFACE_ABGR32, 8, 0x18, 8, 0x10, 8, 8, 8, 0,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_32, 0);
    initFormat(srPixelConvert::SURFACE_RGBA32, 8, 0, 8, 8, 8, 0x10, 8, 0x18,
               srPixelConvert::COLOR_RGB, srPixelConvert::PIXEL_SIZE_32, 0);
    initFormat(srPixelConvert::SURFACE_RGB24, 8, 0, 8, 8, 8, 0x10, 0, 0, srPixelConvert::COLOR_RGB,
               srPixelConvert::PIXEL_SIZE_24, 0);
    for (FormatEntry* entry = format_table; entry < format_table + 25; entry++) {
        switch (entry->format.color_model) {
        case srPixelConvert::COLOR_RGB:
            entry->write = writeRGB;
            entry->read = readRGB;
            break;
        case srPixelConvert::COLOR_YUV:
            entry->write = writeYUV;
            entry->read = readYUV;
            break;
        case srPixelConvert::COLOR_INTENSITY:
            entry->write = writeIntensity;
            entry->read = readIntensity;
            break;
        case srPixelConvert::COLOR_INDEXED:
            entry->write = writeIndexed;
            entry->read = readIndexed;
        }
    }
    format_table[srPixelConvert::SURFACE_RGB555].write = writeRGB555;
    format_table[srPixelConvert::SURFACE_RGB555].read = readRGB555;
    format_table[srPixelConvert::SURFACE_BGRX32].write = writeBGRX;
    format_table[srPixelConvert::SURFACE_BGRX32].read = readBGRX;
    format_table[srPixelConvert::SURFACE_BGRA32].write = writeBGRA;
    format_table[srPixelConvert::SURFACE_BGRA32].read = readBGRA;
    format_table[srPixelConvert::SURFACE_ABGR32].write = writeABGR;
    format_table[srPixelConvert::SURFACE_ABGR32].read = readABGR;
    format_table[srPixelConvert::SURFACE_RGB24].write = writeRGB24;
    format_table[srPixelConvert::SURFACE_RGB24].read = readRGB24;
    if ((srCore.getTimer()->m_cpu_features & (1UL << srTimer::CPU_FEATURE_MMX)) != 0) {
        format_table[srPixelConvert::SURFACE_L8].write = writeL8MMX;
        format_table[srPixelConvert::SURFACE_L8].read = readL8MMX;
        format_table[srPixelConvert::SURFACE_RGB565].write = writeRGB565MMX;
        format_table[srPixelConvert::SURFACE_RGB565].read = readRGB565MMX;
        format_table[srPixelConvert::SURFACE_ARGB1555].write = writeARGB1555MMX;
        format_table[srPixelConvert::SURFACE_ARGB1555].read = readARGB1555MMX;
        format_table[srPixelConvert::SURFACE_ARGB4444].write = writeARGB4444MMX;
        format_table[srPixelConvert::SURFACE_ARGB4444].read = readARGB4444MMX;
        format_table[srPixelConvert::SURFACE_BGR24].write = writeBGR24MMX;
        format_table[srPixelConvert::SURFACE_BGR24].read = readBGR24MMX;
    }
    memset(format_hash, 0, sizeof(format_hash));
    for (FormatEntry* hashed = format_table; hashed < format_table + 25; hashed++) {
        const srPixelConvert::PixelFormat& format = hashed->format;
        w8_ulong hash =
            (format.alpha_shift + format.alpha_bits) ^ (format.blue_shift + format.blue_bits) * 4 ^
            (format.red_shift + format.red_bits) ^ (format.color_model << 3) ^ format.pixel_size;
        hash = (hash >> 5 & 0x1f) ^ (hash & 0x1f);
        hashed->next = format_hash[hash];
        format_hash[hash] = hashed;
    }
    formats_initialized = 1;
}

} // namespace

// FUNCTION: SURRENDER 0x10007E40
int srPixelConvert::PixelFormat::isValid() const
{
    if (PIXEL_SIZE_8 <= pixel_size && pixel_size <= PIXEL_SIZE_32 && COLOR_RGB <= color_model &&
        color_model <= COLOR_INDEXED && red_bits <= 8 && green_bits <= 8 && blue_bits <= 8 &&
        alpha_bits <= 8 && red_shift <= 0x1f && green_shift <= 0x1f && blue_shift <= 0x1f &&
        alpha_shift <= 0x1f) {
        return 1;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x10007E90
void srPixelConvert::PixelFormat::getName(char* const name)
{
    static const char channel_letters[] = "RGBAYUVAIXXAPXXA";

    if (fourcc != 0) {
        name[0] = static_cast<char>(fourcc);
        name[1] = static_cast<char>(fourcc >> 8);
        name[2] = static_cast<char>(fourcc >> 0x10);
        name[3] = static_cast<char>(fourcc >> 0x18);
        name[4] = '\0';
        return;
    }
    if (name == 0) {
        return;
    }
    w8_ulong shifts[4] = {red_shift, green_shift, blue_shift, alpha_shift};
    unsigned char bits[4] = {red_bits, green_bits, blue_bits, alpha_bits};
    unsigned char order[4] = {0, 1, 2, 3};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < i; j++) {
            if (shifts[j] < shifts[i]) {
                w8_ulong shift = shifts[i];
                shifts[i] = shifts[j];
                shifts[j] = shift;
                unsigned char bit = bits[i];
                bits[i] = bits[j];
                bits[j] = bit;
                unsigned char channel = order[i];
                order[i] = order[j];
                order[j] = channel;
            }
        }
    }
    char text[16];
    int length = 0;
    for (int k = 0; k < 4; k++) {
        if (bits[k] != 0) {
            text[length++] = channel_letters[color_model * 4 + order[k]];
        }
    }
    for (int j = 0; j < 4; j++) {
        if (bits[j] != 0) {
            text[length++] = static_cast<char>(bits[j] + '0');
        }
    }
    text[length] = '\0';
    char tail[8];
    sprintf(tail, "/%d", pixel_size * 8 + 8);
    strcat(text, tail);
    strcpy(name, text);
}

// FUNCTION: SURRENDER 0x10008020
w8_ulong srPixelConvert::PixelFormat::match(const PixelFormat* formats,
                                                 w8_ulong count) const
{
    if (count < 2) {
        return 0;
    }
    if (fourcc != 0) {
        for (w8_ulong i = 0; i < count; i++) {
            if (formats[i].fourcc == fourcc) {
                return i;
            }
        }
    }
    w8_ulong wanted = 0;
    if (red_bits != 0) {
        wanted |= 1;
    }
    if (green_bits != 0) {
        wanted |= 2;
    }
    if (blue_bits != 0) {
        wanted |= 4;
    }
    if (alpha_bits != 0) {
        wanted |= 8;
    }
    w8_ulong best = 0;
    int best_distance = 0x7fffffff;
    int best_bytes = 5;
    for (w8_ulong c = 0; c < count; c++) {
        const PixelFormat& candidate = formats[c];
        if (candidate.color_model != color_model) {
            continue;
        }
        w8_ulong have = 0;
        if (candidate.red_bits != 0) {
            have |= 1;
        }
        if (candidate.green_bits != 0) {
            have |= 2;
        }
        if (candidate.blue_bits != 0) {
            have |= 4;
        }
        if (candidate.alpha_bits != 0) {
            have |= 8;
        }
        if ((have & wanted) != wanted) {
            continue;
        }
        /* Only channel shortfall costs distance: a candidate with extra bits
           in a wanted channel scores the same as an exact bit count. */
        int dr = candidate.red_bits - red_bits;
        if (dr > 0) {
            dr = 0;
        }
        int dg = candidate.green_bits - green_bits;
        if (dg > 0) {
            dg = 0;
        }
        int db = candidate.blue_bits - blue_bits;
        if (db > 0) {
            db = 0;
        }
        int da = candidate.alpha_bits - alpha_bits;
        if (da > 0) {
            da = 0;
        }
        int distance = dr * dr + dg * dg + db * db + da * da;
        if (distance < best_distance ||
            (distance == best_distance && candidate.pixel_size < best_bytes)) {
            best = c;
            best_distance = distance;
            best_bytes = candidate.pixel_size;
        }
    }
    if (best_distance < 0x7fffffff) {
        return best;
    }
    /* No class-compatible candidate: pick the widest pixel format. */
    w8_ulong widest = 0;
    int widest_bytes = -1;
    for (w8_ulong w = 0; w < count; w++) {
        if (widest_bytes < formats[w].pixel_size) {
            widest = w;
            widest_bytes = formats[w].pixel_size;
        }
    }
    return widest;
}

// FUNCTION: SURRENDER 0x10008240
srPixelConvert::e_surfaceType srPixelConvert::mapPixelFormat(const PixelFormat& format)
{
    initFormats();
    for (w8_ulong i = 0; i < 25; i++) {
        if (format_table[i].format == format) {
            return static_cast<e_surfaceType>(i);
        }
    }
    return SURFACE_INVALID;
}

// FUNCTION: SURRENDER 0x10008290
void srPixelConvert::mapPixelFormat(e_surfaceType type, PixelFormat& format)
{
    initFormats();
    if (static_cast<int>(type) < 0 || static_cast<int>(type) > 0x18) {
        type = SURFACE_BGRA32;
    }
    format = format_table[type].format;
}

// FUNCTION: SURRENDER 0x100087A0
void srPixelConvert::selectFuncs(const PixelFormat& format, ConversionFunc& write,
                                 ConversionFunc& read)
{
    initFormats();
    w8_ulong hash =
        (format.alpha_shift + format.alpha_bits) ^ (format.blue_shift + format.blue_bits) * 4 ^
        (format.red_shift + format.red_bits) ^ (format.color_model << 3) ^ format.pixel_size;
    FormatEntry* entry = format_hash[(hash >> 5 & 0x1f) ^ (hash & 0x1f)];
    while (entry != 0) {
        if (entry->format == format) {
            read = entry->read;
            write = entry->write;
            return;
        }
        entry = entry->next;
    }
    switch (format.color_model) {
    case srPixelConvert::COLOR_RGB:
        write = writeRGB;
        read = readRGB;
        return;
    case srPixelConvert::COLOR_YUV:
        write = writeYUV;
        read = readYUV;
        return;
    case srPixelConvert::COLOR_INTENSITY:
        write = writeIntensity;
        read = readIntensity;
        return;
    case srPixelConvert::COLOR_INDEXED:
        write = writeIndexed;
        read = readIndexed;
    }
}

/* Clamps a decoded YUV channel to a byte for the srARGB pack. */
static int clampChannel(float value)
{
    if (0.0f < value) {
        if (value < 255.0f) {
            return srFloatToInt(value);
        }
        return 0xff;
    }
    return 0;
}

/* YUVA write dispatcher: every case float-vectorizes the source pixel, dot-products it with the
   rgbToYUV rows, then quantizes the Y, U and V results through the format's channel reduction
   tables. The destination index comes from the shifted green-channel term instead of the loop
   index, so writes scatter across the head of the destination. */
// FUNCTION: SURRENDER 0x100088D0
void __cdecl writeYUV(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    const unsigned char* luts[4];
    unsigned char shifts[4];
    luts[0] = channel_reduce[format->red_bits];
    luts[1] = channel_reduce[format->green_bits];
    luts[2] = channel_reduce[format->blue_bits];
    luts[3] = channel_reduce[format->alpha_bits];
    shifts[0] = format->red_shift;
    shifts[1] = format->green_shift;
    shifts[2] = format->blue_shift;
    shifts[3] = format->alpha_shift;
    const srARGB* source = static_cast<const srARGB*>(info.source);
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        unsigned char* dest = static_cast<unsigned char*>(info.dest);
        for (w8_ulong i = info.count; i != 0; --i, ++source) {
            srARGB pixel = *source;
            srVector3T<float> rgb((float)pixel.red, (float)pixel.green, (float)pixel.blue);
            srVector3T<float> yuv(DotProduct(rgbToYUV[0], rgb), DotProduct(rgbToYUV[1], rgb),
                                  DotProduct(rgbToYUV[2], rgb));
            int y = srFloatToInt(yuv.x);
            int u = srFloatToInt(yuv.y);
            int v = srFloatToInt(yuv.z);
            w8_ulong index = luts[1][(u >> 8) & 0xff] << shifts[1];
            dest[index] = luts[0][(y >> 16) & 0xff] << shifts[0] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][v & 0xff] << shifts[2] |
                          index;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        unsigned short* dest = static_cast<unsigned short*>(info.dest);
        for (w8_ulong i = info.count; i != 0; --i, ++source) {
            srARGB pixel = *source;
            srVector3T<float> rgb((float)pixel.red, (float)pixel.green, (float)pixel.blue);
            srVector3T<float> yuv(DotProduct(rgbToYUV[0], rgb), DotProduct(rgbToYUV[1], rgb),
                                  DotProduct(rgbToYUV[2], rgb));
            int y = srFloatToInt(yuv.x);
            int u = srFloatToInt(yuv.y);
            int v = srFloatToInt(yuv.z);
            w8_ulong index = luts[1][(u >> 8) & 0xff] << shifts[1];
            dest[index] = luts[0][(y >> 16) & 0xff] << shifts[0] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][v & 0xff] << shifts[2] |
                          index;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        unsigned char* dest = static_cast<unsigned char*>(info.dest);
        for (w8_ulong i = info.count; i != 0; --i, ++source) {
            srARGB pixel = *source;
            srVector3T<float> rgb((float)pixel.red, (float)pixel.green, (float)pixel.blue);
            srVector3T<float> yuv(DotProduct(rgbToYUV[0], rgb), DotProduct(rgbToYUV[1], rgb),
                                  DotProduct(rgbToYUV[2], rgb));
            int y = srFloatToInt(yuv.x);
            int u = srFloatToInt(yuv.y);
            int v = srFloatToInt(yuv.z);
            w8_ulong index = luts[1][(u >> 8) & 0xff] << shifts[1];
            w8_ulong packed = luts[0][(y >> 16) & 0xff] << shifts[0] |
                                   luts[3][pixel.alpha] << shifts[3] |
                                   luts[2][v & 0xff] << shifts[2] | index;
            unsigned char* dst = dest + index * 3;
            dst[0] = static_cast<unsigned char>(packed);
            dst[1] = static_cast<unsigned char>(packed >> 8);
            dst[2] = static_cast<unsigned char>(packed >> 16);
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
        for (w8_ulong i = info.count; i != 0; --i, ++source) {
            srARGB pixel = *source;
            srVector3T<float> rgb((float)pixel.red, (float)pixel.green, (float)pixel.blue);
            srVector3T<float> yuv(DotProduct(rgbToYUV[0], rgb), DotProduct(rgbToYUV[1], rgb),
                                  DotProduct(rgbToYUV[2], rgb));
            int y = srFloatToInt(yuv.x);
            int u = srFloatToInt(yuv.y);
            int v = srFloatToInt(yuv.z);
            w8_ulong index = luts[1][(u >> 8) & 0xff] << shifts[1];
            dest[index] = luts[0][(y >> 16) & 0xff] << shifts[0] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][v & 0xff] << shifts[2] |
                          index;
        }
        return;
    }
    }
}

/* YUVA read dispatcher: every case expands the packed Y, U and V channels through the format's
   channel expansion tables, dot-products them with the yuvToRGB rows, clamps the results and packs
   an srARGB. The destination index comes from the expanded green (U) channel instead of the loop
   index. */
// FUNCTION: SURRENDER 0x10008F70
void __cdecl readYUV(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    w8_ulong masks[4];
    unsigned char shifts[4];
    const unsigned char* luts[4];
    masks[0] = (1ul << format->red_bits) - 1;
    masks[1] = (1ul << format->green_bits) - 1;
    masks[2] = (1ul << format->blue_bits) - 1;
    masks[3] = (1ul << format->alpha_bits) - 1;
    shifts[0] = format->red_shift;
    shifts[1] = format->green_shift;
    shifts[2] = format->blue_shift;
    shifts[3] = format->alpha_shift;
    luts[0] = channel_expand[format->red_bits];
    luts[1] = channel_expand[format->green_bits];
    luts[2] = channel_expand[format->blue_bits];
    luts[3] = channel_expand[format->alpha_bits];
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < info.count; i++) {
            w8_ulong pixel = source[i];
            srVector3T<float> yuv((float)luts[0][(pixel >> shifts[0]) & masks[0]],
                                  (float)luts[1][(pixel >> shifts[1]) & masks[1]],
                                  (float)luts[2][(pixel >> shifts[2]) & masks[2]]);
            srVector3T<float> rgb(DotProduct(yuvToRGB[0], yuv), DotProduct(yuvToRGB[1], yuv),
                                  DotProduct(yuvToRGB[2], yuv));
            w8_ulong index = luts[1][(pixel >> shifts[1]) & masks[1]];
            dest[index] = luts[3][(pixel >> shifts[3]) & masks[3]] << 24 |
                          clampChannel(rgb.x) << 16 | clampChannel(rgb.y) << 8 |
                          clampChannel(rgb.z);
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        const unsigned short* source = static_cast<const unsigned short*>(info.source);
        for (w8_ulong i = 0; i < info.count; i++) {
            w8_ulong pixel = source[i];
            srVector3T<float> yuv((float)luts[0][(pixel >> shifts[0]) & masks[0]],
                                  (float)luts[1][(pixel >> shifts[1]) & masks[1]],
                                  (float)luts[2][(pixel >> shifts[2]) & masks[2]]);
            srVector3T<float> rgb(DotProduct(yuvToRGB[0], yuv), DotProduct(yuvToRGB[1], yuv),
                                  DotProduct(yuvToRGB[2], yuv));
            w8_ulong index = luts[1][(pixel >> shifts[1]) & masks[1]];
            dest[index] = luts[3][(pixel >> shifts[3]) & masks[3]] << 24 |
                          clampChannel(rgb.x) << 16 | clampChannel(rgb.y) << 8 |
                          clampChannel(rgb.z);
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < info.count; i++) {
            /* reinterpret-ok: 24-bit records load their high two bytes as a word. */
            w8_ulong pixel =
                readPackedWord(source + 1) * 0x100 + source[0];
            srVector3T<float> yuv((float)luts[0][(pixel >> shifts[0]) & masks[0]],
                                  (float)luts[1][(pixel >> shifts[1]) & masks[1]],
                                  (float)luts[2][(pixel >> shifts[2]) & masks[2]]);
            srVector3T<float> rgb(DotProduct(yuvToRGB[0], yuv), DotProduct(yuvToRGB[1], yuv),
                                  DotProduct(yuvToRGB[2], yuv));
            w8_ulong index = luts[1][(pixel >> shifts[1]) & masks[1]];
            dest[index] = luts[3][(pixel >> shifts[3]) & masks[3]] << 24 |
                          clampChannel(rgb.x) << 16 | clampChannel(rgb.y) << 8 |
                          clampChannel(rgb.z);
            source += 3;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
        for (w8_ulong i = 0; i < info.count; i++) {
            w8_ulong pixel = source[i];
            srVector3T<float> yuv((float)luts[0][(pixel >> shifts[0]) & masks[0]],
                                  (float)luts[1][(pixel >> shifts[1]) & masks[1]],
                                  (float)luts[2][(pixel >> shifts[2]) & masks[2]]);
            srVector3T<float> rgb(DotProduct(yuvToRGB[0], yuv), DotProduct(yuvToRGB[1], yuv),
                                  DotProduct(yuvToRGB[2], yuv));
            w8_ulong index = luts[1][(pixel >> shifts[1]) & masks[1]];
            dest[index] = luts[3][(pixel >> shifts[3]) & masks[3]] << 24 |
                          clampChannel(rgb.x) << 16 | clampChannel(rgb.y) << 8 |
                          clampChannel(rgb.z);
        }
        return;
    }
    }
}

/* PXXA (palette index + optional alpha) write dispatcher: every case
   quantizes the source color through the surface palette, shifts the index
   into the red channel position and overlays the reduced alpha. */
// FUNCTION: SURRENDER 0x10009890
void __cdecl writeIndexed(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    const unsigned char* alpha_lut = channel_reduce[format->alpha_bits];
    unsigned char alpha_shift = format->alpha_shift;
    unsigned char index_shift = format->red_shift;
    const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
    w8_ulong count = info.count;
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        unsigned char* dest = static_cast<unsigned char*>(info.dest);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong color = source[i] & 0xffffff;
            /* reinterpret-ok: packed BGR color. */
            unsigned char index = info.palette->quantize(*reinterpret_cast<const srARGB*>(&color));
            dest[i] = static_cast<unsigned char>(index << index_shift | alpha_lut[source[i] >> 24]
                                                                            << alpha_shift);
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        unsigned short* dest = static_cast<unsigned short*>(info.dest);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong color = source[i] & 0xffffff;
            /* reinterpret-ok: packed BGR color. */
            unsigned char index = info.palette->quantize(*reinterpret_cast<const srARGB*>(&color));
            dest[i] = static_cast<unsigned short>(index << index_shift | alpha_lut[source[i] >> 24]
                                                                             << alpha_shift);
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        unsigned char* dest = static_cast<unsigned char*>(info.dest);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong color = source[i];
            /* reinterpret-ok: packed BGR color. */
            unsigned char index = info.palette->quantize(*reinterpret_cast<const srARGB*>(&color));
            w8_ulong pixel = index << index_shift | alpha_lut[source[i] >> 24] << alpha_shift;
            dest[0] = static_cast<unsigned char>(pixel);
            dest[1] = static_cast<unsigned char>(pixel >> 8);
            dest[2] = static_cast<unsigned char>(pixel >> 16);
            dest += 3;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong color = source[i];
            /* reinterpret-ok: packed BGR color. */
            unsigned char index = info.palette->quantize(*reinterpret_cast<const srARGB*>(&color));
            dest[i] = index << index_shift | alpha_lut[source[i] >> 24] << alpha_shift;
        }
    }
    }
}

/* PXXA read dispatcher: the packed index selects a palette entry whose
   packed BGR is kept, then the expanded alpha is overlaid in the top byte. */
// FUNCTION: SURRENDER 0x10009B00
void __cdecl readIndexed(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    unsigned char index_shift = format->red_shift;
    w8_ulong index_mask = (1ul << format->red_bits) - 1;
    w8_ulong alpha_mask = (1ul << format->alpha_bits) - 1;
    unsigned char alpha_shift = format->alpha_shift;
    const unsigned char* alpha_lut = channel_expand[format->alpha_bits];
    /* reinterpret-ok: palette entries are packed srARGB dwords. */
    const w8_ulong* palette =
        reinterpret_cast<const w8_ulong*>(info.palette->getPaletteDataPtr());
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    w8_ulong count = info.count;
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            dest[i] = (palette[(pixel >> index_shift) & index_mask] & 0xffffff) |
                      static_cast<w8_ulong>(alpha_lut[(pixel >> alpha_shift) & alpha_mask])
                          << 24;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        const unsigned short* source = static_cast<const unsigned short*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            dest[i] = (palette[(pixel >> index_shift) & index_mask] & 0xffffff) |
                      static_cast<w8_ulong>(alpha_lut[(pixel >> alpha_shift) & alpha_mask])
                          << 24;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[0] | source[1] << 8 | source[2] << 16;
            dest[i] = (palette[(pixel >> index_shift) & index_mask] & 0xffffff) |
                      static_cast<w8_ulong>(alpha_lut[(pixel >> alpha_shift) & alpha_mask])
                          << 24;
            source += 3;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            dest[i] = (palette[(pixel >> index_shift) & index_mask] & 0xffffff) |
                      static_cast<w8_ulong>(alpha_lut[(pixel >> alpha_shift) & alpha_mask])
                          << 24;
        }
    }
    }
}

/* IXXA (intensity + optional alpha) write dispatcher: 8-bit destinations
   compute the luma index inline, wider destinations go through the shared
   intensity kernels. */
// FUNCTION: SURRENDER 0x10009DB0
void __cdecl writeIntensity(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    const unsigned char* intensity_lut = channel_reduce[format->red_bits];
    const unsigned char* alpha_lut = channel_reduce[format->alpha_bits];
    int has_alpha = format->alpha_bits != 0;
    const srARGB* source = static_cast<const srARGB*>(info.source);
    srARGB pixel;
    w8_ulong count = info.count;
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        unsigned char* dest = static_cast<unsigned char*>(info.dest);
        w8_ulong i = 0;
        if (has_alpha != 0) {
            for (; i < (count & ~1UL); i += 2) {
                pixel = source[i];
                w8_ulong luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i] = intensity_lut[luma] << format->red_shift | alpha_lut[pixel.alpha]
                                                                         << format->alpha_shift;
                pixel = source[i + 1];
                luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i + 1] = intensity_lut[luma] << format->red_shift | alpha_lut[pixel.alpha]
                                                                             << format->alpha_shift;
            }
            for (; i < count; i++) {
                pixel = source[i];
                w8_ulong luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i] = intensity_lut[luma] << format->red_shift | alpha_lut[pixel.alpha]
                                                                         << format->alpha_shift;
            }
        } else {
            for (; i < (count & ~1UL); i += 2) {
                pixel = source[i];
                w8_ulong luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i] = intensity_lut[luma] << format->red_shift;
                pixel = source[i + 1];
                luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i + 1] = intensity_lut[luma] << format->red_shift;
            }
            for (; i < count; i++) {
                pixel = source[i];
                w8_ulong luma =
                    (lutRamp54[pixel.red] + lutRamp183[pixel.green] + lutRamp18[pixel.blue]) >> 8;
                dest[i] = intensity_lut[luma] << format->red_shift;
            }
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16:
        packIntensity16(static_cast<unsigned short*>(info.dest), source, alpha_lut, intensity_lut,
                        format->alpha_shift, format->red_shift, count, has_alpha);
        return;
    case srPixelConvert::PIXEL_SIZE_24:
        packIntensity24(static_cast<unsigned char*>(info.dest), source, alpha_lut, intensity_lut,
                        format->alpha_shift, format->red_shift, count, has_alpha);
        return;
    case srPixelConvert::PIXEL_SIZE_32:
        packIntensity32(static_cast<w8_ulong*>(info.dest), source, alpha_lut, intensity_lut,
                        format->alpha_shift, format->red_shift, count, has_alpha);
    }
}

/* IXXA read dispatcher: every source width expands inline through the
   intensity and alpha luts, producing grayscale pixels via lutGray with
   the alpha channel overlaid in the top byte. */
// FUNCTION: SURRENDER 0x1000A0D0
void __cdecl readIntensity(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    w8_ulong intensity_mask = (1ul << format->red_bits) - 1;
    w8_ulong alpha_mask = (1ul << format->alpha_bits) - 1;
    const unsigned char* intensity_lut = channel_expand[format->red_bits];
    const unsigned char* alpha_lut = channel_expand[format->alpha_bits];
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    w8_ulong count = info.count;
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            const unsigned char* gray =
                lutGray[intensity_lut[(pixel >> format->red_shift) & intensity_mask]];
            /* reinterpret-ok: packed BGRA gray entry. */
            dest[i] =
                *reinterpret_cast<const w8_ulong*>(gray) |
                static_cast<w8_ulong>(alpha_lut[(pixel >> format->alpha_shift) & alpha_mask])
                    << 24;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        const unsigned short* source = static_cast<const unsigned short*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            const unsigned char* gray =
                lutGray[intensity_lut[(pixel >> format->red_shift) & intensity_mask]];
            /* reinterpret-ok: packed BGRA gray entry. */
            dest[i] =
                *reinterpret_cast<const w8_ulong*>(gray) |
                static_cast<w8_ulong>(alpha_lut[(pixel >> format->alpha_shift) & alpha_mask])
                    << 24;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            /* reinterpret-ok: 24-bit records load their high two bytes as a word. */
            w8_ulong pixel =
                readPackedWord(source + 1) * 0x100 + source[0];
            const unsigned char* gray =
                lutGray[intensity_lut[(pixel >> format->red_shift) & intensity_mask]];
            /* reinterpret-ok: packed BGRA gray entry. */
            dest[i] =
                *reinterpret_cast<const w8_ulong*>(gray) |
                static_cast<w8_ulong>(alpha_lut[(pixel >> format->alpha_shift) & alpha_mask])
                    << 24;
            source += 3;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
        for (w8_ulong i = 0; i < count; i++) {
            w8_ulong pixel = source[i];
            const unsigned char* gray =
                lutGray[intensity_lut[(pixel >> format->red_shift) & intensity_mask]];
            /* reinterpret-ok: packed BGRA gray entry. */
            dest[i] =
                *reinterpret_cast<const w8_ulong*>(gray) |
                static_cast<w8_ulong>(alpha_lut[(pixel >> format->alpha_shift) & alpha_mask])
                    << 24;
        }
    }
    }
}

// FUNCTION: SURRENDER 0x1000A3B0
void __cdecl writeRGB(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    const unsigned char* luts[4];
    unsigned char shifts[4];
    luts[0] = channel_reduce[format->red_bits];
    luts[1] = channel_reduce[format->green_bits];
    luts[2] = channel_reduce[format->blue_bits];
    luts[3] = channel_reduce[format->alpha_bits];
    shifts[0] = format->red_shift;
    shifts[1] = format->green_shift;
    shifts[2] = format->blue_shift;
    shifts[3] = format->alpha_shift;
    int has_alpha = format->alpha_bits != 0;
    const srARGB* source = static_cast<const srARGB*>(info.source);
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        pack8(static_cast<unsigned char*>(info.dest), source, luts, shifts, info.count, has_alpha);
        return;
    case srPixelConvert::PIXEL_SIZE_16:
        pack16(static_cast<unsigned short*>(info.dest), source, luts, shifts, info.count,
               has_alpha);
        return;
    case srPixelConvert::PIXEL_SIZE_24:
        pack24(static_cast<unsigned char*>(info.dest), source, luts, shifts, info.count, has_alpha);
        return;
    case srPixelConvert::PIXEL_SIZE_32:
        pack32(static_cast<w8_ulong*>(info.dest), source, luts, shifts, info.count, has_alpha);
    }
}

// FUNCTION: SURRENDER 0x1000A4D0
void __cdecl readRGB(const srPixelConvert::ConversionInfo& info)
{
    const srPixelConvert::PixelFormat* format = info.format;
    w8_ulong masks[4];
    unsigned char shifts[4];
    const unsigned char* luts[4];
    masks[0] = (1ul << format->red_bits) - 1;
    masks[1] = (1ul << format->green_bits) - 1;
    masks[2] = (1ul << format->blue_bits) - 1;
    masks[3] = (1ul << format->alpha_bits) - 1;
    shifts[0] = format->red_shift;
    shifts[1] = format->green_shift;
    shifts[2] = format->blue_shift;
    shifts[3] = format->alpha_shift;
    luts[0] = channel_expand[format->red_bits];
    luts[1] = channel_expand[format->green_bits];
    luts[2] = channel_expand[format->blue_bits];
    luts[3] = channel_expand[format->alpha_bits];
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    switch (format->pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        const unsigned char* source = static_cast<const unsigned char*>(info.source);
        w8_ulong i = 0;
        for (; i < (info.count & ~3UL); i += 4) {
            w8_ulong pixel = source[i];
            dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
            pixel = source[i + 1];
            dest[i + 1] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                          luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                          luts[2][(pixel >> shifts[2]) & masks[2]] |
                          luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
            pixel = source[i + 2];
            dest[i + 2] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                          luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                          luts[2][(pixel >> shifts[2]) & masks[2]] |
                          luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
            pixel = source[i + 3];
            dest[i + 3] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                          luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                          luts[2][(pixel >> shifts[2]) & masks[2]] |
                          luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        }
        for (; i < info.count; i++) {
            w8_ulong pixel = source[i];
            dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        }
        return;
    }
    case srPixelConvert::PIXEL_SIZE_16:
        unpack16(dest, static_cast<const unsigned short*>(info.source), luts, shifts, masks,
                 info.count);
        return;
    case srPixelConvert::PIXEL_SIZE_24:
        unpack24(dest, static_cast<const unsigned char*>(info.source), luts, shifts, masks,
                 info.count);
        return;
    case srPixelConvert::PIXEL_SIZE_32:
        unpack32(dest, static_cast<const w8_ulong*>(info.source), luts, shifts, masks,
                 info.count);
    }
}

/* Per-format overrides installed by initFormats() for formats whose
   converter does not fit the generic kernels: the 8-bit indexed pair
   delegates to the vector processor copy, the 32-bit color-keyed formats
   mask through _and/_or, and the packed formats run dedicated loops. */
// FUNCTION: SURRENDER 0x1000A8C0
void __cdecl writeRGB24(const srPixelConvert::ConversionInfo& info)
{
    unsigned char* dest = static_cast<unsigned char*>(info.dest);
    const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
    for (w8_ulong i = info.count; i > 0; i--) {
        w8_ulong pixel = *source++;
        dest[0] = static_cast<unsigned char>(pixel >> 16);
        dest[1] = static_cast<unsigned char>(pixel >> 8);
        dest[2] = static_cast<unsigned char>(pixel);
        dest += 3;
    }
}

// FUNCTION: SURRENDER 0x1000A900
void __cdecl readRGB24(const srPixelConvert::ConversionInfo& info)
{
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    const unsigned char* source = static_cast<const unsigned char*>(info.source);
    for (w8_ulong i = info.count; i > 0; i--) {
        w8_ulong pixel = source[0] | 0xffffff00;
        pixel = pixel << 8 | source[1];
        pixel = pixel << 8 | source[2];
        *dest++ = pixel;
        source += 3;
    }
}

/* format_table[srPixelConvert::SURFACE_BGRA32] write/read: straight dword copy for BGRA32. */
// FUNCTION: SURRENDER 0x1000A950
void __cdecl writeBGRA(const srPixelConvert::ConversionInfo& info)
{
    if (info.count != 0 && info.dest != info.source) {
        srVectorProcessor::memcopy(info.dest, info.source, info.count * 4);
    }
}

// FUNCTION: SURRENDER 0x1000A980
void __cdecl readBGRA(const srPixelConvert::ConversionInfo& info)
{
    if (info.count != 0 && info.dest != info.source) {
        srVectorProcessor::memcopy(info.dest, info.source, info.count * 4);
    }
}

// FUNCTION: SURRENDER 0x1000A9B0
void __cdecl writeBGRX(const srPixelConvert::ConversionInfo& info)
{
    srVectorProcessor::bitwiseAnd(static_cast<SRDWORD*>(info.dest),
                                  static_cast<const SRDWORD*>(info.source), 0xffffff, info.count);
}

// FUNCTION: SURRENDER 0x1000A9E0
void __cdecl readBGRX(const srPixelConvert::ConversionInfo& info)
{
    srVectorProcessor::bitwiseOr(static_cast<SRDWORD*>(info.dest),
                                 static_cast<const SRDWORD*>(info.source), 0xff000000, info.count);
}

/* format_table[srPixelConvert::SURFACE_RGBA32] write/read: rotate each BGRA pixel one byte lane so
   red leads the record on write and BGRA is restored on read. */
// FUNCTION: SURRENDER 0x1000AA10
void __cdecl writeABGR(const srPixelConvert::ConversionInfo& info)
{
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
    w8_ulong i = 0;
    for (; i + 8 <= info.count; i += 8) {
        dest[i] = source[i] << 8 | source[i] >> 24;
        dest[i + 1] = source[i + 1] << 8 | source[i + 1] >> 24;
        dest[i + 2] = source[i + 2] << 8 | source[i + 2] >> 24;
        dest[i + 3] = source[i + 3] << 8 | source[i + 3] >> 24;
        dest[i + 4] = source[i + 4] << 8 | source[i + 4] >> 24;
        dest[i + 5] = source[i + 5] << 8 | source[i + 5] >> 24;
        dest[i + 6] = source[i + 6] << 8 | source[i + 6] >> 24;
        dest[i + 7] = source[i + 7] << 8 | source[i + 7] >> 24;
    }
    for (; i < info.count; i++) {
        dest[i] = source[i] << 8 | source[i] >> 24;
    }
}

// FUNCTION: SURRENDER 0x1000AB70
void __cdecl readABGR(const srPixelConvert::ConversionInfo& info)
{
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    const w8_ulong* source = static_cast<const w8_ulong*>(info.source);
    w8_ulong i = 0;
    for (; i + 8 <= info.count; i += 8) {
        dest[i] = source[i] << 24 | source[i] >> 8;
        dest[i + 1] = source[i + 1] << 24 | source[i + 1] >> 8;
        dest[i + 2] = source[i + 2] << 24 | source[i + 2] >> 8;
        dest[i + 3] = source[i + 3] << 24 | source[i + 3] >> 8;
        dest[i + 4] = source[i + 4] << 24 | source[i + 4] >> 8;
        dest[i + 5] = source[i + 5] << 24 | source[i + 5] >> 8;
        dest[i + 6] = source[i + 6] << 24 | source[i + 6] >> 8;
        dest[i + 7] = source[i + 7] << 24 | source[i + 7] >> 8;
    }
    for (; i < info.count; i++) {
        dest[i] = source[i] << 24 | source[i] >> 8;
    }
}

/* format_table[srPixelConvert::SURFACE_RGB555] write/read: 32-bit BGRA packed to RGB555 through the
   5-bit reduction table, and expanded back with alpha forced opaque. */
// FUNCTION: SURRENDER 0x1000ACD0
void __cdecl writeRGB555(const srPixelConvert::ConversionInfo& info)
{
    unsigned short* dest = static_cast<unsigned short*>(info.dest);
    const srARGB* source = static_cast<const srARGB*>(info.source);
    w8_ulong i = 0;
    for (; i < (info.count & ~3UL); i += 4) {
        srARGB pixel = source[i];
        dest[i] =
            lutReduce32[pixel.red] << 10 | lutReduce32[pixel.green] << 5 | lutReduce32[pixel.blue];
        pixel = source[i + 1];
        dest[i + 1] =
            lutReduce32[pixel.red] << 10 | lutReduce32[pixel.green] << 5 | lutReduce32[pixel.blue];
        pixel = source[i + 2];
        dest[i + 2] =
            lutReduce32[pixel.red] << 10 | lutReduce32[pixel.green] << 5 | lutReduce32[pixel.blue];
        pixel = source[i + 3];
        dest[i + 3] =
            lutReduce32[pixel.red] << 10 | lutReduce32[pixel.green] << 5 | lutReduce32[pixel.blue];
    }
    for (; i < info.count; i++) {
        srARGB pixel = source[i];
        dest[i] =
            lutReduce32[pixel.red] << 10 | lutReduce32[pixel.green] << 5 | lutReduce32[pixel.blue];
    }
}

// FUNCTION: SURRENDER 0x1000AE80
void __cdecl readRGB555(const srPixelConvert::ConversionInfo& info)
{
    w8_ulong* dest = static_cast<w8_ulong*>(info.dest);
    const unsigned short* source = static_cast<const unsigned short*>(info.source);
    w8_ulong i = 0;
    for (; i < (info.count & ~3UL); i += 4) {
        w8_ulong pixel = source[i];
        dest[i] = 0xff000000 | lutExpand5And6.expand32[pixel >> 10] << 16 |
                  lutExpand5And6.expand32[pixel >> 5 & 0x1f] << 8 |
                  lutExpand5And6.expand32[pixel & 0x1f];
        pixel = source[i + 1];
        dest[i + 1] = 0xff000000 | lutExpand5And6.expand32[pixel >> 10] << 16 |
                      lutExpand5And6.expand32[pixel >> 5 & 0x1f] << 8 |
                      lutExpand5And6.expand32[pixel & 0x1f];
        pixel = source[i + 2];
        dest[i + 2] = 0xff000000 | lutExpand5And6.expand32[pixel >> 10] << 16 |
                      lutExpand5And6.expand32[pixel >> 5 & 0x1f] << 8 |
                      lutExpand5And6.expand32[pixel & 0x1f];
        pixel = source[i + 3];
        dest[i + 3] = 0xff000000 | lutExpand5And6.expand32[pixel >> 10] << 16 |
                      lutExpand5And6.expand32[pixel >> 5 & 0x1f] << 8 |
                      lutExpand5And6.expand32[pixel & 0x1f];
    }
    for (; i < info.count; i++) {
        w8_ulong pixel = source[i];
        dest[i] = 0xff000000 | lutExpand5And6.expand32[pixel >> 10] << 16 |
                  lutExpand5And6.expand32[pixel >> 5 & 0x1f] << 8 |
                  lutExpand5And6.expand32[pixel & 0x1f];
    }
}

/* MMX conversion workers, installed over the scalar table entries by
   initFormats() when the CPU reports the feature bit. Each handles a
   scalar alignment head, an MMX main loop, then a scalar tail. */

/* format_table[srPixelConvert::SURFACE_BGR24] MMX read: BGR24 source records to srARGB with alpha
   forced opaque. */
// FUNCTION: SURRENDER 0x1000B050
void __cdecl readBGR24MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_ARGB4444] MMX read: ARGB4444 source words to srARGB with each
   nibble replicated into its byte lane. */
// FUNCTION: SURRENDER 0x1000B150
void __cdecl readARGB4444MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_L8] MMX read: L8 source bytes to srARGB by triplicating the
   index and forcing alpha opaque. */
// FUNCTION: SURRENDER 0x1000B250
void __cdecl readL8MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_ARGB1555] MMX read: ARGB1555 source words to srARGB with bit
   replication filling the low channel bits. */
// FUNCTION: SURRENDER 0x1000B330
void __cdecl readARGB1555MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_RGB565] MMX read: RGB565 source words to srARGB with bit
   replication and alpha forced opaque. */
// FUNCTION: SURRENDER 0x1000B440
void __cdecl readRGB565MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_L8] MMX write: srARGB to L8 intensity using the 54/183/19
   luma weights. */
// FUNCTION: SURRENDER 0x1000B570
void __cdecl writeL8MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_RGB565] MMX write: srARGB to RGB565. */
// FUNCTION: SURRENDER 0x1000B6A0
void __cdecl writeRGB565MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_ARGB4444] MMX write: srARGB to ARGB4444 through the high
   nibbles. */
// FUNCTION: SURRENDER 0x1000B7C0
void __cdecl writeARGB4444MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_ARGB1555] MMX write: srARGB to ARGB1555. */
// FUNCTION: SURRENDER 0x1000B8A0
void __cdecl writeARGB1555MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* format_table[srPixelConvert::SURFACE_BGR24] MMX write: srARGB to BGR24 triplets. */
// FUNCTION: SURRENDER 0x1000B9D0
void __cdecl writeBGR24MMX(const srPixelConvert::ConversionInfo& info)
{
    abort(); /* MMX workers are never selected natively */
}

/* Intensity write kernels: 32-bit BGRA source to 16/24/32-bit IXXA
   records. The luma index sums the fixed-point channel weight ramps. */
// FUNCTION: SURRENDER 0x1000BAD0
static void packIntensity16(unsigned short* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                      << intensity_shift;
            pixel = source[i + 1];
            dest[i + 1] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                         lutRamp18[pixel.blue]) >>
                                        8]
                          << intensity_shift;
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                      << intensity_shift;
        }
    } else {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                          << intensity_shift |
                      alpha_lut[pixel.alpha] << alpha_shift;
            pixel = source[i + 1];
            dest[i + 1] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                         lutRamp18[pixel.blue]) >>
                                        8]
                              << intensity_shift |
                          alpha_lut[pixel.alpha] << alpha_shift;
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                          << intensity_shift |
                      alpha_lut[pixel.alpha] << alpha_shift;
        }
    }
}

// FUNCTION: SURRENDER 0x1000BD60
static void packIntensity24(unsigned char* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            w8_ulong value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                                 lutRamp18[pixel.blue]) >>
                                                8]
                                  << intensity_shift;
            /* reinterpret-ok: 24-bit records store their low word separately. */
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 1];
            value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                   lutRamp18[pixel.blue]) >>
                                  8]
                    << intensity_shift;
            writePackedWord(dest + 3, static_cast<unsigned short>(value));
            dest[5] = static_cast<unsigned char>(value >> 16);
            dest += 6;
        }
        for (; i < count; i++) {
            pixel = source[i];
            w8_ulong value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                                 lutRamp18[pixel.blue]) >>
                                                8]
                                  << intensity_shift;
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            dest += 3;
        }
    } else {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            w8_ulong value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                                 lutRamp18[pixel.blue]) >>
                                                8]
                                      << intensity_shift |
                                  alpha_lut[pixel.alpha] << alpha_shift;
            /* reinterpret-ok: 24-bit records store their low word separately. */
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 1];
            value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                   lutRamp18[pixel.blue]) >>
                                  8]
                        << intensity_shift |
                    alpha_lut[pixel.alpha] << alpha_shift;
            writePackedWord(dest + 3, static_cast<unsigned short>(value));
            dest[5] = static_cast<unsigned char>(value >> 16);
            dest += 6;
        }
        for (; i < count; i++) {
            pixel = source[i];
            w8_ulong value = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                                 lutRamp18[pixel.blue]) >>
                                                8]
                                      << intensity_shift |
                                  alpha_lut[pixel.alpha] << alpha_shift;
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            dest += 3;
        }
    }
}

// FUNCTION: SURRENDER 0x1000C0A0
static void packIntensity32(w8_ulong* dest, const srARGB* source,
                            const unsigned char* alpha_lut, const unsigned char* intensity_lut,
                            unsigned char alpha_shift, unsigned char intensity_shift,
                            w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                      << intensity_shift;
            pixel = source[i + 1];
            dest[i + 1] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                         lutRamp18[pixel.blue]) >>
                                        8]
                          << intensity_shift;
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                      << intensity_shift;
        }
    } else {
        for (; i < (count & ~1UL); i += 2) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                          << intensity_shift |
                      alpha_lut[pixel.alpha] << alpha_shift;
            pixel = source[i + 1];
            dest[i + 1] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                         lutRamp18[pixel.blue]) >>
                                        8]
                              << intensity_shift |
                          alpha_lut[pixel.alpha] << alpha_shift;
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = intensity_lut[(lutRamp54[pixel.red] + lutRamp183[pixel.green] +
                                     lutRamp18[pixel.blue]) >>
                                    8]
                          << intensity_shift |
                      alpha_lut[pixel.alpha] << alpha_shift;
        }
    }
}

/* Generic write kernels: 32-bit BGRA source packed through the channel
   reduction luts into 8/16/24/32-bit records. */
// FUNCTION: SURRENDER 0x1000C350
static void pack8(unsigned char* dest, const srARGB* source, const unsigned char* const* luts,
                  const unsigned char* shifts, w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
        }
    } else {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
    }
}

// FUNCTION: SURRENDER 0x1000C800
static void pack16(unsigned short* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
        }
    } else {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
    }
}

// FUNCTION: SURRENDER 0x1000CC90
static void pack24(unsigned char* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            w8_ulong value = luts[0][pixel.red] << shifts[0] |
                                  luts[1][pixel.green] << shifts[1] |
                                  luts[2][pixel.blue] << shifts[2];
            /* reinterpret-ok: 24-bit records store their low word separately. */
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 1];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 3, static_cast<unsigned short>(value));
            dest[5] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 2];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 6, static_cast<unsigned short>(value));
            dest[8] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 3];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 9, static_cast<unsigned short>(value));
            dest[11] = static_cast<unsigned char>(value >> 16);
            dest += 12;
        }
        for (; i < count; i++) {
            pixel = source[i];
            w8_ulong value = luts[0][pixel.red] << shifts[0] |
                                  luts[1][pixel.green] << shifts[1] |
                                  luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            dest += 3;
        }
    } else {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            w8_ulong value =
                luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            /* reinterpret-ok: 24-bit records store their low word separately. */
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 1];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 3, static_cast<unsigned short>(value));
            dest[5] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 2];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 6, static_cast<unsigned short>(value));
            dest[8] = static_cast<unsigned char>(value >> 16);
            pixel = source[i + 3];
            value = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                    luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest + 9, static_cast<unsigned short>(value));
            dest[11] = static_cast<unsigned char>(value >> 16);
            dest += 12;
        }
        for (; i < count; i++) {
            pixel = source[i];
            w8_ulong value =
                luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            writePackedWord(dest, static_cast<unsigned short>(value));
            dest[2] = static_cast<unsigned char>(value >> 16);
            dest += 3;
        }
    }
}

// FUNCTION: SURRENDER 0x1000D280
static void pack32(w8_ulong* dest, const srARGB* source, const unsigned char* const* luts,
                   const unsigned char* shifts, w8_ulong count, int has_alpha)
{
    srARGB pixel;
    w8_ulong i = 0;
    if (has_alpha == 0) {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[2][pixel.blue] << shifts[2];
        }
    } else {
        for (; i < (count & ~3UL); i += 4) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 1];
            dest[i + 1] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 2];
            dest[i + 2] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
            pixel = source[i + 3];
            dest[i + 3] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                          luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
        for (; i < count; i++) {
            pixel = source[i];
            dest[i] = luts[0][pixel.red] << shifts[0] | luts[1][pixel.green] << shifts[1] |
                      luts[3][pixel.alpha] << shifts[3] | luts[2][pixel.blue] << shifts[2];
        }
    }
}

/* Generic read kernels: packed 16/24/32-bit source records expanded to
   32-bit BGRA through the channel expansion luts, shifts and masks. */
// FUNCTION: SURRENDER 0x1000D740
static void unpack16(w8_ulong* dest, const unsigned short* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count)
{
    w8_ulong i = 0;
    for (; i < (count & ~3UL); i += 4) {
        w8_ulong pixel = source[i];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 1];
        dest[i + 1] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 2];
        dest[i + 2] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 3];
        dest[i + 3] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
    }
    for (; i < count; i++) {
        w8_ulong pixel = source[i];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
    }
}

// FUNCTION: SURRENDER 0x1000DA20
static void unpack24(w8_ulong* dest, const unsigned char* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count)
{
    w8_ulong i = 0;
    for (; i < (count & ~3UL); i += 4) {
        /* reinterpret-ok: 24-bit records load their high two bytes as a word. */
        w8_ulong pixel =
            readPackedWord(source + 1) * 0x100 + source[0];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = readPackedWord(source + 4) * 0x100 + source[3];
        dest[i + 1] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = readPackedWord(source + 7) * 0x100 + source[6];
        dest[i + 2] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = readPackedWord(source + 10) * 0x100 + source[9];
        dest[i + 3] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        source += 12;
    }
    for (; i < count; i++) {
        w8_ulong pixel =
            readPackedWord(source + 1) * 0x100 + source[0];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        source += 3;
    }
}

// FUNCTION: SURRENDER 0x1000DD60
static void unpack32(w8_ulong* dest, const w8_ulong* source,
                     const unsigned char* const* luts, const unsigned char* shifts,
                     const w8_ulong* masks, w8_ulong count)
{
    w8_ulong i = 0;
    for (; i < (count & ~3UL); i += 4) {
        w8_ulong pixel = source[i];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 1];
        dest[i + 1] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 2];
        dest[i + 2] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
        pixel = source[i + 3];
        dest[i + 3] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                      luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                      luts[2][(pixel >> shifts[2]) & masks[2]] |
                      luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
    }
    for (; i < count; i++) {
        w8_ulong pixel = source[i];
        dest[i] = luts[0][(pixel >> shifts[0]) & masks[0]] << 16 |
                  luts[1][(pixel >> shifts[1]) & masks[1]] << 8 |
                  luts[2][(pixel >> shifts[2]) & masks[2]] |
                  luts[3][(pixel >> shifts[3]) & masks[3]] << 24;
    }
}
