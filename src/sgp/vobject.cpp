/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <cstdint>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <span>
#include <memory>
#include <string_view>
#include <optional>
#include <utility>

#include "Types.h"
#include "compat/surfaces.h"
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
bool BltVideoObject(SGPVSurface& uiDestVSurface, SGPVObject& hSrcVObject, std::uint16_t usRegionIndex,
                       std::int32_t iDestX, std::int32_t iDestY, std::uint32_t fBltFlags, blt_fx* pBltFx)
{
    const auto& storage = *uiDestVSurface.cpu().surface;
    if (!storage.pixels)
        return false;
    return BltVideoObjectToBuffer(static_cast<std::uint16_t*>(storage.pixels), storage.pitch,
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
std::uint16_t CreateObjectPaletteTables(SGPVObject& pObj, std::uint32_t uiType)
{

    // this creates the highlight table. Specify the glow-type when creating the tables
    // through uiType, symbols are from VOBJECT.H
    pObj.pShades.clear();
    pObj.pShades.resize(16);

    switch (uiType) {
    case HVOBJECT_GLOW_GREEN: // green glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 255, 0, true);
        break;
    case HVOBJECT_GLOW_BLUE: // blue glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 0, 255, true);
        break;
    case HVOBJECT_GLOW_YELLOW: // yellow glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 255, 255, 0, true);
        break;
    case HVOBJECT_GLOW_RED: // red glow
        pObj.pShades[0] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 255, 0, 0, true);
        break;
    }

    // these are the brightening tables, 115%-150% brighter than original
    pObj.pShades[1] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 293, 293, 293, false);
    pObj.pShades[2] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 281, 281, 281, false);
    pObj.pShades[3] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 268, 268, 268, false);

    // Shade 4 always uses the original palette directly.

    // the rest are darkening tables, right down to all-black.
    pObj.pShades[5] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 195, 195, 195, false);
    pObj.pShades[6] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 165, 165, 165, false);
    pObj.pShades[7] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 135, 135, 135, false);
    pObj.pShades[8] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 105, 105, 105, false);
    pObj.pShades[9] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 75, 75, 75, false);
    pObj.pShades[10] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 45, 45, 45, false);
    pObj.pShades[11] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 36, 36, 36, false);
    pObj.pShades[12] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 27, 27, 27, false);
    pObj.pShades[13] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 18, 18, 18, false);
    pObj.pShades[14] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 9, 9, 9, false);
    pObj.pShades[15] = Create16BPPPaletteShaded(pObj.pPaletteEntry, 0, 0, 0, false);

    pObj.currentShade = 4;
    return true;
}

// *******************************************************************
// Blitting Functions
// *******************************************************************

// High level blit function encapsolates ALL effects and BPP
// FUNCTION: WIZ8 0x004066b0
bool BltVideoObjectToBuffer(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SGPVObject& hSrcVObject,
                               std::uint16_t usIndex, std::int32_t iDestX, std::int32_t iDestY, std::int32_t fBltFlags,
                               blt_fx* pBltFx)
{

    // Assertions
    Assert(pBuffer != nullptr);

    if (hSrcVObject.ubBitDepth != 8 || usIndex >= hSrcVObject.sprites.size())
        return true;
    const bool mirror = (fBltFlags & VO_BLT_MIRROR_Y) != 0;
    const bool transparent = mirror || (fBltFlags & VO_BLT_SRCTRANSPARENCY);
    if (transparent || (fBltFlags & VO_BLT_SHADOW))
        return BltSpriteTo16BPPBuffer(pBuffer, uiDestPitchBYTES, hSrcVObject.sprites[usIndex],
                                     hSrcVObject.currentPalette().data(), iDestX, iDestY, nullptr, mirror,
                                     transparent ? SpriteDrawOp::Transparent : SpriteDrawOp::Shadow);

    return (true);
}

// FUNCTION: WIZ8 0x004068e0
bool SetObjectShade(SGPVObject& pObj, std::uint32_t uiShade)
{
    Assert(uiShade >= 0);
    Assert(uiShade < HVOBJECT_SHADE_TABLES);

    if (uiShade != 4 && (uiShade >= pObj.pShades.size() || !pObj.pShades[uiShade])) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Attempt to set shade level to nullptr table");
        return (false);
    }

    pObj.currentShade = uiShade;
    return (true);
}

// FUNCTION: WIZ8 0x00406900
std::optional<std::uint8_t> SpriteFrame::paletteIndexAt(int x, int y) const
{
    if (x < 0 || y < 0 || x >= usWidth || y >= usHeight) return std::nullopt;
    if (!indices) return std::nullopt;
    const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(indices->pixels) + y * indices->pitch);
    const auto value = row[x];
    return value == kTransparent ? 0 : static_cast<std::uint8_t>(value);
}

SDL_Surface* SpriteFrame::coloredSurface(std::span<const std::uint16_t, 256> palette, bool mirror) const
{
    if (!indices) return nullptr;
    if (!colored || !std::equal(colors.begin(), colors.end(), palette.begin())) {
        auto surface = CreateCpuStorage(usWidth, usHeight, SDL_PIXELFORMAT_RGB565);
        if (!surface) return nullptr;
        // RGB555 is stored as raw words, not converted to RGB565. A key outside
        // the whole LUT preserves opaque index zero and every possible shade.
        std::uint16_t key = 0;
        while (std::find(palette.begin(), palette.end(), key) != palette.end()) ++key;
        for (int y = 0; y < usHeight; ++y) {
            const auto* source = reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(indices->pixels) + y * indices->pitch);
            auto* destination = reinterpret_cast<std::uint16_t*>(static_cast<std::uint8_t*>(surface->pixels) + y * surface->pitch);
            for (int x = 0; x < usWidth; ++x)
                destination[x] = source[x] == kTransparent ? key : palette[source[x]];
        }
        if (!SDL_SetSurfaceColorKey(surface.get(), true, key) || !SDL_SetSurfaceRLE(surface.get(), true))
            return nullptr;
        colored = std::move(surface);
        std::copy(palette.begin(), palette.end(), colors.begin());
        mirrored = false;
    }
    if (mirror != mirrored) {
        if (!SDL_LockSurface(colored.get())) return nullptr;
        const bool flipped = SDL_FlipSurface(colored.get(), SDL_FLIP_HORIZONTAL);
        SDL_UnlockSurface(colored.get());
        if (!flipped) return nullptr;
        mirrored = mirror;
    }
    return colored.get();
}
