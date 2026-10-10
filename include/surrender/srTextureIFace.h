#pragma once

#include <memory>

#include "srColorSurfaceIFace.h"
#include "srPalette.h"
#include "srPixelConvert.h"
#include "srTypeRegistry.h"

class srFilter;
class srTexture;
class stSurface2D;

class SR_DLL_IMPORT srTextureIFace
    : public srClassSupport<srTextureIFace, srClass, 0x2100> {
public:
    /* srGERD::setTextureDefaultCompression remaps DEFAULT (4) to 0; the
       srTexture ctor seeds Dimensions::compression with DEFAULT. */
    enum e_compression { COMPRESSION_DEFAULT = 4 };
    /* Copies retain shared palette ownership; the output object must be constructed. */
    struct Dimensions {
        w8_ulong width;
        w8_ulong height;
        std::shared_ptr<srPalette> palette;
        srPixelConvert::PixelFormat format;
        w8_ulong hints;
        e_compression compression;
        srFilter* filter;
    };
    struct MultiRequest {
        w8_long mipmap_level;
        /* Last mipmap level filled; iterated level <= last_level. */
        w8_ulong last_level;
        srColorSurfaceIFace* destinations[12];
    };
    /* srGERD::setTextureSubImage packs the level-0 dimensions and the target
       mipmap level ahead of the clipped destination rect. */
    struct PartialRequest {
        w8_ulong width;
        w8_ulong height;
        w8_long mipmap_level;
        w8_long destination_x;
        w8_long destination_y;
        w8_long source_right;
        w8_long source_bottom;
        srColorSurfaceIFace* destination;
    };
    /* Packed filter/wrap/mipmap state and the mipmap bias. */
    struct Parameters {
        enum {
            CORRECTION_MASK = 0x0003u,
            MAG_FILTER_SHIFT = 4,
            MAG_FILTER_MASK = 0x0070u,
            MIN_FILTER_SHIFT = 7,
            MIN_FILTER_MASK = 0x0380u,
            MIPMAP_SHIFT = 10,
            MIPMAP_MASK = 0x0c00u,
            WRAP_S_SHIFT = 12,
            WRAP_S_MASK = 0x1000u,
            WRAP_T_SHIFT = 13,
            WRAP_T_MASK = 0x2000u
        };
        w8_ulong packed_state;
        float mipmap_bias;
    };
    /* srTexture::dump: NONE / FASTEST / GOOD / BEST, else DEFAULT.
       setTextureDefaultMagFilter remaps DEFAULT (4) to GOOD. */
    enum e_filter {
        FILTER_NONE = 0,
        FILTER_FASTEST = 1,
        FILTER_GOOD = 2,
        FILTER_BEST = 3,
        FILTER_DEFAULT = 4
    };
    /* Dump: NONE / FASTEST / BEST, else DEFAULT. GOOD is not a mipmap
       enumerator. setTextureDefaultMipmap remaps DEFAULT (3) to FASTEST. */
    enum e_mipmap { MIPMAP_NONE = 0, MIPMAP_FASTEST = 1, MIPMAP_BEST = 2, MIPMAP_DEFAULT = 3 };
    /* enableHint ORs 1<<hint into the texture's hint mask. */
    enum e_hint {
        HINT_NO_ALPHA = 1,
        HINT_ONE_BIT_ALPHA = 2,
        HINT_NO_MIPMAPS = 3,
        HINT_ALPHA_ONLY = 4,
        HINT_RESIDENT = 5,
        HINT_POSITIONAL_6 = 6,
        HINT_NO_REDUCTION = 7,
        HINT_INTENSITY = 8
    };
    enum e_wrap { WRAP_REPEAT = 0, WRAP_CLAMP = 1 };
    enum e_correction {
        CORRECTION_FASTEST = 0,
        CORRECTION_GOOD = 1,
        CORRECTION_BEST = 2,
        CORRECTION_DEFAULT = 3
    };

    // FUNCTION: SURRENDER 0x1005F5B0 SYMBOL
    // RECOMP: ?sGetClassName@srTextureIFace@@SAPBDXZ
    static const char* sGetClassName()
    {
        return "srTextureIFace";
    }

    /* srTextureFile's vftable is this interface exactly. */
    virtual w8_ulong getTextureFrameHandle() = 0;
    virtual float getPriority() = 0;
    virtual void getDimensions(Dimensions& dimensions) = 0;
    virtual void getMipmapData(MultiRequest& request) = 0;
    virtual void getMipmapLevelPartial(PartialRequest& request) = 0;
    virtual void getTextureParms(Parameters& parameters) = 0;
    virtual const char* getTextureName();
    virtual void invalidate() = 0;
    ~srTextureIFace() override;

protected:
    virtual void setupDefaultValues() = 0;
};
