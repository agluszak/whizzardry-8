#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <math.h>
#include <stdlib.h>
#include "Types.h"
#include "string.h"
#include "wiz8/filesystem.h"
#include "himage.h"
#include <SDL3_image/SDL_image.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <span>
#include <zlib.h>
#include "WCheck.h"
#include "vobject.h"

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

image_type load_ordinary_image(wiz8::File& file, ImageFormat format, UINT16 contents)
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
    image_type decoded{};
    decoded.usWidth = surface->w;
    decoded.usHeight = surface->h;
    decoded.ubBitDepth = indexed ? 8 : rgb555 ? 16 : 24;
    if (contents & IMAGE_BITMAPDATA) {
        const std::size_t row_bytes = surface->w * (decoded.ubBitDepth / 8);
        decoded.pImageData.resize(row_bytes * surface->h);
        for (int y = 0; y < surface->h; ++y)
            std::copy_n(static_cast<const UINT8*>(surface->pixels) + y * surface->pitch, row_bytes,
                        decoded.pImageData.data() + y * row_bytes);
        decoded.fFlags |= IMAGE_BITMAPDATA;
    }
    if (indexed && (contents & IMAGE_PALETTE)) {
        decoded.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
        const unsigned first_color = format == ImageFormat::tga &&
            (bytes[2] == 1 || bytes[2] == 9) ? little_word(bytes.data() + 3) : 0;
        std::transform(palette->colors, palette->colors + std::min(unsigned(palette->ncolors), 256 - first_color),
            decoded.pPalette.get() + first_color, [](const SDL_Color& color) {
                return SGPPaletteEntry{color.r, color.g, color.b, 0};
            });
        decoded.fFlags |= IMAGE_PALETTE;
    }
    return decoded;
}

// FUNCTION: WIZ8 0x00415130
image_type load_sti(wiz8::File& file, UINT16 contents)
{
    static_assert(sizeof(STCIHeader) == STCI_HEADER_SIZE);
    static_assert(sizeof(ETRLEObject) == STCI_SUBIMAGE_SIZE);
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
    const std::uint64_t palette_bytes = rgb ? 0 : std::uint64_t(header.Indexed.uiNumberOfColours) * sizeof(STCIPaletteElement);
    const std::size_t frame_count = etrle ? header.Indexed.usNumberOfSubImages : 0;
    const auto frames_offset = sizeof(header) + palette_bytes;
    const auto pixels_offset = frames_offset + frame_count * sizeof(ETRLEObject);
    const auto app_offset = pixels_offset + header.uiStoredSize;
    const auto file_size = file.size();
    image_type decoded{};
    decoded.usWidth = header.usWidth;
    decoded.usHeight = header.usHeight;
    decoded.ubBitDepth = header.ubDepth;

    // FUNCTION: WIZ8 0x004153f0
    if (!rgb && (contents & IMAGE_PALETTE)) {
        if (header.Indexed.uiNumberOfColours != 256)
            throw std::runtime_error("STI requires a 256-color palette");
        std::array<STCIPaletteElement, 256> palette{};
        file.read_exact(palette.data(), sizeof(palette));
        decoded.pPalette = std::make_unique<SGPPaletteEntry[]>(256);
        std::transform(palette.begin(), palette.end(), decoded.pPalette.get(),
            [](const STCIPaletteElement& color) {
                return SGPPaletteEntry{color.ubRed, color.ubGreen, color.ubBlue, 0};
            });
        decoded.fFlags |= IMAGE_PALETTE;
    }
    if (contents & IMAGE_BITMAPDATA) {
        if (file_size < 0 || app_offset > std::uint64_t(file_size) || header.uiStoredSize > max_image_bytes)
            throw std::runtime_error("STI pixel data exceeds file bounds");
        if (!etrle && !compressed &&
            std::size_t(header.usWidth) * header.usHeight * (header.ubDepth / 8) > header.uiStoredSize)
            throw std::runtime_error("STI bitmap is shorter than its dimensions");
        file.seek(frames_offset, wiz8::SeekOrigin::begin);
        decoded.pETRLEObject.resize(frame_count);
        file.read_exact(decoded.pETRLEObject.data(), frame_count * sizeof(ETRLEObject));
        decoded.pImageData.resize(header.uiStoredSize);
        file.read_exact(decoded.pImageData.data(), decoded.pImageData.size());
        for (const auto& frame : decoded.pETRLEObject) {
            if (std::uint64_t(frame.uiDataOffset) + frame.uiDataLength > decoded.pImageData.size())
                throw std::runtime_error("STI subimage exceeds pixel buffer bounds");
        }
        decoded.fFlags |= IMAGE_BITMAPDATA;
        if (etrle) decoded.fFlags |= IMAGE_TRLECOMPRESSED;
        if (compressed) decoded.fFlags |= IMAGE_COMPRESSED;

        // FUNCTION: WIZ8 0x00415250
        if (rgb && header.ubDepth == 16 && !compressed &&
            (header.RGB.uiRedMask != gusRedMask || header.RGB.uiGreenMask != gusGreenMask || header.RGB.uiBlueMask != gusBlueMask)) {
            const auto source_format = SDL_GetPixelFormatForMasks(16, header.RGB.uiRedMask, header.RGB.uiGreenMask, header.RGB.uiBlueMask, 0);
            const auto target_format = SDL_GetPixelFormatForMasks(16, gusRedMask, gusGreenMask, gusBlueMask, 0);
            std::vector<UINT8> converted(decoded.pImageData.size());
            if (!SDL_ConvertPixels(header.usWidth, header.usHeight, source_format,
                decoded.pImageData.data(), header.usWidth * 2, target_format, converted.data(), header.usWidth * 2))
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
            decoded.pImageData = std::move(converted);
        }
    }
    if ((contents & IMAGE_APPDATA) && header.uiAppDataSize) {
        if (file_size < 0 || app_offset + header.uiAppDataSize > std::uint64_t(file_size) || header.uiAppDataSize > max_image_bytes)
            throw std::runtime_error("STI application data exceeds file bounds");
        file.seek(app_offset, wiz8::SeekOrigin::begin);
        decoded.pAppData.resize(header.uiAppDataSize);
        file.read_exact(decoded.pAppData.data(), decoded.pAppData.size());
        decoded.fFlags |= IMAGE_APPDATA;
    }
    return decoded;
}
}

// FUNCTION: WIZ8 0x0040f850
std::unique_ptr<image_type> CreateImage(const char* filename, UINT16 contents)
try
{
    if (!filename)
        return nullptr;
    auto image = std::make_unique<image_type>();
    image->ImageFile = filename;
    const auto slash = image->ImageFile.find_last_of("/\\");
    const auto dot = image->ImageFile.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        image->ImageFile += ".PCX";
    if (!LoadImageData(image.get(), contents))
        return nullptr;
    return image;
}
catch (const std::exception&) { return nullptr; }

bool LoadImageData(HIMAGE image, UINT16 contents)
try
{
    if (!image || (contents & ~IMAGE_ALLDATA))
        return false;
    const auto format = image_format(image->ImageFile);
    const auto file = wiz8::open_file(image->ImageFile);
    auto decoded = format == ImageFormat::sti ? load_sti(*file, contents) : load_ordinary_image(*file, format, contents);
    if (decoded.pPalette)
        decoded.pui16BPPPalette = Create16BPPPalette(decoded.pPalette.get());

    // Commit once after decoding. Unrequested buffers retain their owners.
    if (contents & IMAGE_PALETTE) {
        image->pPalette = std::move(decoded.pPalette);
        image->pui16BPPPalette = std::move(decoded.pui16BPPPalette);
    }
    if (contents & IMAGE_BITMAPDATA) {
        image->pImageData = std::move(decoded.pImageData);
        image->pETRLEObject = std::move(decoded.pETRLEObject);
    }
    if (contents & IMAGE_APPDATA)
        image->pAppData = std::move(decoded.pAppData);
    const auto replaced = contents | ((contents & IMAGE_BITMAPDATA) ? IMAGE_COMPRESSED | IMAGE_TRLECOMPRESSED : 0);
    image->fFlags = (image->fFlags & ~replaced) | decoded.fFlags;
    image->usWidth = decoded.usWidth;
    image->usHeight = decoded.usHeight;
    image->ubBitDepth = decoded.ubBitDepth;
    return true;
}
catch (const std::exception& error)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Loading image %s: %s", image->ImageFile.c_str(), error.what());
    return false;
}

// FUNCTION: WIZ8 0x0040fad0
bool CopyImageToBuffer(const image_type& image, UINT32 buffer_type, std::span<UINT8> destination,
                       std::size_t dest_width, std::size_t dest_height, std::size_t x, std::size_t y,
                       const SGPRect& rect)
{
    if (rect.iLeft < 0 || rect.iTop < 0 || rect.iRight <= rect.iLeft || rect.iBottom <= rect.iTop ||
        rect.iRight > image.usWidth || rect.iBottom > image.usHeight || x >= dest_width || y >= dest_height)
        return false;
    const std::size_t width = rect.iRight - rect.iLeft;
    const std::size_t height = rect.iBottom - rect.iTop;
    if (width > dest_width - x || height > dest_height - y || (image.fFlags & IMAGE_TRLECOMPRESSED))
        return false;
    const std::size_t source_depth = image.ubBitDepth / 8;
    const std::size_t target_depth = buffer_type == BUFFER_8BPP ? 1 : buffer_type == BUFFER_16BPP ? 2 : 0;
    if ((image.ubBitDepth != 8 && image.ubBitDepth != 16) || !target_depth || target_depth < source_depth)
        return false;
    if (dest_height > destination.size() / target_depth ||
        dest_width > destination.size() / target_depth / dest_height)
        return false;
    const std::size_t image_bytes = std::size_t(image.usWidth) * image.usHeight * source_depth;
    if (image_bytes > max_image_bytes)
        return false;

    // FUNCTION: WIZ8 0x0040fba0
    // The retail compressed INDEX8-to-INDEX8 path intentionally never copies pixels.
    if ((image.fFlags & IMAGE_COMPRESSED) && target_depth == 1)
        return true;
    if (target_depth != source_depth && !image.pui16BPPPalette)
        return false;

    std::span<const UINT8> source = image.pImageData;
    std::vector<UINT8> unpacked;
    if (image.fFlags & IMAGE_COMPRESSED) {
        if (source_depth != 1 || source.empty())
            return false;
        // FUNCTION: WIZ8 0x0040fca0
        unpacked.resize(image_bytes);
        uLongf output_size = unpacked.size();
        uLong input_size = source.size();
        if (uncompress2(unpacked.data(), &output_size, source.data(), &input_size) != Z_OK ||
            output_size != unpacked.size() || input_size != source.size())
            return false;
        source = unpacked;
    }
    if (source.size() < image_bytes)
        return false;
    // FUNCTION: WIZ8 0x0040fe40
    // FUNCTION: WIZ8 0x0040ff30
    // FUNCTION: WIZ8 0x00410050
    for (std::size_t row = 0; row < height; ++row) {
        const auto input = source.subspan(((rect.iTop + row) * image.usWidth + rect.iLeft) * source_depth,
                                         width * source_depth);
        auto output = destination.subspan(((y + row) * dest_width + x) * target_depth, width * target_depth);
        if (source_depth == target_depth) {
            std::copy(input.begin(), input.end(), output.begin());
        } else {
            for (std::size_t column = 0; column < width; ++column) {
                const UINT16 pixel = image.pui16BPPPalette[input[column]];
                std::memcpy(output.data() + column * sizeof(pixel), &pixel, sizeof(pixel));
            }
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x00410190
std::unique_ptr<UINT16[]> Create16BPPPalette(SGPPaletteEntry* pPalette)
{
    auto p16BPPPalette = std::make_unique<UINT16[]>(256);
    UINT16 r16, g16, b16, usColor;
    UINT32 cnt;
    UINT8 r, g, b;

    Assert(pPalette != nullptr);

    for (cnt = 0; cnt < 256; cnt++) {
        r = pPalette[cnt].peRed;
        g = pPalette[cnt].peGreen;
        b = pPalette[cnt].peBlue;

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

        if (usColor == 0) {
            if ((r + g + b) != 0)
                usColor = BLACK_SUBSTITUTE | gusAlphaMask;
        } else
            usColor |= gusAlphaMask;

        p16BPPPalette[cnt] = usColor;
    }

    return (p16BPPPalette);
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
std::unique_ptr<UINT16[]> Create16BPPPaletteShaded(SGPPaletteEntry* pPalette, UINT32 rscale, UINT32 gscale,
                                 UINT32 bscale, BOOLEAN mono)
{
    auto p16BPPPalette = std::make_unique<UINT16[]>(256);
    UINT16 r16, g16, b16, usColor;
    UINT32 cnt, lumin;
    UINT32 rmod, gmod, bmod;
    UINT8 r, g, b;

    Assert(pPalette != nullptr);

    for (cnt = 0; cnt < 256; cnt++) {
        if (mono) {
            lumin = (pPalette[cnt].peRed * 299 / 1000) + (pPalette[cnt].peGreen * 587 / 1000) +
                    (pPalette[cnt].peBlue * 114 / 1000);
            rmod = (rscale * lumin) / 256;
            gmod = (gscale * lumin) / 256;
            bmod = (bscale * lumin) / 256;
        } else {
            rmod = (rscale * pPalette[cnt].peRed / 256);
            gmod = (gscale * pPalette[cnt].peGreen / 256);
            bmod = (bscale * pPalette[cnt].peBlue / 256);
        }

        r = (UINT8)__min(rmod, 255);
        g = (UINT8)__min(gmod, 255);
        b = (UINT8)__min(bmod, 255);

        if (gusRedShift < 0)
            r16 = ((UINT16)r >> (-gusRedShift));
        else
            r16 = ((UINT16)r << gusRedShift);

        if (gusGreenShift < 0)
            g16 = ((UINT16)g >> (-gusGreenShift));
        else
            g16 = ((UINT16)g << gusGreenShift);

        if (gusBlueShift < 0)
            b16 = ((UINT16)b >> (-gusBlueShift));
        else
            b16 = ((UINT16)b << gusBlueShift);

        // Prevent creation of pure black color
        usColor = (r16 & gusRedMask) | (g16 & gusGreenMask) | (b16 & gusBlueMask);

        if (usColor == 0) {
            if ((r + g + b) != 0)
                usColor = BLACK_SUBSTITUTE | gusAlphaMask;
        } else
            usColor |= gusAlphaMask;

        p16BPPPalette[cnt] = usColor;
    }
    return (p16BPPPalette);
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
