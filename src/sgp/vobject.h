#ifndef __VOBJECT_H
#define __VOBJECT_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_rect.h>
#include <string>
#include <array>
#include <vector>
#include <memory>

#include "Types.h"
#include "himage.h"

// ************************************************************************************
//
// Video Object SGP Module
//
// ************************************************************************************

// This definition mimics what is found in WINDOWS.H ( for Direct Draw compatiblity )
typedef UINT32 COLORVAL;

// Defines for blitting
#define VO_BLT_SRCTRANSPARENCY 0x000000002
#define VO_BLT_DESTTRANSPARENCY 0x000000120
#define VO_BLT_SHADOW 0x000000200
#define VO_BLT_TRANSSHADOW 0x000000003
#define VO_BLT_UNCOMPRESSED 0x000004000
#define VO_BLT_CLIP 0x000000001
#define VO_BLT_MIRROR_Y 0x000001000 // must be the same as VS_BLT_MIRROR_Y for Wiz!!!

// Defines for SGPVObject& limits
#define HVOBJECT_SHADE_TABLES 48

#define HVOBJECT_GLOW_GREEN 0
#define HVOBJECT_GLOW_BLUE 1
#define HVOBJECT_GLOW_YELLOW 2
#define HVOBJECT_GLOW_RED 3

// Effects structure for specialized blitting
typedef struct {
    UINT32 uiShadowLevel;
    SDL_Rect ClipRect;

} blt_fx;

// Z-buffer info structure for properly assigning Z values
typedef struct {
    INT8 bInitialZChange;     // difference in Z value between the leftmost and base strips
    UINT8 ubFirstZStripWidth; // # of pixels in the leftmost strip
    UINT8 ubNumberOfZChanges; // number of strips (after the first)
    std::unique_ptr<INT8[]> pbZChange; // change to the Z value in each strip (after the first)
} ZStripInfo;

typedef struct {
    std::unique_ptr<UINT16[]> p16BPPData;
    UINT16 usRegionIndex;
    UINT8 ubShadeLevel;
    UINT16 usWidth;
    UINT16 usHeight;
    INT16 sOffsetX;
    INT16 sOffsetY;
} SixteenBPPObjectInfo;

// This definition mimics what is found in WINDOWS.H ( for Direct Draw compatiblity )
// From RGB to COLORVAL
#define FROMRGB(r, g, b)                                                                           \
    ((UINT32)(((UINT8)(r) | ((UINT16)(g) << 8)) | (((UINT32)(UINT8)(b)) << 16)))

// Video object creation flags
// Used in the VOBJECT_DESC structure to describe creation flags

#define VOBJECT_CREATE_DEFAULT 0x00000020 // Creates and empty object of given width, height and BPP
#define VOBJECT_CREATE_FROMFILE 0x00000040   // Creates a video object from a file ( using HIMAGE )
#define VOBJECT_CREATE_FROMHIMAGE 0x00000080 // Creates a video object from a pre-loaded hImage

// VOBJECT FLAGS
#define VOBJECT_FLAG_SHADETABLE_SHARED 0x00000100

// This structure is a video object.
// One decoded sprite frame: pixels are palette indices 0-255 or
// kTransparent for ETRLE transparent runs, so opaque palette index 0
// stays distinguishable without a separate mask. Placement metadata
// travels with the pixels; the compressed source is never retained.
struct SpriteFrame {
    static constexpr UINT16 kTransparent = 256;

    std::vector<UINT16> pixels;
    INT16 sOffsetX = 0;
    INT16 sOffsetY = 0;
    UINT16 usHeight = 0;
    UINT16 usWidth = 0;
};

// The video object contains different data based on it's type, compressed or not
struct SGPVObject {
    UINT32 fFlags;                  // Special flags
    std::unique_ptr<SGPPaletteEntry[]> pPaletteEntry; // 8BPP Palette
    COLORVAL TransparentColor;      // Defaults to 0,0,0
    std::shared_ptr<UINT16[]> ownedPalette;
    UINT16* p16BPPPalette; // Borrowed palette view; font drawing may temporarily override it.

    std::vector<SpriteFrame> sprites;   // Decoded frames, one per ETRLE subimage
    std::vector<SixteenBPPObjectInfo> p16BPPObject;
    std::array<std::shared_ptr<UINT16[]>, HVOBJECT_SHADE_TABLES> pShades; // Shading tables
    UINT16* pShadeCurrent;
    UINT16* pGlow;             // glow highlight table
    std::vector<std::unique_ptr<ZStripInfo>> ppZStripInfo; // Z-value strip info arrays

    UINT8 ubBitDepth;         // BPP

};

// This structure describes the creation parameters for a Video Object
struct VOBJECT_DESC {
    UINT32 fCreateFlags{}; // Specifies creation flags like from file or not
    std::string ImageFile;
    HIMAGE hImage = nullptr;
    UINT8 ubBitDepth{}; // BPP, ignored if given from file
};

// **********************************************************************************
//
// Video Object Manager Functions
//
// **********************************************************************************


UINT16 CreateObjectPaletteTables(SGPVObject& pObj, UINT32 uiType);

struct SGPVSurface;

// Blits a video object to a surface
struct SGPVSurface;
BOOLEAN BltVideoObject(SGPVSurface& uiDestVSurface, SGPVObject& hVSrcObject, UINT16 usRegionIndex,
                       INT32 iDestX, INT32 iDestY, UINT32 fBltFlags, blt_fx* pBltFx);


// Sets transparency

// **********************************************************************************
//
// Video Object manipulation functions
//
// **********************************************************************************

// Created from a VOBJECT_DESC structure. Can be from a file via HIMAGE or empty.
std::unique_ptr<SGPVObject> CreateVideoObject(const VOBJECT_DESC& VObjectDesc);

// Sets the object transparency color
BOOLEAN SetVideoObjectTransparencyColor(SGPVObject& hVObject, COLORVAL TransColor);

// Sets SGPVObject& palette, creates if nessessary. Also sets 16BPP palette
BOOLEAN SetVideoObjectPalette(SGPVObject& hVObject, SGPPaletteEntry* pSrcPalette);


// Deletes the 16-bit palette tables
BOOLEAN DestroyObjectPaletteTables(SGPVObject& hVObject);

// Sets the current object shade table
UINT16 SetObjectShade(SGPVObject& pObj, UINT32 uiShade);


// Retrieves an SGPVObject& pixel value
BOOLEAN GetETRLEPixelValue(UINT8* pDest, SGPVObject& hVObject, UINT16 usETLREIndex, UINT16 usX,
                           UINT16 usY);

// ****************************************************************************
//
// Globals
//
// ****************************************************************************

// ****************************************************************************
//
// Macros
//
// ****************************************************************************


// ****************************************************************************
//
// Blt Functions
//
// ****************************************************************************

// These blitting functions more-or less encapsolate all of the functionality of DirectDraw
// Blitting, giving an API layer for portability.

BOOLEAN BltVideoObjectToBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SGPVObject& hSrcVObject,
                               UINT16 usIndex, INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                               blt_fx* pBltFx);


BOOLEAN GetVideoObjectETRLESubregionProperties(SGPVObject& hVObject, UINT16 usIndex,
                                               UINT16* pusWidth, UINT16* pusHeight);

BOOLEAN CopyVideoObjectPalette16BPP(SGPVObject& hVObject, UINT16* ppPal16);


#endif
