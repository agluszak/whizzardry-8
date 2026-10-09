#pragma once

#include "srColorSurfaceIFace.h"
#include "srPalette.h"

// VTABLE: SURRENDER 0x100773A0
// class srClassSupport<srColorSurface, srColorSurfaceIFace, 0, 12560>

// VTABLE: SURRENDER 0x100772D0 srColorSurface
class srColorSurface
    : public srClassSupport<srColorSurface, srColorSurfaceIFace, 0, 0x3110> {
public:
    srColorSurface(const srPixelConvert::PixelFormat& format, w8_ulong width,
                                 w8_ulong height);
    srColorSurface(srPixelConvert::e_surfaceType type, w8_ulong width,
                                 w8_ulong height);
    srColorSurface(const srPixelConvert::PixelFormat& format, void* data,
                                 w8_ulong width, w8_ulong height, w8_ulong pitch);
    srColorSurface(srPixelConvert::e_surfaceType type, void* data,
                                 w8_ulong width, w8_ulong height, w8_ulong pitch);

    srColorSurface& operator=(const srColorSurface& other);

    static const char* sGetClassName();

    virtual void dump(std::ostream& stream) override;
    virtual srClass* vInstance() override;

    virtual w8_ulong getPixelRaw(w8_long x, w8_long y) override;
    virtual void setPixelRaw(w8_long x, w8_long y, w8_ulong pixel) override;
    virtual void getPixels(w8_ulong* pixels, const srVector2i* positions,
                                         w8_long count) override;
    virtual void setPixels(const w8_ulong* pixels, const srVector2i* positions,
                                         w8_long count) override;
    virtual void getPixelsRaw(void* pixels, const srVector2i* positions,
                                            w8_long count) override;
    virtual void setPixelsRaw(const void* pixels, const srVector2i* positions,
                                            w8_long count) override;
    virtual void getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end) override;
    virtual void setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start,
                                              w8_long y_end) override;
    virtual srPalette* getPalette() override;
    virtual void setPalette(srPalette* palette) override;
    virtual void* getDataPtr() override;
    virtual w8_long getDataSize() override;
    virtual int resize(w8_long width, w8_long height) override;
    virtual int rescale(w8_long width, w8_long height) override;
    virtual int changePixelFormat(const srPixelConvert::PixelFormat& format,
                                                int preserve) override;
    virtual void fill(w8_ulong pixel) override;
    virtual void setHLine(w8_long y, w8_long x_start, w8_long x_end,
                                        w8_ulong pixel) override;
    virtual void setVLine(w8_long x, w8_long y_start, w8_long y_end,
                                        w8_ulong pixel) override;
    virtual void blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                                    w8_long source_y, w8_long width, w8_long height) override;
    virtual void swapPixelRows(w8_long x0, w8_long y0, w8_long x1, w8_long y1,
                                             w8_long count) override;
    virtual void flipRectangle(const Rectangle& rectangle) override;
    virtual void getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start,
                                           w8_long x_end) override;
    virtual void setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start,
                                           w8_long x_end) override;
    virtual void getPixelRowRaw(void* pixels, w8_long y, w8_long x_start,
                                              w8_long x_end) override;
    virtual void setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start,
                                              w8_long x_end) override;

    srPixelConvert::ConversionFunc getPixelReadFunc() const;
    srPixelConvert::ConversionFunc getPixelWriteFunc() const;
    void setPixelReadFunc(srPixelConvert::ConversionFunc function);
    void setPixelWriteFunc(srPixelConvert::ConversionFunc function);

protected:
    virtual ~srColorSurface() override;

private:
    virtual void copyNoScaling(srColorSurfaceIFace& source) override;
    virtual void scaleFast(srColorSurfaceIFace& source) override;

    void allocData();
    void freeData();
    void init(const srPixelConvert::PixelFormat& format, w8_ulong width,
                            w8_ulong height, w8_ulong pitch);
    unsigned char* getAddress(w8_long x, w8_long y);
    void convertToARGB8888(w8_ulong* pixels, const void* source,
                                         w8_ulong count);
    void convertFromARGB8888(void* pixels, const w8_ulong* source,
                                           w8_ulong count);
    void reversePixels(void* pixels, w8_ulong count);
    int isCompatible(srColorSurfaceIFace& source);

    srPixelConvert::ConversionFunc pixel_write;
    srPixelConvert::ConversionFunc pixel_read;
    srPtr<srPalette> palette;
    enum { BORROWED_DATA = 0x01u };
    w8_ulong surface_flags;
    w8_long data_size;
    void* data;
};

W8_ABI_ASSERT((sizeof(srColorSurface) == 0x5c), "srColorSurface_must_be_0x5c");

/* Wizardry's client-side srColorSurface. */
typedef srClientSupport<srColorSurface, 0x3110> W8ColorSurface;
