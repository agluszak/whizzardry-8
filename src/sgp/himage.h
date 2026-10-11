#ifndef __IMAGE_H
#define __IMAGE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <array>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "wiz8/compat/surfaces.h"
#include <span>

// Palette structure, mimics that of Win32
typedef struct tagSGPPaletteEntry {
    std::uint8_t peRed;
    std::uint8_t peGreen;
    std::uint8_t peBlue;
    std::uint8_t peFlags;

} SGPPaletteEntry;

using Palette8 = std::array<SGPPaletteEntry, 256>;
using Palette16 = std::array<std::uint16_t, 256>;

// Tight, top-down INDEX8, RGB555 or RGB24 pixels.
struct BitmapImage {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t bitDepth = 0;
    std::optional<Palette8> palette;
    std::vector<std::uint8_t> pixels;
};

// Palette indices 0-255 or kTransparent; opaque index 0 is not a transparent run.
struct SpriteFrame {
    SpriteFrame() = default;
    SpriteFrame(const SpriteFrame& other);
    SpriteFrame& operator=(const SpriteFrame& other);
    SpriteFrame(SpriteFrame&&) noexcept = default;
    SpriteFrame& operator=(SpriteFrame&&) noexcept = default;

    static constexpr std::uint16_t kTransparent = 256;

    SDLSurfaceOwner indices{nullptr, SDL_DestroySurface};
    std::optional<std::uint8_t> paletteIndexAt(int x, int y) const;
    SDL_Surface* coloredSurface(std::span<const std::uint16_t, 256> palette, bool mirror = false) const;
    std::int16_t sOffsetX = 0;
    std::int16_t sOffsetY = 0;
    std::uint16_t usHeight = 0;
    std::uint16_t usWidth = 0;

private:
    mutable SDLSurfaceOwner colored{nullptr, SDL_DestroySurface};
    mutable Palette16 colors{};
    mutable bool mirrored = false;
};

struct SpriteSheet {
    Palette8 palette{};
    std::vector<SpriteFrame> frames;
};

using DecodedImage = std::variant<BitmapImage, SpriteSheet>;

#define SGPGetRValue(rgb) ((std::uint8_t)(rgb))
#define SGPGetBValue(rgb) ((std::uint8_t)((rgb) >> 16))
#define SGPGetGValue(rgb) ((std::uint8_t)(((std::uint16_t)(rgb)) >> 8))

// *****************************************************************************
//
// Function prototypes
//
// *****************************************************************************

// Virtual game paths (including SLF entries), never host paths. The final
// basename extension selects STI or SDL_image PCX/TGA/JPEG; extensionless names
// default to PCX without changing the caller's path. Ordinary images are tight,
// top-down INDEX8 + palettes, packed RGB555 (16-bit TGA), or RGB24.
// STI zlib decoding, RGB mask conversion and ETRLE frame decoding finish
// before a result is returned. Encoded file records never escape the loader.
// Returns no image on failure.
std::optional<DecodedImage> LoadImage(std::string_view filename);

// UTILITY FUNCTIONS

// Used to create a 16BPP Palette from an 8 bit palette, found in himage.c
Palette16 Create16BPPPaletteShaded(const Palette8& pPalette, std::uint32_t rscale, std::uint32_t gscale,
                                 std::uint32_t bscale, bool mono);
Palette16 Create16BPPPalette(const Palette8& pPalette);
std::uint16_t Get16BPPColor(std::uint32_t RGBValue);
extern std::uint16_t gusAlphaMask;
extern std::uint16_t gusRedMask;
extern std::uint16_t gusGreenMask;
extern std::uint16_t gusBlueMask;
extern std::int16_t gusRedShift;
extern std::int16_t gusBlueShift;
extern std::int16_t gusGreenShift;

#endif
