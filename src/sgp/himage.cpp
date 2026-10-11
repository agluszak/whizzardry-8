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

#include "Types.h"
#include "wiz8/filesystem.h"
#include "himage.h"
#include "imgfmt.h"

// This is the color substituted to keep a 24bpp -> 16bpp color
// from going transparent (0x0000) -- DB

#define BLACK_SUBSTITUTE 0x0001

// GLOBAL: WIZ8 0x00650f48
UINT16 gusAlphaMask = 0;
// GLOBAL: WIZ8 0x00650f4a
UINT16 gusRedMask = 0;
// GLOBAL: WIZ8 0x00650f4c
UINT16 gusGreenMask = 0;
// GLOBAL: WIZ8 0x00650f4e
UINT16 gusBlueMask = 0;
// GLOBAL: WIZ8 0x00650f50
INT16 gusRedShift = 0;
// GLOBAL: WIZ8 0x00650f52
INT16 gusBlueShift = 0;
// GLOBAL: WIZ8 0x00650f54
INT16 gusGreenShift = 0;

namespace
{
constexpr std::size_t max_image_bytes = 256 * 1024 * 1024;

using Surface = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

enum class ImageFormat { pcx, tga, jpeg, sti };

ImageFormat image_format(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        throw std::runtime_error("Image path has no extension");
    const auto* extension = path.c_str() + dot + 1;
    if (SDL_strcasecmp(extension, "PCX") == 0) return ImageFormat::pcx;
    if (SDL_strcasecmp(extension, "TGA") == 0) return ImageFormat::tga;
    if (SDL_strcasecmp(extension, "STI") == 0) return ImageFormat::sti;
    if (SDL_strcasecmp(extension, "JPG") == 0 || SDL_strcasecmp(extension, "JPEG") == 0)
        return ImageFormat::jpeg;
    throw std::runtime_error("Unsupported image extension");
}


bool safe_dimensions(std::size_t width, std::size_t height)
{
    return width && height && width <= std::numeric_limits<UINT16>::max() &&
           height <= std::numeric_limits<UINT16>::max() &&
           width * height <= max_image_bytes / 4;
}

unsigned little_word(const UINT8* bytes)
{
    return bytes[0] | (unsigned(bytes[1]) << 8);
}

bool valid_image_header(const std::vector<UINT8>& bytes, ImageFormat format)
{
    if (format == ImageFormat::pcx)
    {
        if (bytes.size() < 128 || bytes[0] != 10 || bytes[2] > 1)
            return false;
        const auto left = little_word(bytes.data() + 4), top = little_word(bytes.data() + 6);
        const auto right = little_word(bytes.data() + 8), bottom = little_word(bytes.data() + 10);
        const auto stride = little_word(bytes.data() + 66);
        return right >= left && bottom >= top && right < 32768 && bottom < 32768 &&
               safe_dimensions(right - left + 1, bottom - top + 1) &&
               bytes[3] == 8 && (bytes[65] == 1 || bytes[65] == 3) &&
               stride >= right - left + 1 && stride < 32768;
    }
    if (format == ImageFormat::tga)
    {
        if (bytes.size() < 18)
            return false;
        if ((bytes[2] == 1 || bytes[2] == 9) &&
            little_word(bytes.data() + 3) + little_word(bytes.data() + 5) > 256)
            return false;
        return (bytes[16] == 8 || bytes[16] == 16 || bytes[16] == 24) &&
               safe_dimensions(little_word(bytes.data() + 12), little_word(bytes.data() + 14));
    }
    if (format == ImageFormat::jpeg)
    {
        if (bytes.size() < 2 || bytes[0] != 0xff || bytes[1] != 0xd8)
            return false;
        std::size_t position = 2;
        while (position < bytes.size())
        {
            if (bytes[position++] != 0xff)
                return false;
            while (position < bytes.size() && bytes[position] == 0xff)
                ++position;
            if (position == bytes.size())
                return false;
            const auto marker = bytes[position++];
            if (marker == 0xda || marker == 0xd9)
                return false;
            if (marker == 1 || (marker >= 0xd0 && marker <= 0xd8))
                continue;
            if (bytes.size() - position < 2)
                return false;
            const std::size_t length = (unsigned(bytes[position]) << 8) | bytes[position + 1];
            if (length < 2 || length > bytes.size() - position)
                return false;
            if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc)
            {
                if (length < 8)
                    return false;
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
    if (size <= 0 || std::uint64_t(size) > max_image_bytes)
        throw std::runtime_error("Invalid image file size");
    std::vector<UINT8> bytes(size);
    file.read_exact(bytes.data(), bytes.size());
    if (!valid_image_header(bytes, format))
        throw std::runtime_error("Invalid image header");

    // Give SDL an independent cursor, including for bounded SLF entries.
    std::unique_ptr<SDL_IOStream, decltype(&SDL_CloseIO)> stream(
        SDL_IOFromConstMem(bytes.data(), bytes.size()), SDL_CloseIO);
    if (!stream)
        throw std::runtime_error(SDL_GetError());
    const char* type = format == ImageFormat::pcx ? "PCX" : format == ImageFormat::tga ? "TGA" : "JPG";
    Surface surface(IMG_LoadTyped_IO(stream.get(), false, type), SDL_DestroySurface);
    if (!surface)
        throw std::runtime_error(SDL_GetError());
    if (!safe_dimensions(surface->w, surface->h))
        throw std::runtime_error("Invalid image dimensions");

    const auto* palette = SDL_GetSurfacePalette(surface.get());
    const bool indexed = surface->format == SDL_PIXELFORMAT_INDEX8 && palette;
    const bool rgb555 = surface->format == SDL_PIXELFORMAT_XRGB1555;
    if (!indexed && !rgb555) {
        Surface converted(SDL_ConvertSurface(surface.get(), SDL_PIXELFORMAT_RGB24), SDL_DestroySurface);
        if (!converted)
            throw std::runtime_error(SDL_GetError());
        surface = std::move(converted);
    }
    BitmapImage decoded{};
    decoded.width = surface->w;
    decoded.height = surface->h;
    decoded.bitDepth = indexed ? 8 : rgb555 ? 16 : 24;
    const std::size_t row_bytes = surface->w * (decoded.bitDepth / 8);
    decoded.pixels.resize(row_bytes * surface->h);
    for (int y = 0; y < surface->h; ++y)
        std::copy_n(static_cast<const UINT8*>(surface->pixels) + y * surface->pitch, row_bytes,
                    decoded.pixels.data() + y * row_bytes);
    if (indexed) {
        decoded.palette.emplace();
        const unsigned first_color = format == ImageFormat::tga &&
            (bytes[2] == 1 || bytes[2] == 9) ? little_word(bytes.data() + 3) : 0;
        std::transform(palette->colors, palette->colors + std::min(unsigned(palette->ncolors), 256 - first_color),
            decoded.palette->begin() + first_color, [](const SDL_Color& color) {
                return SGPPaletteEntry{color.r, color.g, color.b, 0};
            });
    }
    return decoded;
}

std::vector<SpriteFrame> decode_sti_frames(std::span<const STCISubImage> frames,
                                         std::span<const UINT8> data)
{
    if (!frames.empty() && data.empty()) {
        throw std::runtime_error("Invalid STI sprite data");
    }
    std::vector<SpriteFrame> decoded;
    decoded.reserve(frames.size());
    constexpr size_t max_pixels = max_image_bytes / sizeof(UINT16);
    size_t remaining_pixels = max_pixels;
    for (const STCISubImage& frame : frames) {
        SpriteFrame& out = decoded.emplace_back();
        out.sOffsetX = frame.sOffsetX;
        out.sOffsetY = frame.sOffsetY;
        out.usWidth = frame.usWidth;
        out.usHeight = frame.usHeight;
        if (frame.uiDataOffset > data.size() ||
            frame.uiDataLength > data.size() - frame.uiDataOffset)
            throw std::runtime_error("Invalid STI sprite data");
        const size_t pixel_count = size_t(frame.usWidth) * frame.usHeight;
        if (pixel_count > remaining_pixels)
            throw std::runtime_error("STI sprites exceed decoded pixel limit");
        remaining_pixels -= pixel_count;
        if (pixel_count == 0)
            continue;
        const UINT8* src = data.data() + frame.uiDataOffset;
        const UINT8* const end = src + frame.uiDataLength;
        out.pixels.assign(static_cast<size_t>(frame.usWidth) * frame.usHeight,
                          SpriteFrame::kTransparent);
        UINT16* row = out.pixels.data();
        for (UINT32 y = 0; y < frame.usHeight; ++y) {
            UINT32 x = 0;
            for (;;) {
                if (src >= end) {
                    throw std::runtime_error("Invalid STI sprite data");
                }
                const UINT8 control = *src++;
                if (control == 0) {
                    break;
                }
                const UINT32 count = control & 0x7f;
                if (count == 0 || count > frame.usWidth - x) {
                    throw std::runtime_error("Invalid STI sprite data");
                }
                if (control & 0x80) {
                    x += count;
                } else {
                    if (count > static_cast<UINT32>(end - src)) {
                        throw std::runtime_error("Invalid STI sprite data");
                    }
                    for (UINT32 n = 0; n < count; ++n)
                        row[x++] = *src++;
                }
            }
            if (x != frame.usWidth) {
                throw std::runtime_error("Invalid STI sprite data");
            }
            row += frame.usWidth;
        }
    }
    return decoded;
}

// FUNCTION: WIZ8 0x00415130
DecodedImage load_sti(wiz8::File& file)
{
    static_assert(sizeof(STCIHeader) == STCI_HEADER_SIZE);
    static_assert(sizeof(STCISubImage) == STCI_SUBIMAGE_SIZE);
    static_assert(sizeof(STCIPaletteElement) == STCI_PALETTE_ELEMENT_SIZE);
    STCIHeader header{};
    file.read_exact(&header, sizeof(header));
    if (std::memcmp(header.cID, STCI_ID_STRING, STCI_ID_LEN) != 0)
        throw std::runtime_error("Invalid STI header");
    const bool rgb = header.fFlags & STCI_RGB;
    if ((!rgb && !(header.fFlags & STCI_INDEXED)) ||
        (rgb && header.ubDepth != 16 && header.ubDepth != 24) || (!rgb && header.ubDepth != 8))
        throw std::runtime_error("Unsupported STI pixel format");
    const bool etrle = !rgb && (header.fFlags & STCI_ETRLE_COMPRESSED);
    const bool compressed = header.fFlags & STCI_ZLIB_COMPRESSED;
    if ((rgb && (header.fFlags & (STCI_INDEXED | STCI_ETRLE_COMPRESSED))) || (etrle && compressed))
        throw std::runtime_error("Conflicting STI encodings");
    const std::size_t bitmap_bytes = std::size_t(header.usWidth) * header.usHeight * (header.ubDepth / 8);
    if (!etrle && (!safe_dimensions(header.usWidth, header.usHeight) ||
                  (compressed && header.uiOriginalSize != bitmap_bytes)))
        throw std::runtime_error("Invalid STI bitmap dimensions or decoded size");
    const std::uint64_t palette_bytes = rgb ? 0 : std::uint64_t(header.Indexed.uiNumberOfColours) * sizeof(STCIPaletteElement);
    const std::size_t frame_count = etrle ? header.Indexed.usNumberOfSubImages : 0;
    const auto frames_offset = sizeof(header) + palette_bytes;
    const auto pixels_offset = frames_offset + frame_count * sizeof(STCISubImage);
    const auto app_offset = pixels_offset + header.uiStoredSize;
    const auto file_size = file.size();
    BitmapImage decoded{};
    decoded.width = header.usWidth;
    decoded.height = header.usHeight;
    decoded.bitDepth = header.ubDepth;

    // FUNCTION: WIZ8 0x004153f0
    if (!rgb) {
        if (header.Indexed.uiNumberOfColours != 256)
            throw std::runtime_error("STI requires a 256-color palette");
        std::array<STCIPaletteElement, 256> palette{};
        file.read_exact(palette.data(), sizeof(palette));
        decoded.palette.emplace();
        std::transform(palette.begin(), palette.end(), decoded.palette->begin(),
            [](const STCIPaletteElement& color) {
                return SGPPaletteEntry{color.ubRed, color.ubGreen, color.ubBlue, 0};
            });
    }
    if (file_size < 0 || app_offset > std::uint64_t(file_size) || header.uiStoredSize > max_image_bytes)
        throw std::runtime_error("STI pixel data exceeds file bounds");
    // Even though the native renderer doesn't use application data, a declared
    // section extending past EOF means this STI is truncated.
    if (header.uiAppDataSize > std::uint64_t(file_size) - app_offset)
        throw std::runtime_error("STI application data exceeds file bounds");
    if (!etrle && !compressed && bitmap_bytes > header.uiStoredSize)
        throw std::runtime_error("STI bitmap is shorter than its dimensions");
    file.seek(frames_offset, wiz8::SeekOrigin::begin);
    std::vector<STCISubImage> frames(frame_count);
    file.read_exact(frames.data(), frame_count * sizeof(STCISubImage));
    decoded.pixels.resize(header.uiStoredSize);
    file.read_exact(decoded.pixels.data(), decoded.pixels.size());
    if (etrle)
        return SpriteSheet{*decoded.palette, decode_sti_frames(frames, decoded.pixels)};
    // FUNCTION: WIZ8 0x0040fca0
    if (compressed) {
        std::vector<UINT8> unpacked(bitmap_bytes);
        uLongf output_size = unpacked.size();
        uLong input_size = decoded.pixels.size();
        if (uncompress2(unpacked.data(), &output_size, decoded.pixels.data(), &input_size) != Z_OK ||
            output_size != unpacked.size() || input_size != decoded.pixels.size())
            throw std::runtime_error("Invalid STI zlib payload");
        decoded.pixels = std::move(unpacked);
    } else {
        decoded.pixels.resize(bitmap_bytes);
    }
    // FUNCTION: WIZ8 0x00415250
    if (rgb && header.ubDepth == 16 &&
        (header.RGB.uiRedMask != gusRedMask || header.RGB.uiGreenMask != gusGreenMask || header.RGB.uiBlueMask != gusBlueMask)) {
        const auto source_format = SDL_GetPixelFormatForMasks(16, header.RGB.uiRedMask, header.RGB.uiGreenMask, header.RGB.uiBlueMask, 0);
        const auto target_format = SDL_GetPixelFormatForMasks(16, gusRedMask, gusGreenMask, gusBlueMask, 0);
        std::vector<UINT8> converted(decoded.pixels.size());
        if (!SDL_ConvertPixels(header.usWidth, header.usHeight, source_format,
            decoded.pixels.data(), header.usWidth * 2, target_format, converted.data(), header.usWidth * 2))
            throw std::runtime_error(SDL_GetError());
        // Zero is the game's transparent pixel, even when opaque colors use an alpha mask.
        if (gusAlphaMask) {
            for (std::size_t i = 0; i < std::size_t(header.usWidth) * header.usHeight * 2; i += 2) {
                UINT16 pixel;
                std::memcpy(&pixel, converted.data() + i, sizeof(pixel));
                if (pixel) pixel |= gusAlphaMask;
                std::memcpy(converted.data() + i, &pixel, sizeof(pixel));
            }
        }
        decoded.pixels = std::move(converted);
    }
    return decoded;
}
}

// FUNCTION: WIZ8 0x0040f850
std::optional<DecodedImage> LoadImage(std::string_view filename)
{
    if (filename.empty())
        return std::nullopt;
    try {
        std::string path(filename);
        const auto slash = path.find_last_of("/\\");
        const auto dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            path += ".PCX";

        const auto format = image_format(path);
        const auto file = wiz8::open_file(path);
        if (format == ImageFormat::sti)
            return load_sti(*file);
        return load_ordinary_image(*file, format);
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Loading image %.*s: %s",
                     static_cast<int>(filename.size()), filename.data(), error.what());
        return std::nullopt;
    }
}

// FUNCTION: WIZ8 0x00410190
Palette16 Create16BPPPalette(const Palette8& palette)
{
    Palette16 packed{};
    std::transform(palette.begin(), palette.end(), packed.begin(), [](const SGPPaletteEntry& color) {
        return Get16BPPColor(UINT32(color.peRed) | (UINT32(color.peGreen) << 8) | (UINT32(color.peBlue) << 16));
    });
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
// FUNCTION: WIZ8 0x004102c0
Palette16 Create16BPPPaletteShaded(const Palette8& palette, UINT32 rscale,
                                                UINT32 gscale, UINT32 bscale, BOOLEAN mono)
{
    Palette16 packed{};
    std::transform(palette.begin(), palette.end(), packed.begin(), [=](const SGPPaletteEntry& color) {
        const UINT32 luminance = color.peRed * 299 / 1000 + color.peGreen * 587 / 1000 + color.peBlue * 114 / 1000;
        const auto red = std::min(rscale * (mono ? luminance : color.peRed) / 256, 255u);
        const auto green = std::min(gscale * (mono ? luminance : color.peGreen) / 256, 255u);
        const auto blue = std::min(bscale * (mono ? luminance : color.peBlue) / 256, 255u);
        return Get16BPPColor(red | (green << 8) | (blue << 16));
    });
    return packed;
}

// Convert from RGB to 16 bit value
// FUNCTION: WIZ8 0x004104b0
UINT16 Get16BPPColor(UINT32 RGBValue)
{
    UINT16 r16, g16, b16, usColor;
    UINT8 r, g, b;

    r = SGPGetRValue(RGBValue);
    g = SGPGetGValue(RGBValue);
    b = SGPGetBValue(RGBValue);

    if (gusRedShift < 0)
        r16 = ((UINT16)r >> abs(gusRedShift));
    else
        r16 = ((UINT16)r << gusRedShift);

    if (gusGreenShift < 0)
        g16 = ((UINT16)g >> abs(gusGreenShift));
    else
        g16 = ((UINT16)g << gusGreenShift);

    if (gusBlueShift < 0)
        b16 = ((UINT16)b >> abs(gusBlueShift));
    else
        b16 = ((UINT16)b << gusBlueShift);

    usColor = (r16 & gusRedMask) | (g16 & gusGreenMask) | (b16 & gusBlueMask);

    // if our color worked out to absolute black, and the original wasn't
    // absolute black, convert it to a VERY dark grey to avoid transparency
    // problems

    if (usColor == 0) {
        if (RGBValue != 0)
            usColor = BLACK_SUBSTITUTE | gusAlphaMask;
    } else
        usColor |= gusAlphaMask;

    return (usColor);
}
// ConvertToPaletteEntry
// Parameter List : Converts from RGB to SGPPaletteEntry
// Return Value  pointer to the SGPPaletteEntry
// Modification History :
// Dec 15th 1996 -> modified for use by Wizardry

// FUNCTION: WIZ8 0x00406900
std::optional<UINT8> SpriteFrame::paletteIndexAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= usWidth || y >= usHeight) return std::nullopt;
    const auto value = pixels[y * usWidth + x];
    return value == kTransparent ? 0 : static_cast<UINT8>(value);
}
