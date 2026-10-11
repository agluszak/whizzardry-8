#ifndef __IMAGE_H
#define __IMAGE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <array>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "Types.h"

// Palette structure, mimics that of Win32
typedef struct tagSGPPaletteEntry {
    UINT8 peRed;
    UINT8 peGreen;
    UINT8 peBlue;
    UINT8 peFlags;

} SGPPaletteEntry;

using Palette8 = std::array<SGPPaletteEntry, 256>;
using Palette16 = std::array<UINT16, 256>;

// Tight, top-down INDEX8, RGB555 or RGB24 pixels.
struct BitmapImage {
    UINT16 width = 0;
    UINT16 height = 0;
    UINT8 bitDepth = 0;
    std::optional<Palette8> palette;
    std::vector<UINT8> pixels;
};

// Palette indices 0-255 or kTransparent; opaque index 0 is not a transparent run.
struct SpriteFrame {
    static constexpr UINT16 kTransparent = 256;

    std::vector<UINT16> pixels;
    std::optional<UINT8> paletteIndexAt(int x, int y) const;
    INT16 sOffsetX = 0;
    INT16 sOffsetY = 0;
    UINT16 usHeight = 0;
    UINT16 usWidth = 0;
};

struct SpriteSheet {
    Palette8 palette{};
    std::vector<SpriteFrame> frames;
};

using DecodedImage = std::variant<BitmapImage, SpriteSheet>;

#define SGPGetRValue(rgb) ((BYTE)(rgb))
#define SGPGetBValue(rgb) ((BYTE)((rgb) >> 16))
#define SGPGetGValue(rgb) ((BYTE)(((UINT16)(rgb)) >> 8))

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
Palette16 Create16BPPPaletteShaded(const Palette8& pPalette, UINT32 rscale, UINT32 gscale,
                                 UINT32 bscale, BOOLEAN mono);
Palette16 Create16BPPPalette(const Palette8& pPalette);
UINT16 Get16BPPColor(UINT32 RGBValue);
extern UINT16 gusAlphaMask;
extern UINT16 gusRedMask;
extern UINT16 gusGreenMask;
extern UINT16 gusBlueMask;
extern INT16 gusRedShift;
extern INT16 gusBlueShift;
extern INT16 gusGreenShift;

#endif
