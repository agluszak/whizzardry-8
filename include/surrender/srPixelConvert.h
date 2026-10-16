#pragma once

class srPalette;

class srPixelConvert {
public:
    enum e_surfaceType {
        SURFACE_AP44,
        SURFACE_AL44,
        SURFACE_L8,
        SURFACE_A8,
        SURFACE_P8,
        SURFACE_AP88,
        SURFACE_AL88,
        SURFACE_RGB565,
        SURFACE_RGB555,
        SURFACE_ARGB1555,
        SURFACE_RGB444,
        SURFACE_ARGB4444,
        SURFACE_BGR24,
        SURFACE_BGRX32,
        SURFACE_BGRA32,
        SURFACE_RGB332,
        SURFACE_BGR565,
        SURFACE_ARGB32,
        SURFACE_BGR555,
        SURFACE_ABGR32,
        SURFACE_RGBA32,
        SURFACE_RGB24,
        SURFACE_INVALID
    };

    enum e_colorModel { COLOR_RGB, COLOR_INTENSITY, COLOR_INDEXED };
    enum e_pixelSize { PIXEL_SIZE_8 = 0, PIXEL_SIZE_16 = 1, PIXEL_SIZE_24 = 2, PIXEL_SIZE_32 = 3 };

    struct PixelFormat {
        unsigned char red_bits;
        unsigned char red_shift;
        unsigned char green_bits;
        unsigned char green_shift;
        unsigned char blue_bits;
        unsigned char blue_shift;
        unsigned char alpha_bits;
        unsigned char alpha_shift;
        /* Generic converter family; pixel_size stores (bits per pixel / 8) - 1. */
        e_colorModel color_model;
        e_pixelSize pixel_size;
        w8_ulong fourcc;

        PixelFormat() : fourcc(0) {}

        bool operator==(const PixelFormat& other) const = default;
    };

    /* Converters receive the run description by reference; palette carries
       the surface's srPalette for paletted conversion classes. */
    struct ConversionInfo {
        void* dest;
        const void* source;
        w8_ulong count;
        srPalette* palette;
        const PixelFormat* format;
    };
    typedef void (*ConversionFunc)(const ConversionInfo& info);

    static PixelFormat pixelFormat(e_surfaceType type);
    struct ConversionFunctions {
        ConversionFunc write = nullptr;
        ConversionFunc read = nullptr;
    };
    static ConversionFunctions selectFuncs(const PixelFormat& format);
};

/* Builds the conversion lookup tables (channel expansion/reduction ramps,
   dither cube, channel weights, decode/grayscale palettes) at library init. */
void initPixelTables(void);
