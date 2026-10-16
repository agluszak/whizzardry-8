#pragma once

#include <memory>
#include <span>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "compat/compiler.h"
#include "srColorSurfaceIFace.h"
#include "srPalette.h"
#include "surrender/srPixelConvert.h"
#include "wiz8/surface_storage.h"

// VTABLE: SURRENDER 0x100772D0 srColorSurface
class srColorSurface : public srColorSurfaceIFace {
public:
    srColorSurface(const srPixelConvert::PixelFormat& format, w8_ulong width, w8_ulong height);
    srColorSurface(srPixelConvert::e_surfaceType type, w8_ulong width, w8_ulong height);
    srColorSurface(const srPixelConvert::PixelFormat& format, void* data, w8_ulong width,
                   w8_ulong height, w8_ulong pitch);
    srColorSurface(srPixelConvert::e_surfaceType type, void* data, w8_ulong width, w8_ulong height,
                   w8_ulong pitch);
    /* Shares an existing SDL surface; its pixels live as long as either owner. */
    srColorSurface(srPixelConvert::e_surfaceType type, SDL_Surface& shared);

    srColorSurface(const srColorSurface& other);
    srColorSurface& operator=(const srColorSurface& other);

    std::span<unsigned char> rawPixelRow(w8_long y, w8_long x_start, w8_long x_end);
    virtual w8_ulong getPixelRaw(w8_long x, w8_long y) override;
    virtual void setPixelRaw(w8_long x, w8_long y, w8_ulong pixel) override;
    virtual void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start,
                                w8_long y_end) override;
    virtual void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start,
                                w8_long y_end) override;
    virtual std::shared_ptr<srPalette> getPalette() override;
    virtual void setPalette(std::shared_ptr<srPalette> palette) override;
    virtual void* getDataPtr() override;
    virtual w8_long getDataSize() override;
    virtual int resize(w8_long width, w8_long height) override;
    virtual int rescale(w8_long width, w8_long height) override;
    virtual int changePixelFormat(const srPixelConvert::PixelFormat& format, int preserve) override;
    virtual void fill(w8_ulong pixel) override;
    void fillRectangle(SDL_Rect rect, w8_ulong pixel) override;
    virtual void blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                      w8_long source_y, w8_long width, w8_long height) override;
    virtual void flipRectangle(const Rectangle& rectangle) override;
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end) override;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start,
                             w8_long x_end) override;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x_start, w8_long x_end) override;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start,
                                w8_long x_end) override;

private:
    virtual void copyNoScaling(srColorSurfaceIFace& source) override;
    virtual void scaleFast(srColorSurfaceIFace& source, SDL_ScaleMode mode) override;

    void init(const srPixelConvert::PixelFormat& format, w8_ulong width, w8_ulong height,
              w8_ulong pitch);
    unsigned char* getAddress(w8_long x, w8_long y);
    void convertToARGB8888(w8_ulong* pixels, const void* source, w8_ulong count);
    void convertFromARGB8888(void* pixels, const w8_ulong* source, w8_ulong count);
    int isCompatible(srColorSurfaceIFace& source);

    template<class Pixel>
    void transferColumn(std::span<Pixel> pixels, w8_long x, w8_long y);
    void allocate();
    void adopt(SDL_Surface* storage);
    /* Caller memory or a shared surface cannot be reallocated. */
    bool hasFixedStorage() const;

    srPixelConvert::ConversionFunc pixel_write = nullptr;
    srPixelConvert::ConversionFunc pixel_read = nullptr;
    std::shared_ptr<srPalette> palette;
    /* SDL holds the pixels; pixel_format describes their channels. */
    SDLSurfaceOwner storage{nullptr, SDL_DestroySurface};
};
