#include "surrender/pixel_math.h"
#include "glm/ext/vector_uint4.hpp"
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <array>
#include <type_traits>
#include <cstdlib>
#include <cstring>
#include <ios>
#include <iterator>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <ostream>
#include <cmath>
#include <string>
#include <vector>

#include "surrender/srMath.h"
#include "wiz8/compat/surfaces.h"
#include "surrender/srColorSurface.h"
#include "surrender/srCore.h"
#include "surrender/srFilter.h"
#include "surrender/srPalette.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srPixelConvert.h"
#include "surrender/srTypeRegistry.h"

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

// SDL blits clip rather than wrap; texture sampling needs both modes.
static w8_long surfaceCoordinate(w8_long value, w8_long size, bool clamp)
{
    if (clamp)
        return value < 0 ? 0 : std::min(value, size - 1);
    const w8_long wrapped = value % size;
    return wrapped < 0 ? wrapped + size : wrapped;
}

// FUNCTION: SURRENDER 0x100572E0
w8_long srColorSurfaceIFace::getClampedX(w8_long x) const
{
    return surfaceCoordinate(x, width, clamp_modes & CLAMP_HORIZONTAL);
}

// FUNCTION: SURRENDER 0x10057330
w8_long srColorSurfaceIFace::getClampedY(w8_long y) const
{
    return surfaceCoordinate(y, height, clamp_modes & CLAMP_VERTICAL);
}

// FUNCTION: SURRENDER 0x10057380
void srColorSurfaceIFace::clampCoordinates(w8_long& x, w8_long& y)
{
    x = getClampedX(x);
    y = getClampedY(y);
}

// FUNCTION: SURRENDER 0x10059870
double srColorSurfaceIFace::getAspectRatio() const
{
    if (height == 0) {
        return 0.0;
    }
    return width / (double)height;
}

// FUNCTION: SURRENDER 0x100598C0
srFilter* srColorSurfaceIFace::getFilter() const
{
    return filter;
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
    const bool x_major = std::abs(dy) < std::abs(dx);
    w8_long major = x_major ? x0 : y0;
    w8_long minor = x_major ? y0 : x0;
    w8_long last_major = x_major ? x1 : y1;
    w8_long last_minor = x_major ? y1 : x1;
    const w8_long major_delta = x_major ? dx : dy;
    const w8_long minor_delta = x_major ? dy : dx;
    if (major_delta < 0) {
        std::swap(major, last_major);
        std::swap(minor, last_minor);
    }
    const w8_long major_limit = x_major ? width : height;
    const w8_long minor_limit = (x_major ? height : width) - 1;
    const w8_long high_minor = std::max(minor, last_minor);
    const w8_long low_minor = std::min(minor, last_minor);
    // The vertical-major path requires both minor endpoints inside the surface;
    // the horizontal-major path accepts an intersecting minor range.
    const bool minor_visible = x_major ? high_minor >= 0 && low_minor <= minor_limit
                                       : low_minor >= 0 && high_minor <= minor_limit;
    if (last_major < 0 || major >= major_limit || !minor_visible) return;

    const float gradient = static_cast<float>(minor_delta) / major_delta;
    const w8_long step = static_cast<w8_long>(gradient * 65536.0);
    if (major < 0) {
        minor = static_cast<w8_long>(minor - major * gradient);
        major = 0;
    }
    if (minor < last_minor) {
        if (minor < 0) {
            major -= static_cast<w8_long>(minor / gradient);
            minor = 0;
        }
        if (minor_limit <= last_minor) {
            last_major = static_cast<w8_long>(last_major -
                (last_minor - static_cast<float>(minor_limit)) / gradient);
        }
    } else {
        if (last_minor < 0) last_major -= static_cast<w8_long>(last_minor / gradient);
        if (minor_limit <= minor) {
            major = static_cast<w8_long>(major -
                (minor - static_cast<float>(minor_limit)) / gradient);
            minor = minor_limit;
        }
    }
    last_major = std::min(last_major, major_limit);
    w8_long minor_fixed = minor << 16;
    for (; major < last_major; ++major) {
        const w8_long coordinate = (minor_fixed + (minor_fixed >> 31 & 0xffff)) >> 16;
        setPixel(x_major ? major : coordinate, x_major ? coordinate : major, pixel);
        minor_fixed += step;
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
    w8_ulong pixel = 0;
    getPixelRowRaw(&pixel, y, x, x + 1);
    return pixel;
}

// FUNCTION: SURRENDER 0x1005B1B0
void srColorSurfaceIFace::setPixelRaw(w8_long x, w8_long y, w8_ulong pixel)
{
    setPixelRowRaw(&pixel, y, x, x + 1);
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
    std::vector<w8_ulong> source_row_storage(source_width), row_storage(width);
    auto* source_row = source_row_storage.data();
    auto* row = row_storage.data();
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
        std::vector<w8_ulong> row_storage(width);
        auto* row = row_storage.data();
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
    stream << "  Pixel format: " << pixel_format.getName() << '\n';
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
    const auto functions = srPixelConvert::selectFuncs(format);
    pixel_write = functions.write;
    pixel_read = functions.read;
}

// FUNCTION: SURRENDER 0x1005B8A0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, w8_ulong arg_width,
                               w8_ulong arg_height)
{
    const auto format = srPixelConvert::pixelFormat(type);
    init(format, arg_width, arg_height, 0);
    allocate();
    const auto functions = srPixelConvert::selectFuncs(format);
    pixel_write = functions.write;
    pixel_read = functions.read;
}

/* The data-taking variants view caller storage, which must outlive the surface. */
// FUNCTION: SURRENDER 0x1005B9C0
srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    const auto format = srPixelConvert::pixelFormat(type);
    init(format, arg_width, arg_height, arg_pitch);
    adopt(CreateCpuStorage(arg_width, arg_height, StorageFormat(format), data, arg_pitch).release());
    const auto functions = srPixelConvert::selectFuncs(format);
    pixel_write = functions.write;
    pixel_read = functions.read;
}

// FUNCTION: SURRENDER 0x1005BAC0
srColorSurface::srColorSurface(const srPixelConvert::PixelFormat& format, void* data,
                               w8_ulong arg_width, w8_ulong arg_height,
                               w8_ulong arg_pitch)
{
    init(format, arg_width, arg_height, arg_pitch);
    adopt(CreateCpuStorage(arg_width, arg_height, StorageFormat(format), data, arg_pitch).release());
    const auto functions = srPixelConvert::selectFuncs(format);
    pixel_write = functions.write;
    pixel_read = functions.read;
}

srColorSurface::srColorSurface(srPixelConvert::e_surfaceType type, SDL_Surface& shared)
{
    const auto format = srPixelConvert::pixelFormat(type);
    init(format, shared.w, shared.h, shared.pitch);
    ++shared.refcount;
    adopt(&shared);
    const auto functions = srPixelConvert::selectFuncs(format);
    pixel_write = functions.write;
    pixel_read = functions.read;
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
    w8_ulong pixel = 0;
    std::memcpy(&pixel, getAddress(x, y), pixel_format.pixel_size + 1);
    return pixel;
}

// FUNCTION: SURRENDER 0x1005C030
void srColorSurface::setPixelRaw(w8_long x, w8_long y, w8_ulong pixel)
{
    std::memcpy(getAddress(x, y), &pixel, pixel_format.pixel_size + 1);
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

template<class Pixel>
void srColorSurface::transferColumn(std::span<Pixel> pixels, w8_long x, w8_long y)
{
    constexpr bool reading = !std::is_const_v<Pixel>;
    constexpr std::size_t chunkSize = 256;
    std::array<unsigned char, chunkSize * sizeof(w8_ulong)> buffer;
    const unsigned bytes = pixel_format.pixel_size + 1;
    for (std::size_t offset = 0; offset < pixels.size(); offset += chunkSize) {
        const auto count = std::min(chunkSize, pixels.size() - offset);
        auto* chunk = pixels.data() + offset;
        if constexpr (!reading)
            convertFromARGB8888(buffer.data(), chunk, count);
        auto* first = getAddress(x, y + static_cast<w8_long>(offset));
        for (std::size_t i = 0; i < count; ++i) {
            auto* address = first + i * pitch;
            if constexpr (reading)
                std::memcpy(buffer.data() + i * bytes, address, bytes);
            else
                std::memcpy(address, buffer.data() + i * bytes, bytes);
        }
        if constexpr (reading)
            convertToARGB8888(chunk, buffer.data(), count);
    }
}

// FUNCTION: SURRENDER 0x1005D970
void srColorSurface::getPixelColumn(w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end)
{
    if (y_start < y_end)
        transferColumn(std::span<w8_ulong>(pixels, y_end - y_start), x, y_start);
}

// FUNCTION: SURRENDER 0x1005DAD0
void srColorSurface::setPixelColumn(const w8_ulong* pixels, w8_long x, w8_long y_start, w8_long y_end)
{
    if (y_start < y_end)
        transferColumn(std::span<const w8_ulong>(pixels, y_end - y_start), x, y_start);
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

void srColorSurface::fillRectangle(SDL_Rect rect, w8_ulong pixel)
{
    const SDL_Rect bounds{0, 0, width, height};
    if (!SDL_GetRectIntersection(&rect, &bounds, &rect))
        return;
    // Convert through the surface format and palette before the raw SDL fill.
    setPixel(rect.x, rect.y, pixel);
    const auto raw = getPixelRaw(rect.x, rect.y);
    auto destination = ShareCpuSurface(*storage);
    FillCpuSurface(destination, raw, &rect);
}

// FUNCTION: SURRENDER 0x1005C560
void srColorSurface::setHLine(w8_long y, w8_long x_start, w8_long x_end, w8_ulong pixel)
{
    if (getDataPtr() == nullptr) {
        srColorSurfaceIFace::setHLine(y, x_start, x_end, pixel);
        return;
    }
    if (x_end < x_start)
        std::swap(x_start, x_end);
    fillRectangle({x_start, y, x_end - x_start, 1}, pixel);
}

// FUNCTION: SURRENDER 0x1005C740
void srColorSurface::setVLine(w8_long x, w8_long y_start, w8_long y_end, w8_ulong pixel)
{
    if (getDataPtr() == nullptr) {
        srColorSurfaceIFace::setVLine(x, y_start, y_end, pixel);
        return;
    }
    if (y_end < y_start)
        std::swap(y_start, y_end);
    fillRectangle({x, y_start, 1, y_end - y_start}, pixel);
}

// FUNCTION: SURRENDER 0x1005C900
void srColorSurface::fill(w8_ulong pixel)
{
    if (getDataPtr() == nullptr) {
        srColorSurfaceIFace::fill(pixel);
        return;
    }
    fillRectangle({0, 0, width, height}, pixel);
}

// FUNCTION: SURRENDER 0x1005D220
void srColorSurface::blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x, w8_long source_y,
                          w8_long x_end, w8_long y_end)
{
    if (&source != this && !isCompatible(source)) {
        srColorSurfaceIFace::blit(x, y, source, source_x, source_y, x_end, y_end);
        return;
    }
    SDL_Rect sample{source_x, source_y, x_end - source_x, y_end - source_y};
    SDL_Rect dest{x, y, sample.w, sample.h};
    if (!ClipCpuBlit(dest, sample, {0, 0, width, height}, {0, 0, source.width, source.height}))
        return;
    auto destination = ShareCpuSurface(*storage);
    auto input = ShareCpuSurface(*static_cast<srColorSurface&>(source).storage);
    BlitCpuSurface(destination, &dest, input, &sample);
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
            channels[i] = glm::vec4(srPixels::channels(input[i]));
        }
        for (w8_long i = 0; i < size; ++i) {
            glm::vec4 value(0.0f);
            for (const auto& sample : contributions[i]) {
                // Round the product to float before accumulation.
                const glm::vec4 weighted = channels[sample.index] * sample.weight;
                value = weighted + value;
            }
            glm::uvec4 rounded;
            for (unsigned channel = 0; channel < 4; ++channel)
                rounded[channel] = static_cast<unsigned char>(srFloatToInt(value[channel]));
            output[i] = srPixels::pack(rounded);
        }
        if (vertical) setPixelColumn(output.data(), line, 0, size);
        else setPixelRow(output.data(), line, 0, size);
    }
}

// FUNCTION: SURRENDER 0x10058150
void srColorSurfaceIFace::blit(w8_long x, w8_long y, srColorSurfaceIFace& source, w8_long source_x,
                               w8_long source_y, w8_long source_right, w8_long source_bottom)
{
    SDL_Rect sample{source_x, source_y, source_right - source_x, source_bottom - source_y};
    SDL_Rect dest{x, y, sample.w, sample.h};
    if (!ClipCpuBlit(dest, sample, {0, 0, width, height}, {0, 0, source.width, source.height}))
        return;
    // Device-backed surfaces expose row operations, not CPU storage. Snapshot
    // self-blits before writing; other copies need only one converted ARGB row.
    const bool self = &source == this;
    std::vector<w8_ulong> pixels(size_t(sample.w) * (self ? sample.h : 1));
    if (self)
        for (int row = 0; row < sample.h; ++row)
            source.getPixelRow(pixels.data() + size_t(row) * sample.w, sample.y + row,
                               sample.x, sample.x + sample.w);
    for (int row = 0; row < sample.h; ++row) {
        auto* data = pixels.data() + (self ? size_t(row) * sample.w : 0);
        if (!self)
            source.getPixelRow(data, sample.y + row, sample.x, sample.x + sample.w);
        setPixelRow(data, dest.y + row, dest.x, dest.x + dest.w);
    }

}


// FUNCTION: SURRENDER 0x1005A930
void srColorSurfaceIFace::minify(srColorSurfaceIFace& source)
{
    if (width != source.width / 2 || height != source.height / 2 || this == &source ||
        width <= 0 || height <= 0) return;
    std::vector<w8_ulong> first(source.width), second(source.width), row(width);
    for (w8_long y = 0; y < height; ++y) {
        source.getPixelRow(first.data(), y * 2, 0, source.width);
        source.getPixelRow(second.data(), y * 2 + 1, 0, source.width);
        for (w8_long x = 0; x < width; ++x)
            row[x] = srPixels::average(first[x * 2], first[x * 2 + 1],
                                       second[x * 2], second[x * 2 + 1]);
        setPixelRow(row.data(), y, 0, width);
    }
}

namespace {
void magnifyRow(std::span<w8_ulong> destination, std::span<const w8_ulong> source)
{
    for (std::size_t x = 0; x < source.size(); ++x) {
        destination[x * 2] = source[x];
        destination[x * 2 + 1] = x + 1 == source.size() ? source[x] :
            srPixels::average(source[x], source[x + 1]);
    }
}
} // namespace

// FUNCTION: SURRENDER 0x1005ABB0
void srColorSurfaceIFace::magnify(srColorSurfaceIFace& source)
{
    if (width != source.width * 2 || height != source.height * 2 || this == &source ||
        source.width <= 0 || source.height <= 0) return;
    std::vector<w8_ulong> input(source.width), even(width), odd(width);
    source.getPixelRow(input.data(), 0, 0, source.width);
    magnifyRow(even, input);
    // Keep the historical untouched bottom two destination rows.
    for (w8_long y = 0; y < source.height - 1; ++y) {
        source.getPixelRow(input.data(), y + 1, 0, source.width);
        magnifyRow(odd, input);
        setPixelRow(even.data(), y * 2, 0, width);
        for (w8_long x = 0; x < width; ++x)
            even[x] = srPixels::average(odd[x], even[x]);
        setPixelRow(even.data(), y * 2 + 1, 0, width);
        even.swap(odd);
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
