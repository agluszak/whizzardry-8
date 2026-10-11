#ifndef __VSURFACE_H
#define __VSURFACE_H

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <string_view>
#include <memory>

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

#define VS_BLT_USECOLORKEY 0x000000002
#define VS_BLT_USEDESTCOLORKEY 0x000000200
#define VS_BLT_FAST 0x000000004
#define VS_BLT_MIRROR_Y 0x000001000

//
// Effects structure for specialized blitting
//

struct SGPVSurface {
    std::uint16_t usHeight;  // Height of Video Surface
    std::uint16_t usWidth;   // Width of Video Surface
    std::uint8_t ubBitDepth; // 8 or 16 bits per pixel
    std::unique_ptr<CpuSurface> ownedSurface;
    CpuSurface& cpu() { return *ownedSurface; }
    Palette16 p16BPPPalette{};

};

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// Renderer surface views
//
///////////////////////////////////////////////////////////////////////////////////////////////////



// The renderer owns the framebuffer and its CPU storage.
SGPVSurface& GetFrameBufferSurface();

bool ColorFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, std::int32_t iDestX1, std::int32_t iDestY1,
                                  std::int32_t iDestX2, std::int32_t iDestY2, std::uint16_t Color16BPP);

bool ImageFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, std::int32_t iDestX1, std::int32_t iDestY1,
                                  std::int32_t iDestX2, std::int32_t iDestY2, SGPVObject& BkgrndImg, std::uint16_t Index,
                                  std::int16_t Ox, std::int16_t Oy);

///////////////////////////////////////////////////////////////////////////////////////////////////
//
// Video Surface manipulation functions
//
///////////////////////////////////////////////////////////////////////////////////////////////////

// Explicit factories for empty CPU surfaces and images from the virtual filesystem.
std::unique_ptr<SGPVSurface> CreateVideoSurface(std::uint16_t width, std::uint16_t height, std::uint8_t bits);
std::unique_ptr<SGPVSurface> LoadVideoSurface(std::string_view filename);

/////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Blt Functions
//
///////////////////////////////////////////////////////////////////////////////////////////////////

// These blitting functions more-or less encapsolate all of the functionality of DirectDraw
// Blitting, giving an API layer for portability.

bool BltVideoSurfaceToVideoSurface(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                      std::int32_t iDestX, std::int32_t iDestY, std::int32_t fBltFlags,
                                      const SDL_Rect* sourceRect = nullptr);

bool ShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, std::int32_t X1, std::int32_t Y1, std::int32_t X2, std::int32_t Y2);
// If the Dest Rect and the source rect are not the same size, the source surface will be either
//enlraged or shunk.
std::unique_ptr<SGPVSurface> MakeVSurfaceFromVObject(SGPVObject& object, std::uint16_t subIndex);


#endif
