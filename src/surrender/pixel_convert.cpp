#include <SDL3/SDL_pixels.h>
#include "glm/ext/vector_uint4.hpp"
#include "glm/ext/vector_float3.hpp"
#include <algorithm>
#include <iterator>
#include <string>
#include <cstring>

#include "glm/geometric.hpp"
#include "surrender/srPixelConvert.h"
#include "wiz8/compat/surfaces.h"
#include "surrender/srARGB.h"
#include "surrender/srMath.h"
#include "surrender/srPalette.h"
#include "compat/compiler.h"
#include "compat/native.h"

/* Conversion routines stored in the format table. The generic pair is
   selected by PixelFormat::color_model; the per-entry overrides cover
   formats whose converter does not fit a generic kernel. */
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
void __cdecl copyBGRA(const srPixelConvert::ConversionInfo& info);
void __cdecl writeABGR(const srPixelConvert::ConversionInfo& info);
void __cdecl readABGR(const srPixelConvert::ConversionInfo& info);
void __cdecl writeRGB24(const srPixelConvert::ConversionInfo& info);
void __cdecl readRGB24(const srPixelConvert::ConversionInfo& info);

namespace {

static srPixelConvert::ConversionFunctions GenericConversionFunctions(srPixelConvert::e_colorModel model)
{
    switch (model) {
    case srPixelConvert::COLOR_RGB: return {writeRGB, readRGB};
    case srPixelConvert::COLOR_YUV: return {writeYUV, readYUV};
    case srPixelConvert::COLOR_INTENSITY: return {writeIntensity, readIntensity};
    case srPixelConvert::COLOR_INDEXED: return {writeIndexed, readIndexed};
    }
    return {};
}

struct FormatEntry {
    srPixelConvert::PixelFormat format;
    srPixelConvert::ConversionFunc write;
    srPixelConvert::ConversionFunc read;
};

/* Rounded channel expansion/reduction and fixed-point luma tables. */
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
const glm::vec3 yuvToRGB[3] = {
    glm::vec3(1.0f, 0.956f, 0.620f),
    glm::vec3(1.0f, -0.272f, -0.647f),
    glm::vec3(1.0f, -1.108f, 1.705f),
};



// GLOBAL: SURRENDER 0x100A2BFC
unsigned char lutExpand128[128];
// GLOBAL: SURRENDER 0x100A2C7C
w8_long lutRamp18[256];
// GLOBAL: SURRENDER 0x100A397C
w8_long lutRamp54[256];
// GLOBAL: SURRENDER 0x100A3D80
const glm::vec3 rgbToYUV[3] = {
    glm::vec3(0.299f, 0.587f, 0.114f),
    glm::vec3(0.596f, -0.275f, -0.321f),
    glm::vec3(0.212f, -0.528f, 0.311f),
};
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
}

} // namespace

/* Rounded channel expansion/reduction and fixed-point luma weights. */
// FUNCTION: SURRENDER 0x10007850
void __cdecl initPixelTables(void)
{
    int i;
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
    for (auto& entry : format_table) {
        const auto functions = GenericConversionFunctions(entry.format.color_model);
        entry.write = functions.write;
        entry.read = functions.read;
    }
    format_table[srPixelConvert::SURFACE_RGB555].write = writeRGB555;
    format_table[srPixelConvert::SURFACE_RGB555].read = readRGB555;
    format_table[srPixelConvert::SURFACE_BGRX32].write = writeBGRX;
    format_table[srPixelConvert::SURFACE_BGRX32].read = readBGRX;
    format_table[srPixelConvert::SURFACE_BGRA32].write = copyBGRA;
    format_table[srPixelConvert::SURFACE_BGRA32].read = copyBGRA;
    format_table[srPixelConvert::SURFACE_ABGR32].write = writeABGR;
    format_table[srPixelConvert::SURFACE_ABGR32].read = readABGR;
    format_table[srPixelConvert::SURFACE_RGB24].write = writeRGB24;
    format_table[srPixelConvert::SURFACE_RGB24].read = readRGB24;
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
std::string srPixelConvert::PixelFormat::getName() const
{
    static const char channel_letters[] = "RGBAYUVAIXXAPXXA";

    if (fourcc != 0) {
        char code[5] = {static_cast<char>(fourcc), static_cast<char>(fourcc >> 8),
                        static_cast<char>(fourcc >> 16), static_cast<char>(fourcc >> 24), 0};
        return code;
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

    std::string name;
    for (int k = 0; k < 4; k++)
        if (bits[k] != 0)
            name.push_back(channel_letters[color_model * 4 + order[k]]);
    for (int j = 0; j < 4; j++)
        if (bits[j] != 0)
            name.push_back(static_cast<char>(bits[j] + '0'));
    name += '/';
    name += std::to_string(pixel_size * 8 + 8);
    return name;
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
srPixelConvert::PixelFormat srPixelConvert::pixelFormat(e_surfaceType type)
{
    initFormats();
    if (static_cast<int>(type) < 0 || static_cast<int>(type) > 0x18) {
        type = SURFACE_BGRA32;
    }
    return format_table[type].format;
}

// FUNCTION: SURRENDER 0x100087A0
srPixelConvert::ConversionFunctions srPixelConvert::selectFuncs(const PixelFormat& format)
{
    initFormats();
    const auto entry = std::find_if(std::begin(format_table), std::end(format_table),
        [&](const FormatEntry& candidate) { return candidate.format == format; });
    if (entry != std::end(format_table))
        return {entry->write, entry->read};

    return GenericConversionFunctions(format.color_model);
}

// Packed storage is byte-addressed, including unaligned 24-bit pixels.
struct PixelAt {
    w8_ulong value;
    w8_ulong index;
};

template<class Convert>
static void ConvertPixelRun(const srPixelConvert::ConversionInfo& info, unsigned source_bytes,
                            unsigned destination_bytes, Convert convert)
{
    const auto* source = static_cast<const unsigned char*>(info.source);
    auto* destination = static_cast<unsigned char*>(info.dest);
    for (w8_ulong index = 0; index < info.count; ++index) {
        w8_ulong pixel = 0;
        std::memcpy(&pixel, source + index * source_bytes, source_bytes);
        const auto output = convert(pixel, index);
        std::memcpy(destination + output.index * destination_bytes, &output.value, destination_bytes);
    }
}

struct PackedChannel {
    unsigned shift;
    w8_ulong mask;
    const unsigned char* expand_table;
    const unsigned char* reduce_table;

    PackedChannel(unsigned bits, unsigned shift)
        : shift(shift), mask((1u << bits) - 1),
          expand_table(channel_expand[bits]), reduce_table(channel_reduce[bits]) {}
    w8_ulong extract(w8_ulong pixel) const { return (pixel >> shift) & mask; }
    w8_ulong expand(w8_ulong pixel) const { return expand_table[extract(pixel)]; }
    w8_ulong reduce(unsigned value) const { return w8_ulong(reduce_table[value]) << shift; }
};

struct PackedChannels {
    PackedChannel red, green, blue, alpha;
    explicit PackedChannels(const srPixelConvert::PixelFormat& format)
        : red(format.red_bits, format.red_shift), green(format.green_bits, format.green_shift),
          blue(format.blue_bits, format.blue_shift), alpha(format.alpha_bits, format.alpha_shift) {}
    glm::uvec4 expand(w8_ulong pixel) const
    {
        return {red.expand(pixel), green.expand(pixel), blue.expand(pixel), alpha.expand(pixel)};
    }
    w8_ulong reduce(const glm::uvec4& value) const
    {
        return red.reduce(value.r) | green.reduce(value.g) | blue.reduce(value.b) | alpha.reduce(value.a);
    }
};

static glm::uvec4 ARGBChannels(w8_ulong pixel)
{
    return {(pixel >> 16) & 255, (pixel >> 8) & 255, pixel & 255, pixel >> 24};
}

static w8_ulong PackARGB(const glm::uvec4& channels)
{
    return channels.r << 16 | channels.g << 8 | channels.b | channels.a << 24;
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
    const auto& format = *info.format;
    const PackedChannels channels(format);
    ConvertPixelRun(info, 4, format.pixel_size + 1, [&](w8_ulong pixel, w8_ulong) {
        const auto color = ARGBChannels(pixel);
        const glm::vec3 rgb(color);
        const glm::vec3 yuv(glm::dot(rgbToYUV[0], rgb), glm::dot(rgbToYUV[1], rgb),
                            glm::dot(rgbToYUV[2], rgb));
        const auto y = srFloatToInt(yuv.x), u = srFloatToInt(yuv.y), v = srFloatToInt(yuv.z);
        const auto index = channels.green.reduce((u >> 8) & 255);
        return PixelAt{channels.red.reduce((y >> 16) & 255) | index |
                       channels.blue.reduce(v & 255) | channels.alpha.reduce(color.a), index};
    });
}

/* YUVA read dispatcher: every case expands the packed Y, U and V channels through the format's
   channel expansion tables, dot-products them with the yuvToRGB rows, clamps the results and packs
   an srARGB. The destination index comes from the expanded green (U) channel instead of the loop
   index. */
// FUNCTION: SURRENDER 0x10008F70
void __cdecl readYUV(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    const PackedChannels channels(format);
    ConvertPixelRun(info, format.pixel_size + 1, 4, [&](w8_ulong pixel, w8_ulong) {
        const auto color = channels.expand(pixel);
        const glm::vec3 yuv(color);
        const glm::vec3 rgb(glm::dot(yuvToRGB[0], yuv), glm::dot(yuvToRGB[1], yuv),
                            glm::dot(yuvToRGB[2], yuv));
        return PixelAt{PackARGB({clampChannel(rgb.x), clampChannel(rgb.y), clampChannel(rgb.z), color.a}),
                       color.g};
    });
}

/* PXXA (palette index + optional alpha) write dispatcher: every case
   quantizes the source color through the surface palette, shifts the index
   into the red channel position and overlays the reduced alpha. */
// FUNCTION: SURRENDER 0x10009890
void __cdecl writeIndexed(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    const PackedChannel alpha(format.alpha_bits, format.alpha_shift);
    const unsigned bytes = format.pixel_size + 1;
    ConvertPixelRun(info, 4, bytes, [&](w8_ulong pixel, w8_ulong index) {
        const auto color = ARGBChannels(pixel);
        srARGB quantized;
        quantized.red = color.r;
        quantized.green = color.g;
        quantized.blue = color.b;
        quantized.alpha = bytes <= 2 ? 0 : color.a;
        return PixelAt{w8_ulong(info.palette->quantize(quantized)) << format.red_shift |
                       alpha.reduce(color.a), index};
    });
}

/* PXXA read dispatcher: the packed index selects a palette entry whose
   packed BGR is kept, then the expanded alpha is overlaid in the top byte. */
// FUNCTION: SURRENDER 0x10009B00
void __cdecl readIndexed(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    const PackedChannel palette_index(format.red_bits, format.red_shift);
    const PackedChannel alpha(format.alpha_bits, format.alpha_shift);
    /* reinterpret-ok: palette entries are packed srARGB dwords. */
    const auto* palette = reinterpret_cast<const w8_ulong*>(info.palette->getPaletteDataPtr());
    ConvertPixelRun(info, format.pixel_size + 1, 4, [&](w8_ulong pixel, w8_ulong index) {
        return PixelAt{(palette[palette_index.extract(pixel)] & 0xffffff) | alpha.expand(pixel) << 24, index};
    });
}

/* Game-specific fixed-point luma and rounded channel reduction. */
// FUNCTION: SURRENDER 0x10009DB0
void __cdecl writeIntensity(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    const PackedChannel intensity(format.red_bits, format.red_shift);
    const PackedChannel alpha(format.alpha_bits, format.alpha_shift);
    ConvertPixelRun(info, 4, format.pixel_size + 1, [&](w8_ulong pixel, w8_ulong index) {
        const auto color = ARGBChannels(pixel);
        const unsigned luma = (lutRamp54[color.r] + lutRamp183[color.g] + lutRamp18[color.b]) >> 8;
        return PixelAt{intensity.reduce(luma) | alpha.reduce(color.a), index};
    });
}

/* Expand game intensity and alpha channels to ARGB. */
// FUNCTION: SURRENDER 0x1000A0D0
void __cdecl readIntensity(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    const PackedChannel intensity(format.red_bits, format.red_shift);
    const PackedChannel alpha(format.alpha_bits, format.alpha_shift);
    ConvertPixelRun(info, format.pixel_size + 1, 4, [&](w8_ulong pixel, w8_ulong index) {
        return PixelAt{intensity.expand(pixel) * 0x010101u | alpha.expand(pixel) << 24, index};
    });
}

// Byte-channel layouts are exact SDL conversions. Rounded low-bit channels and
// nonstandard masks retain the game's lookup tables.
static SDL_PixelFormat byteChannelFormat(const srPixelConvert::PixelFormat& f)
{
    if (f.red_bits != 8 || f.green_bits != 8 || f.blue_bits != 8 ||
        !((f.pixel_size == srPixelConvert::PIXEL_SIZE_24 && f.alpha_bits == 0) ||
          (f.pixel_size == srPixelConvert::PIXEL_SIZE_32 && f.alpha_bits == 8)))
        return SDL_PIXELFORMAT_UNKNOWN;
    return SDL_GetPixelFormatForMasks((f.pixel_size + 1) * 8,
        255u << f.red_shift, 255u << f.green_shift, 255u << f.blue_shift,
        f.alpha_bits ? 255u << f.alpha_shift : 0);
}

void __cdecl writeRGB(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    if (const auto sdl_format = byteChannelFormat(format); sdl_format != SDL_PIXELFORMAT_UNKNOWN) {
        ConvertCpuPixels(info.dest, info.source, info.count, SDL_PIXELFORMAT_ARGB8888, sdl_format);
        return;
    }
    const PackedChannels channels(format);
    ConvertPixelRun(info, 4, format.pixel_size + 1, [&](w8_ulong pixel, w8_ulong index) {
        return PixelAt{channels.reduce(ARGBChannels(pixel)), index};
    });
}

void __cdecl readRGB(const srPixelConvert::ConversionInfo& info)
{
    const auto& format = *info.format;
    if (const auto sdl_format = byteChannelFormat(format); sdl_format != SDL_PIXELFORMAT_UNKNOWN) {
        ConvertCpuPixels(info.dest, info.source, info.count, sdl_format, SDL_PIXELFORMAT_ARGB8888);
        return;
    }
    const PackedChannels channels(format);
    ConvertPixelRun(info, format.pixel_size + 1, 4, [&](w8_ulong pixel, w8_ulong index) {
        return PixelAt{PackARGB(channels.expand(pixel)), index};
    });
}

/* Per-format overrides installed by initFormats() for formats whose
   converter does not fit the generic kernels: the 8-bit indexed pair
   delegates to the vector processor copy, the 32-bit color-keyed formats
   mask through _and/_or, and the packed formats run dedicated loops. */
static void convertByteChannels(const srPixelConvert::ConversionInfo& info,
                                SDL_PixelFormat sourceFormat, SDL_PixelFormat destFormat)
{
    ConvertCpuPixels(info.dest, info.source, info.count, sourceFormat, destFormat);
}

// FUNCTION: SURRENDER 0x1000A8C0
void __cdecl writeRGB24(const srPixelConvert::ConversionInfo& info)
{
    convertByteChannels(info, SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGB24);
}

// FUNCTION: SURRENDER 0x1000A900
void __cdecl readRGB24(const srPixelConvert::ConversionInfo& info)
{
    convertByteChannels(info, SDL_PIXELFORMAT_RGB24, SDL_PIXELFORMAT_ARGB8888);
}

/* format_table[srPixelConvert::SURFACE_BGRA32] write/read: straight dword copy for BGRA32. */
// FUNCTION: SURRENDER 0x1000A950
void __cdecl copyBGRA(const srPixelConvert::ConversionInfo& info)
{
    if (info.count != 0 && info.dest != info.source) {
        std::memmove(info.dest, info.source, info.count * sizeof(w8_ulong));
    }
}

// FUNCTION: SURRENDER 0x1000A9B0
void __cdecl writeBGRX(const srPixelConvert::ConversionInfo& info)
{
    // SDL writes an opaque X byte; this storage format requires zero instead.
    ConvertPixelRun(info, 4, 4, [](w8_ulong pixel, w8_ulong index) {
        return PixelAt{pixel & 0x00ffffff, index};
    });
}

// FUNCTION: SURRENDER 0x1000A9E0
void __cdecl readBGRX(const srPixelConvert::ConversionInfo& info)
{
    convertByteChannels(info, SDL_PIXELFORMAT_XRGB8888, SDL_PIXELFORMAT_ARGB8888);
}

/* format_table[srPixelConvert::SURFACE_RGBA32] write/read: rotate each BGRA pixel one byte lane so
   red leads the record on write and BGRA is restored on read. */
// FUNCTION: SURRENDER 0x1000AA10
void __cdecl writeABGR(const srPixelConvert::ConversionInfo& info)
{
    convertByteChannels(info, SDL_PIXELFORMAT_ARGB8888, SDL_PIXELFORMAT_RGBA8888);
}

// FUNCTION: SURRENDER 0x1000AB70
void __cdecl readABGR(const srPixelConvert::ConversionInfo& info)
{
    convertByteChannels(info, SDL_PIXELFORMAT_RGBA8888, SDL_PIXELFORMAT_ARGB8888);
}

/* format_table[srPixelConvert::SURFACE_RGB555] write/read: 32-bit BGRA packed to RGB555 through the
   5-bit reduction table, and expanded back with alpha forced opaque. */
// FUNCTION: SURRENDER 0x1000ACD0
void __cdecl writeRGB555(const srPixelConvert::ConversionInfo& info)
{
    ConvertPixelRun(info, 4, 2, [](w8_ulong pixel, w8_ulong index) {
        const auto color = ARGBChannels(pixel);
        return PixelAt{w8_ulong(lutReduce32[color.r]) << 10 |
                       w8_ulong(lutReduce32[color.g]) << 5 | lutReduce32[color.b], index};
    });
}

// FUNCTION: SURRENDER 0x1000AE80
static unsigned char expandRGB555Red(w8_ulong word)
{
    const auto index = word >> 10;
    return index < 32 ? lutExpand5And6.expand32[index] : lutExpand5And6.expand64[index - 32];
}

void __cdecl readRGB555(const srPixelConvert::ConversionInfo& info)
{
    ConvertPixelRun(info, 2, 4, [](w8_ulong pixel, w8_ulong index) {
        return PixelAt{0xff000000 | w8_ulong(expandRGB555Red(pixel)) << 16 |
                       w8_ulong(lutExpand5And6.expand32[pixel >> 5 & 0x1f]) << 8 |
                       lutExpand5And6.expand32[pixel & 0x1f], index};
    });
}
