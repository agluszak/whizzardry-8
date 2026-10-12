#pragma once

#include <cstdint>
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

class srColorSurfaceIFace
    : public srClassSupport<srColorSurfaceIFace, srClass, 0x3100> {
public:
    struct Rectangle {
        std::int32_t left;
        std::int32_t top;
        std::int32_t right;
        std::int32_t bottom;
    };

    enum { CLAMP_HORIZONTAL = 0x01u, CLAMP_VERTICAL = 0x02u };

    struct SurfaceDesc {
        std::uint32_t width;
        std::uint32_t height;
        std::uint32_t pitch;
        std::uint32_t clamp_modes;
        srFilter* filter;
        srPixelConvert::PixelFormat pixel_format;
    };

    virtual ~srColorSurfaceIFace() override = default;
    SR_DLL_IMPORT srColorSurfaceIFace();
    SR_DLL_IMPORT srColorSurfaceIFace(const srColorSurfaceIFace& other);
    SR_DLL_IMPORT srColorSurfaceIFace& operator=(const srColorSurfaceIFace& other);

    static SR_DLL_IMPORT const char* sGetClassName();

    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;
    virtual SR_DLL_IMPORT std::uint32_t getPixel(std::int32_t x, std::int32_t y);
    virtual SR_DLL_IMPORT void setPixel(std::int32_t x, std::int32_t y, std::uint32_t pixel);
    virtual SR_DLL_IMPORT std::uint32_t getPixelRaw(std::int32_t x, std::int32_t y);
    virtual SR_DLL_IMPORT void setPixelRaw(std::int32_t x, std::int32_t y, std::uint32_t pixel);
    virtual SR_DLL_IMPORT void getPixelColumn(std::uint32_t* pixels, std::int32_t x, std::int32_t y_start,
                                              std::int32_t y_end);
    virtual SR_DLL_IMPORT void setPixelColumn(const std::uint32_t* pixels, std::int32_t x, std::int32_t y_start,
                                              std::int32_t y_end);
    virtual SR_DLL_IMPORT std::shared_ptr<srPalette> getPalette();
    virtual SR_DLL_IMPORT void setPalette(std::shared_ptr<srPalette> palette);
    virtual SR_DLL_IMPORT void* getDataPtr();
    virtual SR_DLL_IMPORT std::int32_t getDataSize();
    virtual SR_DLL_IMPORT std::int32_t resize(std::int32_t width, std::int32_t height);
    virtual SR_DLL_IMPORT std::int32_t rescale(std::int32_t width, std::int32_t height);
    virtual SR_DLL_IMPORT std::int32_t changePixelFormat(const srPixelConvert::PixelFormat& format,
                                                std::int32_t preserve);
    virtual SR_DLL_IMPORT void fill(std::uint32_t pixel);
    virtual void fillRectangle(SDL_Rect rectangle, std::uint32_t pixel);
    virtual SR_DLL_IMPORT void setHLine(std::int32_t y, std::int32_t x_start, std::int32_t x_end, std::uint32_t pixel);
    virtual SR_DLL_IMPORT void setVLine(std::int32_t x, std::int32_t y_start, std::int32_t y_end, std::uint32_t pixel);
    virtual SR_DLL_IMPORT void setLine(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::uint32_t pixel);
    virtual SR_DLL_IMPORT void blit(std::int32_t x, std::int32_t y, srColorSurfaceIFace& source, std::int32_t source_x,
                                    std::int32_t source_y, std::int32_t width, std::int32_t height);
    virtual SR_DLL_IMPORT void copy(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void flipRectangle(const Rectangle& rectangle);
    virtual void getPixelRow(std::uint32_t* pixels, std::int32_t y, std::int32_t x_start, std::int32_t x_end) = 0;
    virtual void setPixelRow(const std::uint32_t* pixels, std::int32_t y, std::int32_t x_start, std::int32_t x_end) = 0;
    virtual void getPixelRowRaw(void* pixels, std::int32_t y, std::int32_t x_start, std::int32_t x_end) = 0;
    virtual void setPixelRowRaw(const void* pixels, std::int32_t y, std::int32_t x_start, std::int32_t x_end) = 0;

    SR_DLL_IMPORT void clampCoordinates(std::int32_t& x, std::int32_t& y);
    SR_DLL_IMPORT void flipHorizontal();
    SR_DLL_IMPORT void flipVertical();
    SR_DLL_IMPORT double getAspectRatio() const;
    SR_DLL_IMPORT std::int32_t getClampedX(std::int32_t x) const;
    SR_DLL_IMPORT std::int32_t getClampedY(std::int32_t y) const;
    SR_DLL_IMPORT srFilter* getFilter() const;
// FUNCTION: SURRENDER 0x100598F0 SYMBOL
// RECOMP: ?getHeight@srColorSurfaceIFace@@QBEJXZ

    std::int32_t getHeight() const
    {
        return height;
    }
// FUNCTION: SURRENDER 0x100599E0 SYMBOL
// RECOMP: ?getPitch@srColorSurfaceIFace@@QBEJXZ

    std::int32_t getPitch() const
    {
        return pitch;
    }
    const srPixelConvert::PixelFormat& getPixelFormat() const
    {
        return pixel_format;
    }

    SurfaceDesc getSurfaceDesc() const
    {
        return {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                static_cast<std::uint32_t>(pitch), clamp_modes, filter, pixel_format};
    }
// FUNCTION: SURRENDER 0x10059A60 SYMBOL
// RECOMP: ?getWidth@srColorSurfaceIFace@@QBEJXZ

    std::int32_t getWidth() const
    {
        return width;
    }
// FUNCTION: SURRENDER 0x10059A90 SYMBOL
// RECOMP: ?setFilter@srColorSurfaceIFace@@QAEXPAVsrFilter@@@Z

    void setFilter(srFilter* filter)
    {
        this->filter = filter;
    }
    SR_DLL_IMPORT void setHClampMode(std::int32_t enabled);
    SR_DLL_IMPORT void setVClampMode(std::int32_t enabled);

protected:
    virtual SR_DLL_IMPORT void copyNoScaling(srColorSurfaceIFace& source);
    void scaleAxis(srColorSurfaceIFace& source, bool vertical);
    virtual SR_DLL_IMPORT void scaleFast(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void scale(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void magnify(srColorSurfaceIFace& source);
    virtual SR_DLL_IMPORT void minify(srColorSurfaceIFace& source);

    SR_DLL_IMPORT void copySurfaceParameters(const srColorSurfaceIFace& source);
    SR_DLL_IMPORT std::int32_t isPixelFormatCompatible(const srColorSurfaceIFace& source) const;
    SR_DLL_IMPORT void setSurfaceDesc(const SurfaceDesc& description);

    friend class stTextureFile;
    friend void __stdcall LoadSurfacePixels(std::int32_t handle, srColorSurface* surface,
                                            const W8TgaHeader* header);
    friend class srColorSurface;

    std::uint8_t unknown_18_[0x04]{};
    std::int32_t width;
    std::int32_t height;
    std::int32_t pitch;
    std::uint32_t clamp_modes;
    srFilter* filter;
    srPixelConvert::PixelFormat pixel_format;
};

