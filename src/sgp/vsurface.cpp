#include "wiz8/utility.h"
#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "compat/surfaces.h"
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <map>
#include "Video2.h"
#include "himage.h"
#include "vsurface.h"
#include "video_private.h"
#include "WCheck.h"
#include "vobject_blitters.h"

extern void SetClippingRect(SGPRect* clip);
extern void GetClippingRect(SGPRect* clip);
// Video Surface SGP Module
// Second Revision: Dec 10, 1996, Andrew Emmons
// Defines
// LOCAL functions

BOOLEAN ClipReleatedSrcAndDestRectangles(HVSURFACE hDestVSurface, HVSURFACE hSrcVSurface,
                                         RECT* DestRect, RECT* SrcRect);
BOOLEAN FillSurface(HVSURFACE hDestVSurface, blt_vs_fx* pBltFx);
BOOLEAN FillSurfaceRect(HVSURFACE hDestVSurface, blt_vs_fx* pBltFx);
BOOLEAN BltVSurfaceUsingSDL(HVSURFACE hDestVSurface, HVSURFACE hSrcVSurface, UINT32 fBltFlags,
                           INT32 iDestX, INT32 iDestY, RECT* SrcRect);
BOOLEAN GetVSurfaceRect(HVSURFACE hVSurface, RECT* pRect);

void DeletePrimaryVideoSurfaces();
// LOCAL global variables

// GLOBAL: WIZ8 0x00650dbc
static std::map<UINT32, std::unique_ptr<SGPVSurface>> g_video_surfaces;
// GLOBAL: WIZ8 0x00650dc4
UINT32 guiVSurfaceIndex = 0;

#ifdef _DEBUG
enum {
    DEBUGSTR_NONE,
    DEBUGSTR_SETVIDEOSURFACETRANSPARENCY,
    DEBUGSTR_ADDVIDEOSURFACEREGION,
    DEBUGSTR_GETVIDEOSURFACEDESCRIPTION,
    DEBUGSTR_BLTVIDEOSURFACE_DST,
    DEBUGSTR_BLTVIDEOSURFACE_SRC,
    DEBUGSTR_COLORFILLVIDEOSURFACEAREA,
    DEBUGSTR_SHADOWVIDEOSURFACERECT,
    DEBUGSTR_BLTSTRETCHVIDEOSURFACE_DST,
    DEBUGSTR_BLTSTRETCHVIDEOSURFACE_SRC,
    DEBUGSTR_DELETEVIDEOSURFACEFROMINDEX
};

UINT8 gubVSDebugCode = 0;

void CheckValidVSurfaceIndex(UINT32 uiIndex);
#endif

// GLOBAL: WIZ8 0x00650dd4
std::unique_ptr<SGPVSurface> ghPrimary;
// GLOBAL: WIZ8 0x00650dd8
std::unique_ptr<SGPVSurface> ghBackBuffer;
// GLOBAL: WIZ8 0x00650ddc
std::unique_ptr<SGPVSurface> ghFrameBuffer;
// GLOBAL: WIZ8 0x00650de0
std::unique_ptr<SGPVSurface> ghMouseBuffer;
// Video Surface Manager functions

// FUNCTION: WIZ8 0x00402970
BOOLEAN InitializeVideoSurfaceManager()
{
    //Shouldn't be calling this if the video surface manager already exists.
    //Call shutdown first...
    Assert(g_video_surfaces.empty());

    // Create primary and backbuffer from globals
    if (!SetPrimaryVideoSurfaces()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create primary surfaces");
        return FALSE;
    }

    return TRUE;
}

// FUNCTION: WIZ8 0x00402990
BOOLEAN ShutdownVideoSurfaceManager()
{
    DeletePrimaryVideoSurfaces();
    g_video_surfaces.clear();
    guiVSurfaceIndex = 0;
    return TRUE;
}

// FUNCTION: WIZ8 0x004029f0
BOOLEAN RestoreVideoSurfaces()
{
    for (auto surface = g_video_surfaces.rbegin(); surface != g_video_surfaces.rend(); ++surface) {
        if (!RestoreVideoSurface(surface->second.get())) {
            return FALSE;
        }
    }
    return TRUE;
}

// FUNCTION: WIZ8 0x00402a70
BOOLEAN AddStandardVideoSurface(VSURFACE_DESC* pVSurfaceDesc, UINT32* puiIndex)
{

    HVSURFACE hVSurface;

    // Assertions
    Assert(puiIndex);
    Assert(pVSurfaceDesc);

    // Create video object
    auto surface = std::unique_ptr<SGPVSurface>(CreateVideoSurface(pVSurfaceDesc));
    hVSurface = surface.get();

    if (!hVSurface) {
        // Video Object will set error condition.
        return FALSE;
    }

    // Set transparency to default
    SetVideoSurfaceTransparencyColor(hVSurface, FROMRGB(0, 0, 0));

    const auto index = guiVSurfaceIndex + 2;
    g_video_surfaces.emplace(index, std::move(surface));
    guiVSurfaceIndex = index;
    *puiIndex = index;
    Assert(guiVSurfaceIndex < 0xfffffff0); //unlikely that we will ever use 2 billion VSurfaces!
    //We would have to create about 70 VSurfaces per second for 1 year straight to achieve this...

    return TRUE;
}

// FUNCTION: WIZ8 0x00402b90
BYTE* LockVideoSurface(UINT32 uiVSurface, UINT32* puiPitch)
{
    // Check if given backbuffer or primary buffer

    if (uiVSurface == FRAME_BUFFER) {
        return (BYTE*)LockPrimarySurface(puiPitch);
    }

    if (uiVSurface == MOUSE_BUFFER) {
        return (BYTE*)LockMouseBuffer(puiPitch);
    }
    const auto surface = g_video_surfaces.find(uiVSurface);
    return surface == g_video_surfaces.end() ? nullptr :
        LockVideoSurfaceBuffer(surface->second.get(), puiPitch);
}

// FUNCTION: WIZ8 0x00402c30
void UnLockVideoSurface(UINT32 uiVSurface)
{
    // Check if given backbuffer or primary buffer

    if (uiVSurface == FRAME_BUFFER) {
        UnlockPrimarySurface();
        return;
    }

    if (uiVSurface == MOUSE_BUFFER) {
        UnlockMouseBuffer();
        return;
    }

    const auto surface = g_video_surfaces.find(uiVSurface);
    if (surface != g_video_surfaces.end())
        UnLockVideoSurfaceBuffer(surface->second.get());
}

// FUNCTION: WIZ8 0x00402d00
BOOLEAN SetVideoSurfaceTransparency(UINT32 uiIndex, COLORVAL TransColor)
{
    HVSURFACE hVSurface;
    // Get Video Surface

#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_SETVIDEOSURFACETRANSPARENCY;
#endif
    CHECKF(GetVideoSurface(&hVSurface, uiIndex));
    // Set transparency

    SetVideoSurfaceTransparencyColor(hVSurface, TransColor);

    return (TRUE);
}

// FUNCTION: WIZ8 0x00402db0
BOOLEAN GetVideoSurface(HVSURFACE* hVSurface, UINT32 uiIndex)
{
#ifdef _DEBUG
    CheckValidVSurfaceIndex(uiIndex);
#endif

    if (uiIndex == PRIMARY_SURFACE) {
        *hVSurface = ghPrimary.get();
        return TRUE;
    }

    if (uiIndex == BACKBUFFER) {
        *hVSurface = ghBackBuffer.get();
        return TRUE;
    }

    if (uiIndex == FRAME_BUFFER) {
        *hVSurface = ghFrameBuffer.get();
        return TRUE;
    }

    if (uiIndex == MOUSE_BUFFER) {
        *hVSurface = ghMouseBuffer.get();
        return TRUE;
    }

    const auto surface = g_video_surfaces.find(uiIndex);
    if (surface == g_video_surfaces.end())
        return FALSE;
    *hVSurface = surface->second.get();
    return TRUE;
}

// FUNCTION: WIZ8 0x00402e30
BOOLEAN SetPrimaryVideoSurfaces()
{
    CpuSurface* pSurface;

    // Delete surfaces if they exist
    DeletePrimaryVideoSurfaces();
    // Get Primary surface
    // Get frame buffer surface

    pSurface = GetFrameBufferObject();
    CHECKF(pSurface != nullptr);

    ghFrameBuffer.reset(CreateVideoSurfaceFromCpuSurface(pSurface));
    CHECKF(ghFrameBuffer != nullptr);

    return (TRUE);
}

// FUNCTION: WIZ8 0x00402e60
void DeletePrimaryVideoSurfaces()
{
    // If globals are not null, delete them

    ghPrimary.reset();

    ghBackBuffer.reset();

    ghFrameBuffer.reset();

    ghMouseBuffer.reset();
}
// Given an index to the dest and src vobject contained in our private VSurface list
// Based on flags, blit accordingly
// There are two types, a BltFast and a Blt. BltFast is 10% faster, uses no
// clipping lists

// FUNCTION: WIZ8 0x00402ed0
BOOLEAN BltVideoSurface(UINT32 uiDestVSurface, UINT32 uiSrcVSurface, UINT16 usRegionIndex,
                        INT32 iDestX, INT32 iDestY, UINT32 fBltFlags, blt_vs_fx* pBltFx)
{

    HVSURFACE hDestVSurface;
    HVSURFACE hSrcVSurface;

#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_BLTVIDEOSURFACE_DST;
#endif
    if (!GetVideoSurface(&hDestVSurface, uiDestVSurface)) {
        return FALSE;
    }
#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_BLTVIDEOSURFACE_SRC;
#endif
    if (!GetVideoSurface(&hSrcVSurface, uiSrcVSurface)) {
        return FALSE;
    }
    if (!BltVideoSurfaceToVideoSurface(
            hDestVSurface, hSrcVSurface, usRegionIndex, iDestX, iDestY, fBltFlags,
            pBltFx)) { // VO Blitter will set debug messages for error conditions
        return FALSE;
    }
    return TRUE;
}
// Fills an rectangular area with a specified color value.

// FUNCTION: WIZ8 0x00402fa0
BOOLEAN ColorFillVideoSurfaceArea(UINT32 uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, UINT16 Color16BPP)
{
    blt_vs_fx BltFx;
    HVSURFACE hDestVSurface;
    SGPRect Clip;

#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_COLORFILLVIDEOSURFACEAREA;
#endif
    if (!GetVideoSurface(&hDestVSurface, uiDestVSurface)) {
        return FALSE;
    }

    BltFx.ColorFill = Color16BPP;
    BltFx.DestRegion = 0;
    // Clip fill region coords

    GetClippingRect(&Clip);

    if (iDestX1 < Clip.iLeft)
        iDestX1 = Clip.iLeft;

    if (iDestX1 > Clip.iRight)
        return (FALSE);

    if (iDestX2 > Clip.iRight)
        iDestX2 = Clip.iRight;

    if (iDestX2 < Clip.iLeft)
        return (FALSE);

    if (iDestY1 < Clip.iTop)
        iDestY1 = Clip.iTop;

    if (iDestY1 > Clip.iBottom)
        return (FALSE);

    if (iDestY2 > Clip.iBottom)
        iDestY2 = Clip.iBottom;

    if (iDestY2 < Clip.iTop)
        return (FALSE);

    if ((iDestX2 <= iDestX1) || (iDestY2 <= iDestY1))
        return (FALSE);

    BltFx.SrcRect.iLeft = BltFx.FillRect.iLeft = iDestX1;
    BltFx.SrcRect.iTop = BltFx.FillRect.iTop = iDestY1;
    BltFx.SrcRect.iRight = BltFx.FillRect.iRight = iDestX2;
    BltFx.SrcRect.iBottom = BltFx.FillRect.iBottom = iDestY2;

    return (FillSurfaceRect(hDestVSurface, &BltFx));
}
// Fills an rectangular area with a specified image value.

// FUNCTION: WIZ8 0x00403150
BOOLEAN ImageFillVideoSurfaceArea(UINT32 uiDestVSurface, INT32 iDestX1, INT32 iDestY1,
                                  INT32 iDestX2, INT32 iDestY2, HVOBJECT BkgrndImg, UINT16 Index,
                                  INT16 Ox, INT16 Oy)
{
    INT16 xc, yc, hblits, wblits, aw, pw, ah, ph, w, h, xo, yo;
    ETRLEObject* pTrav;
    SGPRect NewClip, OldClip;

    pTrav = &(BkgrndImg->pETRLEObject[Index]);
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

    NewClip.iLeft = iDestX1;
    NewClip.iTop = iDestY1;
    NewClip.iRight = iDestX2;
    NewClip.iBottom = iDestY2;

    if (NewClip.iLeft < OldClip.iLeft)
        NewClip.iLeft = OldClip.iLeft;

    if (NewClip.iLeft > OldClip.iRight)
        return (FALSE);

    if (NewClip.iRight > OldClip.iRight)
        NewClip.iRight = OldClip.iRight;

    if (NewClip.iRight < OldClip.iLeft)
        return (FALSE);

    if (NewClip.iTop < OldClip.iTop)
        NewClip.iTop = OldClip.iTop;

    if (NewClip.iTop > OldClip.iBottom)
        return (FALSE);

    if (NewClip.iBottom > OldClip.iBottom)
        NewClip.iBottom = OldClip.iBottom;

    if (NewClip.iBottom < OldClip.iTop)
        return (FALSE);

    if ((NewClip.iRight <= NewClip.iLeft) || (NewClip.iBottom <= NewClip.iTop))
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
HVSURFACE CreateVideoSurface(VSURFACE_DESC* VSurfaceDesc)
{
    CHECKF(VSurfaceDesc != nullptr);
    std::unique_ptr<image_type> image;
    UINT16 width = VSurfaceDesc->usWidth;
    UINT16 height = VSurfaceDesc->usHeight;
    UINT8 bits = VSurfaceDesc->ubBitDepth;
    if (VSurfaceDesc->fCreateFlags & VSURFACE_CREATE_FROMFILE) {
        image = CreateImage(VSurfaceDesc->ImageFile.c_str(), IMAGE_ALLIMAGEDATA);
        CHECKF(image != nullptr);
        width = image->usWidth;
        height = image->usHeight;
        bits = image->ubBitDepth;
    }
    CHECKF(width && height && (bits == 8 || bits == 16));
    UINT32 red = 0, green = 0, blue = 0;
    if (bits == 16)
        CHECKF(GetPrimaryRGBDistributionMasks(&red, &green, &blue));
    auto result = std::make_unique<SGPVSurface>();
    result->ownedSurface = CreateCpuSurface(width, height, bits, red, green, blue);
    result->surface = result->ownedSurface.get();
    result->usWidth = width;
    result->usHeight = height;
    result->ubBitDepth = bits;
    result->fFlags = VSURFACE_SYSTEM_MEM_USAGE;
    result->TransparentColor = FROMRGB(0, 0, 0);
    if (image) {
        if (image->fFlags & IMAGE_PALETTE)
            SetVideoSurfacePalette(result.get(), image->pPalette.get());
        SGPRect region{0, 0, width, height};
        SetVideoSurfaceDataFromHImage(result.get(), image.get(), 0, 0, &region);
    }
    SetVideoSurfaceTransparencyColor(result.get(), result->TransparentColor);

    return result.release();
}
// Called when surface is lost, for the most part called by utility functions

BOOLEAN RestoreVideoSurface(HVSURFACE hVSurface)
{
    CHECKF(hVSurface != nullptr);
    // CPU storage persists across focus/display changes; it is never lost video memory.
    return FALSE;
}

// Lock must be followed by release
// Pitch MUST be used for all width calculations ( Pitch is in bytes )
// The time between Locking and unlocking must be minimal
BYTE* LockVideoSurfaceBuffer(HVSURFACE hVSurface, UINT32* pPitch)
{
    Assert(hVSurface != nullptr);
    Assert(pPitch != nullptr);
    if (hVSurface == ghFrameBuffer.get())
        return static_cast<BYTE*>(LockPrimarySurface(pPitch));
    const auto lock = LockCpuSurface(*hVSurface->surface);
    *pPitch = lock.pitch;
    return static_cast<BYTE*>(lock.pixels);
}

void UnLockVideoSurfaceBuffer(HVSURFACE hVSurface)
{
    Assert(hVSurface != nullptr);
    if (hVSurface == ghFrameBuffer.get()) {
        UnlockPrimarySurface();
        return;
    }
    UnlockCpuSurface(*hVSurface->surface);
}

// Given an HIMAGE object, blit imagery into existing Video Surface. Can be from 8->16 BPP
// FUNCTION: WIZ8 0x00403780
BOOLEAN SetVideoSurfaceDataFromHImage(HVSURFACE hVSurface, HIMAGE hImage, UINT16 usX, UINT16 usY,
                                      SGPRect* pSrcRect)
{
    BYTE* pDest;
    UINT32 fBufferBPP = 0;
    UINT32 uiPitch;
    UINT16 usEffectiveWidth;
    SGPRect aRect;

    // Assertions
    Assert(hVSurface != nullptr);
    Assert(hImage != nullptr);

    // Get Size of hImage and determine if it can fit
    CHECKF(hImage->usWidth >= hVSurface->usWidth);
    CHECKF(hImage->usHeight >= hVSurface->usHeight);

    // Check BPP and see if they are the same
    if (hImage->ubBitDepth != hVSurface->ubBitDepth) {
        // They are not the same, but we can go from 8->16 without much cost
        if (hImage->ubBitDepth == 8 && hVSurface->ubBitDepth == 16) {
            fBufferBPP = BUFFER_16BPP;
        }
    } else {
        // Set buffer BPP
        switch (hImage->ubBitDepth) {
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
    usEffectiveWidth = (UINT16)(uiPitch / (hVSurface->ubBitDepth / 8));

    CHECKF(pDest != nullptr);

    // Blit Surface
    // If rect is NULL, use entrie image size
    if (pSrcRect == nullptr) {
        aRect.iLeft = 0;
        aRect.iTop = 0;
        aRect.iRight = hImage->usWidth;
        aRect.iBottom = hImage->usHeight;
    } else {
        aRect.iLeft = pSrcRect->iLeft;
        aRect.iTop = pSrcRect->iTop;
        aRect.iRight = pSrcRect->iRight;
        aRect.iBottom = pSrcRect->iBottom;
    }

    // This HIMAGE function will transparently copy buffer
    if (!CopyImageToBuffer(hImage, fBufferBPP, pDest, usEffectiveWidth, hVSurface->usHeight, usX,
                           usY, &aRect)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error Occured Copying HIMAGE to HVSURFACE");
        UnLockVideoSurfaceBuffer(hVSurface);
        return (FALSE);
    }

    // All is OK
    UnLockVideoSurfaceBuffer(hVSurface);

    return (TRUE);
}

// Indexed surfaces use SDL palettes; custom RGB555 blitters retain their table.
BOOLEAN SetVideoSurfacePalette(HVSURFACE hVSurface, SGPPaletteEntry* pSrcPalette)
{
    CHECKF(hVSurface && pSrcPalette);
    std::copy_n(pSrcPalette, 256, hVSurface->palette.begin());
    hVSurface->hasPalette = true;
    if (hVSurface->ubBitDepth == 8) {
        SDL_Color colors[256];
        for (unsigned i = 0; i < 256; ++i)
            colors[i] = {pSrcPalette[i].peRed, pSrcPalette[i].peGreen, pSrcPalette[i].peBlue, 255};
        CHECKF(SDL_SetPaletteColors(SDL_GetSurfacePalette(hVSurface->surface->surface.get()),
                                   colors, 0, 256));
    }
    hVSurface->p16BPPPalette = Create16BPPPalette(pSrcPalette);
    return TRUE;
}

// Transparency uses the game's packed source key.
// colorkey value.
BOOLEAN SetVideoSurfaceTransparencyColor(HVSURFACE hVSurface, COLORVAL TransColor)
{
    CHECKF(hVSurface && hVSurface->surface);
    hVSurface->TransparentColor = TransColor;
    const UINT32 key = hVSurface->ubBitDepth == 16 ? Get16BPPColor(TransColor) : TransColor;
    hVSurface->surface->sourceKey = SurfaceColorKey{key, key};
    return TRUE;
}

// FUNCTION: WIZ8 0x004039c0
BOOLEAN DeleteVideoSurfaceFromIndex(UINT32 uiIndex)
{
#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_DELETEVIDEOSURFACEFROMINDEX;
    CheckValidVSurfaceIndex(uiIndex);
#endif

    return g_video_surfaces.erase(uiIndex) != 0;
}

// Deletes all palettes, surfaces and region data
// FUNCTION: WIZ8 0x00403a50
BOOLEAN DeleteVideoSurface(HVSURFACE hVSurface)
{
    CHECKF(hVSurface != nullptr);
    delete hVSurface;
    return TRUE;
}

// ********************************************************
// Clipper manipulation functions
// ********************************************************

BOOLEAN SetClipList(HVSURFACE hVSurface, SGPRect* RegionData, UINT16 usNumRegions)
{
    CHECKF(hVSurface && RegionData && usNumRegions);
    std::vector<RECT> rectangles;
    rectangles.reserve(usNumRegions);
    for (UINT16 i = 0; i < usNumRegions; ++i)
        rectangles.push_back({RegionData[i].iLeft, RegionData[i].iTop,
                              RegionData[i].iRight, RegionData[i].iBottom});
    SetSurfaceClipRegions(*hVSurface->surface, rectangles);
    return TRUE;
}

// ********************************************************
// Region manipulation functions
// ********************************************************

BOOLEAN GetVSurfaceRegion(HVSURFACE hVSurface, UINT16 usIndex, VSURFACE_REGION* aRegion)
{
    Assert(hVSurface != nullptr);

    if (!aRegion || usIndex >= hVSurface->RegionList.size()) {
        return FALSE;
    }
    *aRegion = hVSurface->RegionList[usIndex];
    return TRUE;
}

BOOLEAN GetVSurfaceRect(HVSURFACE hVSurface, RECT* pRect)
{
    Assert(hVSurface != nullptr);
    Assert(pRect != nullptr);

    pRect->left = 0;
    pRect->top = 0;
    pRect->right = hVSurface->usWidth;
    pRect->bottom = hVSurface->usHeight;

    return (TRUE);
}

// *******************************************************************
// Blitting Functions
// *******************************************************************

// Ordinary same-depth blits use SDL.
// Will drop down into user-defined blitter if 8->16 BPP blitting is being done

// FUNCTION: WIZ8 0x00403b10
BOOLEAN BltVideoSurfaceToVideoSurface(HVSURFACE hDestVSurface, HVSURFACE hSrcVSurface,
                                      UINT16 usIndex, INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                                      blt_vs_fx* pBltFx)
{
    VSURFACE_REGION aRegion;
    RECT SrcRect, DestRect;
    UINT16 *pDestSurface16, *pSrcSurface16;
    UINT32 uiSrcPitch, uiDestPitch, uiWidth, uiHeight;

    // Assertions
    Assert(hDestVSurface != nullptr);

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
        iDestY = aRegion.RegionCoords.iTop;
        iDestX = aRegion.RegionCoords.iLeft;
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

            SrcRect.top = (int)aRegion.RegionCoords.iTop;
            SrcRect.left = (int)aRegion.RegionCoords.iLeft;
            SrcRect.bottom = (int)aRegion.RegionCoords.iBottom;
            SrcRect.right = (int)aRegion.RegionCoords.iRight;
            break;
        }

        // Use SUBRECT if specified
        if (fBltFlags & VS_BLT_SRCSUBRECT) {
            SGPRect aSubRect;

            CHECKF(pBltFx != nullptr);

            aSubRect = pBltFx->SrcRect;

            SrcRect.top = (int)aSubRect.iTop;
            SrcRect.left = (int)aSubRect.iLeft;
            SrcRect.bottom = (int)aSubRect.iBottom;
            SrcRect.right = (int)aSubRect.iRight;

            break;
        }

        // Here, use default, which is entire Video Surface
        // Check Sizes, SRC size MUST be <= DEST size
        if (hDestVSurface->usHeight < hSrcVSurface->usHeight) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Incompatible height size given in Video Surface blit");
            return (FALSE);
        }
        if (hDestVSurface->usWidth < hSrcVSurface->usWidth) {
            SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Incompatible height size given in Video Surface blit");
            return (FALSE);
        }

        SrcRect.top = (int)0;
        SrcRect.left = (int)0;
        SrcRect.bottom = (int)hSrcVSurface->usHeight;
        SrcRect.right = (int)hSrcVSurface->usWidth;

    } while (FALSE);

    // Once here, assert valid Src
    Assert(hSrcVSurface != nullptr);

    // clipping -- added by DB
    GetVSurfaceRect(hDestVSurface, &DestRect);
    uiWidth = SrcRect.right - SrcRect.left;
    uiHeight = SrcRect.bottom - SrcRect.top;

    // check for position entirely off the screen
    if (iDestX >= DestRect.right)
        return (FALSE);
    if (iDestY >= DestRect.bottom)
        return (FALSE);
    if ((iDestX + (INT32)uiWidth) < (INT32)DestRect.left)
        return (FALSE);
    if ((iDestY + (INT32)uiHeight) < (INT32)DestRect.top)
        return (FALSE);

    // DB The mirroring stuff has to do it's own clipping because
    // it needs to invert some of the numbers
    if (!(fBltFlags & VS_BLT_MIRROR_Y)) {
        if ((iDestX + (INT32)uiWidth) >= (INT32)DestRect.right) {
            SrcRect.right -= ((iDestX + uiWidth) - DestRect.right);
            uiWidth -= ((iDestX + uiWidth) - DestRect.right);
        }
        if ((iDestY + (INT32)uiHeight) >= (INT32)DestRect.bottom) {
            SrcRect.bottom -= ((iDestY + uiHeight) - DestRect.bottom);
            uiHeight -= ((iDestY + uiHeight) - DestRect.bottom);
        }
        if (iDestX < DestRect.left) {
            SrcRect.left += (DestRect.left - iDestX);
            uiWidth -= (DestRect.left - iDestX);
            iDestX = DestRect.left;
        }
        if (iDestY < (INT32)DestRect.top) {
            SrcRect.top += (DestRect.top - iDestY);
            uiHeight -= (DestRect.top - iDestY);
            iDestY = DestRect.top;
        }
    }

    // Same-depth operations use the SDL surface path.
    // First check BPP values for compatibility
    if (hDestVSurface->ubBitDepth == 16 && hSrcVSurface->ubBitDepth == 16) {
        if (fBltFlags & VS_BLT_MIRROR_Y) {
            if ((pSrcSurface16 = (UINT16*)LockVideoSurfaceBuffer(hSrcVSurface, &uiSrcPitch)) ==
                nullptr) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed on lock of 16BPP surface for blitting");
                return (FALSE);
            }

            if ((pDestSurface16 = (UINT16*)LockVideoSurfaceBuffer(hDestVSurface, &uiDestPitch)) ==
                nullptr) {
                UnLockVideoSurfaceBuffer(hSrcVSurface);
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed on lock of 16BPP dest surface for blitting");
                return (FALSE);
            }

            Blt16BPPTo16BPPMirror(pDestSurface16, uiDestPitch, pSrcSurface16, uiSrcPitch, iDestX,
                                  iDestY, SrcRect.left, SrcRect.top, uiWidth, uiHeight);
            UnLockVideoSurfaceBuffer(hSrcVSurface);
            UnLockVideoSurfaceBuffer(hDestVSurface);
            return (TRUE);
        }
        CHECKF(
            BltVSurfaceUsingSDL(hDestVSurface, hSrcVSurface, fBltFlags, iDestX, iDestY, &SrcRect));

    } else if (hDestVSurface->ubBitDepth == 8 && hSrcVSurface->ubBitDepth == 8) {
        return BltVSurfaceUsingSDL(hDestVSurface, hSrcVSurface, fBltFlags,
                                  iDestX, iDestY, &SrcRect);
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Incompatible BPP values with src and dest Video Surfaces for blitting");
        return (FALSE);
    }

    return (TRUE);
}

// ******************************************************************************************
// UTILITY FUNCTIONS
// ******************************************************************************************

// Blt to backup buffer
// FUNCTION: WIZ8 0x00404470

// *****************************************************************************
// Borrowed frame-buffer integration
// *****************************************************************************

// FUNCTION: WIZ8 0x004044c0
HVSURFACE CreateVideoSurfaceFromCpuSurface(CpuSurface* cpuSurface)
{
    CHECKF(cpuSurface != nullptr);
    auto result = std::make_unique<SGPVSurface>();
    const auto& surface = *cpuSurface->surface;
    result->usWidth = surface.w;
    result->usHeight = surface.h;
    result->ubBitDepth = SDL_BYTESPERPIXEL(surface.format) * 8;
    result->surface = cpuSurface;
    result->fFlags = VSURFACE_SYSTEM_MEM_USAGE | VSURFACE_RESERVED_SURFACE;
    if (const auto* palette = SDL_GetSurfacePalette(cpuSurface->surface.get())) {
        for (int i = 0; i < std::min(256, palette->ncolors); ++i)
            result->palette[i] = {palette->colors[i].r, palette->colors[i].g,
                                  palette->colors[i].b, 0};
        result->hasPalette = true;
        result->p16BPPPalette = Create16BPPPalette(result->palette.data());
    }
    return result.release();
}

// UTILITY FUNCTIONS FOR BLITTING

BOOLEAN ClipReleatedSrcAndDestRectangles(HVSURFACE hDestVSurface, HVSURFACE hSrcVSurface,
                                         RECT* DestRect, RECT* SrcRect)
{

    Assert(hDestVSurface != nullptr);
    Assert(hSrcVSurface != nullptr);

    // Check for invalid start positions and clip by ignoring blit
    if (DestRect->left >= hDestVSurface->usWidth || DestRect->top >= hDestVSurface->usHeight) {
        return (FALSE);
    }

    if (SrcRect->left >= hSrcVSurface->usWidth || SrcRect->top >= hSrcVSurface->usHeight) {
        return (FALSE);
    }

    // For overruns
    // Clip destination rectangles
    if (DestRect->right > hDestVSurface->usWidth) {
        // Both have to be modified or by default streching occurs
        DestRect->right = hDestVSurface->usWidth;
        SrcRect->right = SrcRect->left + (DestRect->right - DestRect->left);
    }
    if (DestRect->bottom > hDestVSurface->usHeight) {
        // Both have to be modified or by default streching occurs
        DestRect->bottom = hDestVSurface->usHeight;
        SrcRect->bottom = SrcRect->top + (DestRect->bottom - DestRect->top);
    }

    // Clip src rectangles
    if (SrcRect->right > hSrcVSurface->usWidth) {
        // Both have to be modified or by default streching occurs
        SrcRect->right = hSrcVSurface->usWidth;
        DestRect->right = DestRect->left + (SrcRect->right - SrcRect->left);
    }
    if (SrcRect->bottom > hSrcVSurface->usHeight) {
        // Both have to be modified or by default streching occurs
        SrcRect->bottom = hSrcVSurface->usHeight;
        DestRect->bottom = DestRect->top + (SrcRect->bottom - SrcRect->top);
    }

    // For underruns
    // Clip destination rectangles
    if (DestRect->left < 0) {
        // Both have to be modified or by default streching occurs
        DestRect->left = 0;
        SrcRect->left = SrcRect->right - (DestRect->right - DestRect->left);
    }
    if (DestRect->top < 0) {
        // Both have to be modified or by default streching occurs
        DestRect->top = 0;
        SrcRect->top = SrcRect->bottom - (DestRect->bottom - DestRect->top);
    }

    // Clip src rectangles
    if (SrcRect->left < 0) {
        // Both have to be modified or by default streching occurs
        SrcRect->left = 0;
        DestRect->left = DestRect->right - (SrcRect->right - SrcRect->left);
    }
    if (SrcRect->top < 0) {
        // Both have to be modified or by default streching occurs
        SrcRect->top = 0;
        DestRect->top = DestRect->bottom - (SrcRect->bottom - SrcRect->top);
    }

    return (TRUE);
}

BOOLEAN FillSurface(HVSURFACE hDestVSurface, blt_vs_fx* pBltFx)
{
    CHECKF(hDestVSurface && pBltFx);
    FillCpuSurface(*hDestVSurface->surface, pBltFx->ColorFill);
    return TRUE;
}

BOOLEAN FillSurfaceRect(HVSURFACE hDestVSurface, blt_vs_fx* pBltFx)
{
    CHECKF(hDestVSurface && pBltFx);
    const RECT rect{pBltFx->FillRect.iLeft, pBltFx->FillRect.iTop,
                    pBltFx->FillRect.iRight, pBltFx->FillRect.iBottom};
    FillCpuSurface(*hDestVSurface->surface, pBltFx->ColorFill, &rect);
    return TRUE;
}

BOOLEAN BltVSurfaceUsingSDL(HVSURFACE hDestVSurface, HVSURFACE hSrcVSurface, UINT32 fBltFlags,
                           INT32 iDestX, INT32 iDestY, RECT* SrcRect)
{
    RECT destination{iDestX, iDestY, iDestX + SrcRect->right - SrcRect->left,
                     iDestY + SrcRect->bottom - SrcRect->top};
    if (fBltFlags & VS_BLT_FAST) {
        CHECKF(iDestX >= 0 && iDestY >= 0);
        CHECKF(!hDestVSurface->surface->clipRegions);
        CHECKF(destination.right <= hDestVSurface->usWidth &&
               destination.bottom <= hDestVSurface->usHeight);
    } else if (!ClipReleatedSrcAndDestRectangles(hDestVSurface, hSrcVSurface,
                                               &destination, SrcRect)) {
        return TRUE;
    }
    if (destination.left == destination.right || destination.top == destination.bottom)
        return TRUE;
    BlitCpuSurface(*hDestVSurface->surface, &destination, *hSrcVSurface->surface, SrcRect,
                   fBltFlags & VS_BLT_USECOLORKEY, fBltFlags & VS_BLT_USEDESTCOLORKEY);
    return TRUE;
}

BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               SGPRect* area);

// FUNCTION: WIZ8 0x004045b0
BOOLEAN InternalShadowVideoSurfaceRect(UINT32 uiDestVSurface, INT32 X1, INT32 Y1, INT32 X2,
                                       INT32 Y2, BOOLEAN fLowPercentShadeTable)
{
    UINT16* pBuffer;
    UINT32 uiPitch;
    SGPRect area;
    HVSURFACE hVSurface;

    // CLIP IT!
    // FIRST GET SURFACE
    // Get Video Surface
#ifdef _DEBUG
    gubVSDebugCode = DEBUGSTR_SHADOWVIDEOSURFACERECT;
#endif
    CHECKF(GetVideoSurface(&hVSurface, uiDestVSurface));

    if (X1 < 0)
        X1 = 0;

    if (X2 < 0)
        return (FALSE);

    if (Y2 < 0)
        return (FALSE);

    if (Y1 < 0)
        Y1 = 0;

    if (X2 >= hVSurface->usWidth)
        X2 = hVSurface->usWidth - 1;

    if (Y2 >= hVSurface->usHeight)
        Y2 = hVSurface->usHeight - 1;

    if (X1 >= hVSurface->usWidth)
        return (FALSE);

    if (Y1 >= hVSurface->usHeight)
        return (FALSE);

    if ((X2 - X1) <= 0)
        return (FALSE);

    if ((Y2 - Y1) <= 0)
        return (FALSE);

    area.iTop = Y1;
    area.iBottom = Y2;
    area.iLeft = X1;
    area.iRight = X2;

    // Lock video surface
    pBuffer = (UINT16*)LockVideoSurface(uiDestVSurface, &uiPitch);
    //UnLockVideoSurface( uiDestVSurface );

    if (pBuffer == nullptr) {
        return (FALSE);
    }

    if (!fLowPercentShadeTable) {
        // Now we have the video object and surface, call the shadow function
        if (!Blt16BPPBufferShadowRect(pBuffer, uiPitch, &area)) {
            // Blit has failed if false returned
            return (FALSE);
        }
    } else {
        // Now we have the video object and surface, call the shadow function
        if (!Blt16BPPBufferShadowRectAlternateTable(pBuffer, uiPitch, &area)) {
            // Blit has failed if false returned
            return (FALSE);
        }
    }

    // Mark as dirty if it's the backbuffer
    //if ( uiDestVSurface == BACKBUFFER )
    //{
    //	InvalidateBackbuffer( );
    //}

    UnLockVideoSurface(uiDestVSurface);
    return (TRUE);
}

// FUNCTION: WIZ8 0x004048a0
BOOLEAN ShadowVideoSurfaceRect(UINT32 uiDestVSurface, INT32 X1, INT32 Y1, INT32 X2, INT32 Y2)
{
    return (InternalShadowVideoSurfaceRect(uiDestVSurface, X1, Y1, X2, Y2, FALSE));
}

// FUNCTION: WIZ8 0x004048d0
BOOLEAN MakeVSurfaceFromVObject(UINT32 uiVObject, UINT16 usSubIndex, UINT32* puiVSurface)
{
    HVOBJECT hSrcVObject;
    UINT32 uiVSurface;
    VSURFACE_DESC hDesc{};

    if (GetVideoObject(&hSrcVObject, uiVObject)) {
        hDesc.fCreateFlags = VSURFACE_CREATE_DEFAULT;
        hDesc.usWidth = hSrcVObject->pETRLEObject[usSubIndex].usWidth;
        hDesc.usHeight = hSrcVObject->pETRLEObject[usSubIndex].usHeight;
        hDesc.ubBitDepth = PIXEL_DEPTH;

        if (AddVideoSurface(&hDesc, &uiVSurface)) {
            if (BltVideoObjectFromIndex(uiVSurface, uiVObject, usSubIndex, 0, 0,
                                        VO_BLT_SRCTRANSPARENCY, nullptr)) {
                *puiVSurface = uiVSurface;
                return (TRUE);
            } else
                DeleteVideoSurfaceFromIndex(uiVSurface);
        }
    }

    return (FALSE);
}

#ifdef _DEBUG
void CheckValidVSurfaceIndex(UINT32 uiIndex)
{
    BOOLEAN fAssertError = FALSE;
    if (uiIndex == 0xffffffff) { //-1 index -- deleted
        fAssertError = TRUE;
    } else if (uiIndex % 2 && uiIndex < 0xfffffff0) { //odd numbers are reserved for vobjects
        fAssertError = TRUE;
    }

    if (fAssertError) {
        UINT8 str[60];
        switch (gubVSDebugCode) {
        case DEBUGSTR_SETVIDEOSURFACETRANSPARENCY:
            sprintf(str, "SetVideoSurfaceTransparency");
            break;
        case DEBUGSTR_ADDVIDEOSURFACEREGION:
            sprintf(str, "AddVideoSurfaceRegion");
            break;
        case DEBUGSTR_GETVIDEOSURFACEDESCRIPTION:
            sprintf(str, "GetVideoSurfaceDescription");
            break;
        case DEBUGSTR_BLTVIDEOSURFACE_DST:
            sprintf(str, "BltVideoSurface (dest)");
            break;
        case DEBUGSTR_BLTVIDEOSURFACE_SRC:
            sprintf(str, "BltVideoSurface (src)");
            break;
        case DEBUGSTR_COLORFILLVIDEOSURFACEAREA:
            sprintf(str, "ColorFillVideoSurfaceArea");
            break;
        case DEBUGSTR_SHADOWVIDEOSURFACERECT:
            sprintf(str, "ShadowVideoSurfaceRect");
            break;
        case DEBUGSTR_BLTSTRETCHVIDEOSURFACE_DST:
            sprintf(str, "BltStretchVideoSurface (dest)");
            break;
        case DEBUGSTR_BLTSTRETCHVIDEOSURFACE_SRC:
            sprintf(str, "BltStretchVideoSurface (src)");
            break;
        case DEBUGSTR_DELETEVIDEOSURFACEFROMINDEX:
            sprintf(str, "DeleteVideoSurfaceFromIndex");
            break;
        case DEBUGSTR_NONE:
        default:
            sprintf(str, "GetVideoSurface");
            break;
        }
        if (uiIndex == 0xffffffff) {
            AssertMsg(0, FormatString("Trying to %s with deleted index -1.", str));
        } else {
            AssertMsg(0, FormatString("Trying to %s using a VOBJECT ID %d!", str, uiIndex));
        }
    }
}
#endif
