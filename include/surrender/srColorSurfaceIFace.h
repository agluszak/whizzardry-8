#pragma once

#include <memory>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "srARGB.h"
#include "srClass.h"
#include "srMath.h"

class srColorSurface;
class srPalette;
struct W8TgaHeader;

class srColorSurfaceIFace : public srClass {
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
        SDL_PixelFormat pixel_format;
    };

    virtual ~srColorSurfaceIFace() override = default;
    srColorSurfaceIFace();
    srColorSurfaceIFace(const srColorSurfaceIFace& other);
    srColorSurfaceIFace& operator=(const srColorSurfaceIFace& other);

    virtual w8_ulong getPixel(w8_long x, w8_long y);
    virtual void setPixel(w8_long x, w8_long y, w8_ulong pixel);
    virtual w8_ulong getPixelRaw(w8_long x, w8_long y);
    virtual void setPixelRaw(w8_long x, w8_long y, w8_ulong pixel);
    virtual void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end);
    virtual void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end);
    virtual std::shared_ptr<srPalette> getPalette();
    virtual void setPalette(std::shared_ptr<srPalette> palette);
    virtual void* getDataPtr();
    virtual w8_long getDataSize();
    virtual int resize(w8_long width, w8_long height);
    virtual int rescale(w8_long width, w8_long height);
    virtual int changePixelFormat(SDL_PixelFormat format, int preserve);
    virtual void fill(w8_ulong pixel);
    virtual void fillRectangle(SDL_Rect rectangle, w8_ulong pixel);
    virtual void setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel);
    virtual void setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel);
    virtual void setLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_ulong pixel);
    virtual void blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                      w8_long source_y, w8_long width, w8_long height);
    virtual void copy(srColorSurfaceIFace& source, SDL_ScaleMode mode = SDL_SCALEMODE_NEAREST);
    virtual void flipRectangle(const Rectangle& rectangle);
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start, w8_long x_end) = 0;

    void clampCoordinates(w8_long& x, w8_long& y);
    void flipHorizontal();
    void flipVertical();
    double getAspectRatio() const;
    w8_long getClampedX(w8_long x) const;
    w8_long getClampedY(w8_long y) const;

    w8_long getHeight() const { return height; }

    w8_long getPitch() const { return pitch; }
    SDL_PixelFormat getPixelFormat() const { return pixel_format; }

    SurfaceDesc getSurfaceDesc() const
    {
        return {static_cast<w8_ulong>(width), static_cast<w8_ulong>(height),
                static_cast<w8_ulong>(pitch), clamp_modes, pixel_format};
    }

    w8_long getWidth() const { return width; }

    void setHClampMode(int enabled);
    void setVClampMode(int enabled);

protected:
    virtual void copyNoScaling(srColorSurfaceIFace& source);
    virtual void scaleFast(srColorSurfaceIFace& source, SDL_ScaleMode mode);

    void copySurfaceParameters(const srColorSurfaceIFace& source);
    int isPixelFormatCompatible(const srColorSurfaceIFace& source) const;
    void setSurfaceDesc(const SurfaceDesc& description);

    friend class stTextureFile;
    friend void LoadSurfacePixels(int handle, srColorSurface* surface, const W8TgaHeader* header);
    friend class srColorSurface;

    unsigned char unknown_18_[0x04]{};
    w8_long width;
    w8_long height;
    w8_long pitch;
    w8_ulong clamp_modes;
    SDL_PixelFormat pixel_format;
};
