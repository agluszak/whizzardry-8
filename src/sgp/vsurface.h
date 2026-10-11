#ifndef __VSURFACE_H
#define __VSURFACE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <SDL3/SDL_rect.h>
#include <string_view>
#include <memory>

#include "Types.h"
#include "himage.h"
#include "vobject.h"
#include "compat/surfaces.h"

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// Video Surface SGP Module
//
///////////////////////////////////////////////////////////////////////////////////////////////////

//
// Defines for blitting
//

#define VS_BLT_COLORFILL 0x000000020
#define VS_BLT_USECOLORKEY 0x000000002
#define VS_BLT_USEDESTCOLORKEY 0x000000200
#define VS_BLT_FAST 0x000000004
#define VS_BLT_CLIPPED 0x000000008
#define VS_BLT_SRCSUBRECT 0x000000040
#define VS_BLT_COLORFILLRECT 0x000000100
#define VS_BLT_MIRROR_Y 0x000001000

//
// Effects structure for specialized blitting
//

typedef struct {
    COLORVAL ColorFill; // Used for fill effect
    SDL_Rect SrcRect;    // Given SRC subrect instead of srcregion
    SDL_Rect FillRect;   // Given SRC subrect instead of srcregion

} blt_vs_fx;

struct SGPVSurface {
    UINT16 usHeight;  // Height of Video Surface
    UINT16 usWidth;   // Width of Video Surface
    UINT8 ubBitDepth; // 8 or 16 bits per pixel
    std::unique_ptr<CpuSurface> ownedSurface;
    CpuSurface& cpu() { return *ownedSurface; }
    Palette8 palette{};
    bool hasPalette = false;
    Palette16 p16BPPPalette{};
    COLORVAL TransparentColor;

};

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// Renderer surface views
//
///////////////////////////////////////////////////////////////////////////////////////////////////



// The renderer owns the framebuffer and its CPU storage.
SGPVSurface& GetFrameBufferSurface();

BOOLEAN ColorFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, UINT16 Color16BPP);

BOOLEAN ImageFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, SGPVObject& BkgrndImg, UINT16 Index,
                                  INT16 Ox, INT16 Oy);

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// Video Surface manipulation functions
//
///////////////////////////////////////////////////////////////////////////////////////////////////

// Explicit factories for empty CPU surfaces and images from the virtual filesystem.
std::unique_ptr<SGPVSurface> CreateVideoSurface(UINT16 width, UINT16 height, UINT8 bits);
std::unique_ptr<SGPVSurface> LoadVideoSurface(std::string_view filename);

// Set data from HIMAGE.
BOOLEAN SetVideoSurfaceDataFromHImage(SGPVSurface& hVSurface, const image_type& hImage, UINT16 usX, UINT16 usY,
                                      SDL_Rect* pSrcRect);

// Sets the packed source color key
BOOLEAN SetVideoSurfaceTransparencyColor(SGPVSurface& hVSurface, COLORVAL TransColor);

// Sets HVSurface palette, creates if nessessary. Also sets 16BPP palette
BOOLEAN SetVideoSurfacePalette(SGPVSurface& hVSurface, const Palette8& pSrcPalette);

/////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Blt Functions
//
///////////////////////////////////////////////////////////////////////////////////////////////////

// These blitting functions more-or less encapsolate all of the functionality of DirectDraw
// Blitting, giving an API layer for portability.

BOOLEAN BltVideoSurfaceToVideoSurface(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                      INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                                      blt_vs_fx* pBltFx);

BOOLEAN ShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, INT32 X1, INT32 Y1, INT32 X2, INT32 Y2);
// If the Dest Rect and the source rect are not the same size, the source surface will be either
//enlraged or shunk.
std::unique_ptr<SGPVSurface> MakeVSurfaceFromVObject(SGPVObject& object, UINT16 subIndex);


#endif
