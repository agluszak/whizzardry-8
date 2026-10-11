#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

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

BOOLEAN ClipReleatedSrcAndDestRectangles(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                         SDL_Rect* DestRect, SDL_Rect* SrcRect);
BOOLEAN FillSurface(SGPVSurface& hDestVSurface, blt_vs_fx* pBltFx);
BOOLEAN FillSurfaceRect(SGPVSurface& hDestVSurface, blt_vs_fx* pBltFx);
BOOLEAN BltVSurfaceUsingSDL(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface, UINT32 fBltFlags,
                           INT32 iDestX, INT32 iDestY, SDL_Rect* SrcRect);

// Fills an rectangular area with a specified color value.

// FUNCTION: WIZ8 0x00402fa0
BOOLEAN ColorFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, UINT16 Color16BPP)
{
    blt_vs_fx BltFx;
    SGPVSurface& hDestVSurface = uiDestVSurface;
    SDL_Rect Clip;


    GetClippingRect(&Clip);
    BltFx.FillRect = {iDestX1, iDestY1, iDestX2 - iDestX1, iDestY2 - iDestY1};
    if (!SDL_GetRectIntersection(&BltFx.FillRect, &Clip, &BltFx.FillRect))
        return FALSE;
    BltFx.ColorFill = Color16BPP;
    return FillSurfaceRect(hDestVSurface, &BltFx);
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
std::unique_ptr<SGPVSurface> CreateVideoSurface(const VSURFACE_DESC& VSurfaceDesc)
{
    std::unique_ptr<image_type> image;
    UINT16 width = VSurfaceDesc.usWidth;
    UINT16 height = VSurfaceDesc.usHeight;
    UINT8 bits = VSurfaceDesc.ubBitDepth;
    if (VSurfaceDesc.fCreateFlags & VSURFACE_CREATE_FROMFILE) {
        image = CreateImage(VSurfaceDesc.ImageFile.c_str(), IMAGE_ALLIMAGEDATA);
        CHECKN(image != nullptr);
        width = image->usWidth;
        height = image->usHeight;
        bits = image->ubBitDepth;
    }
    CHECKN(width && height && (bits == 8 || bits == 16));
    UINT32 red = 0, green = 0, blue = 0;
    if (bits == 16)
        CHECKN(GetPrimaryRGBDistributionMasks(&red, &green, &blue));
    auto result = std::make_unique<SGPVSurface>();
    result->ownedSurface = CreateCpuSurface(width, height, bits, red, green, blue);
    result->usWidth = width;
    result->usHeight = height;
    result->ubBitDepth = bits;
    result->fFlags = VSURFACE_SYSTEM_MEM_USAGE;
    result->TransparentColor = FROMRGB(0, 0, 0);
    if (image) {
        if (image->fFlags & IMAGE_PALETTE)
            SetVideoSurfacePalette(*result, image->pPalette.get());
        SDL_Rect region{0, 0, width, height};
        SetVideoSurfaceDataFromHImage(*result, *image, 0, 0, &region);
    }
    SetVideoSurfaceTransparencyColor(*result, result->TransparentColor);

    return result;
}
// Lock must be followed by release
// Pitch MUST be used for all width calculations ( Pitch is in bytes )
// The time between Locking and unlocking must be minimal
BYTE* LockVideoSurfaceBuffer(SGPVSurface& hVSurface, UINT32* pPitch)
{
    Assert(pPitch != nullptr);
    const auto lock = LockCpuSurface(hVSurface.cpu());
    *pPitch = lock.pitch;
    return static_cast<BYTE*>(lock.pixels);
}

void UnLockVideoSurfaceBuffer(SGPVSurface& hVSurface)
{
    UnlockCpuSurface(hVSurface.cpu());
}

// Given an HIMAGE object, blit imagery into existing Video Surface. Can be from 8->16 BPP
// FUNCTION: WIZ8 0x00403780
BOOLEAN SetVideoSurfaceDataFromHImage(SGPVSurface& hVSurface, const image_type& hImage, UINT16 usX, UINT16 usY,
                                      SDL_Rect* pSrcRect)
{
    BYTE* pDest;
    UINT32 fBufferBPP = 0;
    UINT32 uiPitch;
    std::size_t usEffectiveWidth;
    SDL_Rect aRect;

    // Assertions

    // Get Size of hImage and determine if it can fit
    CHECKF(hImage.usWidth >= hVSurface.usWidth);
    CHECKF(hImage.usHeight >= hVSurface.usHeight);

    // Check BPP and see if they are the same
    if (hImage.ubBitDepth != hVSurface.ubBitDepth) {
        // They are not the same, but we can go from 8->16 without much cost
        if (hImage.ubBitDepth == 8 && hVSurface.ubBitDepth == 16) {
            fBufferBPP = BUFFER_16BPP;
        }
    } else {
        // Set buffer BPP
        switch (hImage.ubBitDepth) {
        case 8:

            fBufferBPP = BUFFER_8BPP;
            break;

        case 16:

            fBufferBPP = BUFFER_16BPP;
            break;
        }
    }

    Assert(fBufferBPP != 0);

    // Get surface buffer data
    pDest = LockVideoSurfaceBuffer(hVSurface, &uiPitch);

    // Effective width ( in PIXELS ) is Pitch ( in bytes ) converted to pitch ( IN PIXELS )
    usEffectiveWidth = uiPitch / (hVSurface.ubBitDepth / 8);

    CHECKF(pDest != nullptr);

    // Blit Surface
    // If rect is NULL, use entrie image size
    aRect = pSrcRect ? *pSrcRect : SDL_Rect{0, 0, hImage.usWidth, hImage.usHeight};

    // This HIMAGE function will transparently copy buffer
    if (!CopyImageToBuffer(hImage, fBufferBPP, {pDest, std::size_t(uiPitch) * hVSurface.usHeight},
                           usEffectiveWidth, hVSurface.usHeight, usX, usY, aRect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error copying image data to video surface");
        UnLockVideoSurfaceBuffer(hVSurface);
        return (FALSE);
    }

    // All is OK
    UnLockVideoSurfaceBuffer(hVSurface);

    return (TRUE);
}

// Indexed surfaces use SDL palettes; custom RGB555 blitters retain their table.
BOOLEAN SetVideoSurfacePalette(SGPVSurface& hVSurface, SGPPaletteEntry* pSrcPalette)
{
    CHECKF(pSrcPalette);
    std::copy_n(pSrcPalette, 256, hVSurface.palette.begin());
    hVSurface.hasPalette = true;
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
BOOLEAN SetVideoSurfaceTransparencyColor(SGPVSurface& hVSurface, COLORVAL TransColor)
{
    hVSurface.TransparentColor = TransColor;
    const UINT32 key = hVSurface.ubBitDepth == 16 ? Get16BPPColor(TransColor) : TransColor;
    hVSurface.cpu().sourceKey = SurfaceColorKey{key, key};
    return TRUE;
}

// ********************************************************
// Clipper manipulation functions
// ********************************************************

BOOLEAN SetClipList(SGPVSurface& hVSurface, SDL_Rect* RegionData, UINT16 usNumRegions)
{
    CHECKF(RegionData && usNumRegions);
    SetSurfaceClipRegions(hVSurface.cpu(), {RegionData, usNumRegions});
    return TRUE;
}

// ********************************************************
// Region manipulation functions
// ********************************************************

BOOLEAN GetVSurfaceRegion(SGPVSurface& hVSurface, UINT16 usIndex, VSURFACE_REGION* aRegion)
{

    if (!aRegion || usIndex >= hVSurface.RegionList.size()) {
        return FALSE;
    }
    *aRegion = hVSurface.RegionList[usIndex];
    return TRUE;
}

// FUNCTION: WIZ8 0x00403b10
BOOLEAN BltVideoSurfaceToVideoSurface(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                      UINT16 usIndex, INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                                      blt_vs_fx* pBltFx)
{
    VSURFACE_REGION aRegion;
    SDL_Rect SrcRect;
    UINT16 *pDestSurface16, *pSrcSurface16;
    UINT32 uiSrcPitch, uiDestPitch;

    // Assertions

    // Check that both region and subrect are not given
    if ((fBltFlags & VS_BLT_SRCREGION) && (fBltFlags & VS_BLT_SRCSUBRECT)) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Inconsistant blit flags given");
        return (FALSE);
    }

    // Check for dest src options
    if (fBltFlags & VS_BLT_DESTREGION) {
        CHECKF(pBltFx != nullptr);
        CHECKF(GetVSurfaceRegion(hDestVSurface, pBltFx->DestRegion, &aRegion));

        // Set starting coordinates from destination region
        iDestY = aRegion.RegionCoords.y;
        iDestX = aRegion.RegionCoords.x;
    }

    // Check for fill, if true, fill entire region with color
    if (fBltFlags & VS_BLT_COLORFILL) {
        return (FillSurface(hDestVSurface, pBltFx));
    }

    // Check for colorfill rectangle
    if (fBltFlags & VS_BLT_COLORFILLRECT) {
        return (FillSurfaceRect(hDestVSurface, pBltFx));
    }


    // Check for source coordinate options - from region, specific rect or full src dimensions
    do {
        // Get Region from index, if specified
        if (fBltFlags & VS_BLT_SRCREGION) {
            CHECKF(GetVSurfaceRegion(hSrcVSurface, usIndex, &aRegion))

            SrcRect = aRegion.RegionCoords;
            break;
        }

        // Use SUBRECT if specified
        if (fBltFlags & VS_BLT_SRCSUBRECT) {
            CHECKF(pBltFx != nullptr);
            SrcRect = pBltFx->SrcRect;

            break;
        }

        // Here, use default, which is entire Video Surface
        // Check Sizes, SRC size MUST be <= DEST size
        if (hDestVSurface.usHeight < hSrcVSurface.usHeight) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Incompatible height size given in Video Surface blit");
            return (FALSE);
        }
        if (hDestVSurface.usWidth < hSrcVSurface.usWidth) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Incompatible height size given in Video Surface blit");
            return (FALSE);
        }

        SrcRect = {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight};

    } while (FALSE);

    // Once here, assert valid Src

    const SDL_Rect bounds{0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight};
    const SDL_Rect destination{iDestX, iDestY, SrcRect.w, SrcRect.h};
    if (!SDL_HasRectIntersection(&destination, &bounds))
        return FALSE;

    CHECKF(hDestVSurface.ubBitDepth == hSrcVSurface.ubBitDepth &&
           (hDestVSurface.ubBitDepth == 8 || hDestVSurface.ubBitDepth == 16));
    if (hDestVSurface.ubBitDepth == 16 && (fBltFlags & VS_BLT_MIRROR_Y)) {
        pSrcSurface16 = (UINT16*)LockVideoSurfaceBuffer(hSrcVSurface, &uiSrcPitch);
        CHECKF(pSrcSurface16);
        pDestSurface16 = (UINT16*)LockVideoSurfaceBuffer(hDestVSurface, &uiDestPitch);
        if (!pDestSurface16) {
            UnLockVideoSurfaceBuffer(hSrcVSurface);
            return FALSE;
        }
        Blt16BPPTo16BPPMirror(pDestSurface16, uiDestPitch, pSrcSurface16, uiSrcPitch,
                             iDestX, iDestY, SrcRect.x, SrcRect.y, SrcRect.w, SrcRect.h);
        UnLockVideoSurfaceBuffer(hSrcVSurface);
        UnLockVideoSurfaceBuffer(hDestVSurface);
        return TRUE;
    }
    return BltVSurfaceUsingSDL(hDestVSurface, hSrcVSurface, fBltFlags, iDestX, iDestY, &SrcRect);
}

// UTILITY FUNCTIONS FOR BLITTING

// Clip an unscaled blit while keeping source and destination pixels aligned.
BOOLEAN ClipReleatedSrcAndDestRectangles(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                         SDL_Rect* DestRect, SDL_Rect* SrcRect)
{

    return ClipCpuBlit(*DestRect, *SrcRect,
                       {0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight},
                       {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight});
}

BOOLEAN FillSurface(SGPVSurface& hDestVSurface, blt_vs_fx* pBltFx)
{
    CHECKF(pBltFx);
    FillCpuSurface(hDestVSurface.cpu(), pBltFx->ColorFill);
    return TRUE;
}

BOOLEAN FillSurfaceRect(SGPVSurface& hDestVSurface, blt_vs_fx* pBltFx)
{
    CHECKF(pBltFx);
    FillCpuSurface(hDestVSurface.cpu(), pBltFx->ColorFill, &pBltFx->FillRect);
    return TRUE;
}

BOOLEAN BltVSurfaceUsingSDL(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface, UINT32 fBltFlags,
                           INT32 iDestX, INT32 iDestY, SDL_Rect* SrcRect)
{
    SDL_Rect destination{iDestX, iDestY, SrcRect->w, SrcRect->h};
    if (fBltFlags & VS_BLT_FAST) {
        CHECKF(iDestX >= 0 && iDestY >= 0);
        CHECKF(!hDestVSurface.cpu().clipRegions);
        CHECKF((destination.x + destination.w) <= hDestVSurface.usWidth &&
               (destination.y + destination.h) <= hDestVSurface.usHeight);
    } else if (!ClipReleatedSrcAndDestRectangles(hDestVSurface, hSrcVSurface,
                                               &destination, SrcRect)) {
        return TRUE;
    }
    if (destination.w == 0 || destination.h == 0)
        return TRUE;
    BlitCpuSurface(hDestVSurface.cpu(), &destination, hSrcVSurface.cpu(), SrcRect,
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

    // Lock video surface
    pBuffer = (UINT16*)LockVideoSurfaceBuffer(uiDestVSurface, &uiPitch);
    //UnLockVideoSurfaceBuffer( uiDestVSurface );

    if (pBuffer == nullptr) {
        return (FALSE);
    }

    const BOOLEAN result = fLowPercentShadeTable
        ? Blt16BPPBufferShadowRectAlternateTable(pBuffer, uiPitch, &area)
        : Blt16BPPBufferShadowRect(pBuffer, uiPitch, &area);
    UnLockVideoSurfaceBuffer(uiDestVSurface);
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
    VSURFACE_DESC desc{};
    desc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
    desc.usWidth = object.sprites[subIndex].usWidth;
    desc.usHeight = object.sprites[subIndex].usHeight;
    desc.ubBitDepth = PIXEL_DEPTH;
    auto surface = CreateVideoSurface(desc);
    if (!surface || !BltVideoObject(*surface, object, subIndex, 0, 0,
                                    VO_BLT_SRCTRANSPARENCY, nullptr))
        return nullptr;
    return surface;
}
