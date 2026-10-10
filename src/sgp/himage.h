/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __IMAGE_H
#define __IMAGE_H

#include "Types.h"
#include <memory>
#include <string>
#include <vector>
#include <span>
#include "imgfmt.h"

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
#define IMAGE_COMPRESSED 0x0001
#define IMAGE_TRLECOMPRESSED 0x0002
#define IMAGE_PALETTE 0x0004
#define IMAGE_BITMAPDATA 0x0008
#define IMAGE_APPDATA 0x0010
#define IMAGE_ALLIMAGEDATA 0x000C
#define IMAGE_ALLDATA 0x001C

// Palette structure, mimics that of Win32
typedef struct tagSGPPaletteEntry {
    UINT8 peRed;
    UINT8 peGreen;
    UINT8 peBlue;
    UINT8 peFlags;

} SGPPaletteEntry;

#define AUX_FULL_TILE 0x01
#define AUX_ANIMATED_TILE 0x02
#define AUX_DYNAMIC_TILE 0x04
#define AUX_INTERACTIVE_TILE 0x08
#define AUX_IGNORES_HEIGHT 0x10
#define AUX_USES_LAND_Z 0x20

typedef struct {
    UINT8 ubWallOrientation;
    UINT8 ubNumberOfTiles;
    UINT16 usTileLocIndex;
    UINT8 ubUnused1[3];
    UINT8 ubCurrentFrame;
    UINT8 ubNumberOfFrames;
    UINT8 fFlags;
    UINT8 ubUnused[6];
} AuxObjectData;

typedef struct {
    INT8 bTileOffsetX;
    INT8 bTileOffsetY;
} RelTileLoc; // relative tile location

// TRLE subimage structure, mirroring that of ST(C)I
typedef struct tagETRLEObject {
    UINT32 uiDataOffset;
    UINT32 uiDataLength;
    INT16 sOffsetX;
    INT16 sOffsetY;
    UINT16 usHeight;
    UINT16 usWidth;
} ETRLEObject;

// Image header structure
typedef struct {
    UINT16 usWidth;
    UINT16 usHeight;
    UINT8 ubBitDepth;
    UINT16 fFlags;
    std::string ImageFile;
    std::unique_ptr<SGPPaletteEntry[]> pPalette;
    std::unique_ptr<UINT16[]> pui16BPPPalette;
    std::vector<UINT8> pAppData;
    std::vector<UINT8> pImageData;
    std::vector<ETRLEObject> pETRLEObject;

} image_type, *HIMAGE;

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
// Returns NULL on failure without retaining image allocations.
std::unique_ptr<image_type> CreateImage(const char* ImageFile, UINT16 fContents);

// This function will attept to Load data from an existing image object's filename
// In this way, dynamic loading of image data can be done
bool LoadImageData(HIMAGE hImage, UINT16 fContents);

bool CopyImageToBuffer(const image_type& image, UINT32 buffer_type, std::span<UINT8> destination,
                       std::size_t dest_width, std::size_t dest_height, std::size_t x, std::size_t y,
                       const SGPRect& source_rect);

// UTILITY FUNCTIONS

// Used to create a 16BPP Palette from an 8 bit palette, found in himage.c
std::unique_ptr<UINT16[]> Create16BPPPaletteShaded(const SGPPaletteEntry* pPalette, UINT32 rscale, UINT32 gscale,
                                 UINT32 bscale, BOOLEAN mono);
std::unique_ptr<UINT16[]> Create16BPPPalette(const SGPPaletteEntry* pPalette);
UINT16 Get16BPPColor(UINT32 RGBValue);
extern UINT16 gusAlphaMask;
extern UINT16 gusRedMask;
extern UINT16 gusGreenMask;
extern UINT16 gusBlueMask;
extern INT16 gusRedShift;
extern INT16 gusBlueShift;
extern INT16 gusGreenShift;

#endif
