#include "surrender/srMath.h"
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <utility>

#include "surrender/srColorSurface.h"

#include <math.h>
#include <ostream>

#include "surrender/srCore.h"
#include "surrender/srFilter.h"
#include "surrender/srPalette.h"

/* srColorSurface surface-flag names; never assigned, so dump reports numeric bit indices. */
// GLOBAL: SURRENDER 0x100A4A10
static const char* s_flag_names2;

// The byte-depth-preserving SDL format for viewing a raw surface buffer; the
// channel layout is irrelevant for copies and raw-value fills, but BGR24
// keeps the little-endian byte order of the packed pixel value.
static SDL_PixelFormat viewFormat(srPixelConvert::e_pixelSize size)
{
    switch (size) {
    case srPixelConvert::PIXEL_SIZE_8:
        return SDL_PIXELFORMAT_INDEX8;
    case srPixelConvert::PIXEL_SIZE_16:
        return SDL_PIXELFORMAT_RGB565;
    case srPixelConvert::PIXEL_SIZE_24:
        return SDL_PIXELFORMAT_BGR24;
    default:
        return SDL_PIXELFORMAT_ARGB8888;
    }
}

using SurfaceView = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

/* Comma-separated bit-name walker shared by the sr dumps; each TU keeps its own
   copy. */
static void dumpFlags(std::ostream& stream, w8_ulong flags, const char* names)
{
    if (flags == 0) {
        stream << "[NONE]";
        return;
    }
    stream << '[';
    bool first = true;
    for (w8_ulong bit = 0; bit < 0x20; ++bit) {
        if ((flags & (1 << bit)) != 0) {
            if (first) {
                first = false;
            } else {
                stream << ',';
            }
            if (names == 0 || *names == 0) {
                stream << bit;
            } else {
                while (*names != 0 && *names != ',') {
                    stream << *names++;
                }
                if (*names == ',') {
                    ++names;
                }
            }
        } else if (names != 0) {
            while (*names != 0 && *names != ',') {
                ++names;
            }
            if (*names == ',') {
                ++names;
            }
        }
    }
    stream << ']';
}

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
    : srClassSupport<srColorSurfaceIFace, srClass, true, 0x3100>()
{
    *this = other;
}

// FUNCTION: SURRENDER 0x1005B280
const char* srColorSurfaceIFace::sGetClassName()
{
    return "srColorSurfaceIFace";
}

// FUNCTION: SURRENDER 0x10021310
srPalette* srColorSurfaceIFace::getPalette()
{
    return 0;
}

// FUNCTION: SURRENDER 0x10021320
void srColorSurfaceIFace::setPalette(srPalette* palette) {}

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

// FUNCTION: SURRENDER 0x10057B80
void srColorSurfaceIFace::addNoise(double amplitude, int monochrome)
{
    int magnitude = (int)fabs(amplitude * 255.0);
    if (magnitude != 0) {
        w8_long height = this->height;
        w8_long width = this->width;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        unsigned char* row = (unsigned char*)row_colors;
        for (w8_long y = 0; y < height; ++y) {
            getPixelRow((w8_ulong*)row, y, 0, width);
            if (monochrome == 0) {
                for (w8_long x = 0; x < width; ++x) {
                    unsigned char* pixel = row + x * 4 + 3;
                    for (int channel = 0; channel < 4; ++channel) {
                        int value = *pixel + (rand() % (magnitude * 2) - magnitude);
                        if (value < 0) {
                            value = 0;
                        } else if (0xff < value) {
                            value = 0xff;
                        }
                        *pixel = (unsigned char)value;
                        --pixel;
                    }
                }
            } else {
                for (w8_long x = 0; x < width; ++x) {
                    int delta = rand() % (magnitude * 2) - magnitude;
                    unsigned char* pixel = row + x * 4 + 3;
                    for (int channel = 0; channel < 4; ++channel) {
                        int value = *pixel + delta;
                        if (value < 0) {
                            value = 0;
                        } else if (0xff < value) {
                            value = 0xff;
                        }
                        *pixel = (unsigned char)value;
                        --pixel;
                    }
                }
            }
            setPixelRow((const w8_ulong*)row, y, 0, width);
        }
    }
}

// FUNCTION: SURRENDER 0x10057EB0
void srColorSurfaceIFace::adjustSaturation(double saturation)
{
    if (saturation != 1.0) {
        w8_long height = this->height;
        w8_long width = this->width;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        unsigned char* row = (unsigned char*)row_colors;
        for (w8_long y = 0; y < height; ++y) {
            getPixelRow((w8_ulong*)row, y, 0, width);
            unsigned char* pixel = row + 2;
            for (w8_long x = 0; x < width; ++x) {
                float red = pixel[0] * 0.003921569f;
                float green = pixel[-1] * 0.003921569f;
                float blue = pixel[-2] * 0.003921569f;
                float alpha = pixel[1] * 0.003921569f;
                float luminance = red * 0.2125f + green * 0.7154f + blue * 0.0721f;
                double channel = alpha * 255.0;
                if (0.0 < channel) {
                    if (255.0 <= channel) {
                        channel = 255.0;
                    }
                } else {
                    channel = 0.0;
                }
                double value = ((red - luminance) * saturation + luminance) * 255.0;
                pixel[1] = (unsigned char)srFloatToInt(channel);
                if (0.0 < value) {
                    if (255.0 <= value) {
                        value = 255.0;
                    }
                } else {
                    value = 0.0;
                }
                channel = ((green - luminance) * saturation + luminance) * 255.0;
                pixel[0] = (unsigned char)srFloatToInt(value);
                if (0.0 < channel) {
                    if (255.0 <= channel) {
                        channel = 255.0;
                    }
                } else {
                    channel = 0.0;
                }
                value = ((blue - luminance) * saturation + luminance) * 255.0;
                pixel[-1] = (unsigned char)srFloatToInt(channel);
                if (0.0 < value) {
                    if (255.0 <= value) {
                        value = 255.0;
                    }
                } else {
                    value = 0.0;
                }
                pixel[-2] = (unsigned char)srFloatToInt(value);
                pixel += 4;
            }
            setPixelRow((const w8_ulong*)row, y, 0, width);
        }
    }
}

// FUNCTION: SURRENDER 0x10057D10
void srColorSurfaceIFace::adjust(const srVector4T<float>& scale, const srVector4T<float>& offset,
                                 const srVector4T<float>& gamma)
{
    w8_long height = this->height;
    w8_long width = this->width;
    const float* scale_v = &scale.x;
    const float* offset_v = &offset.x;
    const float* gamma_v = &gamma.x;
    unsigned char lut[4][256];
    for (int channel = 0; channel < 4; ++channel) {
        for (int i = 0; i < 0x100; ++i) {
            double value = scale_v[channel] * (offset_v[channel] * (i - 128.0) + 128.0) +
                           gamma_v[channel] * 255.0 + 0.5;
            if (value <= 0.0) {
                value = 0.0;
            } else if (value >= 255.0) {
                value = 255.0;
            }
            lut[channel][i] = (unsigned char)(int)value;
        }
    }
    std::vector<srARGB> row_storage(width);
    srARGB* row_colors = row_storage.data();
    unsigned char* row = (unsigned char*)row_colors;
    for (w8_long y = 0; y < height; ++y) {
        getPixelRow((w8_ulong*)row, y, 0, width);
        for (w8_long x = 0; x < width; ++x) {
            unsigned char* pixel = row + x * 4;
            pixel[2] = lut[0][pixel[2]];
            pixel[1] = lut[1][pixel[1]];
            pixel[0] = lut[2][pixel[0]];
            pixel[3] = lut[3][pixel[3]];
        }
        setPixelRow((const w8_ulong*)row, y, 0, width);
    }
}

// FUNCTION: SURRENDER 0x1005B040
void srColorSurfaceIFace::remapPixels(const srARGB& from, const srARGB& to)
{
    w8_long width = this->width;
    w8_long height = this->height;
    std::vector<srARGB> row_storage(width);
    srARGB* row_colors = row_storage.data();
    w8_ulong* row = (w8_ulong*)row_colors;
    for (w8_long y = 0; y < height; ++y) {
        getPixelRow(row, y, 0, width);
        w8_long replaced = 0;
        for (w8_long x = 0; x < width; ++x) {
            if (row[x] == *(const w8_ulong*)&to) { /* reinterpret-ok: packed ARGB dword */
                row[x] = *(const w8_ulong*)&from;  /* reinterpret-ok: packed ARGB dword */
                ++replaced;
            }
        }
        if (replaced != 0) {
            setPixelRow(row, y, 0, width);
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

// FUNCTION: SURRENDER 0x10059420
void srColorSurfaceIFace::flipColorChannels(srARGB::e_index first, srARGB::e_index second)
{
    if ((int)first >= 0 && (int)first < 4 && (int)second >= 0 && (int)second < 4 &&
        first != second) {
        w8_long height = this->height;
        w8_long width = this->width;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        unsigned char* row = (unsigned char*)row_colors;
        for (w8_long y = 0; y < height; ++y) {
            getPixelRow((w8_ulong*)row, y, 0, width);
            for (w8_long x = 0; x < width; ++x) {
                unsigned char* pixel = row + x * 4;
                unsigned char temp = pixel[3 - second];
                pixel[3 - second] = pixel[3 - first];
                pixel[3 - first] = temp;
            }
            setPixelRow((const w8_ulong*)row, y, 0, width);
        }
    }
}

// FUNCTION: SURRENDER 0x10059520
void srColorSurfaceIFace::copyColorChannel(srARGB::e_index destination, srARGB::e_index source)
{
    if ((int)destination >= 0 && (int)destination < 4 && (int)source >= 0 && (int)source < 4 &&
        destination != source) {
        w8_long height = this->height;
        w8_long width = this->width;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        unsigned char* row = (unsigned char*)row_colors;
        for (w8_long y = 0; y < height; ++y) {
            getPixelRow((w8_ulong*)row, y, 0, width);
            for (w8_long x = 0; x < width; ++x) {
                unsigned char* pixel = row + x * 4;
                pixel[3 - destination] = pixel[3 - source];
            }
            setPixelRow((const w8_ulong*)row, y, 0, width);
        }
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

// FUNCTION: SURRENDER 0x10059240
void srColorSurfaceIFace::getChannelStatistics(srStat& statistics, srARGB::e_index channel)
{
    w8_long height = this->height;
    w8_long width = this->width;
    if ((int)channel >= 0 && (int)channel < 4) {
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        unsigned char* row = (unsigned char*)row_colors;
        std::vector<int> histogram(0x100);
        int i;
        for (i = 0; i < 0x100; ++i) {
            histogram[i] = 0;
        }
        for (w8_long y = 0; y < height; ++y) {
            getPixelRow((w8_ulong*)row, y, 0, width);
            for (w8_long x = 0; x < width; ++x) {
                ++histogram[row[x * 4 + 3 - channel]];
            }
        }
        statistics.count = 0;
        statistics.mean = 0.0;
        statistics.deviation = 0.0;
        statistics.median = 0;
        statistics.min = 0x100;
        statistics.max = 0;
        for (i = 0; i < 0x100; ++i) {
            if (histogram[i] != 0) {
                if (i < statistics.min) {
                    statistics.min = i;
                }
                if (statistics.max < i) {
                    statistics.max = i;
                }
            }
        }
        for (i = 0; i < 0x100; ++i) {
            statistics.mean = (histogram[i] * i) + statistics.mean;
            statistics.count = statistics.count + histogram[i];
        }
        statistics.mean = statistics.mean / statistics.count;
        for (i = 0; i < 0x100; ++i) {
            double difference = i - statistics.mean;
            statistics.deviation = histogram[i] * difference * difference + statistics.deviation;
        }
        statistics.deviation = sqrt(statistics.deviation / statistics.count);
        w8_long running = 0;
        for (i = 0; i < 0x100; ++i) {
            running += histogram[i];
            statistics.median = i;
            if (statistics.count / 2 < running) {
                break;
            }
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
                scaleVertical(source);
                return;
            }
            if (source_height == height) {
                scaleHorizontal(source);
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
    scaled_iface.scaleHorizontal(source);
    scaleVertical(scaled);
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

// FUNCTION: SURRENDER 0x1005B790
srColorSurface::srColorSurface(const srPixelConvert::PixelFormat& format, w8_ulong arg_width,
                               w8_ulong arg_height)
{
    init(format, arg_width, arg_height, (format.pixel_size + 1) * arg_width);
    owned_data.resize(pitch * height);
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005B8A0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, w8_ulong arg_width,
                               w8_ulong arg_height)
{
    srPixelConvert::PixelFormat format;
    srPixelConvert::mapPixelFormat(type, format);
    init(format, arg_width, arg_height, (format.pixel_size + 1) * arg_width);
    owned_data.resize(pitch * height);
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

/* The data-taking variants borrow caller storage; only owned_data owns pixels. */
// FUNCTION: SURRENDER 0x1005B9C0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    srPixelConvert::PixelFormat format;
    srPixelConvert::mapPixelFormat(type, format);
    init(format, arg_width, arg_height, arg_pitch);
    surface_flags |= BORROWED_DATA;
    borrowed_data = static_cast<unsigned char*>(data);
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005D520
srColorSurface& srColorSurface::operator=(const srColorSurface& other)
{
    if (&other != this) {
        const unsigned char* source = (other.surface_flags & BORROWED_DATA)
                                          ? other.borrowed_data
                                          : other.owned_data.data();
        std::vector<unsigned char> pixels(other.pitch * other.height);
        if (source != nullptr && !pixels.empty()) {
            std::copy_n(source, pixels.size(), pixels.data());
        }
        srColorSurfaceIFace::operator=(other);
        palette = other.palette;
        surface_flags = other.surface_flags & ~BORROWED_DATA;
        pixel_write = other.pixel_write;
        pixel_read = other.pixel_read;
        owned_data = std::move(pixels);
        borrowed_data = nullptr;
    }
    return *this;
}

srColorSurface::srColorSurface(const srColorSurface& other)
    : srClassSupport<srColorSurface, srColorSurfaceIFace, 0, 0x3110>()
{
    *this = other;
}

// FUNCTION: SURRENDER 0x1005BAC0
srColorSurface::srColorSurface(const srPixelConvert::PixelFormat& format, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    init(format, arg_width, arg_height, arg_pitch);
    surface_flags |= BORROWED_DATA;
    borrowed_data = static_cast<unsigned char*>(data);
    srPixelConvert::selectFuncs(format, pixel_write, pixel_read);
}

// FUNCTION: SURRENDER 0x1005B550
void* srColorSurface::getDataPtr()
{
    if (surface_flags & BORROWED_DATA) {
        return borrowed_data;
    }
    return owned_data.empty() ? nullptr : owned_data.data();
}

// FUNCTION: SURRENDER 0x1005B560
w8_long srColorSurface::getDataSize()
{
    return pitch * height;
}

// FUNCTION: SURRENDER 0x1005B570
srPalette* srColorSurface::getPalette()
{
    return palette;
}

// FUNCTION: SURRENDER 0x1005B580
void srColorSurface::setPalette(srPalette* palette)
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
    info.palette = palette;
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
    info.palette = palette;
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

// FUNCTION: SURRENDER 0x1005DDB0
srClass* srColorSurface::vInstance()
{
    return new srColorSurface(pixel_format, 1, 1);
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
        if (!(surface_flags & BORROWED_DATA)) {
            w8_long new_pitch = (pixel_format.pixel_size + 1) * arg_width;
            std::vector<unsigned char> pixels(new_pitch * arg_height);
            SurfaceDesc desc;
            desc.width = arg_width;
            desc.height = arg_height;
            desc.pitch = new_pitch;
            desc.clamp_modes = clamp_modes;
            desc.filter = filter;
            desc.pixel_format = pixel_format;
            setSurfaceDesc(desc);
            owned_data = std::move(pixels);
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
        if (!(surface_flags & BORROWED_DATA)) {
            srColorSurface scaled(srPixelConvert::SURFACE_BGRA32, arg_width, arg_height);
            scaled.copySurfaceParameters(*this);
            scaled.copy(*this);
            srColorSurface resized(pixel_format, arg_width, arg_height);
            resized.setPalette(palette);
            resized.pixel_write = pixel_write;
            resized.pixel_read = pixel_read;
            resized.copy(scaled);
            owned_data = std::move(resized.owned_data);
            width = arg_width;
            height = arg_height;
            pitch = resized.pitch;
            return 1;
        }
    }
    return 0;
}

// FUNCTION: SURRENDER 0x1005BDE0
int srColorSurface::changePixelFormat(const srPixelConvert::PixelFormat& format, int preserve)
{
    if (surface_flags & BORROWED_DATA) {
        return 0;
    }
    if (!(format == pixel_format)) {
        srColorSurface converted(format, width, height);
        converted.setPalette(palette);
        if (preserve != 0) {
            converted.copy(*this);
        }
        owned_data = std::move(converted.owned_data);
        pitch = converted.pitch;
        pixel_format = converted.pixel_format;
        clamp_modes = converted.clamp_modes;
        filter = converted.filter;
        pixel_write = converted.pixel_write;
        pixel_read = converted.pixel_read;
    }
    return 1;
}

// FUNCTION: SURRENDER 0x1005C4D0
int srColorSurface::isCompatible(srColorSurfaceIFace& source)
{
    if (source.getClassID() != getClassID()) {
        return 0;
    }
    if (pixel_format == source.pixel_format) {
        if (pixel_format.color_model == srPixelConvert::COLOR_INDEXED &&
            source.getPalette() != getPalette()) {
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
        switch (pixel_format.pixel_size) {
        case srPixelConvert::PIXEL_SIZE_8: {
            for (w8_ulong i = 0; i < count; ++i) {
                buffer[i] = *address;
                address += pitch;
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_16: {
            if (count != 0) {
                unsigned char* out = buffer;
                w8_ulong i = count;
                do {
                    *(unsigned short*)out = *(unsigned short*)address;
                    out += 2;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_24: {
            if (count != 0) {
                unsigned char* out = buffer;
                w8_ulong i = count;
                do {
                    out[0] = address[0];
                    out[1] = address[1];
                    out[2] = address[2];
                    out += 3;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_32: {
            if (count != 0) {
                unsigned char* out = buffer;
                w8_ulong i = count;
                do {
                    *(w8_ulong*)out = *(w8_ulong*)address;
                    out += 4;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        }
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
        switch (pixel_format.pixel_size) {
        case srPixelConvert::PIXEL_SIZE_8: {
            for (w8_ulong i = 0; i < count; ++i) {
                *address = buffer[i];
                address += pitch;
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_16: {
            if (count != 0) {
                const unsigned char* in = buffer;
                w8_ulong i = count;
                do {
                    *(unsigned short*)address = *(const unsigned short*)in;
                    in += 2;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_24: {
            if (count != 0) {
                const unsigned char* in = buffer;
                w8_ulong i = count;
                do {
                    address[0] = in[0];
                    address[1] = in[1];
                    address[2] = in[2];
                    in += 3;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_32: {
            if (count != 0) {
                const unsigned char* in = buffer;
                w8_ulong i = count;
                do {
                    *(w8_ulong*)address = *(const w8_ulong*)in;
                    in += 4;
                    address += pitch;
                    --i;
                } while (i != 0);
            }
            break;
        }
        }
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
    unsigned char* out = (unsigned char*)pixels;
    w8_ulong pixel_count = static_cast<w8_ulong>(count);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, ++out) {
            *out = *getAddress(positions->x, positions->y);
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, out += 2) {
            *(unsigned short*)out = *(unsigned short*)getAddress(positions->x, positions->y);
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, out += 3) {
            const unsigned char* address = getAddress(positions->x, positions->y);
            out[0] = address[0];
            out[1] = address[1];
            out[2] = address[2];
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, out += 4) {
            *(w8_ulong*)out = *(w8_ulong*)getAddress(positions->x, positions->y);
        }
        break;
    }
    }
}

// FUNCTION: SURRENDER 0x1005D680
void srColorSurface::setPixelsRaw(const void* pixels, const srVector2i* positions, w8_long count)
{
    const unsigned char* in = (const unsigned char*)pixels;
    w8_ulong pixel_count = static_cast<w8_ulong>(count);
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, ++in) {
            *getAddress(positions->x, positions->y) = *in;
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, in += 2) {
            *(unsigned short*)getAddress(positions->x, positions->y) = *(const unsigned short*)in;
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_24: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, in += 3) {
            unsigned char* address = getAddress(positions->x, positions->y);
            address[0] = in[0];
            address[1] = in[1];
            address[2] = in[2];
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_32: {
        for (w8_ulong i = 0; i < pixel_count; ++i, ++positions, in += 4) {
            *(w8_ulong*)getAddress(positions->x, positions->y) = *(const w8_ulong*)in;
        }
        break;
    }
    }
}

// FUNCTION: SURRENDER 0x1005E230
static void reversePixelTriplets(unsigned char* pixels, w8_ulong count)
{
    w8_ulong half = count >> 1;
    w8_ulong i = 0;
    for (; i < (half & ~3UL); i += 4) {
        /* reinterpret-ok: each 24-bit pixel record swaps its low word plus
           high byte separately. */
        unsigned short w = *reinterpret_cast<unsigned short*>(pixels + i * 3);
        unsigned char b = pixels[i * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + i * 3) =
            *reinterpret_cast<unsigned short*>(pixels + (count - 1 - i) * 3);
        pixels[i * 3 + 2] = pixels[(count - 1 - i) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (count - 1 - i) * 3) = w;
        pixels[(count - 1 - i) * 3 + 2] = b;
        w = *reinterpret_cast<unsigned short*>(pixels + (i + 1) * 3);
        b = pixels[(i + 1) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (i + 1) * 3) =
            *reinterpret_cast<unsigned short*>(pixels + (count - 2 - i) * 3);
        pixels[(i + 1) * 3 + 2] = pixels[(count - 2 - i) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (count - 2 - i) * 3) = w;
        pixels[(count - 2 - i) * 3 + 2] = b;
        w = *reinterpret_cast<unsigned short*>(pixels + (i + 2) * 3);
        b = pixels[(i + 2) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (i + 2) * 3) =
            *reinterpret_cast<unsigned short*>(pixels + (count - 3 - i) * 3);
        pixels[(i + 2) * 3 + 2] = pixels[(count - 3 - i) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (count - 3 - i) * 3) = w;
        pixels[(count - 3 - i) * 3 + 2] = b;
        w = *reinterpret_cast<unsigned short*>(pixels + (i + 3) * 3);
        b = pixels[(i + 3) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (i + 3) * 3) =
            *reinterpret_cast<unsigned short*>(pixels + (count - 4 - i) * 3);
        pixels[(i + 3) * 3 + 2] = pixels[(count - 4 - i) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (count - 4 - i) * 3) = w;
        pixels[(count - 4 - i) * 3 + 2] = b;
    }
    for (; i < half; ++i) {
        unsigned short w = *reinterpret_cast<unsigned short*>(pixels + i * 3);
        unsigned char b = pixels[i * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + i * 3) =
            *reinterpret_cast<unsigned short*>(pixels + (count - 1 - i) * 3);
        pixels[i * 3 + 2] = pixels[(count - 1 - i) * 3 + 2];
        *reinterpret_cast<unsigned short*>(pixels + (count - 1 - i) * 3) = w;
        pixels[(count - 1 - i) * 3 + 2] = b;
    }
}

// FUNCTION: SURRENDER 0x1005C150
void srColorSurface::reversePixels(void* pixels, w8_ulong count)
{
    unsigned char* address = (unsigned char*)pixels;
    switch (pixel_format.pixel_size) {
    case srPixelConvert::PIXEL_SIZE_8: {
        w8_ulong half = count >> 1;
        w8_ulong i = 0;
        for (; i < (half & ~3UL); i += 4) {
            unsigned char t = address[i];
            address[i] = address[count - 1 - i];
            address[count - 1 - i] = t;
            t = address[i + 1];
            address[i + 1] = address[count - 2 - i];
            address[count - 2 - i] = t;
            t = address[i + 2];
            address[i + 2] = address[count - 3 - i];
            address[count - 3 - i] = t;
            t = address[i + 3];
            address[i + 3] = address[count - 4 - i];
            address[count - 4 - i] = t;
        }
        for (; i < half; ++i) {
            unsigned char t = address[i];
            address[i] = address[count - 1 - i];
            address[count - 1 - i] = t;
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_16: {
        w8_ulong half = count >> 1;
        unsigned short* row = (unsigned short*)address;
        w8_ulong i = 0;
        for (; i < (half & ~3UL); i += 4) {
            unsigned short t = row[i];
            row[i] = row[count - 1 - i];
            row[count - 1 - i] = t;
            t = row[i + 1];
            row[i + 1] = row[count - 2 - i];
            row[count - 2 - i] = t;
            t = row[i + 2];
            row[i + 2] = row[count - 3 - i];
            row[count - 3 - i] = t;
            t = row[i + 3];
            row[i + 3] = row[count - 4 - i];
            row[count - 4 - i] = t;
        }
        for (; i < half; ++i) {
            unsigned short t = row[i];
            row[i] = row[count - 1 - i];
            row[count - 1 - i] = t;
        }
        break;
    }
    case srPixelConvert::PIXEL_SIZE_24:
        reversePixelTriplets(address, count);
        break;
    case srPixelConvert::PIXEL_SIZE_32:
        if (count != 0) {
            w8_ulong* row = reinterpret_cast<w8_ulong*>(address);
            std::reverse(row, row + count);
        }
        break;
        break;
    }
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
        w8_ulong width = x_hi - x_lo;
        w8_ulong height = y_hi - y_lo;
        if (width != 0 && height != 0) {
            int bpp = pixel_format.pixel_size;
            w8_long middle = y_lo + (w8_long)height / 2;
            unsigned char* base = static_cast<unsigned char*>(getDataPtr()) + (bpp + 1) * x_lo;
            w8_ulong mirror = height;
            for (w8_long row = y_lo; row < middle; ++row) {
                --mirror;
                void* row_address = (void*)(pitch * row + base);
                void* mirror_address = (void*)(pitch * mirror + base);
                if (flip_y) {
                    unsigned char* first = static_cast<unsigned char*>(row_address);
                    unsigned char* second = static_cast<unsigned char*>(mirror_address);
                    if (first != second) {
                        std::swap_ranges(first, first + (bpp + 1) * width, second);
                    }
                }
                if (flip_x) {
                    reversePixels(row_address, width);
                    reversePixels(mirror_address, width);
                }
            }
            if (flip_x && (height & 1) != 0) {
                reversePixels((void*)(pitch * middle + base), width);
            }
        }
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
            const SurfaceView view{
                SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                      viewFormat(pixel_format.pixel_size), getDataPtr(),
                                      static_cast<int>(pitch)),
                SDL_DestroySurface};
            if (view) {
                const SDL_Rect run{static_cast<int>(x_start), static_cast<int>(y),
                                   static_cast<int>(x_hi - x_start), 1};
                SDL_FillSurfaceRect(view.get(), &run, raw);
            }
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
            const SurfaceView view{
                SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                      viewFormat(pixel_format.pixel_size), getDataPtr(),
                                      static_cast<int>(pitch)),
                SDL_DestroySurface};
            if (view) {
                const SDL_Rect run{static_cast<int>(x), static_cast<int>(y_start), 1,
                                   static_cast<int>(y_hi - y_start)};
                SDL_FillSurfaceRect(view.get(), &run, raw);
            }
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
    // A byte-depth-matched SDL view turns the raw-pattern fill into an
    // ordinary SDL_FillSurfaceRect; the color is the packed pixel value.
    const SurfaceView view{SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                                 viewFormat(pixel_format.pixel_size), getDataPtr(),
                                                 static_cast<int>(pitch)),
                           SDL_DestroySurface};
    if (view) {
        SDL_FillSurfaceRect(view.get(), nullptr, raw);
    }
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
                if (&source != this) {
                    // Ordinary compatible copy: the byte-depth views make it
                    // a plain SDL blit. Self-blits keep the retail overlap
                    // paths below.
                    const SurfaceView dest_view{
                        SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                                              viewFormat(pixel_format.pixel_size), getDataPtr(),
                                              static_cast<int>(pitch)),
                        SDL_DestroySurface};
                    const SurfaceView src_view{
                        SDL_CreateSurfaceFrom(static_cast<int>(source.width),
                                              static_cast<int>(source.height),
                                              viewFormat(pixel_format.pixel_size),
                                              source.getDataPtr(),
                                              static_cast<int>(source.pitch)),
                        SDL_DestroySurface};
                    if (dest_view && src_view) {
                        const SDL_Rect src_rect{static_cast<int>(source_x),
                                                static_cast<int>(source_y),
                                                static_cast<int>(x_end - source_x),
                                                static_cast<int>(y_end - source_y)};
                        SDL_Rect dst_rect{static_cast<int>(x), static_cast<int>(y),
                                          static_cast<int>(x_end - source_x),
                                          static_cast<int>(y_end - source_y)};
                        SDL_BlitSurface(src_view.get(), &src_rect, dest_view.get(), &dst_rect);
                    }
                    return;
                }
                w8_long dest_pitch = pitch;
                w8_long source_pitch = source.pitch;
                int bpp = pixel_format.pixel_size + 1;
                w8_ulong row_bytes = (x_end - source_x) * bpp;
                unsigned char* dest = (unsigned char*)getDataPtr() + dest_pitch * y + bpp * x;
                unsigned char* src =
                    (unsigned char*)source.getDataPtr() + source_pitch * source_y + bpp * source_x;
                if (source_y <= y) {
                    if (source_y != y || source_x != x) {
                        w8_long dest_right = (x - source_x) + x_end;
                        if (source_y == y && ((source_x <= x && x < x_end) ||
                                              (source_x <= dest_right && dest_right < x_end))) {
                            std::vector<unsigned char> pixels(row_bytes);
                            unsigned char* temp = pixels.data();
                            for (w8_long row = source_y; row < y_end; ++row) {
                                if (row_bytes != 0) {
                                    if (temp != src) {
                                        std::memcpy(temp, src, row_bytes);
                                    }
                                    if (dest != temp) {
                                        std::memcpy(dest, temp, row_bytes);
                                    }
                                }
                                dest += dest_pitch;
                                src += source_pitch;
                            }
                            return;
                        }
                        w8_long rows = y_end - source_y;
                        src += (rows - 1) * source_pitch;
                        dest += (rows - 1) * dest_pitch;
                        do {
                            if (row_bytes != 0 && dest != src) {
                                std::memcpy(dest, src, row_bytes);
                            }
                            dest -= dest_pitch;
                            src -= source_pitch;
                            --rows;
                        } while (rows != 0);
                        return;
                    }
                } else {
                    if (source_y < y_end) {
                        w8_long rows = y_end - source_y;
                        do {
                            if (row_bytes != 0 && dest != src) {
                                std::memcpy(dest, src, row_bytes);
                            }
                            dest += dest_pitch;
                            src += source_pitch;
                            --rows;
                        } while (rows != 0);
                    }
                }
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
    // Same pixel format and different storage: an ordinary SDL blit.
    const SurfaceView dest_view{
        SDL_CreateSurfaceFrom(static_cast<int>(width), static_cast<int>(height),
                              viewFormat(pixel_format.pixel_size), getDataPtr(),
                              static_cast<int>(pitch)),
        SDL_DestroySurface};
    const SurfaceView src_view{
        SDL_CreateSurfaceFrom(static_cast<int>(source.width), static_cast<int>(source.height),
                              viewFormat(pixel_format.pixel_size), source.getDataPtr(),
                              static_cast<int>(source.pitch)),
        SDL_DestroySurface};
    if (dest_view && src_view && getDataPtr() != source.getDataPtr()) {
        const SDL_Rect rect{0, 0, static_cast<int>(width), static_cast<int>(height)};
        SDL_Rect at{0, 0, static_cast<int>(width), static_cast<int>(height)};
        SDL_BlitSurface(src_view.get(), &rect, dest_view.get(), &at);
    }
}

// FUNCTION: SURRENDER 0x1005CC90
void srColorSurface::scaleFast(srColorSurfaceIFace& source)
{
    if (isCompatible(source) == 0) {
        srColorSurfaceIFace::scaleFast(source);
        return;
    }
    w8_long dest_width = width;
    w8_long dest_height = height;
    if (source.width == dest_width && source.height == dest_height) {
        copyNoScaling(source);
        return;
    }
    std::vector<w8_long> columns(dest_width);
    unsigned char* dest = (unsigned char*)getDataPtr();
    unsigned char* src = (unsigned char*)source.getDataPtr();
    w8_long source_pitch = source.pitch;
    srPixelConvert::e_pixelSize bpp = pixel_format.pixel_size;
    w8_long dest_pitch = pitch;
    double x_ratio = source.width / (double)dest_width;
    double y_ratio = source.height / (double)dest_height;
    for (w8_long i = 0; i < dest_width; ++i) {
        columns[i] = source.getClampedX((w8_long)(i * x_ratio));
    }
    for (w8_long row = 0; row < dest_height; ++row) {
        w8_long source_y = source.getClampedY((w8_long)(row * y_ratio));
        const unsigned char* source_row = src + source_y * source_pitch;
        switch (bpp) {
        case srPixelConvert::PIXEL_SIZE_8: {
            w8_long x = 0;
            for (; x < (dest_width & ~3L); x += 4) {
                dest[x] = source_row[columns[x]];
                dest[x + 1] = source_row[columns[x + 1]];
                dest[x + 2] = source_row[columns[x + 2]];
                dest[x + 3] = source_row[columns[x + 3]];
            }
            for (; x < dest_width; ++x) {
                dest[x] = source_row[columns[x]];
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_16: {
            unsigned short* out = (unsigned short*)dest;
            const unsigned short* in = (const unsigned short*)source_row;
            w8_long x = 0;
            for (; x < (dest_width & ~3L); x += 4) {
                out[x] = in[columns[x]];
                out[x + 1] = in[columns[x + 1]];
                out[x + 2] = in[columns[x + 2]];
                out[x + 3] = in[columns[x + 3]];
            }
            for (; x < dest_width; ++x) {
                out[x] = in[columns[x]];
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_24: {
            w8_long x = 0;
            for (; x < (dest_width & ~3L); x += 4) {
                const unsigned char* pixel = source_row + columns[x] * 3;
                /* reinterpret-ok: the 24-bit pixel record copies its low word
                   plus high byte separately. */
                *reinterpret_cast<unsigned short*>(dest + x * 3) =
                    *reinterpret_cast<const unsigned short*>(pixel);
                dest[x * 3 + 2] = pixel[2];
                pixel = source_row + columns[x + 1] * 3;
                *reinterpret_cast<unsigned short*>(dest + x * 3 + 3) =
                    *reinterpret_cast<const unsigned short*>(pixel);
                dest[x * 3 + 5] = pixel[2];
                pixel = source_row + columns[x + 2] * 3;
                *reinterpret_cast<unsigned short*>(dest + x * 3 + 6) =
                    *reinterpret_cast<const unsigned short*>(pixel);
                dest[x * 3 + 8] = pixel[2];
                pixel = source_row + columns[x + 3] * 3;
                *reinterpret_cast<unsigned short*>(dest + x * 3 + 9) =
                    *reinterpret_cast<const unsigned short*>(pixel);
                dest[x * 3 + 11] = pixel[2];
            }
            for (; x < dest_width; ++x) {
                const unsigned char* pixel = source_row + columns[x] * 3;
                *reinterpret_cast<unsigned short*>(dest + x * 3) =
                    *reinterpret_cast<const unsigned short*>(pixel);
                dest[x * 3 + 2] = pixel[2];
            }
            break;
        }
        case srPixelConvert::PIXEL_SIZE_32:
            for (w8_long x = 0; x < dest_width; ++x) {
                reinterpret_cast<w8_ulong*>(dest)[x] =
                    reinterpret_cast<const w8_ulong*>(source_row)[columns[x]];
            }
            break;
        }
        dest += dest_pitch;
    }
}

/* The horizontal and vertical filters use the same two records: a source index plus a float weight,
   and a count plus a pointer to those records. The names are descriptive. A zero total weight is
   not guarded. */
struct SampleWeight {
    w8_long index;
    float weight;
};

struct SampleContributions {
    w8_long count;
    SampleWeight* samples;

    void append(w8_long index, double weight)
    {
        SampleWeight& sample = samples[count++];
        sample.index = index;
        sample.weight = static_cast<float>(weight);
    }

    void normalize(double total)
    {
        float scale = static_cast<float>(1.0 / total);
        for (w8_long index = 0; index < count; ++index) {
            samples[index].weight = scale * samples[index].weight;
        }
    }
};

W8_ABI_ASSERT(sizeof(SampleWeight) == 8, "SampleWeight_must_be_8");
W8_ABI_ASSERT(sizeof(SampleContributions) == 8, "SampleContributions_must_be_8");

// FUNCTION: SURRENDER 0x10059AC0
void srColorSurfaceIFace::scaleHorizontal(srColorSurfaceIFace& source)
{
    w8_long height = source.height;
    w8_long width = this->width;
    w8_long source_width = source.width;
    if (width != source_width) {
        double support = source.filter->getSupport();
        double scale = (double)width / source_width;
        std::vector<SampleContributions> contributions(width);
        SampleContributions* counts = contributions.data();
        std::vector<srARGB> source_row_storage(source_width);
        srARGB* source_row_colors = source_row_storage.data();
        w8_ulong* source_row = (w8_ulong*)source_row_colors;
        std::vector<srARGB> row_storage(width);
        srARGB* row_colors = row_storage.data();
        w8_ulong* row = (w8_ulong*)row_colors;
        std::vector<srVector4T<float>> channel_vectors(source_width);
        std::vector<SampleWeight> sample_storage;
        SampleWeight* storage;
        if (1.0 <= scale) {
            w8_long entries = 1 - (w8_long)(support * -2.0);
            sample_storage.resize(entries * width);
            storage = sample_storage.data();
            for (w8_long x = 0; x < width; x++) {
                SampleContributions* entry = counts + x;
                entry->count = 0;
                entry->samples = storage + x * entries;
                double center = x / scale - 0.5;
                double total = 0.0;
                w8_long first = (w8_long)ceil(center - support);
                w8_long last = (w8_long)floor(center + support);
                for (; first <= last; first++) {
                    double weight = source.filter->getWeight(center - first);
                    if (0.0 < weight) {
                        w8_long index = source.getClampedX(first);
                        entry->append(index, weight);
                        total = weight + total;
                    }
                }
                entry->normalize(total);
            }
        } else {
            double scaled_support = support / scale;
            double inverse = 1.0 / scale;
            w8_long entries = 1 - (w8_long)(scaled_support * -2.0);
            sample_storage.resize(entries * width);
            storage = sample_storage.data();
            for (w8_long x = 0; x < width; x++) {
                SampleContributions* entry = counts + x;
                entry->count = 0;
                entry->samples = storage + x * entries;
                double center = x / scale + 0.5;
                double total = 0.0;
                w8_long first = (w8_long)ceil(center - scaled_support);
                w8_long last = (w8_long)floor(center + scaled_support);
                for (; first <= last; first++) {
                    double weight = source.filter->getWeight((center - first) / inverse) / inverse;
                    if (0.0 < weight) {
                        w8_long index = source.getClampedX(first);
                        entry->append(index, weight);
                        total = weight + total;
                    }
                }
                entry->normalize(total);
            }
        }
        for (w8_long y = 0; y < height; y++) {
            source.getPixelRow(source_row, y, 0, source_width);
            w8_long x;
            for (x = 0; x < source_width; x++) {
                channel_vectors[x].x = static_cast<float>(source_row_colors[x].alpha);
                channel_vectors[x].y = static_cast<float>(source_row_colors[x].red);
                channel_vectors[x].z = static_cast<float>(source_row_colors[x].green);
                channel_vectors[x].w = static_cast<float>(source_row_colors[x].blue);
            }
            for (x = 0; x < width; x++) {
                SampleContributions* entry = counts + x;
                w8_long count = entry->count;
                SampleWeight* slot = entry->samples;
                float a = 0.0f;
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                for (; 0 < count; count--) {
                    float weight = slot->weight;
                    /* The weighted sample is a float vector temporary: retail rounds each
                       product to float before accumulating (0x10059F22, 0x1005A6AF). */
                    srVector4T<float> weighted = channel_vectors[slot->index] * weight;
                    ++slot;
                    a = weighted.x + a;
                    r = weighted.y + r;
                    g = weighted.z + g;
                    b = weighted.w + b;
                }
                row_colors[x].alpha = static_cast<unsigned char>(srFloatToInt(a));
                row_colors[x].red = static_cast<unsigned char>(srFloatToInt(r));
                row_colors[x].green = static_cast<unsigned char>(srFloatToInt(g));
                row_colors[x].blue = static_cast<unsigned char>(srFloatToInt(b));
            }
            setPixelRow(row, y, 0, width);
        }
        return;
    }
    copyNoScaling(source);
}

// FUNCTION: SURRENDER 0x1005A250
void srColorSurfaceIFace::scaleVertical(srColorSurfaceIFace& source)
{
    w8_long width = this->width;
    w8_long height = this->height;
    w8_long source_height = source.height;
    if (height != source_height) {
        double support = source.filter->getSupport();
        double scale = height / (double)source_height;
        std::vector<SampleContributions> contributions(height);
        SampleContributions* counts = contributions.data();
        std::vector<srARGB> source_column_storage(source_height);
        srARGB* source_column_colors = source_column_storage.data();
        w8_ulong* source_column = (w8_ulong*)source_column_colors;
        std::vector<srARGB> column_storage(height);
        srARGB* column_colors = column_storage.data();
        w8_ulong* column = (w8_ulong*)column_colors;
        std::vector<srVector4T<float>> channel_vectors(source_height);
        std::vector<SampleWeight> sample_storage;
        SampleWeight* storage;
        /* Unlike scaleHorizontal, both branches center the source window at y / scale + 0.5
           (retail 0x1005A501 and 0x1005A3AD). */
        if (1.0 <= scale) {
            w8_long entries = 1 - (w8_long)(support * -2.0);
            sample_storage.resize(entries * height);
            storage = sample_storage.data();
            for (w8_long y = 0; y < height; y++) {
                SampleContributions* entry = counts + y;
                entry->count = 0;
                entry->samples = storage + y * entries;
                double center = y / scale + 0.5;
                double total = 0.0;
                w8_long first = (w8_long)ceil(center - support);
                w8_long last = (w8_long)floor(center + support);
                for (; first <= last; first++) {
                    double weight = source.filter->getWeight(center - first);
                    if (0.0 < weight) {
                        w8_long index = source.getClampedY(first);
                        entry->append(index, weight);
                        total = weight + total;
                    }
                }
                entry->normalize(total);
            }
        } else {
            double scaled_support = support / scale;
            double inverse = 1.0 / scale;
            w8_long entries = 1 - (w8_long)(scaled_support * -2.0);
            sample_storage.resize(entries * height);
            storage = sample_storage.data();
            for (w8_long y = 0; y < height; y++) {
                SampleContributions* entry = counts + y;
                entry->count = 0;
                entry->samples = storage + y * entries;
                double center = y / scale + 0.5;
                double total = 0.0;
                w8_long first = (w8_long)ceil(center - scaled_support);
                w8_long last = (w8_long)floor(center + scaled_support);
                for (; first <= last; first++) {
                    double weight = source.filter->getWeight((center - first) / inverse) / inverse;
                    if (0.0 < weight) {
                        w8_long index = source.getClampedY(first);
                        entry->append(index, weight);
                        total = weight + total;
                    }
                }
                entry->normalize(total);
            }
        }
        for (w8_long x = 0; x < width; x++) {
            source.getPixelColumn(source_column, x, 0, source_height);
            w8_long y;
            for (y = 0; y < source_height; y++) {
                channel_vectors[y].x = static_cast<float>(source_column_colors[y].alpha);
                channel_vectors[y].y = static_cast<float>(source_column_colors[y].red);
                channel_vectors[y].z = static_cast<float>(source_column_colors[y].green);
                channel_vectors[y].w = static_cast<float>(source_column_colors[y].blue);
            }
            for (y = 0; y < height; y++) {
                SampleContributions* entry = counts + y;
                w8_long count = entry->count;
                SampleWeight* slot = entry->samples;
                float a = 0.0f;
                float r = 0.0f;
                float g = 0.0f;
                float b = 0.0f;
                for (; 0 < count; count--) {
                    float weight = slot->weight;
                    /* The weighted sample is a float vector temporary: retail rounds each
                       product to float before accumulating (0x10059F22, 0x1005A6AF). */
                    srVector4T<float> weighted = channel_vectors[slot->index] * weight;
                    ++slot;
                    a = weighted.x + a;
                    r = weighted.y + r;
                    g = weighted.z + g;
                    b = weighted.w + b;
                }
                column_colors[y].alpha = static_cast<unsigned char>(srFloatToInt(a));
                column_colors[y].red = static_cast<unsigned char>(srFloatToInt(r));
                column_colors[y].green = static_cast<unsigned char>(srFloatToInt(g));
                column_colors[y].blue = static_cast<unsigned char>(srFloatToInt(b));
            }
            setPixelColumn(column, x, 0, height);
        }
        return;
    }
    copyNoScaling(source);
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

// FUNCTION: SURRENDER 0x10058450
void srColorSurfaceIFace::blit(const BlitInfo& info, srColorSurfaceIFace& source)
{
    if (info.destination.left == info.destination.right) {
        return;
    }
    if (info.destination.top == info.destination.bottom) {
        return;
    }
    if (info.source.left == info.source.right) {
        return;
    }
    if (info.source.top == info.source.bottom) {
        return;
    }
    int flip_h = info.destination.right < info.destination.left;
    int flip_v = info.destination.bottom < info.destination.top;
    if (info.source.right < info.source.left) {
        flip_h = flip_h == 0;
    }
    if (info.source.bottom < info.source.top) {
        flip_v = flip_v == 0;
    }
    Rectangle destination = info.destination;
    w8_long source_left = info.source.left;
    w8_long source_top = info.source.top;
    w8_long source_right = info.source.right;
    w8_long source_bottom = info.source.bottom;
    if (source_right < source_left) {
        w8_long swap = source_left;
        source_left = source_right;
        source_right = swap;
    }
    if (source_bottom < source_top) {
        w8_long swap = source_top;
        source_top = source_bottom;
        source_bottom = swap;
    }
    if (destination.right < destination.left) {
        w8_long swap = destination.left;
        destination.left = destination.right;
        destination.right = swap;
    }
    if (destination.bottom < destination.top) {
        w8_long swap = destination.top;
        destination.top = destination.bottom;
        destination.bottom = swap;
    }
    if (width <= destination.left) {
        return;
    }
    if (height <= destination.top) {
        return;
    }
    if (destination.right < 1) {
        return;
    }
    if (destination.bottom < 1) {
        return;
    }
    if (source_left < 0) {
        return;
    }
    if (source.width < source_right) {
        return;
    }
    if (source_top < 0) {
        return;
    }
    if (source.height < source_bottom) {
        return;
    }
    int full_destination = 0;
    if ((destination.left != 0) || (destination.top != 0) || (destination.right != width)) {
        full_destination = 0;
    } else {
        full_destination = destination.bottom == height;
    }
    int full_source = 0;
    if ((source_left == 0) && (source_top == 0) && (source_right == source.width) &&
        (source_bottom == source.height)) {
        full_source = 1;
    }
    int clipped = 0;
    if ((destination.left < 0) || (width < destination.right) || (destination.top < 0) ||
        (height < destination.bottom)) {
        clipped = 1;
    }
    if ((full_destination != 0) && (full_source != 0)) {
        copy(source);
        Rectangle rectangle;
        if (flip_h == 0) {
            if (flip_v == 0) {
                return;
            }
            rectangle.left = 0;
            rectangle.top = height;
            rectangle.right = width;
            rectangle.bottom = 0;
            flipRectangle(rectangle);
            return;
        }
        if (flip_v != 0) {
            rectangle.left = width;
            rectangle.top = height;
            rectangle.right = 0;
            rectangle.bottom = 0;
            flipRectangle(rectangle);
            return;
        }
        rectangle.left = width;
        rectangle.top = 0;
        rectangle.right = 0;
        rectangle.bottom = height;
        flipRectangle(rectangle);
        return;
    }
    w8_long destination_width = destination.right - destination.left;
    w8_long source_width = source_right - source_left;
    if (destination_width == source_width) {
        if ((flip_h == 0) && (flip_v == 0)) {
            blit(destination.left, destination.top, source, source_left, source_top, source_right,
                 source_bottom);
            return;
        }
        if (clipped == 0) {
            blit(destination.left, destination.top, source, source_left, source_top, source_right,
                 source_bottom);
            Rectangle rectangle;
            rectangle.left = destination.left;
            rectangle.top = destination.top;
            rectangle.right = destination.right;
            rectangle.bottom = destination.bottom;
            if (flip_h != 0) {
                w8_long swap = rectangle.left;
                rectangle.left = rectangle.right;
                rectangle.right = swap;
            }
            if (flip_v != 0) {
                w8_long swap = rectangle.top;
                rectangle.top = rectangle.bottom;
                rectangle.bottom = swap;
            }
            flipRectangle(rectangle);
            return;
        }
    }
    std::unique_ptr<srColorSurface> scaled_storage;
    srColorSurfaceIFace* scaled = nullptr;
    if ((full_source == 0) || (flip_h != 0) || (flip_v != 0)) {
        srPixelConvert::PixelFormat format;
        source.getPixelFormat(format);
        scaled_storage =
            std::make_unique<srColorSurface>(format, source_width, source_bottom - source_top);
        scaled = scaled_storage.get();
        srColorSurfaceIFace* scaled_iface = scaled;
        scaled_iface->copySurfaceParameters(source);
        scaled_iface->blit(0, 0, source, source_left, source_top, source_right, source_bottom);
        Rectangle rectangle;
        if (flip_h == 0) {
            if (flip_v != 0) {
                rectangle.left = 0;
                rectangle.top = scaled->height;
                rectangle.right = scaled->width;
                rectangle.bottom = 0;
                scaled->flipRectangle(rectangle);
            }
        } else {
            if (flip_v == 0) {
                rectangle.left = scaled->width;
                rectangle.top = 0;
                rectangle.right = 0;
                rectangle.bottom = scaled->height;
            } else {
                rectangle.left = scaled->width;
                rectangle.top = scaled->height;
                rectangle.right = 0;
                rectangle.bottom = 0;
            }
            scaled->flipRectangle(rectangle);
        }
    } else {
        scaled = &source;
    }
    if (full_destination == 0) {
        srPixelConvert::PixelFormat format = pixel_format;
        srColorSurface temporary(format, destination_width, destination.bottom - destination.top);
        srColorSurfaceIFace& temporary_iface = temporary;
        temporary_iface.copySurfaceParameters(*this);
        temporary.copy(*scaled);
        blit(destination.left, destination.top, temporary, 0, 0, destination.right,
             destination.bottom);
    } else {
        copy(*scaled);
    }
}

// FUNCTION: SURRENDER 0x100589D0
void srColorSurfaceIFace::composite(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                                    w8_long source_y, w8_long source_right, w8_long source_bottom,
                                    double alpha)
{
    w8_long width = this->width;
    w8_long height = this->height;
    w8_long source_height = source.height;
    if (0.0 < alpha) {
        if (1.0 <= alpha) {
            alpha = 1.0;
        }
        if ((source.pixel_format.alpha_bits == 0) && (alpha == 1.0)) {
            blit(x, y, source, source_x, source_y, source_right, source_bottom);
            return;
        }
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
            if (source.width < source_right) {
                source_right = source.width;
            }
            if (source_height < source_bottom) {
                source_bottom = source_height;
            }
            if (((source_x < source.width) && (source_x < source_right)) &&
                ((source_y < source_height) && (source_y < source_bottom))) {
                w8_long dest_span = x - source_x;
                if (width < dest_span + source_right) {
                    source_right = (width - x) + source_x;
                }
                if (height < source_bottom + (y - source_y)) {
                    source_bottom = (height - y) + source_y;
                }
                if ((source_x < source_right) && (source_y < source_bottom)) {
                    if ((((&source == this) && (x < source_right) && (y < source_bottom)) &&
                         (source_x < dest_span + source_right)) &&
                        ((source_y < source_bottom + (y - source_y)) && (source_y <= y))) {
                        w8_long rows = source_bottom - source_y;
                        w8_long span = source_right - source_x;
                        std::vector<srARGB> temp_storage(rows * span);
                        srARGB* temp_colors = temp_storage.data();
                        w8_ulong* temp = (w8_ulong*)temp_colors;
                        std::vector<srARGB> row_storage(span);
                        srARGB* row_colors = row_storage.data();
                        w8_ulong* row = (w8_ulong*)row_colors;
                        w8_long r;
                        for (r = source_y; r < source_bottom; r++) {
                            source.getPixelRow(temp + (r - source_y) * span, r, source_x,
                                               source_right);
                        }
                        for (r = source_bottom - source_y; r != 0; r--) {
                            getPixelRow(row, y, x, dest_span + source_right);
                            for (w8_long i = 0; i < span; i++) {
                                unsigned char* source_pixel =
                                    (unsigned char*)&temp[(source_bottom - source_y - r) * span +
                                                          i];
                                unsigned char* dest_pixel = (unsigned char*)&row[i];
                                double blend = alpha;
                                if ((source_pixel[3] != 0) && (0.0 < alpha)) {
                                    if (1.0 < alpha) {
                                        blend = 1.0;
                                    }
                                    blend = source_pixel[3] * 0.00392156862745098 * blend;
                                    double inverse = 1.0 - blend;
                                    dest_pixel[2] = (unsigned char)srFloatToInt(
                                        dest_pixel[2] * inverse + source_pixel[2] * blend);
                                    dest_pixel[1] = (unsigned char)srFloatToInt(
                                        dest_pixel[1] * inverse + source_pixel[1] * blend);
                                    dest_pixel[0] = (unsigned char)srFloatToInt(
                                        dest_pixel[0] * inverse + source_pixel[0] * blend);
                                    dest_pixel[3] = (unsigned char)srFloatToInt(
                                        blend * 255.0 + dest_pixel[3] * inverse);
                                }
                            }
                            setPixelRow(row, y, x, dest_span + source_right);
                            y = y + 1;
                        }
                        return;
                    }
                    w8_long span = source_right - source_x;
                    std::vector<srARGB> source_row_storage(span);
                    srARGB* source_row_colors = source_row_storage.data();
                    w8_ulong* source_row = (w8_ulong*)source_row_colors;
                    std::vector<srARGB> row_storage(span);
                    srARGB* row_colors = row_storage.data();
                    w8_ulong* row = (w8_ulong*)row_colors;
                    if (alpha == 1.0) {
                        for (; source_y < source_bottom; source_y++) {
                            getPixelRow(row, y, x, dest_span + source_right);
                            source.getPixelRow(source_row, source_y, source_x, source_right);
                            for (w8_long i = 0; i < span; i++) {
                                unsigned char* source_pixel = (unsigned char*)&source_row[i];
                                unsigned char* dest_pixel = (unsigned char*)&row[i];
                                unsigned char alpha_byte = source_pixel[3];
                                if (alpha_byte != 0) {
                                    if (alpha_byte == 0xff) {
                                        row[i] = source_row[i];
                                    } else {
                                        double blend = alpha_byte * 0.00392156862745098;
                                        double inverse = 1.0 - blend;
                                        dest_pixel[2] = (unsigned char)srFloatToInt(
                                            dest_pixel[2] * inverse + source_pixel[2] * blend);
                                        dest_pixel[1] = (unsigned char)srFloatToInt(
                                            dest_pixel[1] * inverse + source_pixel[1] * blend);
                                        dest_pixel[0] = (unsigned char)srFloatToInt(
                                            dest_pixel[0] * inverse + source_pixel[0] * blend);
                                        dest_pixel[3] = (unsigned char)srFloatToInt(
                                            blend * 255.0 + dest_pixel[3] * inverse);
                                    }
                                }
                            }
                            setPixelRow(row, y, x, dest_span + source_right);
                            y = y + 1;
                        }
                    } else {
                        for (; source_y < source_bottom; source_y++) {
                            getPixelRow(row, y, x, dest_span + source_right);
                            source.getPixelRow(source_row, source_y, source_x, source_right);
                            for (w8_long i = 0; i < span; i++) {
                                unsigned char* source_pixel = (unsigned char*)&source_row[i];
                                unsigned char* dest_pixel = (unsigned char*)&row[i];
                                double blend = alpha;
                                if ((source_pixel[3] != 0) && (0.0 < alpha)) {
                                    if (1.0 < alpha) {
                                        blend = 1.0;
                                    }
                                    blend = source_pixel[3] * 0.00392156862745098 * blend;
                                    double inverse = 1.0 - blend;
                                    dest_pixel[2] = (unsigned char)srFloatToInt(
                                        dest_pixel[2] * inverse + source_pixel[2] * blend);
                                    dest_pixel[1] = (unsigned char)srFloatToInt(
                                        dest_pixel[1] * inverse + source_pixel[1] * blend);
                                    dest_pixel[0] = (unsigned char)srFloatToInt(
                                        dest_pixel[0] * inverse + source_pixel[0] * blend);
                                    dest_pixel[3] = (unsigned char)srFloatToInt(
                                        blend * 255.0 + dest_pixel[3] * inverse);
                                }
                            }
                            setPixelRow(row, y, x, dest_span + source_right);
                            y = y + 1;
                        }
                    }
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
        stream << "  Palette: ";
        getPalette()->getUniqueName(stream);
        stream << '\n';
    }
    stream.width(0x20);
    stream << "  Flags: ";
    dumpFlags(stream, surface_flags, s_flag_names2);
    stream << '\n';
    stream.flags(static_cast<std::ios::fmtflags>(flags & 0x7fff));
}
