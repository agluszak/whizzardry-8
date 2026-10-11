/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_log.h>
#include <memory>
#include <string>
#include <string_view>
#include <optional>

#include "Types.h"
#include "himage.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "vsurface.h"

// ******************************************************************************
// Video Object SGP Module
// Video Objects are used to contain any imagery which requires blitting. The data
// is contained within a Direct Draw surface. Palette information is in both
// a Direct Draw Palette and a 16BPP palette structure for 8->16 BPP Blits.
// Blitting is done via Direct Draw as well as custum blitters. Regions are
// used to define local coordinates within the surface
// Second Revision: Dec 10, 1996, Andrew Emmons
// *******************************************************************************

// *******************************************************************************
// Defines
// *******************************************************************************

// *******************************************************************************
// External Functions and variables
// *******************************************************************************

// *******************************************************************************
// LOCAL functions
// *******************************************************************************

// Given indices to the destination and source video objects
// Based on flags, blit accordingly
// There are two types, a BltFast and a Blt. BltFast is 10% faster, uses no
// clipping lists
// FUNCTION: WIZ8 0x00406110
BOOLEAN BltVideoObject(SGPVSurface& uiDestVSurface, SGPVObject& hSrcVObject, UINT16 usRegionIndex,
                       INT32 iDestX, INT32 iDestY, UINT32 fBltFlags, blt_fx* pBltFx)
{
    const auto& storage = *uiDestVSurface.cpu().surface;
    if (!storage.pixels)
        return FALSE;
    return BltVideoObjectToBuffer(static_cast<UINT16*>(storage.pixels), storage.pitch,
                                  hSrcVObject, usRegionIndex, iDestX, iDestY, fBltFlags, pBltFx);
}

// *******************************************************************************
// Video Object Manipulation Functions
// *******************************************************************************

// FUNCTION: WIZ8 0x00406180
std::unique_ptr<SGPVObject> CreateVideoObject(const image_type& image)
{
    if (!image.etrle)
        return nullptr;
    auto owner = std::make_unique<SGPVObject>();
    owner->ubBitDepth = image.ubBitDepth;
    if (image.ubBitDepth == 8) {
        if (!image.pPalette) return nullptr;
        owner->setPalette(*image.pPalette);
    }
    // Decode once; neither the source palette nor compressed bytes are retained.
    if (!DecodeVideoObjectSprites(*owner, image.pETRLEObject, image.pImageData))
        return nullptr;
    return owner;
}

std::unique_ptr<SGPVObject> LoadVideoObject(std::string_view filename)
{
    auto image = LoadImage(filename);
    return image ? CreateVideoObject(*image) : nullptr;
}

void SGPVObject::setPalette(const Palette8& colors)
{
    pPaletteEntry = colors;
    palette = Create16BPPPalette(colors);
    currentShade = 4;
}

/**********************************************************************************************
 CreateObjectPaletteTables

		Creates the shading tables for 8-bit brushes. One highlight table is created, based on
	the object-type, 3 brightening tables, 1 normal, and 11 darkening tables. The entries are
	created iteratively, rather than in a loop to allow hand-tweaking of the values. If you
	change the HVOBJECT_SHADE_TABLES symbol, remember to add/delete entries here, it won't
	adjust automagically.

**********************************************************************************************/

// FUNCTION: WIZ8 0x00406460
UINT16 CreateObjectPaletteTables(SGPVObject& pObj, UINT32 uiType)
{

    // this creates the highlight table. Specify the glow-type when creating the tables
    // through uiType, symbols are from VOBJECT.H
    pObj.pShades.clear();
    pObj.pShades.resize(16);

    switch (uiType) {
    case HVOBJECT_GLOW_GREEN: // green glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 255, 0, TRUE);
        break;
    case HVOBJECT_GLOW_BLUE: // blue glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 0, 255, TRUE);
        break;
    case HVOBJECT_GLOW_YELLOW: // yellow glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 255, 255, 0, TRUE);
        break;
    case HVOBJECT_GLOW_RED: // red glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 255, 0, 0, TRUE);
        break;
    }

    // these are the brightening tables, 115%-150% brighter than original
    pObj.pShades[1] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 293, 293, 293, FALSE);
    pObj.pShades[2] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 281, 281, 281, FALSE);
    pObj.pShades[3] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 268, 268, 268, FALSE);

    // Shade 4 always uses the original palette directly.

    // the rest are darkening tables, right down to all-black.
    pObj.pShades[5] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 195, 195, 195, FALSE);
    pObj.pShades[6] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 165, 165, 165, FALSE);
    pObj.pShades[7] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 135, 135, 135, FALSE);
    pObj.pShades[8] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 105, 105, 105, FALSE);
    pObj.pShades[9] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 75, 75, 75, FALSE);
    pObj.pShades[10] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 45, 45, 45, FALSE);
    pObj.pShades[11] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 36, 36, 36, FALSE);
    pObj.pShades[12] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 27, 27, 27, FALSE);
    pObj.pShades[13] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 18, 18, 18, FALSE);
    pObj.pShades[14] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 9, 9, 9, FALSE);
    pObj.pShades[15] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 0, 0, FALSE);

    pObj.currentShade = 4;
    return TRUE;
}

// *******************************************************************
// Blitting Functions
// *******************************************************************

// High level blit function encapsolates ALL effects and BPP
// FUNCTION: WIZ8 0x004066b0
BOOLEAN BltVideoObjectToBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SGPVObject& hSrcVObject,
                               UINT16 usIndex, INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                               blt_fx* pBltFx)
{

    // Assertions
    Assert(pBuffer != nullptr);

    if (hSrcVObject.ubBitDepth != 8 || usIndex >= hSrcVObject.sprites.size())
        return TRUE;
    const bool mirror = (fBltFlags & VO_BLT_MIRROR_Y) != 0;
    const bool transparent = mirror || (fBltFlags & VO_BLT_SRCTRANSPARENCY);
    if (transparent || (fBltFlags & VO_BLT_SHADOW))
        return BltSpriteTo16BPPBuffer(pBuffer, uiDestPitchBYTES, hSrcVObject.sprites[usIndex],
                                     hSrcVObject.currentPalette().data(), iDestX, iDestY, nullptr, mirror,
                                     transparent ? SpriteDrawOp::Transparent : SpriteDrawOp::Shadow);

    return (TRUE);
}

// FUNCTION: WIZ8 0x004068e0
UINT16 SetObjectShade(SGPVObject& pObj, UINT32 uiShade)
{
    Assert(uiShade >= 0);
    Assert(uiShade < HVOBJECT_SHADE_TABLES);

    if (uiShade != 4 && (uiShade >= pObj.pShades.size() || !pObj.pShades[uiShade])) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Attempt to set shade level to nullptr table");
        return (FALSE);
    }

    pObj.currentShade = uiShade;
    return (TRUE);
}

// FUNCTION: WIZ8 0x00406900
std::optional<UINT8> SpriteFrame::paletteIndexAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= usWidth || y >= usHeight) return std::nullopt;
    const auto value = pixels[y * usWidth + x];
    return value == kTransparent ? 0 : static_cast<UINT8>(value);
}
