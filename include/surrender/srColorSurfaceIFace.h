#pragma once

#include <SDL3/SDL_rect.h>

#include <memory>

#include "srARGB.h"
#include "srFilter.h"
#include "srMath.h"
#include "srPixelConvert.h"
#include "srTypeRegistry.h"

class srColorSurface;
class srPalette;
struct W8TgaHeader;

// VTABLE: SURRENDER 0x10076708
// class srClassSupport<srColorSurfaceIFace, srClass, 12544>

class srColorSurfaceIFace : public srClassSupport<srColorSurfaceIFace, srClass, 0x3100> {
public:
    struct Rectangle {
        w8_long left;
        w8_long top;
        w8_long right;
        w8_long bottom;
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

    virtual ~srColorSurfaceIFace() override = default;
    SR_DLL_IMPORT srColorSurfaceIFace();
    SR_DLL_IMPORT srColorSurfaceIFace(const srColorSurfaceIFace& other);
    SR_DLL_IMPORT srColorSurfaceIFace& operator=(const srColorSurfaceIFace& other);

    static SR_DLL_IMPORT const char* sGetClassName();

    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;
    virtual SR_DLL_IMPORT w8_ulong getPixel(w8_long x, w8_long y);
    virtual SR_DLL_IMPORT void setPixel(w8_long x, w8_long y, w8_ulong pixel);
    virtual SR_DLL_IMPORT w8_ulong getPixelRaw(w8_long x, w8_long y);
    virtual SR_DLL_IMPORT void setPixelRaw(w8_long x, w8_long y, w8_ulong pixel);
    virtual SR_DLL_IMPORT void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end);
    virtual SR_DLL_IMPORT void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end);
    virtual SR_DLL_IMPORT std::shared_ptr<srPalette> getPalette();
    virtual SR_DLL_IMPORT void setPalette(std::shared_ptr<srPalette> palette);
    virtual SR_DLL_IMPORT void* getDataPtr();
    virtual SR_DLL_IMPORT w8_long getDataSize();
    virtual SR_DLL_IMPORT int resize(w8_long width, w8_long height);
    virtual SR_DLL_IMPORT int rescale(w8_long width, w8_long height);
    virtual SR_DLL_IMPORT int changePixelFormat(const srPixelConvert::PixelFormat& format,
                                                int preserve);
    virtual SR_DLL_IMPORT void fill(w8_ulong pixel);
    virtual void fillRectangle(SDL_Rect rectangle, w8_ulong pixel);
    virtual SR_DLL_IMPORT void setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel);
    virtual SR_DLL_IMPORT void setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel);
    virtual SR_DLL_IMPORT void setLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1,
                                       w8_ulong pixel);
    virtual SR_DLL_IMPORT void blit(w8_long x, w8_long y, srColorSurfaceIFace& source,
                                    w8_long source_x, w8_long source_y, w8_long width,
                                    w8_long height);
    virtual SR_DLL_IMPORT void copy(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void flipRectangle(const Rectangle& rectangle);
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;

    SR_DLL_IMPORT void clampCoordinates(w8_long& x, w8_long& y);
    SR_DLL_IMPORT void flipHorizontal();
    SR_DLL_IMPORT void flipVertical();
    SR_DLL_IMPORT double getAspectRatio() const;
    SR_DLL_IMPORT w8_long getClampedX(w8_long x) const;
    SR_DLL_IMPORT w8_long getClampedY(w8_long y) const;
    SR_DLL_IMPORT srFilter* getFilter() const;
    // FUNCTION: SURRENDER 0x100598F0 SYMBOL
    // RECOMP: ?getHeight@srColorSurfaceIFace@@QBEJXZ

    w8_long getHeight() const
    {
        return height;
    }
    // FUNCTION: SURRENDER 0x100599E0 SYMBOL
    // RECOMP: ?getPitch@srColorSurfaceIFace@@QBEJXZ

    w8_long getPitch() const
    {
        return pitch;
    }
    const srPixelConvert::PixelFormat& getPixelFormat() const
    {
        return pixel_format;
    }

    SurfaceDesc getSurfaceDesc() const
    {
        return {static_cast<w8_ulong>(width),
                static_cast<w8_ulong>(height),
                static_cast<w8_ulong>(pitch),
                clamp_modes,
                filter,
                pixel_format};
    }
    // FUNCTION: SURRENDER 0x10059A60 SYMBOL
    // RECOMP: ?getWidth@srColorSurfaceIFace@@QBEJXZ

    w8_long getWidth() const
    {
        return width;
    }
    // FUNCTION: SURRENDER 0x10059A90 SYMBOL
    // RECOMP: ?setFilter@srColorSurfaceIFace@@QAEXPAVsrFilter@@@Z

    void setFilter(srFilter* filter)
    {
        this->filter = filter;
    }
    SR_DLL_IMPORT void setHClampMode(int enabled);
    SR_DLL_IMPORT void setVClampMode(int enabled);

protected:
    virtual SR_DLL_IMPORT void copyNoScaling(srColorSurfaceIFace& source);
    void scaleAxis(srColorSurfaceIFace& source, bool vertical);
    virtual SR_DLL_IMPORT void scaleFast(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void scale(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void magnify(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void minify(srColorSurfaceIFace& source);

    SR_DLL_IMPORT void copySurfaceParameters(const srColorSurfaceIFace& source);
    SR_DLL_IMPORT int isPixelFormatCompatible(const srColorSurfaceIFace& source) const;
    SR_DLL_IMPORT void setSurfaceDesc(const SurfaceDesc& description);

    friend class stTextureFile;
    friend void __stdcall LoadSurfacePixels(int handle, srColorSurface* surface,
                                            const W8TgaHeader* header);
    friend class srColorSurface;

    unsigned char unknown_18_[0x04]{};
    w8_long width;
    w8_long height;
    w8_long pitch;
    w8_ulong clamp_modes;
    srFilter* filter;
    srPixelConvert::PixelFormat pixel_format;
};
