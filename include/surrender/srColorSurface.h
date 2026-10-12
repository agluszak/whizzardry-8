#pragma once

#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_rect.h>
#include <memory>
#include <ostream>
#include <span>

#include "compat/compiler.h"
#include "srColorSurfaceIFace.h"
#include "srPalette.h"

#include "surrender/srTypeRegistry.h"
#include "surrender/srPixelConvert.h"
#include "wiz8/surface_storage.h"

// VTABLE: SURRENDER 0x100773A0
// class srClassSupport<srColorSurface, srColorSurfaceIFace, 12560>

// VTABLE: SURRENDER 0x100772D0 srColorSurface
class srColorSurface
    : public srClassSupport<srColorSurface, srColorSurfaceIFace, 0x3110> {
public:
    SR_DLL_IMPORT srColorSurface(const srPixelConvert::PixelFormat& format, std::uint32_t width,
                                 std::uint32_t height);
    SR_DLL_IMPORT srColorSurface(srPixelConvert::e_surfaceType type, std::uint32_t width,
                                 std::uint32_t height);
    SR_DLL_IMPORT srColorSurface(const srPixelConvert::PixelFormat& format, void* data,
                                 std::uint32_t width, std::uint32_t height, std::uint32_t pitch);
    SR_DLL_IMPORT srColorSurface(srPixelConvert::e_surfaceType type, void* data,
                                 std::uint32_t width, std::uint32_t height, std::uint32_t pitch);
    /* Shares an existing SDL surface; its pixels live as long as either owner. */
    srColorSurface(srPixelConvert::e_surfaceType type, SDL_Surface& shared);

    SR_DLL_IMPORT srColorSurface(const srColorSurface& other);
    SR_DLL_IMPORT srColorSurface& operator=(const srColorSurface& other);

    static SR_DLL_IMPORT const char* sGetClassName();

    virtual SR_DLL_IMPORT void dump(std::ostream& stream) override;

    std::span<std::uint8_t> rawPixelRow(std::int32_t y, std::int32_t x_start, std::int32_t x_end);
    virtual SR_DLL_IMPORT std::uint32_t getPixelRaw(std::int32_t x, std::int32_t y) override;
    virtual SR_DLL_IMPORT void setPixelRaw(std::int32_t x, std::int32_t y, std::uint32_t pixel) override;
    virtual SR_DLL_IMPORT void getPixelColumn(std::uint32_t* pixels, std::int32_t x, std::int32_t y_start,
                                              std::int32_t y_end) override;
    virtual SR_DLL_IMPORT void setPixelColumn(const std::uint32_t* pixels, std::int32_t x, std::int32_t y_start,
                                              std::int32_t y_end) override;
    virtual SR_DLL_IMPORT std::shared_ptr<srPalette> getPalette() override;
    virtual SR_DLL_IMPORT void setPalette(std::shared_ptr<srPalette> palette) override;
    virtual SR_DLL_IMPORT void* getDataPtr() override;
    virtual SR_DLL_IMPORT std::int32_t getDataSize() override;
    virtual SR_DLL_IMPORT std::int32_t resize(std::int32_t width, std::int32_t height) override;
    virtual SR_DLL_IMPORT std::int32_t rescale(std::int32_t width, std::int32_t height) override;
    virtual SR_DLL_IMPORT std::int32_t changePixelFormat(const srPixelConvert::PixelFormat& format,
                                                std::int32_t preserve) override;
    virtual SR_DLL_IMPORT void fill(std::uint32_t pixel) override;
    void fillRectangle(SDL_Rect rect, std::uint32_t pixel) override;
    virtual SR_DLL_IMPORT void blit(std::int32_t x, std::int32_t y, srColorSurfaceIFace& source, std::int32_t source_x,
                                    std::int32_t source_y, std::int32_t width, std::int32_t height) override;
    virtual SR_DLL_IMPORT void flipRectangle(const Rectangle& rectangle) override;
    virtual SR_DLL_IMPORT void getPixelRow(std::uint32_t* pixels, std::int32_t y, std::int32_t x_start,
                                           std::int32_t x_end) override;
    virtual SR_DLL_IMPORT void setPixelRow(const std::uint32_t* pixels, std::int32_t y, std::int32_t x_start,
                                           std::int32_t x_end) override;
    virtual SR_DLL_IMPORT void getPixelRowRaw(void* pixels, std::int32_t y, std::int32_t x_start,
                                              std::int32_t x_end) override;
    virtual SR_DLL_IMPORT void setPixelRowRaw(const void* pixels, std::int32_t y, std::int32_t x_start,
                                              std::int32_t x_end) override;


private:
    virtual SR_DLL_IMPORT void copyNoScaling(srColorSurfaceIFace& source) override;
    virtual SR_DLL_IMPORT void scaleFast(srColorSurfaceIFace& source) override;

    SR_DLL_IMPORT void init(const srPixelConvert::PixelFormat& format, std::uint32_t width,
                            std::uint32_t height, std::uint32_t pitch);
    SR_DLL_IMPORT std::uint8_t* getAddress(std::int32_t x, std::int32_t y);
    SR_DLL_IMPORT void convertToARGB8888(std::uint32_t* pixels, const void* source,
                                         std::uint32_t count);
    SR_DLL_IMPORT void convertFromARGB8888(void* pixels, const std::uint32_t* source,
                                           std::uint32_t count);
    SR_DLL_IMPORT std::int32_t isCompatible(srColorSurfaceIFace& source);

    template<class Pixel>
    void transferColumn(std::span<Pixel> pixels, std::int32_t x, std::int32_t y);
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
