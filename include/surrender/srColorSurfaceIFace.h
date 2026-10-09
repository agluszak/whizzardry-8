#pragma once

#include "srARGB.h"
#include "srFilter.h"
#include "srMath.h"
#include "srPixelConvert.h"
#include "srPtr.h"
#include "srStat.h"
#include "srTypeRegistry.h"

class srColorSurface;
class srPalette;
struct W8TgaHeader;

// VTABLE: SURRENDER 0x10076708
// class srClassSupport<srColorSurfaceIFace, srClass, 1, 12544>

class srColorSurfaceIFace
    : public srClassSupport<srColorSurfaceIFace, srClass, true, 0x3100> {
public:
    struct Rectangle {
        w8_long left;
        w8_long top;
        w8_long right;
        w8_long bottom;
    };

    struct BlitInfo {
        Rectangle destination;
        Rectangle source;
    };

    enum { CLAMP_HORIZONTAL = 0x01u, CLAMP_VERTICAL = 0x02u };

    struct SurfaceDesc {
        w8_ulong width;
        w8_ulong height;
        w8_ulong pitch;
        w8_ulong clamp_modes;
        srFilter* filter;
        srPixelConvert::PixelFormat pixel_format;
    };

    srColorSurfaceIFace();
    srColorSurfaceIFace(const srColorSurfaceIFace& other);
    srColorSurfaceIFace& operator=(const srColorSurfaceIFace& other);

    static const char* sGetClassName();

    virtual void dump(std::ostream& stream) override;
    virtual w8_ulong getPixel(w8_long x, w8_long y);
    virtual void setPixel(w8_long x, w8_long y, w8_ulong pixel);
    virtual w8_ulong getPixelRaw(w8_long x, w8_long y);
    virtual void setPixelRaw(w8_long x, w8_long y, w8_ulong pixel);
    virtual void getPixels(w8_ulong* pixels, const srVector2i* positions,
                                         w8_long count);
    virtual void setPixels(const w8_ulong* pixels, const srVector2i* positions,
                                         w8_long count);
    virtual void getPixelsRaw(void* pixels, const srVector2i* positions, w8_long count);
    virtual void setPixelsRaw(const void* pixels, const srVector2i* positions,
                                            w8_long count);
    virtual void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end);
    virtual void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end);
    virtual srPalette* getPalette();
    virtual void setPalette(srPalette* palette);
    virtual void* getDataPtr();
    virtual w8_long getDataSize();
    virtual int resize(w8_long width, w8_long height);
    virtual int rescale(w8_long width, w8_long height);
    virtual int changePixelFormat(const srPixelConvert::PixelFormat& format,
                                                int preserve);
    virtual void fill(w8_ulong pixel);
    virtual void setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel);
    virtual void setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel);
    virtual void setLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_ulong pixel);
    virtual void composite(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                                         w8_long source_y, w8_long width, w8_long height, double alpha);
    virtual void blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                                    w8_long source_y, w8_long width, w8_long height);
    virtual void blit(const BlitInfo& info, srColorSurfaceIFace& source);
    virtual void copy(srColorSurfaceIFace& source);
    virtual void swapPixelRows(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_long count);
    virtual void flipRectangle(const Rectangle& rectangle);
    virtual void adjust(const srVector4T<float>& scale,
                                      const srVector4T<float>& offset,
                                      const srVector4T<float>& gamma);
    virtual void adjustSaturation(double saturation);
    virtual void getChannelStatistics(srStat& statistics, srARGB::e_index channel);
    virtual void remapPixels(const srARGB& from, const srARGB& to);
    virtual void copyColorChannel(srARGB::e_index destination,
                                                srARGB::e_index source);
    virtual void flipColorChannels(srARGB::e_index first, srARGB::e_index second);
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;

    void addNoise(double amplitude, int monochrome);
    void clampCoordinates(w8_long& x, w8_long& y);
    void flipHorizontal();
    void flipVertical();
    w8_ulong getAlphaBits() const;
    double getAspectRatio() const;
    w8_long getBitsPerPixel() const;
    w8_long getBlueBits() const;
    w8_long getBytesPerPixel() const;
    w8_long getClampedX(w8_long x) const;
    w8_long getClampedY(w8_long y) const;
    srFilter* getFilter() const;
    w8_long getGreenBits() const;
    int getHClampMode() const;
// FUNCTION: SURRENDER 0x100598F0 SYMBOL
// RECOMP: ?getHeight@srColorSurfaceIFace@@QBEJXZ
#if defined(SURRENDER_BUILD)

#endif
    w8_long getHeight() const
    {
        return height;
    }
// FUNCTION: SURRENDER 0x100599E0 SYMBOL
// RECOMP: ?getPitch@srColorSurfaceIFace@@QBEJXZ
#if defined(SURRENDER_BUILD)

#endif
    w8_long getPitch() const
    {
        return pitch;
    }
// FUNCTION: SURRENDER 0x100599F0 SYMBOL
// RECOMP: ?getPixelFormat@srColorSurfaceIFace@@QBEXAAUPixelFormat@srPixelConvert@@@Z
#if defined(SURRENDER_BUILD)

#endif
    void getPixelFormat(srPixelConvert::PixelFormat& format) const
    {
        format = pixel_format;
    }
    w8_long getRedBits() const;
// FUNCTION: SURRENDER 0x10059A10 SYMBOL
// RECOMP: ?getSurfaceDesc@srColorSurfaceIFace@@QBEXAAUSurfaceDesc@1@@Z
#if defined(SURRENDER_BUILD)

#endif
    void getSurfaceDesc(SurfaceDesc& description) const
    {
        description.width = width;
        description.height = height;
        description.pitch = pitch;
        description.clamp_modes = clamp_modes;
        description.filter = filter;
        description.pixel_format = pixel_format;
    }
    int getVClampMode() const;
// FUNCTION: SURRENDER 0x10059A60 SYMBOL
// RECOMP: ?getWidth@srColorSurfaceIFace@@QBEJXZ
#if defined(SURRENDER_BUILD)

#endif
    w8_long getWidth() const
    {
        return width;
    }
    int isAlpha() const;
    int isPaletted() const;
    void rotate180();
// FUNCTION: SURRENDER 0x10059A90 SYMBOL
// RECOMP: ?setFilter@srColorSurfaceIFace@@QAEXPAVsrFilter@@@Z
#if defined(SURRENDER_BUILD)

#endif
    void setFilter(srFilter* filter)
    {
        this->filter = filter;
    }
    void setHClampMode(int enabled);
    void setVClampMode(int enabled);

protected:
    virtual void copyNoScaling(srColorSurfaceIFace& source);
    virtual void scaleHorizontal(srColorSurfaceIFace& source);
    virtual void scaleVertical(srColorSurfaceIFace& source);
    virtual void scaleFast(srColorSurfaceIFace& source);
    virtual void scale(srColorSurfaceIFace& source);
    virtual void magnify(srColorSurfaceIFace& source);
    virtual void minify(srColorSurfaceIFace& source);

    void copySurfaceParameters(const srColorSurfaceIFace& source);
    const srPixelConvert::PixelFormat* getPixelFormat() const;
    int isPixelFormatCompatible(const srColorSurfaceIFace& source) const;
    void setSurfaceDesc(const SurfaceDesc& description);

    friend class stTextureFile;
    friend void __stdcall LoadSurfacePixels(int handle, srColorSurface* surface,
                                            const W8TgaHeader* header);
    friend class srColorSurface;

    unsigned char unknown_18_[0x04];
    w8_long width;
    w8_long height;
    w8_long pitch;
    w8_ulong clamp_modes;
    srFilter* filter;
    srPixelConvert::PixelFormat pixel_format;
};

W8_ABI_ASSERT(sizeof(srColorSurfaceIFace) == 0x44, "srColorSurfaceIFace_must_be_0x44");

W8_ABI_ASSERT((sizeof(srColorSurfaceIFace::SurfaceDesc) == 0x28), "srSurfaceDesc_must_be_0x28");
