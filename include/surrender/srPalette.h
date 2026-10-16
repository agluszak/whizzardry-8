#pragma once

#include <memory>
#include <vector>

#include "srARGB.h"
#include "srClass.h"

// VTABLE: SURRENDER 0x100753CC srPalette
class srPalette : public srClass {
public:
    /* Two-level lookup: lut_rg[(g<<8)|r] selects a palette row, lut_rgb[(row<<8)|b] yields the
       index. */
    class Quantizer {
    public:
        Quantizer();
        // FUNCTION: SURRENDER 0x10004B40
        Quantizer(srARGB* colors, w8_long color_count, unsigned char* duplicates,
                  unsigned char red_bits, unsigned char green_bits, unsigned char blue_bits)
        {
            setPalette(colors, color_count, duplicates, red_bits, green_bits, blue_bits);
        }
        // FUNCTION: SURRENDER 0x10004D00
        Quantizer(const Quantizer& other) = default;
        // Retail destructor: SURRENDER 0x10004B80 (implicit destruction).

        void setPalette(srARGB* colors, w8_long color_count, unsigned char* duplicates,
                        unsigned char red_bits, unsigned char green_bits, unsigned char blue_bits);
        unsigned char quantize(const srARGB& color);
        unsigned char quantize(unsigned char red, unsigned char green, unsigned char blue);
        void quantize(unsigned char* indices, const srARGB* colors, w8_long color_count);

    private:
        struct Entry {
            w8_ulong red;
            w8_ulong green;
            w8_ulong blue;
            w8_ulong index;
        };

        void checkLuts();
        void initRG(w8_long red_lo, w8_long red_hi, w8_long green_lo, w8_long green_hi);
        unsigned char findRG(w8_long red, w8_long green);
        void createRGTable();
        void initRGB(w8_long red, w8_long green, w8_long blue_lo, w8_long blue_hi);
        unsigned char findRGB(w8_long blue);
        void partitionRGB(w8_long blue_lo, w8_long blue_hi);
        void createRGBTable();

        static int initialized;

        unsigned char lut_rg[0x10000];  /* 0x00000 */
        unsigned char lut_rgb[0x10000]; /* 0x10000 */
        srARGB palette[0x100];          /* 0x20000 */
        unsigned char duplicate[0x100]; /* 0x20400 */
        w8_long color_count;               /* 0x20500 */
        w8_ulong red_bits;         /* 0x20504 */
        w8_ulong green_bits;       /* 0x20508 */
        w8_ulong blue_bits;        /* 0x2050c */
        Entry entries[0x100];           /* 0x20510 */
        w8_long entry_count;               /* 0x21510 */
        unsigned char* lut_row;         /* 0x21514 */
        w8_long rg_dist[0x100];            /* 0x21518 */
    };

    srPalette(srARGB* colors = 0, w8_long color_count = 1);

    srPalette& operator=(const srPalette& other);

    srARGB getColor(w8_long index) const;
    const srARGB* getPaletteDataPtr();
    w8_long getPaletteSize() const;
    int matchPalette(const srARGB* const colors, w8_long color_count) const;
    unsigned char quantize(const srARGB& color);
    void quantize(unsigned char* const indices, const srARGB* const colors, w8_long color_count);
    void releaseQuantizer();
    void setColor(w8_long index, const srARGB& color);
    void setColors(w8_long destination_index, const srARGB* const colors, w8_long color_count);
    void update();

public:
    // FUNCTION: SURRENDER 0x10004610
    virtual ~srPalette() override = default;

private:
    void updateQuantizer();

    w8_ulong flags;
    std::vector<srARGB> colors;
    w8_long color_count;
    std::unique_ptr<Quantizer> quantizer;
};
