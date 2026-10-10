#include "surrender/srMath.h"
#include "wiz8/compat/surfaces.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <new>
#include <utility>

#include "surrender/srColorSurface.h"

#include <math.h>
#include <ostream>

#include "surrender/srCore.h"
#include "surrender/srFilter.h"
#include "surrender/srPalette.h"

// FUNCTION: SURRENDER 0x100571F0
srColorSurfaceIFace::srColorSurfaceIFace()
    : width(0), height(0), pitch(0), clamp_modes(0), filter(nullptr)
{
    pixel_format.red_bits = 0;
    pixel_format.red_shift = 0;
    pixel_format.green_bits = 0;
    pixel_format.green_shift = 0;
    pixel_format.blue_bits = 0;
    pixel_format.blue_shift = 0;
    pixel_format.alpha_bits = 0;
    pixel_format.alpha_shift = 0;
    pixel_format.color_model = srPixelConvert::COLOR_RGB;
    pixel_format.pixel_size = srPixelConvert::PIXEL_SIZE_8;
}

// FUNCTION: SURRENDER 0x1005A120
srColorSurfaceIFace::srColorSurfaceIFace(const srColorSurfaceIFace& other)
    : srClassSupport<srColorSurfaceIFace, srClass, 0x3100>()
{
    *this = other;
}

// FUNCTION: SURRENDER 0x1005B280
const char* srColorSurfaceIFace::sGetClassName()
{
    return "srColorSurfaceIFace";
}

// FUNCTION: SURRENDER 0x10021310
std::shared_ptr<srPalette> srColorSurfaceIFace::getPalette()
{
    return 0;
}

// FUNCTION: SURRENDER 0x10021320
void srColorSurfaceIFace::setPalette(std::shared_ptr<srPalette> palette) {}

// FUNCTION: SURRENDER 0x10021330
void* srColorSurfaceIFace::getDataPtr()
{
    return 0;
}

// FUNCTION: SURRENDER 0x10021340
w8_long srColorSurfaceIFace::getDataSize()
{
    return pitch * height;
}

// FUNCTION: SURRENDER 0x10021350
int srColorSurfaceIFace::resize(w8_long width, w8_long height)
{
    return 0;
}

// FUNCTION: SURRENDER 0x10021360
int srColorSurfaceIFace::rescale(w8_long width, w8_long height)
{
    return 0;
}

// FUNCTION: SURRENDER 0x10021370
int srColorSurfaceIFace::changePixelFormat(const srPixelConvert::PixelFormat& format, int preserve)
{
    return 0;
}

// FUNCTION: SURRENDER 0x100572E0
w8_long srColorSurfaceIFace::getClampedX(w8_long x) const
{
    w8_long width = this->width;
    if ((clamp_modes & CLAMP_HORIZONTAL) != 0) {
        if (x < 0) {
            return 0;
        }
        if (width <= x) {
            return width - 1;
        }
    } else {
        if (x < 0) {
            return width - (-1 - x) % width - 1;
        }
        if (width <= x) {
            return x % width;
        }
    }
    return x;
}

// FUNCTION: SURRENDER 0x10057330
w8_long srColorSurfaceIFace::getClampedY(w8_long y) const
{
    w8_long height = this->height;
    if ((clamp_modes & CLAMP_VERTICAL) != 0) {
        if (height <= y) {
            return height - 1;
        }
        if (y < 0) {
            return 0;
        }
    } else {
        if (y < 0) {
            return height - (-1 - y) % height - 1;
        }
        if (height <= y) {
            return y % height;
        }
    }
    return y;
}

// FUNCTION: SURRENDER 0x10057380
void srColorSurfaceIFace::clampCoordinates(w8_long& x, w8_long& y)
{
    x = getClampedX(x);
    y = getClampedY(y);
}

// FUNCTION: SURRENDER 0x10059860
w8_ulong srColorSurfaceIFace::getAlphaBits() const
{
    return pixel_format.alpha_bits;
}

// FUNCTION: SURRENDER 0x10059870
double srColorSurfaceIFace::getAspectRatio() const
{
    if (height == 0) {
        return 0.0;
    }
    return width / (double)height;
}

// FUNCTION: SURRENDER 0x10059890
w8_long srColorSurfaceIFace::getBitsPerPixel() const
{
    return (pixel_format.pixel_size + 1) * 8;
}

// FUNCTION: SURRENDER 0x100598A0
w8_long srColorSurfaceIFace::getBlueBits() const
{
    return pixel_format.blue_bits;
}

// FUNCTION: SURRENDER 0x100598B0
w8_long srColorSurfaceIFace::getBytesPerPixel() const
{
    return pixel_format.pixel_size + 1;
}

// FUNCTION: SURRENDER 0x100598C0
srFilter* srColorSurfaceIFace::getFilter() const
{
    return filter;
}

// FUNCTION: SURRENDER 0x100598D0
w8_long srColorSurfaceIFace::getGreenBits() const
{
    return pixel_format.green_bits;
}

// FUNCTION: SURRENDER 0x100598E0
int srColorSurfaceIFace::getHClampMode() const
{
    return clamp_modes & CLAMP_HORIZONTAL;
}

// FUNCTION: SURRENDER 0x100599D0
w8_long srColorSurfaceIFace::getRedBits() const
{
    return pixel_format.red_bits;
}

// FUNCTION: SURRENDER 0x10059A50
int srColorSurfaceIFace::getVClampMode() const
{
    return (clamp_modes >> 1) & 1;
}

// FUNCTION: SURRENDER 0x10059A70
int srColorSurfaceIFace::isAlpha() const
{
    return pixel_format.alpha_bits != 0;
}

// FUNCTION: SURRENDER 0x10059A80
int srColorSurfaceIFace::isPaletted() const
{
    return pixel_format.color_model == srPixelConvert::COLOR_INDEXED;
}

// FUNCTION: SURRENDER 0x10059AA0
void srColorSurfaceIFace::setHClampMode(int enabled)
{
    if (enabled != 0) {
        clamp_modes = clamp_modes | CLAMP_HORIZONTAL;
        return;
    }
    clamp_modes = clamp_modes & ~CLAMP_HORIZONTAL;
}

// FUNCTION: SURRENDER 0x1005A020
void srColorSurfaceIFace::setVClampMode(int enabled)
{
    if (enabled != 0) {
        clamp_modes = clamp_modes | CLAMP_VERTICAL;
        return;
    }
    clamp_modes = clamp_modes & ~CLAMP_VERTICAL;
}

// FUNCTION: SURRENDER 0x1005A0D0
const srPixelConvert::PixelFormat* srColorSurfaceIFace::getPixelFormat() const
{
    return &pixel_format;
}

// FUNCTION: SURRENDER 0x1005A0E0
int srColorSurfaceIFace::isPixelFormatCompatible(const srColorSurfaceIFace& source) const
{
    return pixel_format == source.pixel_format;
}

// FUNCTION: SURRENDER 0x1005ADD0
void srColorSurfaceIFace::setSurfaceDesc(const SurfaceDesc& description)
{
    width = description.width;
    height = description.height;
    pitch = description.pitch;
    clamp_modes = description.clamp_modes;
    filter = description.filter;
    pixel_format = description.pixel_format;
}

// FUNCTION: SURRENDER 0x1005B230
void srColorSurfaceIFace::copySurfaceParameters(const srColorSurfaceIFace& source)
{
    filter = source.filter;
    if ((source.clamp_modes & CLAMP_HORIZONTAL) != 0) {
        clamp_modes = clamp_modes | CLAMP_HORIZONTAL;
    } else {
        clamp_modes = clamp_modes & ~CLAMP_HORIZONTAL;
    }
    if ((source.clamp_modes & CLAMP_VERTICAL) != 0) {
        clamp_modes = clamp_modes | CLAMP_VERTICAL;
        return;
    }
    clamp_modes = clamp_modes & ~CLAMP_VERTICAL;
}

// FUNCTION: SURRENDER 0x10057810
void srColorSurfaceIFace::setLine(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_ulong pixel)
{
    w8_long dx = x1 - x0;
    w8_long dy = y1 - y0;
    if (dx == 0) {
        setVLine(x0, y0, y1, pixel);
        return;
    }
    if (dy == 0) {
        setHLine(y0, x0, x1, pixel);
        return;
    }
    w8_long width = this->width;
    w8_long height = this->height;
    if ((dy < 0 ? -dy : dy) < (dx < 0 ? -dx : dx)) {
        w8_long last_x = x1;
        w8_long last_y = y1;
        if (dx < 0) {
            last_x = x0;
            last_y = y0;
            y0 = y1;
            x0 = x1;
        }
        w8_long high_y = last_y;
        w8_long low_y = y0;
        if (last_y <= y0) {
            high_y = y0;
            low_y = last_y;
        }
        height = height - 1;
        if ((-1 < last_x) && (x0 < width) && (-1 < high_y) && (low_y <= height)) {
            float gradient = (float)dy / dx;
            w8_long step = (w8_long)(gradient * 65536.0);
            if (x0 < 0) {
                y0 = (w8_long)(y0 - x0 * gradient);
                x0 = 0;
            }
            if (y0 < last_y) {
                if (y0 < 0) {
                    x0 = x0 - (w8_long)(y0 / gradient);
                    y0 = 0;
                }
                if (height <= last_y) {
                    last_x = (w8_long)(last_x - (last_y - (float)height) / gradient);
                }
            } else {
                if (last_y < 0) {
                    last_x = last_x - (w8_long)(last_y / gradient);
                }
                if (height <= y0) {
                    x0 = (w8_long)(x0 - (y0 - (float)height) / gradient);
                    y0 = height;
                }
            }
            if (width < last_x) {
                last_x = width;
            }
            w8_long y_fixed = y0 << 0x10;
            for (; x0 < last_x; x0 = x0 + 1) {
                setPixel(x0, (y_fixed + (y_fixed >> 0x1f & 0xffff)) >> 0x10, pixel);
                y_fixed = y_fixed + step;
            }
        }
    } else {
        w8_long last_y = x1;
        w8_long last_x = y1;
        if (dy < 0) {
            last_y = x0;
            last_x = y0;
            y0 = y1;
            x0 = x1;
        }
        w8_long high_x = last_y;
        w8_long low_x = x0;
        if (last_y <= x0) {
            high_x = x0;
            low_x = last_y;
        }
        width = width - 1;
        if (((-1 < low_x) && (high_x <= width) && (-1 < last_x)) && (y0 < height)) {
            float gradient = dx / (float)dy;
            w8_long step = (w8_long)(gradient * 65536.0);
            if (y0 < 0) {
                x0 = (w8_long)(x0 - y0 * gradient);
                y0 = 0;
            }
            if (x0 < last_y) {
                if (x0 < 0) {
                    y0 = y0 - (w8_long)(x0 / gradient);
                    x0 = 0;
                }
                if (width <= last_y) {
                    last_x = (w8_long)(last_x - (last_y - (float)width) / gradient);
                }
            } else {
                if (last_y < 0) {
                    last_x = last_x - (w8_long)(last_y / gradient);
                }
                if (width <= x0) {
                    y0 = (w8_long)(y0 - (x0 - (float)width) / gradient);
                    x0 = width;
                }
            }
            if (height < last_x) {
                last_x = height;
            }
            w8_long x_fixed = x0 << 0x10;
            for (; y0 < last_x; y0 = y0 + 1) {
                setPixel((x_fixed + (x_fixed >> 0x1f & 0xffff)) >> 0x10, y0, pixel);
                x_fixed = x_fixed + step;
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1005B290
srColorSurfaceIFace& srColorSurfaceIFace::operator=(const srColorSurfaceIFace& other)
{
    if (&other != this) {
        srClass::operator=(other);
        std::copy(std::begin(other.unknown_18_), std::end(other.unknown_18_),
                  std::begin(unknown_18_));
        width = other.width;
        height = other.height;
        pitch = other.pitch;
        clamp_modes = other.clamp_modes;
        filter = other.filter;
        pixel_format = other.pixel_format;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1005B2E0
w8_ulong srColorSurfaceIFace::getPixel(w8_long x, w8_long y)
{
    w8_ulong pixel;
    getPixelRow(&pixel, y, x, x + 1);
    return pixel;
}

// FUNCTION: SURRENDER 0x1005B470
void srColorSurfaceIFace::setPixel(w8_long x, w8_long y, w8_ulong pixel)
{
    setPixelRow(&pixel, y, x, x + 1);
}

// FUNCTION: SURRENDER 0x1005B100
w8_ulong srColorSurfaceIFace::getPixelRaw(w8_long x, w8_long y)
{
    w8_ulong pixel;
    getPixelRowRaw(&pixel, y, x, x + 1);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        return pixel & 0xff;
    case srPixelConvert::PIXEL_SIZE_16:
        return pixel & 0xffff;
    case srPixelConvert::PIXEL_SIZE_24:
        return pixel & 0xffffff;
    case srPixelConvert::PIXEL_SIZE_32:
        return pixel;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1005B1B0
void srColorSurfaceIFace::setPixelRaw(w8_long x, w8_long y, w8_ulong pixel)
{
    /* reinterpret-ok: the retail setter writes only the pixel word's low bytes
       for each raw format before passing that same word to setPixelRowRaw. */
    unsigned char* bytes = reinterpret_cast<unsigned char*>(&pixel);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        bytes[0] = static_cast<unsigned char>(pixel);
        break;
    case srPixelConvert::PIXEL_SIZE_16:
        /* reinterpret-ok: a two-byte write into the raw pixel word. */
        *reinterpret_cast<unsigned short*>(bytes) = static_cast<unsigned short>(pixel);
        break;
    case srPixelConvert::PIXEL_SIZE_24:
        bytes[0] = static_cast<unsigned char>(pixel);
        bytes[1] = static_cast<unsigned char>(pixel >> 8);
        bytes[2] = static_cast<unsigned char>(pixel >> 16);
        break;
    case srPixelConvert::PIXEL_SIZE_32:
        break;
    }
    setPixelRowRaw(&pixel, y, x, x + 1);
}

// FUNCTION: SURRENDER 0x1005B310
void srColorSurfaceIFace::getPixels(w8_ulong* pixels, const srVector2i* positions, w8_long count)
{
    for (; count > 0; --count, ++pixels, ++positions) {
        *pixels = getPixel(positions->x, positions->y);
    }
}

// FUNCTION: SURRENDER 0x1005B3A0
void srColorSurfaceIFace::setPixels(const w8_ulong* pixels, const srVector2i* positions,
                                    w8_long count)
{
    for (; count > 0; --count, ++pixels, ++positions) {
        setPixel(positions->x, positions->y, *pixels);
    }
}

// FUNCTION: SURRENDER 0x1005B350
void srColorSurfaceIFace::getPixelsRaw(void* pixels, const srVector2i* positions, w8_long count)
{
    unsigned char* out = (unsigned char*)pixels;
    w8_long bytes = pixel_format.pixel_size + 1;
    for (; count > 0; --count, ++positions, out += bytes) {
        getPixelRowRaw(out, positions->y, positions->x, positions->x + 1);
    }
}

// FUNCTION: SURRENDER 0x1005B3E0
void srColorSurfaceIFace::setPixelsRaw(const void* pixels, const srVector2i* positions, w8_long count)
{
    const unsigned char* in = (const unsigned char*)pixels;
    w8_long bytes = pixel_format.pixel_size + 1;
    for (; count > 0; --count, ++positions, in += bytes) {
        setPixelRowRaw(in, positions->y, positions->x, positions->x + 1);
    }
}

// FUNCTION: SURRENDER 0x1005B430
void srColorSurfaceIFace::getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end)
{
    for (; y_start < y_end; ++y_start, ++pixels) {
        *pixels = getPixel(x, y_start);
    }
}

// FUNCTION: SURRENDER 0x1005B490
void srColorSurfaceIFace::setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start,
                                         w8_long y_end)
{
    for (; y_start < y_end; ++y_start, ++pixels) {
        setPixel(x, y_start, *pixels);
    }
}

// FUNCTION: SURRENDER 0x100573B0
void srColorSurfaceIFace::fill(w8_ulong pixel)
{
    w8_long width = this->width;
    w8_long height = this->height;
    std::vector<w8_ulong> pixels(width, pixel);
    w8_ulong* row = pixels.data();
    for (w8_long y = 0; y < height; ++y) {
        setPixelRow(row, y, 0, width);
    }
}

// FUNCTION: SURRENDER 0x10057750
void srColorSurfaceIFace::setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel)
{
    if (y >= 0 && y < height) {
        if (x_end < x_start) {
            w8_long swap = x_start;
            x_start = x_end;
            x_end = swap;
        }
        if (x_start < 0) {
            x_start = 0;
        }
        if (width < x_end) {
            x_end = width;
        }
        for (; x_start < x_end; ++x_start) {
            setPixel(x_start, y, pixel);
        }
    }
}

// FUNCTION: SURRENDER 0x100577B0
void srColorSurfaceIFace::setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel)
{
    if (x >= 0 && x < width) {
        if (y_end < y_start) {
            w8_long swap = y_start;
            y_start = y_end;
            y_end = swap;
        }
        if (y_start < 0) {
            y_start = 0;
        }
        if (height < y_end) {
            y_end = height;
        }
        for (; y_start < y_end; ++y_start) {
            setPixel(x, y_start, pixel);
        }
    }
}

// FUNCTION: SURRENDER 0x10057420
void srColorSurfaceIFace::swapPixelRows(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_long count)
{
    w8_long width = this->width;
    if (y0 >= 0 && y0 < height && y1 >= 0 && y1 < height && count > 0) {
        if (x0 < 0) {
            x1 = x1 - x0;
            count = count + x0;
            x0 = 0;
        }
        if (x1 < 0) {
            x0 = x0 - x1;
            count = count + x1;
            x1 = 0;
        }
        if (x0 < width && x1 < width) {
            if (width < x0 + count) {
                count = width - x0;
            }
            if (width < count + x1) {
                count = width - x1;
            }
            if (count > 0) {
                w8_long bytes = (pixel_format.pixel_size + 1) * count;
                std::vector<unsigned char> pixels(bytes * 2);
                unsigned char* buffer = pixels.data();
                unsigned char* second = buffer + bytes;
                getPixelRowRaw(buffer, y0, x0, x0 + count);
                getPixelRowRaw(second, y1, x1, x1 + count);
                setPixelRowRaw(buffer, y1, x1, x1 + count);
                setPixelRowRaw(second, y0, x0, x0 + count);
            }
        }
    }
}

// FUNCTION: SURRENDER 0x10057550
void srColorSurfaceIFace::flipRectangle(const Rectangle& rectangle)
{
    w8_long x_lo = rectangle.left;
    w8_long x_hi = rectangle.right;
    bool flip_x = x_hi < x_lo;
    if (flip_x) {
        x_lo = rectangle.right;
        x_hi = rectangle.left;
    }
    w8_long y_lo = rectangle.top;
    w8_long y_hi = rectangle.bottom;
    bool flip_y = y_hi < y_lo;
    if (flip_y) {
        y_lo = rectangle.bottom;
        y_hi = rectangle.top;
    }
    if ((flip_x || flip_y) && x_lo >= 0 && y_lo >= 0 && x_hi <= this->width &&
        y_hi <= this->height) {
        w8_long width = x_hi - x_lo;
        w8_long height = y_hi - y_lo;
        if (width != 0 && height != 0) {
            std::vector<w8_ulong> pixels(width * 2);
            w8_ulong* buffer = pixels.data();
            w8_long middle = y_lo + height / 2;
            w8_long mirror = y_hi - 1;
            for (w8_long row = y_lo; row < middle; ++row, --mirror) {
                getPixelRow(buffer, row, x_lo, width);
                getPixelRow(buffer + width, mirror, x_lo, width);
                if (flip_x) {
                    std::reverse(buffer, buffer + width);
                    std::reverse(buffer + width, buffer + width * 2);
                }
                w8_ulong* row_pixels = flip_y ? buffer + width : buffer;
                w8_ulong* mirror_pixels = flip_y ? buffer : buffer + width;
                setPixelRow(row_pixels, row, x_lo, width);
                setPixelRow(mirror_pixels, mirror, x_lo, width);
            }
            if (flip_x && (height & 1) != 0) {
                getPixelRow(buffer, middle, x_lo, width);
                std::reverse(buffer, buffer + width);
                setPixelRow(buffer, middle, x_lo, width);
            }
        }
    }
}



// FUNCTION: SURRENDER 0x10059690
void srColorSurfaceIFace::scaleFast(srColorSurfaceIFace& source)
{
    w8_long height = this->height;
    w8_long source_width = source.width;
    w8_long source_height = source.height;
    w8_long width = this->width;
    if ((source_width == width) && (source_height == height)) {
        copyNoScaling(source);
        return;
    }
    std::vector<w8_long> column_map(width);
    std::vector<srARGB> source_row_storage(source_width);
    srARGB* source_row_colors = source_row_storage.data();
    w8_ulong* source_row = (w8_ulong*)source_row_colors;
    std::vector<srARGB> row_storage(width);
    srARGB* row_colors = row_storage.data();
    w8_ulong* row = (w8_ulong*)row_colors;
    for (w8_long x = 0; x < width; x++) {
        column_map[x] = source.getClampedX((w8_long)((float)x * source_width / width));
    }
    w8_long last_y = -1;
    for (w8_long y = 0; y < height; y++) {
        w8_long source_y = source.getClampedY((w8_long)((float)y * source_height / height));
        if ((source_y != last_y) && (source.getPixelRow(source_row, source_y, 0, source_width),
                                     last_y = source_y, 0 < width)) {
            for (w8_long x = 0; x < width; x++) {
                row[x] = source_row[column_map[x]];
            }
        }
        setPixelRow(row, y, 0, width);
    }
}


// FUNCTION: SURRENDER 0x10059600
void srColorSurfaceIFace::copyNoScaling(srColorSurfaceIFace& source)
{
    if (this != &source) {
        w8_long height = source.height;
        w8_long width = source.width;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        w8_ulong* row = (w8_ulong*)row_colors;
        for (w8_long y = 0; y < height; y++) {
            source.getPixelRow(row, y, 0, width);
            setPixelRow(row, y, 0, width);
        }
    }
}

// FUNCTION: SURRENDER 0x10059900
void srColorSurfaceIFace::copy(srColorSurfaceIFace& source)
{
    if (&source != this) {
        w8_long width = this->width;
        w8_long source_width = source.width;
        w8_long height = this->height;
        w8_long source_height = source.height;
        if (source_width == width && source_height == height) {
            copyNoScaling(source);
            return;
        }
        srFilter* filter = source.filter;
        if (filter != &srBoxFilter && filter != 0) {
            if (filter == &srTriangleFilter) {
                if (width == source_width * 2 && height == source_height * 2) {
                    magnify(source);
                    return;
                }
                if (width * 2 == source_width && height * 2 == source_height) {
                    minify(source);
                    return;
                }
            }
            if (source_width == width) {
                scaleAxis(source, true);
                return;
            }
            if (source_height == height) {
                scaleAxis(source, false);
                return;
            }
            scale(source);
            return;
        }
        scaleFast(source);
    }
}

// FUNCTION: SURRENDER 0x1005A7A0
void srColorSurfaceIFace::scale(srColorSurfaceIFace& source)
{
    w8_ulong source_height = source.height;
    w8_ulong width = this->width;
    srColorSurface scaled(srPixelConvert::SURFACE_BGRA32, width, source_height);
    srColorSurfaceIFace& scaled_iface = scaled;
    scaled_iface.copySurfaceParameters(source);
    scaled_iface.scaleAxis(source, false);
    scaleAxis(scaled, true);
}

// FUNCTION: SURRENDER 0x1005AE10
void srColorSurfaceIFace::dump(std::ostream& stream)
{
    srClass::dump(stream);
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    stream.width(0x20);
    if (filter != 0) {
        stream << "  Filter: " << filter->getName() << '\n';
    } else {
        stream << "  Filter:"
               << "not defined" << '\n';
    }
    stream.width(0x20);
    stream << "  Horizontal clamp mode: " << (clamp_modes & CLAMP_HORIZONTAL) << '\n';
    stream.width(0x20);
    stream << "  Vertical clamp mode: " << (clamp_modes >> 1 & 1) << '\n';
    stream.width(0x20);
    stream << "  Dimensions: " << width << 'x' << height << 'x' << (pixel_format.pixel_size * 8 + 8)
           << '\n';
    stream.width(0x20);
    stream << "  Pitch: " << pitch << '\n';
    stream.width(0x20);
    stream << "  Dataptr: " << getDataPtr() << '\n';
    stream.width(0x20);
    stream << "  Data size: " << getDataSize() << " bytes" << '\n';
    stream.width(0x20);
    char format_name[12];
    pixel_format.getName(format_name);
    stream << "  Pixel format: " << format_name << '\n';
    stream.flags(static_cast<std::ios::fmtflags>(flags & 0x7fff));
}

// FUNCTION: SURRENDER 0x1005A040
void srColorSurfaceIFace::flipVertical()
{
    Rectangle rectangle;
    rectangle.left = 0;
    rectangle.top = height;
    rectangle.right = width;
    rectangle.bottom = 0;
    flipRectangle(rectangle);
}

// FUNCTION: SURRENDER 0x1005A070
void srColorSurfaceIFace::flipHorizontal()
{
    Rectangle rectangle;
    rectangle.left = width;
    rectangle.top = 0;
    rectangle.right = 0;
    rectangle.bottom = height;
    flipRectangle(rectangle);
}

// FUNCTION: SURRENDER 0x1005A0A0
void srColorSurfaceIFace::rotate180()
{
    Rectangle rectangle;
    rectangle.left = width;
    rectangle.top = height;
    rectangle.right = 0;
    rectangle.bottom = 0;
    flipRectangle(rectangle);
}

/* The SDL format matching a SurRender layout, or a raw container of the same size. */
static SDL_PixelFormat StorageFormat(const srPixelConvert::PixelFormat& format)
{
    const int bits = (format.pixel_size + 1) * 8;
    if (format.color_model == srPixelConvert::COLOR_RGB && format.fourcc == 0) {
        const auto mask = [](unsigned bits, unsigned shift) {
            return bits == 0 ? 0u : ((1u << bits) - 1u) << shift;
        };
        const SDL_PixelFormat matched = SDL_GetPixelFormatForMasks(
            bits, mask(format.red_bits, format.red_shift),
            mask(format.green_bits, format.green_shift), mask(format.blue_bits, format.blue_shift),
            mask(format.alpha_bits, format.alpha_shift));
        if (matched != SDL_PIXELFORMAT_UNKNOWN) {
            return matched;
        }
    }
    switch (format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        return SDL_PIXELFORMAT_INDEX8;
    case srPixelConvert::PIXEL_SIZE_16:
        return SDL_PIXELFORMAT_RGB565;
    case srPixelConvert::PIXEL_SIZE_24:
        return SDL_PIXELFORMAT_RGB24;
    default:
        return SDL_PIXELFORMAT_ARGB8888;
    }
}

void srColorSurface::adopt(SDL_Surface* surface)
{
    if (surface == nullptr) {
        throw std::bad_alloc();
    }
    storage.reset(surface);
    pitch = storage->pitch;
}

void srColorSurface::allocate()
{
    adopt(CreateCpuStorage(width, height, StorageFormat(pixel_format)).release());
}

bool srColorSurface::hasFixedStorage() const
{
    return (storage->flags & SDL_SURFACE_PREALLOCATED) != 0 || storage->refcount > 1;
}

// FUNCTION: SURRENDER 0x1005B790
srColorSurface::srColorSurface(const srPixelConvert::PixelFormat& format, w8_ulong arg_width,
                               w8_ulong arg_height)
{
    init(format, arg_width, arg_height, 0);
    allocate();
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005B8A0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, w8_ulong arg_width,
                               w8_ulong arg_height)
{
    srPixelConvert::PixelFormat format;
    srPixelConvert::mapPixelFormat(type, format);
    init(format, arg_width, arg_height, 0);
    allocate();
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

/* The data-taking variants view caller storage, which must outlive the surface. */
// FUNCTION: SURRENDER 0x1005B9C0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    srPixelConvert::PixelFormat format;
    srPixelConvert::mapPixelFormat(type, format);
    init(format, arg_width, arg_height, arg_pitch);
    adopt(CreateCpuStorage(arg_width, arg_height, StorageFormat(format), data, arg_pitch).release());
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005BAC0
srColorSurface::srColorSurface(const srPixelConvert::PixelFormat& format, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    init(format, arg_width, arg_height, arg_pitch);
    adopt(CreateCpuStorage(arg_width, arg_height, StorageFormat(format), data, arg_pitch).release());
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, SDL_Surface& shared)
{
    srPixelConvert::PixelFormat format;
    srPixelConvert::mapPixelFormat(type, format);
    init(format, shared.w, shared.h, shared.pitch);
    ++shared.refcount;
    adopt(&shared);
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005D520
srColorSurface& srColorSurface::operator=(const srColorSurface& other)
{
    if (&other != this) {
        srColorSurfaceIFace::operator=(other);
        adopt(CopyCpuStorage(*other.storage).release());
        palette = other.palette;
        pixel_write = other.pixel_write;
        pixel_read = other.pixel_read;
    }
    return *this;
}

srColorSurface::srColorSurface(const srColorSurface& other)
    : srClassSupport<srColorSurface, srColorSurfaceIFace, 0x3110>()
{
    *this = other;
}

// FUNCTION: SURRENDER 0x1005B550
void* srColorSurface::getDataPtr()
{
    return storage->pixels;
}

// FUNCTION: SURRENDER 0x1005B560
w8_long srColorSurface::getDataSize()
{
    return pitch * height;
}

// FUNCTION: SURRENDER 0x1005B570
std::shared_ptr<srPalette> srColorSurface::getPalette()
{
    return palette;
}

// FUNCTION: SURRENDER 0x1005B580
void srColorSurface::setPalette(std::shared_ptr<srPalette> palette)
{
    this->palette = palette;
}

// FUNCTION: SURRENDER 0x1005B5B0
unsigned char* srColorSurface::getAddress(w8_long x, w8_long y)
{
    return static_cast<unsigned char*>(getDataPtr()) + pitch * y + (pixel_format.pixel_size + 1) * x;
}

// FUNCTION: SURRENDER 0x1005B5D0
void srColorSurface::convertToARGB8888(w8_ulong* pixels, const void* source,
                                       w8_ulong count)
{
    srPixelConvert::ConversionInfo info;
    info.dest = pixels;
    info.source = source;
    info.count = count;
    info.palette = palette.get();
    info.format = &pixel_format;
    pixel_read(info);
}

// FUNCTION: SURRENDER 0x1005B610
void srColorSurface::convertFromARGB8888(void* pixels, const w8_ulong* source,
                                         w8_ulong count)
{
    srPixelConvert::ConversionInfo info;
    info.dest = pixels;
    info.source = source;
    info.count = count;
    info.palette = palette.get();
    info.format = &pixel_format;
    pixel_write(info);
}

// FUNCTION: SURRENDER 0x1005B6B0
void srColorSurface::init(const srPixelConvert::PixelFormat& format, w8_ulong arg_width,
                          w8_ulong arg_height, w8_ulong arg_pitch)
{
    SurfaceDesc desc{};
    desc.width = arg_width;
    desc.height = arg_height;
    desc.pitch = arg_pitch;
    desc.pixel_format = format;
    setSurfaceDesc(desc);
    palette = srCore.getPalette();
    pixel_write = 0;
    pixel_read = 0;
}

// FUNCTION: SURRENDER 0x1005DD70
srPixelConvert::ConversionFunc srColorSurface::getPixelWriteFunc() const
{
    return pixel_write;
}

// FUNCTION: SURRENDER 0x1005DD80
srPixelConvert::ConversionFunc srColorSurface::getPixelReadFunc() const
{
    return pixel_read;
}

// FUNCTION: SURRENDER 0x1005DD90
void srColorSurface::setPixelWriteFunc(srPixelConvert::ConversionFunc function)
{
    if (function != 0) {
        pixel_write = function;
    }
}

// FUNCTION: SURRENDER 0x1005DDA0
void srColorSurface::setPixelReadFunc(srPixelConvert::ConversionFunc function)
{
    if (function != 0) {
        pixel_read = function;
    }
}

// FUNCTION: SURRENDER 0x1005DE20
const char* srColorSurface::sGetClassName()
{
    return "srColorSurface";
}

// FUNCTION: SURRENDER 0x1005BEC0
int srColorSurface::resize(w8_long arg_width, w8_long arg_height)
{
    if (arg_width > 0 && arg_height > 0) {
        if (arg_width == width && arg_height == height) {
            return 1;
        }
        if (!hasFixedStorage()) {
            SurfaceDesc desc;
            desc.width = arg_width;
            desc.height = arg_height;
            desc.pitch = 0;
            desc.clamp_modes = clamp_modes;
            desc.filter = filter;
            desc.pixel_format = pixel_format;
            setSurfaceDesc(desc);
            allocate();
            return 1;
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1005BD10
int srColorSurface::rescale(w8_long arg_width, w8_long arg_height)
{
    if (arg_width > 0 && arg_height > 0) {
        if (arg_width == width && arg_height == height) {
            return 1;
        }
        if (!hasFixedStorage()) {
            srColorSurface scaled(srPixelConvert::SURFACE_BGRA32, arg_width, arg_height);
            scaled.copySurfaceParameters(*this);
            scaled.copy(*this);
            srColorSurface resized(pixel_format, arg_width, arg_height);
            resized.setPalette(palette);
            resized.pixel_write = pixel_write;
            resized.pixel_read = pixel_read;
            resized.copy(scaled);
            width = arg_width;
            height = arg_height;
            adopt(resized.storage.release());
            return 1;
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1005BDE0
int srColorSurface::changePixelFormat(const srPixelConvert::PixelFormat& format, int preserve)
{
    if (hasFixedStorage()) {
        return 0;
    }
    if (!(format == pixel_format)) {
        srColorSurface converted(format, width, height);
        converted.setPalette(palette);
        if (preserve != 0) {
            converted.copy(*this);
        }
        adopt(converted.storage.release());
        pixel_format = converted.pixel_format;
        clamp_modes = converted.clamp_modes;
        filter = converted.filter;
        pixel_write = converted.pixel_write;
        pixel_read = converted.pixel_read;
    }
    return 1;
}

/* Indexed pixels copy verbatim only between equal palettes. */
static bool samePalette(srPalette* first, srPalette* second)
{
    return first == second ||
           (first && second && first->getPaletteSize() == second->getPaletteSize() &&
            first->matchPalette(second->getPaletteDataPtr(), second->getPaletteSize()));
}

// FUNCTION: SURRENDER 0x1005C4D0
int srColorSurface::isCompatible(srColorSurfaceIFace& source)
{
    if (source.getClassID() != getClassID()) {
        return 0;
    }
    if (pixel_format == source.pixel_format) {
        if (pixel_format.color_model == srPixelConvert::COLOR_INDEXED &&
            !samePalette(getPalette().get(), source.getPalette().get())) {
            return 0;
        }
        return source.getDataPtr() != 0;
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1005C460
w8_ulong srColorSurface::getPixelRaw(w8_long x, w8_long y)
{
    unsigned char* address = getAddress(x, y);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        return *address;
    case srPixelConvert::PIXEL_SIZE_16:
        return *(unsigned short*)address;
    case srPixelConvert::PIXEL_SIZE_24:
        return address[0] | (address[1] << 8) | (address[2] << 0x10);
    case srPixelConvert::PIXEL_SIZE_32:
        return *(w8_ulong*)address;
    default:
        return 0;
    }
}

// FUNCTION: SURRENDER 0x1005C030
void srColorSurface::setPixelRaw(w8_long x, w8_long y, w8_ulong pixel)
{
    unsigned char* address = getAddress(x, y);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8:
        *address = (unsigned char)pixel;
        break;
        break;
    case srPixelConvert::PIXEL_SIZE_16:
        *(unsigned short*)address = (unsigned short)pixel;
        break;
        break;
    case srPixelConvert::PIXEL_SIZE_24:
        address[0] = (unsigned char)pixel;
        address[1] = (unsigned char)(pixel >> 8);
        address[2] = (unsigned char)(pixel >> 0x10);
        break;
        break;
    case srPixelConvert::PIXEL_SIZE_32:
        *(w8_ulong*)address = pixel;
        break;
        break;
    }
}

// FUNCTION: SURRENDER 0x1005D940
void srColorSurface::getPixelRow(w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end)
{
    if (x_start < x_end) {
        convertToARGB8888(pixels, getAddress(x_start, y), x_end - x_start);
    }
}

// FUNCTION: SURRENDER 0x1005DAA0
void srColorSurface::setPixelRow(const w8_ulong* pixels, w8_long y, w8_long x_start, w8_long x_end)
{
    if (x_start < x_end) {
        convertFromARGB8888(getAddress(x_start, y), pixels, x_end - x_start);
    }
}

// FUNCTION: SURRENDER 0x1005BF70
void srColorSurface::getPixelRowRaw(void* pixels, w8_long y, w8_long x_start, w8_long x_end)
{
    if (y >= 0 && y < height && x_start >= 0 && x_start < x_end && x_end <= width) {
        w8_long count = (x_end - x_start) * (pixel_format.pixel_size + 1);
        unsigned char* address = getAddress(x_start, y);
        if (count != 0 && pixels != address) {
            std::memcpy(pixels, address, count);
        }
    }
}

// FUNCTION: SURRENDER 0x1005BFD0
void srColorSurface::setPixelRowRaw(const void* pixels, w8_long y, w8_long x_start, w8_long x_end)
{
    if (y >= 0 && y < height && x_start >= 0 && x_start < x_end && x_end <= width) {
        w8_long count = (x_end - x_start) * (pixel_format.pixel_size + 1);
        unsigned char* address = getAddress(x_start, y);
        if (count != 0 && address != pixels) {
            std::memcpy(address, pixels, count);
        }
    }
}

// FUNCTION: SURRENDER 0x1005D970
void srColorSurface::getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end)
{
    unsigned char buffer[0x400];
    for (; y_start < y_end; y_start += 0x100) {
        w8_ulong count = y_end - y_start;
        if (count > 0x100) {
            count = 0x100;
        }
        unsigned char* address = getAddress(x, y_start);
        const unsigned bytes = pixel_format.pixel_size + 1;
        for (w8_ulong i = 0; i < count; ++i, address += pitch)
            std::memcpy(buffer + i * bytes, address, bytes);
        convertToARGB8888(pixels, (const void*)buffer, count);
        pixels += 0x100;
    }
}

// FUNCTION: SURRENDER 0x1005DAD0
void srColorSurface::setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end)
{
    unsigned char buffer[0x400];
    for (; y_start < y_end; y_start += 0x100) {
        w8_ulong count = y_end - y_start;
        if (count > 0x100) {
            count = 0x100;
        }
        convertFromARGB8888(buffer, pixels, count);
        unsigned char* address = getAddress(x, y_start);
        const unsigned bytes = pixel_format.pixel_size + 1;
        for (w8_ulong i = 0; i < count; ++i, address += pitch)
            std::memcpy(address, buffer + i * bytes, bytes);
        pixels += 0x100;
    }
}

// FUNCTION: SURRENDER 0x1005D7A0
void srColorSurface::getPixels(w8_ulong* pixels, const srVector2i* positions, w8_long count)
{
    unsigned char buffer[0x400];
    for (w8_long i = 0; i < count; i += 0x100) {
        w8_ulong chunk = count - i;
        if (chunk > 0x100) {
            chunk = 0x100;
        }
        getPixelsRaw(buffer, positions, chunk);
        convertToARGB8888(pixels, buffer, chunk);
        positions += 0x100;
        pixels += 0x100;
    }
}

// FUNCTION: SURRENDER 0x1005D5F0
void srColorSurface::setPixels(const w8_ulong* pixels, const srVector2i* positions, w8_long count)
{
    unsigned char buffer[0x400];
    for (w8_long i = 0; i < count; i += 0x100) {
        w8_ulong chunk = count - i;
        if (chunk > 0x100) {
            chunk = 0x100;
        }
        convertFromARGB8888(buffer, pixels, chunk);
        setPixelsRaw(buffer, positions, chunk);
        positions += 0x100;
        pixels += 0x100;
    }
}

// FUNCTION: SURRENDER 0x1005D830
void srColorSurface::getPixelsRaw(void* pixels, const srVector2i* positions, w8_long count)
{
    auto* data = static_cast<unsigned char*>(pixels);
    const unsigned bytes = pixel_format.pixel_size + 1;
    for (w8_long i = 0; i < count; ++i)
        std::memcpy(data + i * bytes, getAddress(positions[i].x, positions[i].y), bytes);

}

// FUNCTION: SURRENDER 0x1005D680
void srColorSurface::setPixelsRaw(const void* pixels, const srVector2i* positions, w8_long count)
{
    const auto* data = static_cast<const unsigned char*>(pixels);
    const unsigned bytes = pixel_format.pixel_size + 1;
    for (w8_long i = 0; i < count; ++i)
        std::memcpy(getAddress(positions[i].x, positions[i].y), data + i * bytes, bytes);

}

// FUNCTION: SURRENDER 0x1005C0A0
void srColorSurface::swapPixelRows(w8_long x0, w8_long y0, w8_long x1, w8_long y1, w8_long count)
{
    if (y0 >= 0 && y0 < height && y1 >= 0 && y1 < height && count > 0) {
        if (x0 < 0) {
            x1 -= x0;
            count += x0;
            x0 = 0;
        }
        if (x1 < 0) {
            x0 -= x1;
            count += x1;
            x1 = 0;
        }
        if (x0 < width && x1 < width) {
            if (x0 + count > width) {
                count = width - x0;
            }
            if (x1 + count > width) {
                count = width - x1;
            }
            if (count > 0) {
                unsigned char* first = getAddress(x0, y0);
                unsigned char* second = getAddress(x1, y1);
                w8_long bytes = (pixel_format.pixel_size + 1) * count;
                for (w8_long i = 0; i < bytes; ++i) {
                    std::swap(first[i], second[i]);
                }
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1005C2E0
void srColorSurface::flipRectangle(const Rectangle& rectangle)
{
    const SDL_Rect region{std::min(rectangle.left, rectangle.right),
                          std::min(rectangle.top, rectangle.bottom),
                          std::abs(rectangle.right - rectangle.left),
                          std::abs(rectangle.bottom - rectangle.top)};
    if (SDL_RectEmpty(&region) || region.x < 0 || region.y < 0 ||
        region.x + region.w > width || region.y + region.h > height)
        return;
    const auto flip = SDL_FlipMode((rectangle.right < rectangle.left ? SDL_FLIP_HORIZONTAL : 0) |
                                  (rectangle.bottom < rectangle.top ? SDL_FLIP_VERTICAL : 0));
    if (flip != SDL_FLIP_NONE) {
        auto surface = ShareCpuSurface(*storage);
        FlipCpuSurface(surface, flip, &region);
    }

}

// FUNCTION: SURRENDER 0x1005C560
void srColorSurface::setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel)
{
    if (getDataPtr() == nullptr) {
        srColorSurfaceIFace::setHLine(y, x_start, x_end, pixel);
        return;
    }
    if (y >= 0 && y < height) {
        w8_long x_hi = x_end;
        if (x_end < x_start) {
            x_hi = x_start;
            x_start = x_end;
        }
        if (x_start < 0) {
            x_start = 0;
        }
        if (x_hi > width) {
            x_hi = width;
        }
        if (x_start < x_hi) {
            setPixel(x_start, y, pixel);
            w8_ulong raw = getPixelRaw(x_start, y);
            const SDL_Rect rect{x_start, y, x_hi - x_start, 1};
            auto destination = ShareCpuSurface(*storage);
            FillCpuSurface(destination, raw, &rect);
        }
    }
}

// FUNCTION: SURRENDER 0x1005C740
void srColorSurface::setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel)
{
    if (getDataPtr() == nullptr) {
        srColorSurfaceIFace::setVLine(x, y_start, y_end, pixel);
        return;
    }
    if (x >= 0 && x < width) {
        w8_long y_hi = y_end;
        if (y_end < y_start) {
            y_hi = y_start;
            y_start = y_end;
        }
        if (y_start < 0) {
            y_start = 0;
        }
        if (y_hi > height) {
            y_hi = height;
        }
        if (y_start < y_hi) {
            setPixel(x, y_start, pixel);
            w8_ulong raw = getPixelRaw(x, y_start);
            const SDL_Rect rect{x, y_start, 1, y_hi - y_start};
            auto destination = ShareCpuSurface(*storage);
            FillCpuSurface(destination, raw, &rect);
        }
    }
}

// FUNCTION: SURRENDER 0x1005C900
void srColorSurface::fill(w8_ulong pixel)
{
    if (getDataPtr() == 0) {
        srColorSurfaceIFace::fill(pixel);
        return;
    }
    setPixel(0, 0, pixel);
    w8_ulong raw = getPixelRaw(0, 0);
    auto destination = ShareCpuSurface(*storage);
    FillCpuSurface(destination, raw);
}

// FUNCTION: SURRENDER 0x1005D220
void srColorSurface::blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x, w8_long source_y,
                          w8_long x_end, w8_long y_end)
{
    if (&source != this && isCompatible(source) == 0) {
        srColorSurfaceIFace::blit(x, y, source, source_x, source_y, x_end, y_end);
        return;
    }
    if (x < width && y < height) {
        if (x < 0) {
            source_x -= x;
            x = 0;
        }
        if (y < 0) {
            source_y -= y;
            y = 0;
        }
        if (source_x < 0) {
            x -= source_x;
            source_x = 0;
        }
        if (source_y < 0) {
            y -= source_y;
            source_y = 0;
        }
        if (x_end > source.width) {
            x_end = source.width;
        }
        if (y_end > source.height) {
            y_end = source.height;
        }
        if (source_x < source.width && source_x < x_end && source_y < source.height &&
            source_y < y_end) {
            if (width < (x - source_x) + x_end) {
                x_end = width - x + source_x;
            }
            if (height < (y - source_y) + y_end) {
                y_end = height - y + source_y;
            }
            if (source_x < x_end && source_y < y_end) {
                auto destination = ShareCpuSurface(*storage);
                auto input = ShareCpuSurface(*static_cast<srColorSurface&>(source).storage);
                const SDL_Rect dest{x, y, x_end - source_x, y_end - source_y};
                const SDL_Rect sample{source_x, source_y, dest.w, dest.h};
                BlitCpuSurface(destination, &dest, input, &sample);
            }
        }
    }
}

// FUNCTION: SURRENDER 0x1005D010
void srColorSurface::copyNoScaling(srColorSurfaceIFace& source)
{
    if (this == &source) {
        return;
    }
    if (isCompatible(source) == 0) {
        if (source.getClassID() != getClassID()) {
            srColorSurfaceIFace::copyNoScaling(source);
            return;
        }
        if (pixel_format.color_model == srPixelConvert::COLOR_RGB &&
            pixel_format.pixel_size == srPixelConvert::PIXEL_SIZE_32 &&
            pixel_format.red_bits == 8 && pixel_format.green_bits == 8 &&
            pixel_format.blue_bits == 8 && pixel_format.alpha_bits == 8 &&
            pixel_format.red_shift == 0x10 && pixel_format.green_shift == 8 &&
            pixel_format.blue_shift == 0 && pixel_format.alpha_shift == 0x18) {
            unsigned char* dest = (unsigned char*)getDataPtr();
            for (w8_long row = 0; row < height; ++row) {
                source.getPixelRow((w8_ulong*)dest, row, 0, width);
                dest += pitch;
            }
            return;
        }
        const srPixelConvert::PixelFormat& source_format = source.pixel_format;
        if (source_format.color_model != 0 || source_format.pixel_size != 3 ||
            source_format.red_bits != 8 || source_format.green_bits != 8 ||
            source_format.blue_bits != 8 || source_format.alpha_bits != 8 ||
            source_format.red_shift != 0x10 || source_format.green_shift != 8 ||
            source_format.blue_shift != 0 || source_format.alpha_shift != 0x18 ||
            source.getDataPtr() == 0) {
            srColorSurfaceIFace::copyNoScaling(source);
            return;
        }
        unsigned char* src = (unsigned char*)source.getDataPtr();
        for (w8_long row = 0; row < height; ++row) {
            setPixelRow((const w8_ulong*)src, row, 0, width);
            src += source.pitch;
        }
        return;
    }
    blit(0, 0, source, 0, 0, width, height);
}

// FUNCTION: SURRENDER 0x1005CC90
void srColorSurface::scaleFast(srColorSurfaceIFace& source)
{
    if (isCompatible(source) == 0) {
        srColorSurfaceIFace::scaleFast(source);
        return;
    }
    if (width <= 0 || height <= 0 || source.width <= 0 || source.height <= 0)
        return;
    auto destination = ShareCpuSurface(*storage);
    auto input = ShareCpuSurface(*static_cast<srColorSurface&>(source).storage);
    BlitCpuSurface(destination, nullptr, input);
}

// SDL does not implement SurRender's filter kernels, wrapping, or asymmetric
// sample centers. Keep one separable resampler for both axes.
void srColorSurfaceIFace::scaleAxis(srColorSurfaceIFace& source, bool vertical)
{
    const w8_long size = vertical ? height : width;
    const w8_long sourceSize = vertical ? source.height : source.width;
    const w8_long lines = vertical ? width : source.height;
    if (size == sourceSize) {
        copyNoScaling(source);
        return;
    }
    if (size <= 0 || sourceSize <= 0)
        return;
    struct Sample { w8_long index; float weight; };
    std::vector<std::vector<Sample>> contributions(size);
    const double scale = double(size) / sourceSize;
    const bool shrinking = scale < 1.0;
    const double inverse = 1.0 / scale;
    const double support = source.filter->getSupport() / (shrinking ? scale : 1.0);
    for (w8_long position = 0; position < size; ++position) {
        const double center = position / scale + (vertical || shrinking ? 0.5 : -0.5);
        double total = 0.0;
        auto& samples = contributions[position];
        const w8_long first = w8_long(std::ceil(center - support));
        const w8_long last = w8_long(std::floor(center + support));
        for (w8_long i = first; i <= last; ++i) {
            const double weight = shrinking ?
                source.filter->getWeight((center - i) / inverse) / inverse :
                source.filter->getWeight(center - i);
            if (weight > 0.0) {
                samples.push_back({vertical ? source.getClampedY(i) : source.getClampedX(i),
                                   static_cast<float>(weight)});
                total = weight + total;
            }
        }
        const float normalization = static_cast<float>(1.0 / total);
        for (auto& sample : samples)
            sample.weight *= normalization;
    }
    std::vector<w8_ulong> input(sourceSize), output(size);
    std::vector<glm::vec4> channels(sourceSize);
    for (w8_long line = 0; line < lines; ++line) {
        if (vertical) source.getPixelColumn(input.data(), line, 0, sourceSize);
        else source.getPixelRow(input.data(), line, 0, sourceSize);
        for (w8_long i = 0; i < sourceSize; ++i) {
            const auto pixel = input[i];
            channels[i] = glm::vec4(pixel >> 24, (pixel >> 16) & 255,
                                    (pixel >> 8) & 255, pixel & 255);
        }
        for (w8_long i = 0; i < size; ++i) {
            glm::vec4 value(0.0f);
            for (const auto& sample : contributions[i]) {
                // Round the product to float before accumulation.
                const glm::vec4 weighted = channels[sample.index] * sample.weight;
                value = weighted + value;
            }
            const auto channel = [](float value) {
                return w8_ulong(static_cast<unsigned char>(srFloatToInt(value)));
            };
            output[i] = channel(value.x) << 24 | channel(value.y) << 16 |
                        channel(value.z) << 8 | channel(value.w);
        }
        if (vertical) setPixelColumn(output.data(), line, 0, size);
        else setPixelRow(output.data(), line, 0, size);
    }
}

// FUNCTION: SURRENDER 0x10058150
void srColorSurfaceIFace::blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                               w8_long source_y, w8_long source_right, w8_long source_bottom)
{
    w8_long width = this->width;
    w8_long source_width = source.width;
    w8_long source_height = source.height;
    w8_long height = this->height;
    if ((x < width) && (y < height)) {
        if (x < 0) {
            source_x = source_x - x;
            x = 0;
        }
        if (y < 0) {
            source_y = source_y - y;
            y = 0;
        }
        if (source_x < 0) {
            x = x - source_x;
            source_x = 0;
        }
        if (source_y < 0) {
            y = y - source_y;
            source_y = 0;
        }
        w8_long right = source_right;
        if (source_width < source_right) {
            right = source_width;
        }
        w8_long bottom = source_bottom;
        if (source_height < source_bottom) {
            bottom = source_height;
        }
        if (((source_x < source_width) && (source_x < right)) &&
            ((source_y < source_height) && (source_y < bottom))) {
            w8_long dest_span = x - source_x;
            if (width < dest_span + right) {
                right = (width - x) + source_x;
            }
            if (height < (y - source_y) + bottom) {
                bottom = (height - y) + source_y;
            }
            if ((source_x < right) && (source_y < bottom)) {
                if (((&source == this) && (x < right) && (y < bottom)) &&
                    (source_x < dest_span + right) &&
                    ((source_y < (y - source_y) + bottom) && (source_y <= y))) {
                    w8_long span = right - source_x;
                    std::vector<srARGB> temp_storage((bottom - source_y) * span);
                    srARGB* temp_colors = temp_storage.data();
                    w8_ulong* temp = (w8_ulong*)temp_colors;
                    if (source_y < bottom) {
                        w8_long row;
                        for (row = source_y; row < bottom; row++) {
                            source.getPixelRow(temp + (row - source_y) * span, row, source_x,
                                               right);
                        }
                        for (row = 0; row < bottom - source_y; row++) {
                            setPixelRow(temp + row * span, y, x, dest_span + right);
                            y = y + 1;
                        }
                    }
                    return;
                }
                std::vector<srARGB> temp_storage(right - source_x);
                srARGB* temp_colors = temp_storage.data();
                w8_ulong* temp = (w8_ulong*)temp_colors;
                for (; source_y < bottom; source_y++) {
                    source.getPixelRow(temp, source_y, source_x, right);
                    setPixelRow(temp, y, x, dest_span + right);
                    y = y + 1;
                }
            }
        }
    }
}


// FUNCTION: SURRENDER 0x1005A930
void srColorSurfaceIFace::minify(srColorSurfaceIFace& source)
{
    w8_long height = this->height;
    w8_ulong width = this->width;
    w8_long source_width = source.width;
    if (((width == (w8_ulong)(source_width / 2)) && (height == source.height / 2)) &&
        (this != &source)) {
        std::vector<srARGB> buffer_storage(width + source_width * 2);
        srARGB* buffer_colors = buffer_storage.data();
        w8_ulong* buffer = (w8_ulong*)buffer_colors;
        w8_ulong* second = buffer + source_width;
        w8_ulong* row = buffer + source_width * 2;
        for (w8_long y = 0; y < height; y++) {
            source.getPixelRow(buffer, y * 2, 0, source_width);
            source.getPixelRow(second, y * 2 + 1, 0, source_width);
            for (w8_ulong x = 0; x < width; x++) {
                unsigned char* top = (unsigned char*)&buffer[x * 2];
                unsigned char* bottom = (unsigned char*)&second[x * 2];
                unsigned char* pixel = (unsigned char*)&row[x];
                pixel[0] = (unsigned char)((top[0] + top[4] + bottom[0] + bottom[4] + 3) >> 2);
                pixel[1] = (unsigned char)((top[1] + top[5] + bottom[1] + bottom[5] + 3) >> 2);
                pixel[2] = (unsigned char)((top[2] + top[6] + bottom[2] + bottom[6] + 3) >> 2);
                pixel[3] = (unsigned char)((top[3] + top[7] + bottom[3] + bottom[7] + 3) >> 2);
            }
            setPixelRow(row, y, 0, width);
        }
    }
}

// FUNCTION: SURRENDER 0x1005ABB0
void srColorSurfaceIFace::magnify(srColorSurfaceIFace& source)
{
    w8_long source_height = source.height;
    w8_long width = this->width;
    w8_long source_width = source.width;
    if (width == source_width * 2 && height == source_height * 2 && this != &source) {
        std::vector<srARGB> buffer_storage(source_width + width * 2);
        srARGB* buffer_colors = buffer_storage.data();
        w8_ulong* buffer = (w8_ulong*)buffer_colors;
        w8_ulong* even = buffer + source_width;
        w8_ulong* odd = even + width;
        source.getPixelRow(buffer, 0, 0, source_width);
        w8_long x;
        for (x = 0; x < source_width - 1; x++) {
            even[x * 2] = buffer[x];
            even[x * 2 + 1] = (buffer[x + 1] >> 1 & 0x7f7f7f7f) + (buffer[x] >> 1 & 0x7f7f7f7f);
        }
        even[(source_width - 1) * 2] = buffer[source_width - 1];
        even[(source_width - 1) * 2 + 1] = buffer[source_width - 1];
        /* Each pass writes the widened row y and the average of rows y and y + 1; the widened last
           source row is never written, so the bottom two destination rows keep their contents. */
        for (w8_long y = 0; y < source_height - 1; y++) {
            source.getPixelRow(buffer, y + 1, 0, source_width);
            for (x = 0; x < source_width - 1; x++) {
                odd[x * 2] = buffer[x];
                odd[x * 2 + 1] = (buffer[x + 1] >> 1 & 0x7f7f7f7f) + (buffer[x] >> 1 & 0x7f7f7f7f);
            }
            odd[(source_width - 1) * 2] = buffer[source_width - 1];
            odd[(source_width - 1) * 2 + 1] = buffer[source_width - 1];
            setPixelRow(even, y * 2, 0, width);
            for (x = 0; x < width; x++) {
                even[x] = (odd[x] >> 1 & 0x7f7f7f7f) + (even[x] >> 1 & 0x7f7f7f7f);
            }
            setPixelRow(even, y * 2 + 1, 0, width);
            w8_ulong* swap = even;
            even = odd;
            odd = swap;
        }
    }
}

// FUNCTION: SURRENDER 0x1005DBE0
void srColorSurface::dump(std::ostream& stream)
{
    srColorSurfaceIFace::dump(stream);
    std::ios::fmtflags flags = stream.flags();
    stream.setf(std::ios::left, std::ios::adjustfield);
    if (getPalette() != 0) {
        stream.width(0x20);
        stream << "  Palette: " << getPalette()->getName() << '\n';
    }
    stream.flags(static_cast<std::ios::fmtflags>(flags & 0x7fff));
}
