#include "wiz8/utility.h"
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <stdio.h>
#include <map>
#include "Video2.h"
#include "himage.h"
#include "vobject.h"
#include <cstdint>
#include "WCheck.h"
#include "vobject_blitters.h"
#include "wiz8/application.h"

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

#define COMPRESS_TRANSPARENT 0x80
#define COMPRESS_RUN_MASK 0x7F

// *******************************************************************************
// External Functions and variables
// *******************************************************************************

// *******************************************************************************
// LOCAL functions
// *******************************************************************************

// *******************************************************************************
// LOCAL global variables
// *******************************************************************************

// GLOBAL: WIZ8 0x00650e20
BOOLEAN gfVideoObjectsInit = FALSE;

// GLOBAL: WIZ8 0x00650e24
static std::map<UINT32, std::unique_ptr<SGPVObject>> g_video_objects;
// GLOBAL: WIZ8 0x005ff5e8
UINT32 guiVObjectIndex = 1;

#ifdef _DEBUG
enum {
    DEBUGSTR_NONE,
    DEBUGSTR_SETVIDEOOBJECTTRANSPARENCY,
    DEBUGSTR_BLTVIDEOOBJECTFROMINDEX,
    DEBUGSTR_SETOBJECTHANDLESHADE,
    DEBUGSTR_GETVIDEOOBJECTETRLESUBREGIONPROPERTIES,
    DEBUGSTR_GETVIDEOOBJECTETRLEPROPERTIESFROMINDEX,
    DEBUGSTR_SETVIDEOOBJECTPALETTE8BPP,
    DEBUGSTR_GETVIDEOOBJECTPALETTE16BPP,
    DEBUGSTR_COPYVIDEOOBJECTPALETTE16BPP,
    DEBUGSTR_BLTVIDEOOBJECTOUTLINEFROMINDEX,
    DEBUGSTR_BLTVIDEOOBJECTOUTLINESHADOWFROMINDEX,
    DEBUGSTR_DELETEVIDEOOBJECTFROMINDEX
};

UINT8 gubVODebugCode = 0;

void CheckValidVObjectIndex(UINT32 uiIndex);
#endif

// **************************************************************
// Video Object Manager functions
// **************************************************************

// FUNCTION: WIZ8 0x00405e60
BOOLEAN InitializeVideoObjectManager()
{
    //Shouldn't be calling this if the video object manager already exists.
    //Call shutdown first...
    Assert(g_video_objects.empty());
    gfVideoObjectsInit = TRUE;
    return TRUE;
}

// FUNCTION: WIZ8 0x00405e80
BOOLEAN ShutdownVideoObjectManager()
{
    g_video_objects.clear();
    guiVObjectIndex = 1;
    gfVideoObjectsInit = FALSE;
    return TRUE;
}

// FUNCTION: WIZ8 0x00405ef0
BOOLEAN AddStandardVideoObject(VOBJECT_DESC* pVObjectDesc, UINT32* puiIndex)
{

    HVOBJECT hVObject;

    // Assertions
    Assert(puiIndex);
    Assert(pVObjectDesc);

    // Create video object
    auto object = std::unique_ptr<SGPVObject>(CreateVideoObject(pVObjectDesc));
    hVObject = object.get();

    if (!hVObject) {
        // Video Object will set error condition.
        return FALSE;
    }

    // Set transparency to default
    SetVideoObjectTransparencyColor(hVObject, FROMRGB(0, 0, 0));

    const auto index = guiVObjectIndex + 2;
    g_video_objects.emplace(index, std::move(object));
    guiVObjectIndex = index;
    *puiIndex = index;
    Assert(guiVObjectIndex < 0xfffffff0); //unlikely that we will ever use 2 billion vobjects!
    //We would have to create about 70 vobjects per second for 1 year straight to achieve this...

    return TRUE;
}

// FUNCTION: WIZ8 0x00405fc0
BOOLEAN GetVideoObject(HVOBJECT* hVObject, UINT32 uiIndex)
{
#ifdef _DEBUG
    CheckValidVObjectIndex(uiIndex);
#endif

    const auto object = g_video_objects.find(uiIndex);
    if (object == g_video_objects.end())
        return FALSE;
    *hVObject = object->second.get();
    return TRUE;
}

// FUNCTION: WIZ8 0x00405ff0
BOOLEAN BltVideoObjectFromIndex(UINT32 uiDestVSurface, UINT32 uiSrcVObject, UINT16 usRegionIndex,
                                INT32 iDestX, INT32 iDestY, UINT32 fBltFlags, blt_fx* pBltFx)
{
    UINT16* pBuffer;
    UINT32 uiPitch;
    HVOBJECT hSrcVObject;

    // Lock video surface
    pBuffer = (UINT16*)LockVideoSurface(uiDestVSurface, &uiPitch);

    if (pBuffer == nullptr) {
        return (FALSE);
    }

    // Get video object
#ifdef _DEBUG
    gubVODebugCode = DEBUGSTR_BLTVIDEOOBJECTFROMINDEX;
#endif
    if (!GetVideoObject(&hSrcVObject, uiSrcVObject)) {
        UnLockVideoSurface(uiDestVSurface);
        return FALSE;
    }

    // Now we have the video object and surface, call the VO blitter function
    if (!BltVideoObjectToBuffer(pBuffer, uiPitch, hSrcVObject, usRegionIndex, iDestX, iDestY,
                                fBltFlags, pBltFx)) {
        UnLockVideoSurface(uiDestVSurface);
        // VO Blitter will set debug messages for error conditions
        return FALSE;
    }

    UnLockVideoSurface(uiDestVSurface);
    return (TRUE);
}

// FUNCTION: WIZ8 0x00406080
BOOLEAN DeleteVideoObjectFromIndex(UINT32 uiVObject)
{
#ifdef _DEBUG
    gubVODebugCode = DEBUGSTR_DELETEVIDEOOBJECTFROMINDEX;
    CheckValidVObjectIndex(uiVObject);
#endif

    return g_video_objects.erase(uiVObject) != 0;
}

// Given indices to the destination and source video objects
// Based on flags, blit accordingly
// There are two types, a BltFast and a Blt. BltFast is 10% faster, uses no
// clipping lists
// FUNCTION: WIZ8 0x00406110
BOOLEAN BltVideoObject(UINT32 uiDestVSurface, HVOBJECT hSrcVObject, UINT16 usRegionIndex,
                       INT32 iDestX, INT32 iDestY, UINT32 fBltFlags, blt_fx* pBltFx)
{

    UINT16* pBuffer;
    UINT32 uiPitch;

    // Lock video surface
    pBuffer = (UINT16*)LockVideoSurface(uiDestVSurface, &uiPitch);

    if (pBuffer == nullptr) {
        return (FALSE);
    }

    // Now we have the video object and surface, call the VO blitter function
    if (!BltVideoObjectToBuffer(pBuffer, uiPitch, hSrcVObject, usRegionIndex, iDestX, iDestY,
                                fBltFlags, pBltFx)) {
        UnLockVideoSurface(uiDestVSurface);
        // VO Blitter will set debug messages for error conditions
        return (FALSE);
    }

    UnLockVideoSurface(uiDestVSurface);
    return (TRUE);
}

// *******************************************************************************
// Video Object Manipulation Functions
// *******************************************************************************

// FUNCTION: WIZ8 0x00406180
HVOBJECT CreateVideoObject(VOBJECT_DESC* VObjectDesc)
{
    std::unique_ptr<image_type> imageOwner;
    HIMAGE hImage = VObjectDesc->hImage;
    if (VObjectDesc->fCreateFlags & VOBJECT_CREATE_FROMFILE) {
        imageOwner = CreateImage(VObjectDesc->ImageFile.c_str(), IMAGE_ALLIMAGEDATA);
        hImage = imageOwner.get();
    } else if (!(VObjectDesc->fCreateFlags & VOBJECT_CREATE_FROMHIMAGE)) {
        return nullptr;
    }
    if (!hImage || !(hImage->fFlags & IMAGE_TRLECOMPRESSED))
        return nullptr;
    for (const auto& frame : hImage->pETRLEObject) {
        if (std::uint64_t(frame.uiDataOffset) + frame.uiDataLength > hImage->pImageData.size())
            return nullptr;
    }
    auto owner = std::make_unique<SGPVObject>();
    owner->ubBitDepth = hImage->ubBitDepth;
    if (hImage->ubBitDepth == 8) {
        if (!hImage->pPalette || !SetVideoObjectPalette(owner.get(), hImage->pPalette.get()))
            return nullptr;
        owner->pShade8 = ubColorTables[DEFAULT_SHADE_LEVEL];
        owner->pGlow8 = ubColorTables[0];
    }
    if (imageOwner) {
        owner->pETRLEObject = std::move(hImage->pETRLEObject);
        owner->pPixData = std::move(hImage->pImageData);
    } else {
        owner->pETRLEObject = hImage->pETRLEObject;
        owner->pPixData = hImage->pImageData;
    }
    // Ordinary draws use decoded SDL surfaces; undecodable data keeps
    // the streaming blitters.
    DecodeVideoObjectSprites(owner.get());
    return owner.release();
}

// Palette setting is expensive, need to set both DDPalette and create 16BPP palette
BOOLEAN SetVideoObjectPalette(HVOBJECT hVObject, SGPPaletteEntry* pSrcPalette)
{

    Assert(hVObject != nullptr);
    Assert(pSrcPalette != nullptr);

    // Create palette object if not already done so
    if (hVObject->pPaletteEntry == nullptr) {
        // Create palette
        hVObject->pPaletteEntry = std::make_unique<SGPPaletteEntry[]>(256);
        CHECKF(hVObject->pPaletteEntry != nullptr);

        // Copy src into palette
        memcpy(hVObject->pPaletteEntry.get(), pSrcPalette, sizeof(SGPPaletteEntry) * 256);

    } else {
        // Just Change entries
        memcpy(hVObject->pPaletteEntry.get(), pSrcPalette, sizeof(SGPPaletteEntry) * 256);
    }

    std::shared_ptr<UINT16[]> palette = Create16BPPPalette(pSrcPalette);
    const auto* oldPalette = hVObject->ownedPalette.get();
    for (auto& shade : hVObject->pShades)
        if (shade.get() == oldPalette)
            shade = palette;
    hVObject->ownedPalette = std::move(palette);
    hVObject->p16BPPPalette = hVObject->ownedPalette.get();
    hVObject->pShadeCurrent = hVObject->p16BPPPalette;

    return (TRUE);
}

// Transparency needs to take RGB value and find best fit and place it into DD Surface
// colorkey value.
BOOLEAN SetVideoObjectTransparencyColor(HVOBJECT hVObject, COLORVAL TransColor)
{

    // Assertions
    Assert(hVObject != nullptr);

    //Set trans color into video object
    hVObject->TransparentColor = TransColor;

    return (TRUE);
}

// Deletes all palettes, surfaces and region data
// FUNCTION: WIZ8 0x00406320
BOOLEAN DeleteVideoObject(HVOBJECT hVObject)
{
    CHECKF(hVObject != nullptr);
    delete hVObject;
    return (TRUE);
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
UINT16 CreateObjectPaletteTables(HVOBJECT pObj, UINT32 uiType)
{
    UINT32 count;

    // this creates the highlight table. Specify the glow-type when creating the tables
    // through uiType, symbols are from VOBJECT.H
    for (auto& shade : pObj->pShades)
        shade.reset();

    switch (uiType) {
    case HVOBJECT_GLOW_GREEN: // green glow
        pObj->pShades[0] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 0, 255, 0, TRUE);
        break;
    case HVOBJECT_GLOW_BLUE: // blue glow
        pObj->pShades[0] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 0, 0, 255, TRUE);
        break;
    case HVOBJECT_GLOW_YELLOW: // yellow glow
        pObj->pShades[0] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 255, 255, 0, TRUE);
        break;
    case HVOBJECT_GLOW_RED: // red glow
        pObj->pShades[0] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 255, 0, 0, TRUE);
        break;
    }

    // these are the brightening tables, 115%-150% brighter than original
    pObj->pShades[1] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 293, 293, 293, FALSE);
    pObj->pShades[2] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 281, 281, 281, FALSE);
    pObj->pShades[3] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 268, 268, 268, FALSE);

    // palette 4 is the non-modified palette.
    // if the standard one has already been made, we'll use it
    if (pObj->ownedPalette)
        pObj->pShades[4] = pObj->ownedPalette;
    else {
        // or create our own, and assign it to the standard one
        pObj->pShades[4] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 255, 255, 255, FALSE);
        pObj->ownedPalette = pObj->pShades[4];
        pObj->p16BPPPalette = pObj->ownedPalette.get();
    }

    // the rest are darkening tables, right down to all-black.
    pObj->pShades[5] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 195, 195, 195, FALSE);
    pObj->pShades[6] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 165, 165, 165, FALSE);
    pObj->pShades[7] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 135, 135, 135, FALSE);
    pObj->pShades[8] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 105, 105, 105, FALSE);
    pObj->pShades[9] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 75, 75, 75, FALSE);
    pObj->pShades[10] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 45, 45, 45, FALSE);
    pObj->pShades[11] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 36, 36, 36, FALSE);
    pObj->pShades[12] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 27, 27, 27, FALSE);
    pObj->pShades[13] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 18, 18, 18, FALSE);
    pObj->pShades[14] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 9, 9, 9, FALSE);
    pObj->pShades[15] = Create16BPPPaletteShaded(pObj->pPaletteEntry.get(), 0, 0, 0, FALSE);

    // Set current shade table to neutral color
    pObj->pShadeCurrent = pObj->pShades[4].get();

    // check to make sure every table got a palette
    for (count = 0; (count < HVOBJECT_SHADE_TABLES) && (pObj->pShades[count] != nullptr); count++)
        ;

    // return the result of the check
    return (count == HVOBJECT_SHADE_TABLES);
}

// *******************************************************************
// Blitting Functions
// *******************************************************************

// High level blit function encapsolates ALL effects and BPP
// FUNCTION: WIZ8 0x004066b0
BOOLEAN BltVideoObjectToBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES, HVOBJECT hSrcVObject,
                               UINT16 usIndex, INT32 iDestX, INT32 iDestY, INT32 fBltFlags,
                               blt_fx* pBltFx)
{

    // Assertions
    Assert(pBuffer != nullptr);

    if (hSrcVObject == nullptr) {
    }

    Assert(hSrcVObject != nullptr);

    // Check For Flags and bit depths
    switch (hSrcVObject->ubBitDepth) {
    case 16:

        break;

    case 8:

        // Switch based on flags given
        do {
            if (gbPixelDepth == 16) {
                if (fBltFlags & VO_BLT_MIRROR_Y) {
                    if (!BltIsClipped(hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect))
                        Blt8BPPDataTo16BPPBufferTransMirror(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                            iDestX, iDestY, usIndex);
                    // CLipping version not done -- DB
                    //								Blt8BPPDataTo16BPPBufferTransMirrorClip( pBuffer, uiDestPitchBYTES, hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect);
                    break;
                } else if (fBltFlags & VO_BLT_SRCTRANSPARENCY) {
                    if (BltIsClipped(hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect))
                        Blt8BPPDataTo16BPPBufferTransparentClip(pBuffer, uiDestPitchBYTES,
                                                                hSrcVObject, iDestX, iDestY,
                                                                usIndex, &ClippingRect);
                    else
                        Blt8BPPDataTo16BPPBufferTransparent(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                            iDestX, iDestY, usIndex);
                    break;
                } else if (fBltFlags & VO_BLT_SHADOW) {
                    if (BltIsClipped(hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect))
                        Blt8BPPDataTo16BPPBufferShadowClip(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                           iDestX, iDestY, usIndex, &ClippingRect);
                    else
                        Blt8BPPDataTo16BPPBufferShadow(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                       iDestX, iDestY, usIndex);
                    break;
                }

            } else if (gbPixelDepth == 8) {
                if (fBltFlags & VO_BLT_SRCTRANSPARENCY) {
                    if (BltIsClipped(hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect))
                        Blt8BPPDataTo8BPPBufferTransparentClip(pBuffer, uiDestPitchBYTES,
                                                               hSrcVObject, iDestX, iDestY, usIndex,
                                                               &ClippingRect);
                    else
                        Blt8BPPDataTo8BPPBufferTransparent(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                           iDestX, iDestY, usIndex);
                    break;
                } else if (fBltFlags & VO_BLT_SHADOW) {
                    if (BltIsClipped(hSrcVObject, iDestX, iDestY, usIndex, &ClippingRect))
                        Blt8BPPDataTo8BPPBufferShadowClip(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                          iDestX, iDestY, usIndex, &ClippingRect);
                    else
                        Blt8BPPDataTo8BPPBufferShadow(pBuffer, uiDestPitchBYTES, hSrcVObject,
                                                      iDestX, iDestY, usIndex);
                    break;
                }
            }
            // Use default blitter here
            //Blt8BPPDataTo16BPPBuffer( hDestVObject, hSrcVObject, (UINT16)iDestX, (UINT16)iDestY, (SGPRect*)&SrcRect );

        } while (FALSE);

        break;
    }

    return (TRUE);
}

/**********************************************************************************************
 DestroyObjectPaletteTables

	Destroys the palette tables of a video object. All memory is deallocated, and
	the pointers set to NULL. Be careful not to try and blit this object until new
	tables are calculated, or things WILL go boom.

**********************************************************************************************/
BOOLEAN DestroyObjectPaletteTables(HVOBJECT hVObject)
{
    for (auto& shade : hVObject->pShades)
        shade.reset();
    hVObject->ownedPalette.reset();
    hVObject->p16BPPPalette = nullptr;
    hVObject->pShadeCurrent = nullptr;
    hVObject->pGlow = nullptr;

    return (TRUE);
}

// FUNCTION: WIZ8 0x004068e0
UINT16 SetObjectShade(HVOBJECT pObj, UINT32 uiShade)
{
    Assert(pObj != nullptr);
    Assert(uiShade >= 0);
    Assert(uiShade < HVOBJECT_SHADE_TABLES);

    if (pObj->pShades[uiShade] == nullptr) {
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Attempt to set shade level to nullptr table");
        return (FALSE);
    }

    pObj->pShadeCurrent = pObj->pShades[uiShade].get();
    return (TRUE);
}

/********************************************************************************************
	GetETRLEPixelValue

	Given a VOBJECT and ETRLE image index, retrieves the value of the pixel located at the
	given image coordinates. The value returned is an 8-bit palette index
********************************************************************************************/
// FUNCTION: WIZ8 0x00406900
BOOLEAN GetETRLEPixelValue(UINT8* pDest, HVOBJECT hVObject, UINT16 usETRLEIndex, UINT16 usX,
                           UINT16 usY)
{
    UINT8* pCurrent;
    UINT16 usLoopX = 0;
    UINT16 usLoopY = 0;
    UINT16 ubRunLength;
    ETRLEObject* pETRLEObject;

    // Do a bunch of checks
    CHECKF(hVObject != nullptr);
    CHECKF(usETRLEIndex < hVObject->pETRLEObject.size());

    pETRLEObject = &(hVObject->pETRLEObject[usETRLEIndex]);

    CHECKF(usX < pETRLEObject->usWidth);
    CHECKF(usY < pETRLEObject->usHeight);

    // Both transparent runs and a literal index 0 read back as zero in the
    // decoded surface, which is exactly the value this routine returns.
    if (usETRLEIndex < hVObject->sprites.size() && hVObject->sprites[usETRLEIndex]) {
        const SDL_Surface* sprite = hVObject->sprites[usETRLEIndex].get();
        *pDest = static_cast<const UINT8*>(sprite->pixels)[usY * sprite->pitch + usX];
        return (TRUE);
    }

    // Assuming everything's okay, go ahead and look...
    pCurrent = &(hVObject->pPixData.data())[pETRLEObject->uiDataOffset];

    // Skip past all uninteresting scanlines
    while (usLoopY < usY) {
        while (*pCurrent != 0) {
            if (*pCurrent & COMPRESS_TRANSPARENT) {
                pCurrent++;
            } else {
                pCurrent += *pCurrent & COMPRESS_RUN_MASK;
            }
        }
        usLoopY++;
    }

    // Now look in this scanline for the appropriate byte
    do {
        ubRunLength = *pCurrent & COMPRESS_RUN_MASK;

        if (*pCurrent & COMPRESS_TRANSPARENT) {
            if (usLoopX + ubRunLength >= usX) {
                *pDest = 0;
                return (TRUE);
            } else {
                pCurrent++;
            }
        } else {
            if (usLoopX + ubRunLength >= usX) {
                // skip to the correct byte; skip at least 1 to get past the byte defining the run
                pCurrent += (usX - usLoopX) + 1;
                *pDest = *pCurrent;
                return (TRUE);
            } else {
                pCurrent += ubRunLength + 1;
            }
        }
        usLoopX += ubRunLength;
    } while (usLoopX < usX);
    // huh???
    return (FALSE);
}

// FUNCTION: WIZ8 0x00406a10
BOOLEAN GetVideoObjectETRLEProperties(HVOBJECT hVObject, ETRLEObject* pETRLEObject, UINT16 usIndex)
{
    CHECKF(usIndex >= 0);
    CHECKF(usIndex < hVObject->pETRLEObject.size());

    memcpy(pETRLEObject, &(hVObject->pETRLEObject[usIndex]), sizeof(ETRLEObject));

    return (TRUE);
}

// FUNCTION: WIZ8 0x00406a50
BOOLEAN GetVideoObjectETRLESubregionProperties(UINT32 uiVideoObject, UINT16 usIndex,
                                               UINT16* pusWidth, UINT16* pusHeight)
{
    HVOBJECT hVObject;
    ETRLEObject ETRLEObject;

    // Get video object
#ifdef _DEBUG
    gubVODebugCode = DEBUGSTR_GETVIDEOOBJECTETRLESUBREGIONPROPERTIES;
#endif
    CHECKF(GetVideoObject(&hVObject, uiVideoObject));

    CHECKF(GetVideoObjectETRLEProperties(hVObject, &ETRLEObject, usIndex));

    *pusWidth = ETRLEObject.usWidth;
    *pusHeight = ETRLEObject.usHeight;

    return (TRUE);
}

// FUNCTION: WIZ8 0x00406ad0
BOOLEAN GetVideoObjectETRLEPropertiesFromIndex(UINT32 uiVideoObject, ETRLEObject* pETRLEObject,
                                               UINT16 usIndex)
{
    HVOBJECT hVObject;

    // Get video object
#ifdef _DEBUG
    gubVODebugCode = DEBUGSTR_GETVIDEOOBJECTETRLEPROPERTIESFROMINDEX;
#endif
    CHECKF(GetVideoObject(&hVObject, uiVideoObject));

    CHECKF(GetVideoObjectETRLEProperties(hVObject, pETRLEObject, usIndex));

    return (TRUE);
}

// FUNCTION: WIZ8 0x00406b30
BOOLEAN CopyVideoObjectPalette16BPP(INT32 uiVideoObject, UINT16* ppPal16)
{
    HVOBJECT hVObject;

    // Get video object
#ifdef _DEBUG
    gubVODebugCode = DEBUGSTR_COPYVIDEOOBJECTPALETTE16BPP;
#endif
    CHECKF(GetVideoObject(&hVObject, uiVideoObject));

    memcpy(ppPal16, hVObject->p16BPPPalette, 256 * 2);

    return (TRUE);
}

#ifdef _DEBUG
void CheckValidVObjectIndex(UINT32 uiIndex)
{
    BOOLEAN fAssertError = FALSE;
    if (uiIndex == 0xffffffff) { //-1 index -- deleted
        fAssertError = TRUE;
    }
    if (!(uiIndex % 2) && uiIndex < 0xfffffff0 ||
        uiIndex >=
            0xfffffff0) { //even numbers are reserved for vsurfaces as well as the 0xfffffff0+ values
        fAssertError = TRUE;
    }

    if (fAssertError) {
        UINT8 str[60];
        switch (gubVODebugCode) {
        case DEBUGSTR_SETVIDEOOBJECTTRANSPARENCY:
            sprintf(str, "SetVideoObjectTransparency");
            break;
        case DEBUGSTR_BLTVIDEOOBJECTFROMINDEX:
            sprintf(str, "BltVideoObjectFromIndex");
            break;
        case DEBUGSTR_SETOBJECTHANDLESHADE:
            sprintf(str, "SetObjectHandleShade");
            break;
        case DEBUGSTR_GETVIDEOOBJECTETRLESUBREGIONPROPERTIES:
            sprintf(str, "GetVideoObjectETRLESubRegionProperties");
            break;
        case DEBUGSTR_GETVIDEOOBJECTETRLEPROPERTIESFROMINDEX:
            sprintf(str, "GetVideoObjectETRLEPropertiesFromIndex");
            break;
        case DEBUGSTR_SETVIDEOOBJECTPALETTE8BPP:
            sprintf(str, "SetVideoObjectPalette8BPP");
            break;
        case DEBUGSTR_GETVIDEOOBJECTPALETTE16BPP:
            sprintf(str, "GetVideoObjectPalette16BPP");
            break;
        case DEBUGSTR_COPYVIDEOOBJECTPALETTE16BPP:
            sprintf(str, "CopyVideoObjectPalette16BPP");
            break;
        case DEBUGSTR_BLTVIDEOOBJECTOUTLINEFROMINDEX:
            sprintf(str, "BltVideoObjectOutlineFromIndex");
            break;
        case DEBUGSTR_BLTVIDEOOBJECTOUTLINESHADOWFROMINDEX:
            sprintf(str, "BltVideoObjectOutlineShadowFromIndex");
            break;
        case DEBUGSTR_DELETEVIDEOOBJECTFROMINDEX:
            sprintf(str, "DeleteVideoObjectFromIndex");
            break;
        case DEBUGSTR_NONE:
        default:
            sprintf(str, "GetVideoObject");
            break;
        }
        if (uiIndex == 0xffffffff) {
            AssertMsg(0, FormatString("Trying to %s with deleted index -1.", str));
        } else {
            AssertMsg(0, FormatString("Trying to %s using a VSURFACE ID %d!", str, uiIndex));
        }
    }
}
#endif
