#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstddef>
#include <memory>
#include <string_view>
#include <optional>

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "Types.h"
#include "compat/surfaces.h"
#include "Video2.h"
#include "himage.h"
#include "vsurface.h"
#include "WCheck.h"
#include "vobject.h"
#include "vobject_blitters.h"

extern void SetClippingRect(SDL_Rect* clip);
extern void GetClippingRect(SDL_Rect* clip);
// Video Surface SGP Module
// Second Revision: Dec 10, 1996, Andrew Emmons
// Defines
// LOCAL functions

static BOOLEAN SetVideoSurfacePalette(SGPVSurface& surface, const Palette8& colors);

// Fills an rectangular area with a specified color value.

// FUNCTION: WIZ8 0x00402fa0
BOOLEAN ColorFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, UINT16 Color16BPP)
{
    SDL_Rect clip;
    GetClippingRect(&clip);
    SDL_Rect area{iDestX1, iDestY1, iDestX2 - iDestX1, iDestY2 - iDestY1};
    if (!SDL_GetRectIntersection(&area, &clip, &area))
        return FALSE;
    FillCpuSurface(uiDestVSurface.cpu(), Color16BPP, &area);
    return TRUE;
}
// Fills an rectangular area with a specified image value.

// FUNCTION: WIZ8 0x00403150
BOOLEAN ImageFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, SGPVObject& BkgrndImg, UINT16 Index,
                                  INT16 Ox, INT16 Oy)
{
    INT16 xc, yc, hblits, wblits, aw, pw, ah, ph, w, h, xo, yo;
    SpriteFrame* pTrav;
    SDL_Rect NewClip, OldClip;

    pTrav = &(BkgrndImg.sprites[Index]);
    ph = (INT16)(pTrav->usHeight + pTrav->sOffsetY);
    pw = (INT16)(pTrav->usWidth + pTrav->sOffsetX);

    ah = (INT16)(iDestY2 - iDestY1);
    aw = (INT16)(iDestX2 - iDestX1);

    Ox %= pw;
    Oy %= ph;

    if (Ox > 0)
        Ox -= pw;
    xo = (-Ox) % pw;

    if (Oy > 0)
        Oy -= ph;
    yo = (-Oy) % ph;

    if (Ox < 0)
        xo = (-Ox) % pw;
    else {
        xo = pw - (Ox % pw);
        Ox -= pw;
    }

    if (Oy < 0)
        yo = (-Oy) % ph;
    else {
        yo = ph - (Oy % pw);
        Oy -= ph;
    }

    hblits = ((ah + yo) / ph) + (((ah + yo) % ph) ? 1 : 0);
    wblits = ((aw + xo) / pw) + (((aw + xo) % pw) ? 1 : 0);

    if ((hblits == 0) || (wblits == 0))
        return (FALSE);
    // Clip fill region coords

    GetClippingRect(&OldClip);

    NewClip = {iDestX1, iDestY1, iDestX2 - iDestX1, iDestY2 - iDestY1};
    if (!SDL_GetRectIntersection(&NewClip, &OldClip, &NewClip))
        return (FALSE);

    SetClippingRect(&NewClip);

    yc = (INT16)iDestY1;
    for (h = 0; h < hblits; h++) {
        xc = (INT16)iDestX1;
        for (w = 0; w < wblits; w++) {
            BltVideoObject(uiDestVSurface, BkgrndImg, Index, xc + Ox, yc + Oy,
                           VO_BLT_SRCTRANSPARENCY, nullptr);
            xc += pw;
        }
        yc += ph;
    }

    SetClippingRect(&OldClip);
    return (TRUE);
}
// Video Surface Manipulation Functions

// FUNCTION: WIZ8 0x004033d0
std::unique_ptr<SGPVSurface> CreateVideoSurface(UINT16 width, UINT16 height, UINT8 bits)
{
    CHECKN(width && height && (bits == 8 || bits == 16));
    UINT32 red = 0, green = 0, blue = 0;
    if (bits == 16)
        CHECKN(GetPrimaryRGBDistributionMasks(&red, &green, &blue));
    auto result = std::make_unique<SGPVSurface>();
    result->ownedSurface = CreateCpuSurface(width, height, bits, red, green, blue);
    CHECKN(result->ownedSurface != nullptr);
    result->usWidth = width;
    result->usHeight = height;
    result->ubBitDepth = bits;
    SetVideoSurfaceTransparencyColor(*result, FROMRGB(0, 0, 0));
    return result;
}

std::unique_ptr<SGPVSurface> LoadVideoSurface(std::string_view filename)
{
    auto image = LoadImage(filename);
    if (!image) return nullptr;
    auto result = CreateVideoSurface(image->usWidth, image->usHeight, image->ubBitDepth);
    if (!result) return nullptr;
    if (image->pPalette && !SetVideoSurfacePalette(*result, *image->pPalette))
        return nullptr;

    const auto& storage = *result->cpu().surface;
    const auto bits = result->ubBitDepth;
    const auto format = bits == 8 ? BUFFER_8BPP : BUFFER_16BPP;
    const SDL_Rect bounds{0, 0, result->usWidth, result->usHeight};
    if (!CopyImageToBuffer(*image, format,
            {static_cast<UINT8*>(storage.pixels), std::size_t(storage.pitch) * result->usHeight},
            storage.pitch / (bits / 8), result->usHeight, 0, 0, bounds)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error copying image data to video surface");
        return nullptr;
    }
    return result;
}
// Indexed surfaces use SDL palettes; custom RGB555 blitters retain their table.
static BOOLEAN SetVideoSurfacePalette(SGPVSurface& hVSurface, const Palette8& pSrcPalette)
{
    if (hVSurface.ubBitDepth == 8) {
        SDL_Color colors[256];
        for (unsigned i = 0; i < 256; ++i)
            colors[i] = {pSrcPalette[i].peRed, pSrcPalette[i].peGreen, pSrcPalette[i].peBlue, 255};
        CHECKF(SDL_SetPaletteColors(SDL_GetSurfacePalette(hVSurface.cpu().surface.get()),
                                   colors, 0, 256));
    }
    hVSurface.p16BPPPalette = Create16BPPPalette(pSrcPalette);
    return TRUE;
}

// Transparency uses the game's packed source key.
// colorkey value.
void SetVideoSurfaceTransparencyColor(SGPVSurface& surface, COLORVAL color)
{
    const UINT32 key = surface.ubBitDepth == 16 ? Get16BPPColor(color) : color;
    surface.cpu().sourceKey = SurfaceColorKey{key, key};
}

// FUNCTION: WIZ8 0x00403b10
BOOLEAN BltVideoSurfaceToVideoSurface(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                      INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                                      const SDL_Rect* sourceRect)
{
    SDL_Rect SrcRect;
    UINT16 *pDestSurface16, *pSrcSurface16;
    UINT32 uiSrcPitch, uiDestPitch;

    if (sourceRect) {
        SrcRect = *sourceRect;
    } else {
        CHECKF(hDestVSurface.usHeight >= hSrcVSurface.usHeight);
        CHECKF(hDestVSurface.usWidth >= hSrcVSurface.usWidth);
        SrcRect = {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight};
    }

    // Once here, assert valid Src

    const SDL_Rect bounds{0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight};
    const SDL_Rect destination{iDestX, iDestY, SrcRect.w, SrcRect.h};
    if (!SDL_HasRectIntersection(&destination, &bounds))
        return FALSE;

    CHECKF(hDestVSurface.ubBitDepth == hSrcVSurface.ubBitDepth &&
           (hDestVSurface.ubBitDepth == 8 || hDestVSurface.ubBitDepth == 16));
    if (hDestVSurface.ubBitDepth == 16 && (fBltFlags & VS_BLT_MIRROR_Y)) {
        const auto& source_storage = *hSrcVSurface.cpu().surface;
        uiSrcPitch = source_storage.pitch;
        pSrcSurface16 = static_cast<UINT16*>(source_storage.pixels);
        CHECKF(pSrcSurface16);
        const auto& destination_storage = *hDestVSurface.cpu().surface;
        uiDestPitch = destination_storage.pitch;
        pDestSurface16 = static_cast<UINT16*>(destination_storage.pixels);
        if (!pDestSurface16) {
            return FALSE;
        }
        Blt16BPPTo16BPPMirror(pDestSurface16, uiDestPitch, pSrcSurface16, uiSrcPitch,
                             iDestX, iDestY, SrcRect.x, SrcRect.y, SrcRect.w, SrcRect.h);
        return TRUE;
    }
    SDL_Rect clippedDestination = destination;
    if (fBltFlags & VS_BLT_FAST) {
        CHECKF(iDestX >= 0 && iDestY >= 0);
        CHECKF(!hDestVSurface.cpu().clipRegions);
        CHECKF((destination.x + destination.w) <= hDestVSurface.usWidth &&
               (destination.y + destination.h) <= hDestVSurface.usHeight);
    } else if (!ClipCpuBlit(clippedDestination, SrcRect,
                            {0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight},
                            {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight})) {
        return TRUE;
    }
    if (clippedDestination.w == 0 || clippedDestination.h == 0)
        return TRUE;
    BlitCpuSurface(hDestVSurface.cpu(), &clippedDestination, hSrcVSurface.cpu(), &SrcRect,
                   fBltFlags & VS_BLT_USECOLORKEY, fBltFlags & VS_BLT_USEDESTCOLORKEY);
    return TRUE;
}

BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               SDL_Rect* area);

// FUNCTION: WIZ8 0x004045b0
BOOLEAN InternalShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, INT32 X1, INT32 Y1, INT32 X2,
                                       INT32 Y2, BOOLEAN fLowPercentShadeTable)
{
    UINT16* pBuffer;
    UINT32 uiPitch;
    SDL_Rect area;
    SGPVSurface& hVSurface = uiDestVSurface;

    // CLIP IT!
    // FIRST GET SURFACE
    // Get Video Surface


    // The shadow blitter adds the final pixel on each axis; keep its inclusive
    // endpoint convention while intersecting the edge-based area here.
    const SDL_Rect bounds{0, 0, hVSurface.usWidth - 1, hVSurface.usHeight - 1};
    area = {X1, Y1, X2 - X1, Y2 - Y1};
    if (!SDL_GetRectIntersection(&area, &bounds, &area))
        return FALSE;

    const auto& destination_storage = *uiDestVSurface.cpu().surface;
    uiPitch = destination_storage.pitch;
    pBuffer = static_cast<UINT16*>(destination_storage.pixels);


    if (pBuffer == nullptr) {
        return (FALSE);
    }

    const BOOLEAN result = fLowPercentShadeTable
        ? Blt16BPPBufferShadowRectAlternateTable(pBuffer, uiPitch, &area)
        : Blt16BPPBufferShadowRect(pBuffer, uiPitch, &area);
    return result;
}

// FUNCTION: WIZ8 0x004048a0
BOOLEAN ShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, INT32 X1, INT32 Y1, INT32 X2, INT32 Y2)
{
    return (InternalShadowVideoSurfaceRect(uiDestVSurface, X1, Y1, X2, Y2, FALSE));
}

// FUNCTION: WIZ8 0x004048d0
std::unique_ptr<SGPVSurface> MakeVSurfaceFromVObject(SGPVObject& object, UINT16 subIndex)
{
    if (subIndex >= object.sprites.size())
        return nullptr;
    auto surface = CreateVideoSurface(object.sprites[subIndex].usWidth,
                                      object.sprites[subIndex].usHeight, PIXEL_DEPTH);
    if (!surface || !BltVideoObject(*surface, object, subIndex, 0, 0,
                                    VO_BLT_SRCTRANSPARENCY, nullptr))
        return nullptr;
    return surface;
}
