#ifndef __IMAGE_H
#define __IMAGE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <memory>
#include <array>
#include <optional>
#include <string_view>
#include <vector>
#include <span>
#include <cstddef>


// The HIMAGE module provides a common interface for managing image data. This module
// includes:
// - A set of data structures representing image data. Data can be 8 or 16 bpp and/or
//   compressed
// - A set of file loaders which load specific file formats into the internal data format
// - A set of blitters which blt the data to memory
// - A comprehensive automatic blitter which blits the appropriate type based on the
//   image header.

// Defines for buffer bit depth
#define BUFFER_8BPP 0x1
#define BUFFER_16BPP 0x2

// Defines for image charactoristics

// Palette structure, mimics that of Win32
typedef struct tagSGPPaletteEntry {
    std::uint8_t peRed;
    std::uint8_t peGreen;
    std::uint8_t peBlue;
    std::uint8_t peFlags;

} SGPPaletteEntry;

#define AUX_FULL_TILE 0x01
#define AUX_ANIMATED_TILE 0x02
#define AUX_DYNAMIC_TILE 0x04
#define AUX_INTERACTIVE_TILE 0x08
#define AUX_IGNORES_HEIGHT 0x10
#define AUX_USES_LAND_Z 0x20

typedef struct {
    std::uint8_t ubWallOrientation;
    std::uint8_t ubNumberOfTiles;
    std::uint16_t usTileLocIndex;
    std::uint8_t ubUnused1[3];
    std::uint8_t ubCurrentFrame;
    std::uint8_t ubNumberOfFrames;
    std::uint8_t fFlags;
    std::uint8_t ubUnused[6];
} AuxObjectData;

typedef struct {
    std::int8_t bTileOffsetX;
    std::int8_t bTileOffsetY;
} RelTileLoc; // relative tile location

// TRLE subimage structure, mirroring that of ST(C)I
typedef struct tagETRLEObject {
    std::uint32_t uiDataOffset;
    std::uint32_t uiDataLength;
    std::int16_t sOffsetX;
    std::int16_t sOffsetY;
    std::uint16_t usHeight;
    std::uint16_t usWidth;
} ETRLEObject;

using Palette8 = std::array<SGPPaletteEntry, 256>;
using Palette16 = std::array<std::uint16_t, 256>;

// Image header structure
struct image_type {
    std::uint16_t usWidth;
    std::uint16_t usHeight;
    std::uint8_t ubBitDepth;
    bool compressed = false;
    bool etrle = false;
    std::optional<Palette8> pPalette;
    std::optional<Palette16> pui16BPPPalette;
    std::vector<std::uint8_t> pImageData;
    std::vector<ETRLEObject> pETRLEObject;

};

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
// Returns no image on failure.
std::optional<image_type> LoadImage(std::string_view filename);

bool CopyImageToBuffer(const image_type& image, std::uint32_t buffer_type, std::span<std::uint8_t> destination,
                       std::size_t dest_width, std::size_t dest_height, std::size_t x, std::size_t y,
                       const SDL_Rect& source_rect);

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
