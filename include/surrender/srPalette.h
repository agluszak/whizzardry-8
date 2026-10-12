#pragma once

#include <cstdint>
#include "srARGB.h"
#include "srTypeRegistry.h"

#include <memory>
#include <vector>

class srColorSurfaceIFace;

// VTABLE: SURRENDER 0x100753EC
// class srClassSupport<srPalette, srClass, 10496>

// VTABLE: SURRENDER 0x100753CC srPalette
class srPalette : public srClassSupport<srPalette, srClass, 0x2900> {
public:
    /* Two-level lookup: lut_rg[(g<<8)|r] selects a palette row, lut_rgb[(row<<8)|b] yields the
       index. */
    class Quantizer {
    public:
        Quantizer();
        // FUNCTION: SURRENDER 0x10004B40
        Quantizer(srARGB* colors, std::int32_t color_count, std::uint8_t* duplicates,
                  std::uint8_t red_bits, std::uint8_t green_bits, std::uint8_t blue_bits)
        {
            setPalette(colors, color_count, duplicates, red_bits, green_bits, blue_bits);
        }
        // FUNCTION: SURRENDER 0x10004D00
        Quantizer(const Quantizer& other) = default;
        // Retail destructor: SURRENDER 0x10004B80 (implicit destruction).

        void setPalette(srARGB* colors, std::int32_t color_count, std::uint8_t* duplicates,
                        std::uint8_t red_bits, std::uint8_t green_bits, std::uint8_t blue_bits);
        std::uint8_t quantize(const srARGB& color);
        std::uint8_t quantize(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
        void quantize(std::uint8_t* indices, const srARGB* colors, std::int32_t color_count);

    private:
        struct Entry {
            std::uint32_t red;
            std::uint32_t green;
            std::uint32_t blue;
            std::uint32_t index;
        };

        void checkLuts();
        void initRG(std::int32_t red_lo, std::int32_t red_hi, std::int32_t green_lo, std::int32_t green_hi);
        std::uint8_t findRG(std::int32_t red, std::int32_t green);
        void createRGTable();
        void initRGB(std::int32_t red, std::int32_t green, std::int32_t blue_lo, std::int32_t blue_hi);
        std::uint8_t findRGB(std::int32_t blue);
        void partitionRGB(std::int32_t blue_lo, std::int32_t blue_hi);
        void createRGBTable();

        static std::int32_t initialized;

        std::uint8_t lut_rg[0x10000];  /* 0x00000 */
        std::uint8_t lut_rgb[0x10000]; /* 0x10000 */
        srARGB palette[0x100];          /* 0x20000 */
        std::uint8_t duplicate[0x100]; /* 0x20400 */
        std::int32_t color_count;               /* 0x20500 */
        std::uint32_t red_bits;         /* 0x20504 */
        std::uint32_t green_bits;       /* 0x20508 */
        std::uint32_t blue_bits;        /* 0x2050c */
        Entry entries[0x100];           /* 0x20510 */
        std::int32_t entry_count;               /* 0x21510 */
        std::uint8_t* lut_row;         /* 0x21514 */
        std::int32_t rg_dist[0x100];            /* 0x21518 */
    };

    /* Color sampler. Sampled colors are quantized to sample_bits per channel and accumulated in a
       0x8000-bucket open hash; entries hold the packed color and its total weight, linked through a
       parallel index array. */
    class Sampler {
    public:
        struct ColorEntry {
            srARGB color;
            std::int32_t count;
        };

        Sampler(std::int32_t sample_limit = 0);
        // FUNCTION: SURRENDER 0x10004BA0
        Sampler(const Sampler& other) = default;
        // Retail destructor: SURRENDER 0x10006930 (implicit destruction).

        std::int32_t getColorCount();
        std::int32_t getOutputPaletteSize();
        std::int32_t getSampleCount();
        double getSampleFactor();
        std::int32_t getSampleBits();
        void setOutputPaletteSize(std::int32_t size);
        void setSampleBits(std::int32_t bits);
        void setSampleFactor(double factor);
        void discard();
        void removeMaskColor(std::int32_t index);
        void setMaskColor(std::int32_t index, const srARGB& color);
        void addColor(const srARGB& color, std::int32_t weight);
        void addColors(const srARGB* colors, std::int32_t color_count, std::int32_t weight);
        void addSurface(const char* name, std::int32_t weight);
        void addSurface(srColorSurfaceIFace& surface, std::int32_t weight);
        void addSurfaces(srColorSurfaceIFace** surfaces, std::int32_t surface_count, std::int32_t weight);
        std::shared_ptr<srPalette> createOptimalPalette();
        void dump(std::ostream& stream);

    private:
        void shiftDown(srARGB& color);
        void shiftUp(srARGB& color);
        void reallocColors(std::int32_t capacity);

        std::int32_t sample_limit;               /* 0x000 */
        double sample_factor;            /* 0x008 */
        std::int32_t sample_bits;                /* 0x010 */
        std::int32_t color_count;                /* 0x014 */
        std::int32_t sample_count;               /* 0x018 */
        std::int32_t capacity;                   /* 0x01c */
        std::int32_t output_palette_size;        /* 0x020 */
        std::uint8_t mask_flags[0x100]; /* 0x024 */
        srARGB mask_colors[0x100];       /* 0x124 */
        std::vector<ColorEntry> colors;
        std::vector<std::int32_t> links;
        std::int32_t buckets[0x8000];            /* 0x52c */
    };

    /* Optimal-palette builder; an octree over the sampled 5-5-5 color space. Level arrays hold
       8^level nodes for levels 0-4; level-4 children index the sparse 15-bit leaf-bucket map. Leaf
       bucket nodes own Leaf records {color, weight, error}. */
    class Optimizer {
    public:
        struct PaletteInfo {
            const Sampler::ColorEntry* colors; /* 0x00 */
            std::int32_t color_count;                  /* 0x04 */
            std::int32_t palette_size;                 /* 0x08 */
            const srARGB* mask_colors;         /* 0x0c */
            const std::uint8_t* mask_flags;   /* 0x10 */
            std::int32_t mask_count;                   /* 0x14 */
        };

        static std::shared_ptr<srPalette> createOptimalPalette(const PaletteInfo& info);

    private:
        struct HashEntry {
            HashEntry* next;
            std::uint32_t color;
            std::int32_t count;
        };

        struct Leaf {
            std::uint32_t color;
            std::int32_t weight;
            double error;
        };

        struct Node {
            double err_min;      /* 0x00 */
            double err_total;    /* 0x08 */
            std::uint32_t color; /* 0x10 */
            srARGB bound_lo;     /* 0x14 */
            srARGB bound_hi;     /* 0x18 */
            Leaf* leaves;        /* 0x1c */
            std::int32_t leaf_count;     /* 0x20 */
            Node* parent;        /* 0x24 */
            Node* children[8];   /* 0x28 */
        };

        struct LUT {
            float dist_r[0x200]; /* 0x0000 (i-256)^2 * 0.299 */
            float dist_g[0x200]; /* 0x0800 (i-256)^2 * 0.587 */
            float dist_b[0x200]; /* 0x1000 (i-256)^2 * 0.114 */
            float* r;            /* 0x1800 dist_r biased by (0x100 - red) */
            float* g;            /* 0x1804 */
            float* b;            /* 0x1808 */
            srARGB color;        /* 0x180c */
        };

        static void findOptimalColor(Node* node, const LUT& lut);
        static void setupLUT(LUT& lut, const srARGB& color);

    };

    static SR_DLL_IMPORT const char* sGetClassName();

    SR_DLL_IMPORT srPalette(srARGB* colors = 0, std::int32_t color_count = 1);

    SR_DLL_IMPORT srPalette& operator=(const srPalette& other);

    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;

    SR_DLL_IMPORT srARGB getColor(std::int32_t index) const;
    SR_DLL_IMPORT const srARGB* getPaletteDataPtr();
    SR_DLL_IMPORT std::int32_t getPaletteSize() const;
    SR_DLL_IMPORT std::int32_t matchPalette(const srARGB* const colors, std::int32_t color_count) const;
    SR_DLL_IMPORT std::uint8_t quantize(const srARGB& color);
    SR_DLL_IMPORT void quantize(std::uint8_t* const indices, const srARGB* const colors,
                                std::int32_t color_count);
    SR_DLL_IMPORT void releaseQuantizer();
    SR_DLL_IMPORT void setColor(std::int32_t index, const srARGB& color);
    SR_DLL_IMPORT void setColors(std::int32_t destination_index, const srARGB* const colors,
                                 std::int32_t color_count);
    SR_DLL_IMPORT void update();

public:
    // FUNCTION: SURRENDER 0x10004610
    virtual SR_DLL_IMPORT ~srPalette() override = default;

private:
    SR_DLL_IMPORT void updateQuantizer();

    std::uint32_t flags;
    std::vector<srARGB> colors;
    std::int32_t color_count;
    std::unique_ptr<Quantizer> quantizer;
};

