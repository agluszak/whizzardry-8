/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_log.h>
#include <SDL3_image/SDL_image.h>
#include <zlib.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <zconf.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <cstdlib>
#include <exception>
#include <span>
#include <cstddef>

#include "wiz8/filesystem.h"
#include "himage.h"
#include "imgfmt.h"

// Nonblack colors that pack to 0 are nudged to 1 so they stay opaque.
constexpr std::uint32_t kBlackSubstitute = 0x0001;

SDL_PixelFormat g_sgp_pixel_format = SDL_PIXELFORMAT_XRGB1555;

namespace {
constexpr std::size_t max_image_bytes = 256 * 1024 * 1024;

enum class ImageFormat { pcx, tga, jpeg, sti };

ImageFormat image_format(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        throw std::runtime_error("Image path has no extension");
    }
    const auto* extension = path.c_str() + dot + 1;
    if (SDL_strcasecmp(extension, "PCX") == 0) {
        return ImageFormat::pcx;
    }
    if (SDL_strcasecmp(extension, "TGA") == 0) {
        return ImageFormat::tga;
    }
    if (SDL_strcasecmp(extension, "STI") == 0) {
        return ImageFormat::sti;
    }
    if (SDL_strcasecmp(extension, "JPG") == 0 || SDL_strcasecmp(extension, "JPEG") == 0) {
        return ImageFormat::jpeg;
    }
    throw std::runtime_error("Unsupported image extension");
}

bool safe_dimensions(std::size_t width, std::size_t height)
{
    return width && height && width <= std::numeric_limits<std::uint16_t>::max() &&
           height <= std::numeric_limits<std::uint16_t>::max() && width * height <= max_image_bytes / 4;
}

unsigned little_word(const std::uint8_t* bytes)
{
    return bytes[0] | (unsigned(bytes[1]) << 8);
}

void set_palette(SDL_Surface& surface, const Palette8& colors)
{
    std::unique_ptr<SDL_Palette, decltype(&SDL_DestroyPalette)> palette(SDL_CreatePalette(int(colors.size())),
                                                                        SDL_DestroyPalette);
    if (!palette || !SDL_SetPaletteColors(palette.get(), colors.data(), 0, int(colors.size())) ||
        !SDL_SetSurfacePalette(&surface, palette.get())) {
        throw std::runtime_error(SDL_GetError());
    }
}

BitmapImage bitmap_from_pixels(int width, int height, SDL_PixelFormat format, std::span<const std::uint8_t> pixels)
{
    BitmapImage surface(SDL_CreateSurface(width, height, format), SDL_DestroySurface);
    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    const std::size_t row_bytes = std::size_t(width) * SDL_BYTESPERPIXEL(format);
    for (int y = 0; y < height; ++y) {
        std::copy_n(pixels.data() + y * row_bytes, row_bytes,
                    static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch);
    }
    return surface;
}

bool valid_image_header(const std::vector<std::uint8_t>& bytes, ImageFormat format)
{
    if (format == ImageFormat::pcx) {
        if (bytes.size() < 128 || bytes[0] != 10 || bytes[2] > 1) {
            return false;
        }
        const auto left = little_word(bytes.data() + 4), top = little_word(bytes.data() + 6);
        const auto right = little_word(bytes.data() + 8), bottom = little_word(bytes.data() + 10);
        const auto stride = little_word(bytes.data() + 66);
        return right >= left && bottom >= top && right < 32768 && bottom < 32768 &&
               safe_dimensions(right - left + 1, bottom - top + 1) && bytes[3] == 8 &&
               (bytes[65] == 1 || bytes[65] == 3) && stride >= right - left + 1 && stride < 32768;
    }
    if (format == ImageFormat::tga) {
        if (bytes.size() < 18) {
            return false;
        }
        if ((bytes[2] == 1 || bytes[2] == 9) && little_word(bytes.data() + 3) + little_word(bytes.data() + 5) > 256) {
            return false;
        }
        return (bytes[16] == 8 || bytes[16] == 16 || bytes[16] == 24) &&
               safe_dimensions(little_word(bytes.data() + 12), little_word(bytes.data() + 14));
    }
    if (format == ImageFormat::jpeg) {
        if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xd8) {
            return false;
        }
        std::size_t position = 2;
        while (position < bytes.size()) {
            if (bytes[position++] != 0xff) {
                return false;
            }
            while (position < bytes.size() && bytes[position] == 0xff) {
                ++position;
            }
            if (position == bytes.size()) {
                return false;
            }
            const auto marker = bytes[position++];
            if (marker == 0xda || marker == 0xd9) {
                return false;
            }
            if (marker == 1 || (marker >= 0xd0 && marker <= 0xd8)) {
                continue;
            }
            if (bytes.size() - position < 2) {
                return false;
            }
            const std::size_t length = (unsigned(bytes[position]) << 8) | bytes[position + 1];
            if (length < 2 || length > bytes.size() - position) {
                return false;
            }
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
                if (length < 8) {
                    return false;
                }
                const auto height = (unsigned(bytes[position + 3]) << 8) | bytes[position + 4];
                const auto width = (unsigned(bytes[position + 5]) << 8) | bytes[position + 6];
                return safe_dimensions(width, height);
            }
            position += length;
        }
    }
    return false;
}

BitmapImage load_ordinary_image(wiz8::File& file, ImageFormat format)
{
    const auto size = file.size();
    if (size <= 0 || std::uint64_t(size) > max_image_bytes) {
        throw std::runtime_error("Invalid image file size");
    }
    std::vector<std::uint8_t> bytes(size);
    file.read_exact(bytes.data(), bytes.size());
    if (!valid_image_header(bytes, format)) {
        throw std::runtime_error("Invalid image header");
    }

    // Give SDL an independent cursor, including for bounded SLF entries.
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(SDL_IOFromConstMem(bytes.data(), bytes.size()),
                                                                 SDL_CloseIO);
    if (!stream) {
        throw std::runtime_error(SDL_GetError());
    }
    const char* type = format == ImageFormat::pcx ? "PCX" : format == ImageFormat::tga ? "TGA" : "JPG";
    BitmapImage surface(IMG_LoadTyped_IO(stream.get(), false, type), SDL_DestroySurface);
    if (!surface) {
        throw std::runtime_error(SDL_GetError());
    }
    if (!safe_dimensions(surface->w, surface->h)) {
        throw std::runtime_error("Invalid image dimensions");
    }

    const auto* palette = SDL_GetSurfacePalette(surface.get());
    if (SDL_ISPIXELFORMAT_INDEXED(surface->format)) {
        if (surface->format != SDL_PIXELFORMAT_INDEX8 || !palette) {
            throw std::runtime_error("Unsupported indexed image format");
        }
        // Colour-mapped TGA palettes start at their declared first entry.
        Palette8 colors{};
        colors.fill({0, 0, 0, SDL_ALPHA_OPAQUE});
        const unsigned first_color =
            format == ImageFormat::tga && (bytes[2] == 1 || bytes[2] == 9) ? little_word(bytes.data() + 3) : 0;
        std::transform(palette->colors, palette->colors + std::min(unsigned(palette->ncolors), 256 - first_color),
                       colors.begin() + first_color,
                       [](const SDL_Color& color) { return SDL_Color{color.r, color.g, color.b, SDL_ALPHA_OPAQUE}; });
        set_palette(*surface, colors);
    }
    return surface;
}

std::vector<SpriteFrame> decode_sti_frames(std::span<const STCISubImage> frames, std::span<const std::uint8_t> data)
{
    if (!frames.empty() && data.empty()) {
        throw std::runtime_error("Invalid STI sprite data");
    }
    std::vector<SpriteFrame> decoded;
    decoded.reserve(frames.size());
    constexpr size_t max_pixels = max_image_bytes / sizeof(std::uint16_t);
    size_t remaining_pixels = max_pixels;
    for (const STCISubImage& frame : frames) {
        SpriteFrame& out = decoded.emplace_back();
        out.offset = {frame.sOffsetX, frame.sOffsetY};
        out.width = frame.usWidth;
        out.height = frame.usHeight;
        if (frame.uiDataOffset > data.size() || frame.uiDataLength > data.size() - frame.uiDataOffset) {
            throw std::runtime_error("Invalid STI sprite data");
        }
        const size_t pixel_count = size_t(frame.usWidth) * frame.usHeight;
        if (pixel_count > remaining_pixels) {
            throw std::runtime_error("STI sprites exceed decoded pixel limit");
        }
        remaining_pixels -= pixel_count;
        if (pixel_count == 0) {
            continue;
        }
        const std::uint8_t* src = data.data() + frame.uiDataOffset;
        const std::uint8_t* const end = src + frame.uiDataLength;
        out.indices.assign(pixel_count, SpriteFrame::kTransparent);
        for (std::uint32_t y = 0; y < frame.usHeight; ++y) {
            std::uint16_t* row = out.indices.data() + std::size_t(y) * frame.usWidth;
            std::uint32_t x = 0;
            for (;;) {
                if (src >= end) {
                    throw std::runtime_error("Invalid STI sprite data");
                }
                const std::uint8_t control = *src++;
                if (control == 0) {
                    break;
                }
                const std::uint32_t count = control & 0x7f;
                if (count == 0 || count > frame.usWidth - x) {
                    throw std::runtime_error("Invalid STI sprite data");
                }
                if (control & 0x80) {
                    x += count;
                } else {
                    if (count > static_cast<std::uint32_t>(end - src)) {
                        throw std::runtime_error("Invalid STI sprite data");
                    }
                    for (std::uint32_t n = 0; n < count; ++n) {
                        row[x++] = *src++;
                    }
                }
            }
            if (x != frame.usWidth) {
                throw std::runtime_error("Invalid STI sprite data");
            }
        }
    }
    return decoded;
}

DecodedImage load_sti(wiz8::File& file)
{
    STCIHeader header{};
    file.read_exact(&header, sizeof(header));
    if (std::memcmp(header.cID, STCI_ID_STRING, STCI_ID_LEN) != 0) {
        throw std::runtime_error("Invalid STI header");
    }
    const bool rgb = header.fFlags & STCI_RGB;
    if ((!rgb && !(header.fFlags & STCI_INDEXED)) || (rgb && header.ubDepth != 16 && header.ubDepth != 24) ||
        (!rgb && header.ubDepth != 8)) {
        throw std::runtime_error("Unsupported STI pixel format");
    }
    const bool etrle = !rgb && (header.fFlags & STCI_ETRLE_COMPRESSED);
    const bool compressed = header.fFlags & STCI_ZLIB_COMPRESSED;
    if ((rgb && (header.fFlags & (STCI_INDEXED | STCI_ETRLE_COMPRESSED))) || (etrle && compressed)) {
        throw std::runtime_error("Conflicting STI encodings");
    }
    const std::size_t bitmap_bytes = std::size_t(header.usWidth) * header.usHeight * (header.ubDepth / 8);
    if (!etrle &&
        (!safe_dimensions(header.usWidth, header.usHeight) || (compressed && header.uiOriginalSize != bitmap_bytes))) {
        throw std::runtime_error("Invalid STI bitmap dimensions or decoded size");
    }
    const std::uint64_t palette_bytes =
        rgb ? 0 : std::uint64_t(header.Indexed.uiNumberOfColours) * sizeof(STCIPaletteElement);
    const std::size_t frame_count = etrle ? header.Indexed.usNumberOfSubImages : 0;
    const auto frames_offset = sizeof(header) + palette_bytes;
    const auto pixels_offset = frames_offset + frame_count * sizeof(STCISubImage);
    const auto app_offset = pixels_offset + header.uiStoredSize;
    const auto file_size = file.size();
    Palette8 palette{};
    if (!rgb) {
        if (header.Indexed.uiNumberOfColours != 256) {
            throw std::runtime_error("STI requires a 256-color palette");
        }
        std::array<STCIPaletteElement, 256> stored_palette{};
        file.read_exact(stored_palette.data(), sizeof(stored_palette));
        std::transform(stored_palette.begin(), stored_palette.end(), palette.begin(),
                       [](const STCIPaletteElement& color) {
                           return SDL_Color{color.ubRed, color.ubGreen, color.ubBlue, SDL_ALPHA_OPAQUE};
                       });
    }
    if (file_size < 0 || app_offset > std::uint64_t(file_size) || header.uiStoredSize > max_image_bytes) {
        throw std::runtime_error("STI pixel data exceeds file bounds");
    }
    // Even though the native renderer doesn't use application data, a declared
    // section extending past EOF means this STI is truncated.
    if (header.uiAppDataSize > std::uint64_t(file_size) - app_offset) {
        throw std::runtime_error("STI application data exceeds file bounds");
    }
    if (!etrle && !compressed && bitmap_bytes > header.uiStoredSize) {
        throw std::runtime_error("STI bitmap is shorter than its dimensions");
    }
    file.seek(frames_offset, wiz8::SeekOrigin::begin);
    std::vector<STCISubImage> frames(frame_count);
    file.read_exact(frames.data(), frame_count * sizeof(STCISubImage));
    std::vector<std::uint8_t> pixels(header.uiStoredSize);
    file.read_exact(pixels.data(), pixels.size());
    if (etrle) {
        return SpriteSheet{palette, decode_sti_frames(frames, pixels)};
    }
    if (compressed) {
        std::vector<std::uint8_t> unpacked(bitmap_bytes);
        uLongf output_size = unpacked.size();
        uLong input_size = pixels.size();
        if (uncompress2(unpacked.data(), &output_size, pixels.data(), &input_size) != Z_OK ||
            output_size != unpacked.size() || input_size != pixels.size()) {
            throw std::runtime_error("Invalid STI zlib payload");
        }
        pixels = std::move(unpacked);
    }
    if (!rgb) {
        auto bitmap = bitmap_from_pixels(header.usWidth, header.usHeight, SDL_PIXELFORMAT_INDEX8, pixels);
        set_palette(*bitmap, palette);
        return bitmap;
    }
    const auto format =
        header.ubDepth == 24
            ? SDL_PIXELFORMAT_RGB24
            : SDL_GetPixelFormatForMasks(16, header.RGB.uiRedMask, header.RGB.uiGreenMask, header.RGB.uiBlueMask, 0);
    if (format == SDL_PIXELFORMAT_UNKNOWN) {
        throw std::runtime_error("Unsupported STI pixel format");
    }
    return bitmap_from_pixels(header.usWidth, header.usHeight, format, pixels);
}
} // namespace

std::optional<DecodedImage> LoadImage(std::string_view filename)
{
    if (filename.empty()) {
        return std::nullopt;
    }
    try {
        std::string path(filename);
        const auto slash = path.find_last_of("/\\");
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
            path += ".PCX";
        }

        const auto format = image_format(path);
        const auto file = wiz8::open_file(path);
        if (format == ImageFormat::sti) {
            return load_sti(*file);
        }
        return load_ordinary_image(*file, format);
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Loading image %.*s: %s", static_cast<int>(filename.size()),
                     filename.data(), error.what());
        return std::nullopt;
    }
}

Palette16 Create16BPPPalette(const Palette8& palette)
{
    Palette16 packed{};
    std::transform(palette.begin(), palette.end(), packed.begin(),
                   [](const SDL_Color& color) { return Get16BPPColor(color); });
    return packed;
}

/**********************************************************************************************
 Create16BPPPaletteShaded

	Creates an 8 bit to 16 bit palette table, and modifies the colors as it builds.

	Parameters:
		rscale, gscale, bscale:
				Color mode: Percentages (255=100%) of color to translate into destination palette.
				Mono mode:  Color for monochrome palette.
		mono:
				TRUE or FALSE to create a monochrome palette. In mono mode, Luminance values for
				colors are calculated, and the RGB color is shaded according to each pixel's brightness.

	This can be used in several ways:

	1) To "brighten" a palette, pass down RGB values that are higher than 100% ( > 255) for all
			three. mono=FALSE.
	2) To "darken" a palette, do the same with less than 100% ( < 255) values. mono=FALSE.

	3) To create a "glow" palette, select mono=TRUE, and pass the color in the RGB parameters.

	4) For gamma correction, pass in weighted values for each color.

**********************************************************************************************/
Palette16 Create16BPPPaletteShaded(const Palette8& palette, std::uint32_t rscale, std::uint32_t gscale,
                                   std::uint32_t bscale, bool mono)
{
    Palette16 packed{};
    std::transform(palette.begin(), palette.end(), packed.begin(), [=](const SDL_Color& color) {
        const std::uint32_t luminance = color.r * 299 / 1000 + color.g * 587 / 1000 + color.b * 114 / 1000;
        const auto red = std::min(rscale * (mono ? luminance : color.r) / 256, 255u);
        const auto green = std::min(gscale * (mono ? luminance : color.g) / 256, 255u);
        const auto blue = std::min(bscale * (mono ? luminance : color.b) / 256, 255u);
        return Get16BPPColor(SDL_Color{static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                                       static_cast<std::uint8_t>(blue), SDL_ALPHA_OPAQUE});
    });
    return packed;
}

// Convert from RGB to 16 bit value
std::uint16_t Get16BPPColor(SDL_Color color)
{
    const auto* details = SDL_GetPixelFormatDetails(g_sgp_pixel_format);
    if (!details) {
        throw std::runtime_error("Unsupported 16-bit pixel format");
    }
    const std::uint16_t pixel =
        SDL_MapRGB(details, nullptr, color.r, color.g, color.b) & (details->Rmask | details->Gmask | details->Bmask);
    if (!pixel) {
        return (color.r || color.g || color.b) ? (kBlackSubstitute | details->Amask) : 0;
    }
    return pixel | details->Amask;
}

// ConvertToPaletteEntry
// Parameter List : Converts from RGB to SGPPaletteEntry
// Return Value  pointer to the SGPPaletteEntry
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry

std::optional<std::uint8_t> SpriteFrame::paletteIndexAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width || y >= height || indices.empty()) {
        return std::nullopt;
    }
    const auto value = indices[std::size_t(y) * width + x];
    return value == kTransparent ? 0 : static_cast<std::uint8_t>(value);
}
