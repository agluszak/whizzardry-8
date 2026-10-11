#ifndef __IMAGE_H
#define __IMAGE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "Types.h"

// The HIMAGE module provides a common interface for managing image data. This module
// includes:
// - A set of data structures representing image data. Data can be 8 or 16 bpp and/or
//   compressed
// - A set of file loaders which load specific file formats into the internal data format
// - A set of blitters which blt the data to memory
// - A comprehensive automatic blitter which blits the appropriate type based on the
//   image header.

// Palette structure, mimics that of Win32
typedef struct tagSGPPaletteEntry {
    UINT8 peRed;
    UINT8 peGreen;
    UINT8 peBlue;
    UINT8 peFlags;

} SGPPaletteEntry;

// TRLE subimage structure, mirroring that of ST(C)I
typedef struct tagETRLEObject {
    UINT32 uiDataOffset;
    UINT32 uiDataLength;
    INT16 sOffsetX;
    INT16 sOffsetY;
    UINT16 usHeight;
    UINT16 usWidth;
} ETRLEObject;

using Palette8 = std::array<SGPPaletteEntry, 256>;
using Palette16 = std::array<UINT16, 256>;

// Image header structure
struct image_type {
    UINT16 usWidth;
    UINT16 usHeight;
    UINT8 ubBitDepth;
    bool etrle = false;
    std::optional<Palette8> pPalette;
    std::vector<UINT8> pImageData;
    std::vector<ETRLEObject> pETRLEObject;

};

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
// STI bitmaps are also tight pixels: zlib decoding and RGB mask conversion
// finish before a result is returned. ETRLE sheets retain their frame records
// and run data for CreateVideoObject. Returns no image on failure.
std::optional<image_type> LoadImage(std::string_view filename);

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
