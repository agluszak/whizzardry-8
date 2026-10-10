#include <SDL3/SDL_log.h>
/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <stdio.h>
#include "Video2.h" // Wiz8
#include "himage.h"
#include "vobject.h"
#include "WCheck.h"
#include "vobject.h"
#include "vobject_blitters.h"
#include "shading.h"
#include <string.h>

namespace {
// The assembly permits unaligned 16-bit pixels (including in the routines
// named "8BPP ... Shadow"). Keep byte addressing and word loads explicit.
UINT16 NativeReadWord(const UINT8* data)
{
    UINT16 value;
    memcpy(&value, data, sizeof(value));
    return value;
}
void NativeWriteWord(UINT8* data, UINT16 value)
{
    memcpy(data, &value, sizeof(value));
}

template <class Opaque, class Transparent>
void NativeBltETRLE(UINT8* src, UINT8* dest, int height, int step, UINT32 line_skip,
                   Opaque opaque, Transparent transparent)
{
    for (int y = 0; y < height; ++y) {
        UINT8 control;
        while ((control = *src++) != 0) {
            const int count = control & 0x7f;
            for (int x = 0; x < count; ++x) {
                if (control & 0x80) {
                    transparent(dest);
                } else {
                    opaque(dest, *src++);
                }
                dest += step;
            }
        }
        dest += line_skip;
    }
}

template <class Opaque, class Transparent>
void NativeBltETRLEClip(UINT8* src, UINT8* dest, int top_skip, int left_skip,
                       int width, int height, int step, UINT32 line_skip,
                       Opaque opaque, Transparent transparent)
{
    while (top_skip-- > 0) {
        UINT8 control;
        while ((control = *src++) != 0) {
            if (!(control & 0x80)) {
                src += control;
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        int skipped = left_skip;
        int count = 0;
        UINT8 control = 0;
        while (skipped > 0) {
            control = *src++;
            count = control & 0x7f;
            const int advance = count < skipped ? count : skipped;
            if (!(control & 0x80)) {
                src += advance;
            }
            skipped -= advance;
            count -= advance;
        }
        int remaining = width;
        while (remaining > 0) {
            if (count == 0) {
                control = *src++;
                count = control & 0x7f;
            }
            const int visible = count < remaining ? count : remaining;
            for (int x = 0; x < visible; ++x) {
                if (control & 0x80) {
                    transparent(dest);
                } else {
                    opaque(dest, *src++);
                }
                dest += step;
            }
            remaining -= visible;
            if (!(control & 0x80)) {
                src += count - visible;
            }
            count = 0;
        }
        // Retail scans bytes for the first zero after the visible span,
        // rather than decoding the clipped tail's remaining RLE runs.
        while (*src++ != 0) {}
        dest += line_skip;
    }
}
} // namespace


// GLOBAL: WIZ8 0x00600078
SGPRect ClippingRect = {0, 0, 640, 480};
//555      565
UINT32 guiTranslucentMask = 0x3def; //0x7bef;		// mask for halving 5,6,5

// GLOBALS for pre-calculating skip values
INT32 gLeftSkip, gRightSkip, gTopSkip, gBottomSkip;
BOOLEAN gfUsePreCalcSkips = FALSE;

//*Experimental**********************************************************************

/***********************************************************************************/

//** 8 Bit Blitters
//**

/**********************************************************************************************
 Blt8BPPDataTo8BPPBufferMonoShadowClip

	Uses a bitmap an 8BPP template for blitting. Anywhere a 1 appears in the bitmap, a shadow
	is blitted to the destination (a black pixel). Any other value above zero is considered a
	forground color, and zero is background. If the parameter for the background color is zero,
	transparency is used for the background.

	**********************************************************************************************/
// FUNCTION: WIZ8 0x00410750
BOOLEAN Blt8BPPDataTo8BPPBufferMonoShadowClip(UINT8* pBuffer, UINT32 uiDestPitchBYTES,
                                              HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                              UINT16 usIndex, SGPRect* clipregion,
                                              UINT8 ubForeground, UINT8 ubBackground)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // check if whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // check if whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip));
    LineSkip = (uiDestPitchBYTES - (BlitLength));

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 1, LineSkip,
        [&](UINT8* dest, UINT8 index) {
            if (index == 1) { *dest = 0; }
            else if (index != 0) { *dest = ubForeground; }
            else if (ubBackground != 0) { *dest = ubBackground; }
        },
        [&](UINT8* dest) { if (ubBackground != 0) { *dest = ubBackground; } });

    return (TRUE);
}

/******************************************************************************
 Blt8BPPDataTo8BPPBufferTransparentClip

	Blits an image into the destination buffer, using an ETRLE brush as a source, and a 16-bit
	buffer as a destination. Clips the brush.

*******************************************************************************/
// FUNCTION: WIZ8 0x004109f0
BOOLEAN Blt8BPPDataTo8BPPBufferTransparentClip(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                               UINT16 usIndex, SGPRect* clipregion)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;
    UINT8* pPal8BPP;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // check if whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // check if whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip));
    LineSkip = (uiDestPitchBYTES - (BlitLength));
    pPal8BPP = hSrcVObject->pShade8;

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 1, LineSkip,
        [&](UINT8* dest, UINT8 index) { *dest = pPal8BPP[index]; },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 Blt8BPPDataTo8BPPBufferTransparent

	Blits an image into the destination buffer, using an ETRLE brush as a source, and a 16-bit
	buffer as a destination.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00410ca0
BOOLEAN Blt8BPPDataTo8BPPBufferTransparent(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                           HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr, *pPal8BPP;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX);
    LineSkip = (uiDestPitchBYTES - (usWidth));
    pPal8BPP = hSrcVObject->pShade8;

    NativeBltETRLE(SrcPtr, DestPtr, usHeight, 1, LineSkip,
        [&](UINT8* dest, UINT8 index) { *dest = pPal8BPP[index]; },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 Blt8BPPDataTo8BPPBufferShadow

	Modifies the destination buffer. Darkens the destination pixels by 25%, using the source
	image as a mask. Any Non-zero index pixels are used to darken destination pixels.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00410db0
BOOLEAN Blt8BPPDataTo8BPPBufferShadow(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                      HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX);
    LineSkip = (uiDestPitchBYTES - (usWidth));

    NativeBltETRLE(SrcPtr, DestPtr, usHeight, 2, LineSkip,
        [](UINT8* dest, UINT8) { NativeWriteWord(dest, ShadeTable[NativeReadWord(dest)]); },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 Blt8BPPDataTo8BPPBufferShadowClip

	Modifies the destination buffer. Darkens the destination pixels by 25%, using the source
	image as a mask. Any Non-zero index pixels are used to darken destination pixels. Blitter
	clips brush if it doesn't fit on the viewport.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00410ed0
BOOLEAN Blt8BPPDataTo8BPPBufferShadowClip(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                          HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex,
                                          SGPRect* clipregion)
{
    UINT8* pPal8BPP;
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip));
    pPal8BPP = hSrcVObject->pShade8;
    LineSkip = (uiDestPitchBYTES - (BlitLength));

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 2, LineSkip,
        [&](UINT8* dest, UINT8) {
            // Retail reads a word at a byte offset into pShade8. Its caller
            // must supply destination indices within that palette's storage.
            NativeWriteWord(dest, NativeReadWord(pPal8BPP + NativeReadWord(dest)));
        },
        [](UINT8*) {});

    return (TRUE);
}

//** 16 Bit Blitters
//**

/**********************************************************************************************
 Blt8BPPDataTo16BPPBufferMonoShadowClip

	Uses a bitmap an 8BPP template for blitting. Anywhere a 1 appears in the bitmap, a shadow
	is blitted to the destination (a black pixel). Any other value above zero is considered a
	forground color, and zero is background. If the parameter for the background color is zero,
	transparency is used for the background.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411190
BOOLEAN Blt8BPPDataTo16BPPBufferMonoShadowClip(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                               UINT16 usIndex, SGPRect* clipregion,
                                               UINT16 usForeground, UINT16 usBackground,
                                               UINT16 usShadow)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // check if whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // check if whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip) * 2);
    LineSkip = (uiDestPitchBYTES - (BlitLength * 2));

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 2, LineSkip,
        [&](UINT8* dest, UINT8 index) {
            if (index == 1) {
                if (usShadow != 0) { NativeWriteWord(dest, usShadow); }
            } else if (index != 0) { NativeWriteWord(dest, usForeground); }
            else if (usBackground != 0) { NativeWriteWord(dest, usBackground); }
        },
        [&](UINT8* dest) { if (usBackground != 0) { NativeWriteWord(dest, usBackground); } });

    return (TRUE);
}

/**********************************************************************************************
	Blt16BPPTo16BPP

	Copies a rect of 16 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411430
BOOLEAN Blt16BPPTo16BPP(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                        INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                        UINT32 uiWidth, UINT32 uiHeight)
{
    UINT16 *pSrcPtr, *pDestPtr;

    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);

    pSrcPtr = (UINT16*)((UINT8*)pSrc + (iSrcYPos * uiSrcPitch) + (iSrcXPos * 2));
    pDestPtr = (UINT16*)((UINT8*)pDest + (iDestYPos * uiDestPitch) + (iDestXPos * 2));

    const UINT8* src = reinterpret_cast<const UINT8*>(pSrcPtr);
    UINT8* dest = reinterpret_cast<UINT8*>(pDestPtr);
    for (UINT32 y = 0; y < uiHeight; ++y) {
        // Match the MOVSB/MOVSW prefix and forward MOVSD groups, including
        // when the source and destination regions overlap.
        UINT32 offset = 0;
        if (uiWidth & 1) {
            NativeWriteWord(dest, NativeReadWord(src));
            offset = 2;
        }
        for (; offset < uiWidth * 2; offset += 4) {
            UINT32 value;
            memcpy(&value, src + offset, sizeof(value));
            memcpy(dest + offset, &value, sizeof(value));
        }
        src += uiSrcPitch;
        dest += uiDestPitch;
    }

    return (TRUE);
}

/**********************************************************************************************
	Blt16BPPTo16BPPTrans

	Copies a rect of 16 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip. Transparent color is
	not copied.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004114c0
BOOLEAN Blt16BPPTo16BPPTrans(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                             INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                             UINT32 uiWidth, UINT32 uiHeight, UINT16 usTrans)
{
    UINT16 *pSrcPtr, *pDestPtr;

    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);

    pSrcPtr = (UINT16*)((UINT8*)pSrc + (iSrcYPos * uiSrcPitch) + (iSrcXPos * 2));
    pDestPtr = (UINT16*)((UINT8*)pDest + (iDestYPos * uiDestPitch) + (iDestXPos * 2));

    const UINT8* src = reinterpret_cast<const UINT8*>(pSrcPtr);
    UINT8* dest = reinterpret_cast<UINT8*>(pDestPtr);
    for (UINT32 y = 0; y < uiHeight; ++y) {
        for (UINT32 x = 0; x < uiWidth; ++x) {
            if (NativeReadWord(src + x * 2) != usTrans) {
                NativeWriteWord(dest + x * 2, NativeReadWord(src + x * 2));
            }
        }
        src += uiSrcPitch;
        dest += uiDestPitch;
    }

    return (TRUE);
}

/**********************************************************************************************
	Blt16BPPTo16BPPMirror

	Copies a rect of 16 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411540
BOOLEAN Blt16BPPTo16BPPMirror(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                              INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                              UINT32 uiWidth, UINT32 uiHeight)
{
    UINT16 *pSrcPtr, *pDestPtr;
    INT32 RightSkip, LeftSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 iTempX, iTempY, ClipX1, ClipY1, ClipX2, ClipY2;
    SGPRect* clipregion = nullptr;

    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);

    // Add to start position of dest buffer
    iTempX = iDestXPos;
    iTempY = iDestYPos;

    if (clipregion == nullptr) {
        ClipX1 = 0;   //ClippingRect.iLeft;
        ClipY1 = 0;   //ClippingRect.iTop;
        ClipX2 = 640; //ClippingRect.iRight;
        ClipY2 = 480; //ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - __min(ClipX1, iTempX), (INT32)uiWidth);
    RightSkip = __min(__max(ClipX2, (iTempX + (INT32)uiWidth)) - ClipX2, (INT32)uiWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)uiHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)uiHeight)) - ClipY2, (INT32)uiHeight);

    iTempX = __max(ClipX1, iDestXPos);
    iTempY = __max(ClipY1, iDestYPos);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)uiWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)uiHeight - TopSkip - BottomSkip);

    // check if whole thing is clipped
    if ((LeftSkip >= (INT32)uiWidth) || (RightSkip >= (INT32)uiWidth))
        return (TRUE);

    // check if whole thing is clipped
    if ((TopSkip >= (INT32)uiHeight) || (BottomSkip >= (INT32)uiHeight))
        return (TRUE);

    pSrcPtr = (UINT16*)((UINT8*)pSrc + (TopSkip * uiSrcPitch) + (RightSkip * 2));
    pDestPtr =
        (UINT16*)((UINT8*)pDest + (iTempY * uiDestPitch) + (iTempX * 2) + ((BlitLength - 1) * 2));

    const UINT8* src = reinterpret_cast<const UINT8*>(pSrcPtr);
    UINT8* dest = reinterpret_cast<UINT8*>(pDestPtr);
    for (INT32 y = 0; y < BlitHeight; ++y) {
        for (INT32 x = 0; x < BlitLength; ++x) {
            NativeWriteWord(dest - x * 2, NativeReadWord(src + x * 2));
        }
        src += uiSrcPitch;
        dest += uiDestPitch;
    }

    return (TRUE);
}

/***********************************************************************************************
	Blt8BPPTo8BPP

	Copies a rect of an 8 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004116c0
BOOLEAN Blt8BPPTo8BPP(UINT8* pDest, UINT32 uiDestPitch, UINT8* pSrc, UINT32 uiSrcPitch,
                      INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                      UINT32 uiWidth, UINT32 uiHeight)
{
    UINT8 *pSrcPtr, *pDestPtr;

    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);

    pSrcPtr = pSrc + (iSrcYPos * uiSrcPitch) + (iSrcXPos);
    pDestPtr = pDest + (iDestYPos * uiDestPitch) + (iDestXPos);

    const UINT8* src = reinterpret_cast<const UINT8*>(pSrcPtr);
    UINT8* dest = reinterpret_cast<UINT8*>(pDestPtr);
    for (UINT32 y = 0; y < uiHeight; ++y) {
        // Match the MOVSB/MOVSW prefix and forward MOVSD groups, including
        // when the source and destination regions overlap.
        UINT32 offset = 0;
        if (uiWidth & 1) {
            dest[offset] = src[offset];
            ++offset;
        }
        if (uiWidth & 2) {
            NativeWriteWord(dest + offset, NativeReadWord(src + offset));
            offset += 2;
        }
        for (; offset < uiWidth * 1; offset += 4) {
            UINT32 value;
            memcpy(&value, src + offset, sizeof(value));
            memcpy(dest + offset, &value, sizeof(value));
        }
        src += uiSrcPitch;
        dest += uiDestPitch;
    }

    return (TRUE);
}

#if 0

BlitNTL4:

		// TEST FOR Z FIRST!
		mov		ax, [ebx]
		cmp		ax, usZValue
		ja		BlitNTL8

		// Write it NOW!
		jmp		BlitNTL7

BlitNTL8:

		test	uiLineFlag, 1
		jz		BlitNTL6

		test	edi, 2
		jz		BlitNTL5
		jmp		BlitNTL9

BlitNTL6:
		test	edi, 2
		jnz		BlitNTL5

BlitNTL7:

		// Write normal z value
		mov		ax, usZValue
		mov		[ebx], ax
		jmp   BlitNTL10

BlitNTL9:

		// Write high z
		mov		ax, 32767
		mov		[ebx], ax

BlitNTL10:

		xor		eax, eax
		mov		al, [esi]
		mov		ax, [edx+eax*2]
		mov		[edi], ax
#endif

/**********************************************************************************************
 Blt8BPPDataSubTo16BPPBuffer

	Blits a subrect from a flat 8 bit surface to a 16-bit buffer.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411730
BOOLEAN Blt8BPPDataSubTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                    HVSURFACE hSrcVSurface, UINT8* pSrcBuffer, UINT32 uiSrcPitch,
                                    INT32 iX, INT32 iY, SGPRect* pRect)
{
    UINT16* p16BPPPalette;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LeftSkip, TopSkip, BlitLength, BlitHeight;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVSurface != nullptr);
    Assert(pSrcBuffer != nullptr);
    Assert(pBuffer != nullptr);

    // Add to start position of dest buffer
    iTempX = iX;
    iTempY = iY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    LeftSkip = pRect->iLeft;
    TopSkip = pRect->iTop * uiSrcPitch;
    BlitLength = pRect->iRight - pRect->iLeft;
    BlitHeight = pRect->iBottom - pRect->iTop;

    SrcPtr = (UINT8*)(pSrcBuffer + TopSkip + LeftSkip);
    DestPtr = ((UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2));
    p16BPPPalette = hSrcVSurface->p16BPPPalette.get();

    for (UINT32 y = 0; y < BlitHeight; ++y) {
        for (UINT32 x = 0; x < BlitLength; ++x) {
            NativeWriteWord(DestPtr + x * 2, p16BPPPalette[SrcPtr[x]]);
        }
        SrcPtr += uiSrcPitch;
        DestPtr += uiDestPitchBYTES;
    }

    return (TRUE);
}

/****************************INCOMPLETE***********************************************/

// FUNCTION: WIZ8 0x004117f0
void SetClippingRect(SGPRect* clip)
{
    Assert(clip != nullptr);
    Assert(clip->iLeft < clip->iRight);
    Assert(clip->iTop < clip->iBottom);

    memcpy(&ClippingRect, clip, sizeof(SGPRect));
}

// FUNCTION: WIZ8 0x00411820
void GetClippingRect(SGPRect* clip)
{
    Assert(clip != nullptr);

    memcpy(clip, &ClippingRect, sizeof(SGPRect));
}

/**********************************************************************************************
	Blt16BPPBufferPixelateRectWithColor

		Given an 8x8 pattern and a color, pixelates an area by repeatedly "applying the color" to pixels whereever there
		is a non-zero value in the pattern.

		KM:  Added Nov. 23, 1998
		This is all the code that I moved from Blt16BPPBufferPixelateRect().
		This function now takes a color field (which previously was
		always black.  The 3rd assembler line in this function:

				mov		ax, usColor				// color of pixel

		used to be:

				xor   eax, eax					// color of pixel (black or 0)

	  This was the only internal modification I made other than adding the usColor argument.

*********************************************************************************************/
// FUNCTION: WIZ8 0x00411850
BOOLEAN Blt16BPPBufferPixelateRectWithColor(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SGPRect* area,
                                            UINT8 Pattern[8][8], UINT16 usColor)
{
    INT32 width, height;
    UINT16* DestPtr;
    INT32 iLeft, iTop, iRight, iBottom;

    // Assertions
    Assert(pBuffer != nullptr);
    Assert(Pattern != nullptr);

    iLeft = __max(ClippingRect.iLeft, area->iLeft);
    iTop = __max(ClippingRect.iTop, area->iTop);
    iRight = __min(ClippingRect.iRight - 1, area->iRight);
    iBottom = __min(ClippingRect.iBottom - 1, area->iBottom);

    DestPtr = (pBuffer + (iTop * (uiDestPitchBYTES / 2)) + iLeft);
    width = iRight - iLeft + 1;
    height = iBottom - iTop + 1;

    CHECKF(width >= 1);
    CHECKF(height >= 1);

    UINT8* dest = reinterpret_cast<UINT8*>(DestPtr);
    for (INT32 y = 0; y < height; ++y) {
        for (INT32 x = 0; x < width; ++x) {
            // The first column of each row uses pattern[0][0]. After that
            // EBX includes the row bits, as in the original instruction loop.
            if (x == 0 ? Pattern[0][0] : Pattern[y & 7][x & 7]) {
                NativeWriteWord(dest + x * 2, usColor);
            }
        }
        dest += uiDestPitchBYTES;
    }

    return (TRUE);
}

//Uses black hatch color
// FUNCTION: WIZ8 0x00411930
BOOLEAN Blt16BPPBufferHatchRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SGPRect* area)
{
    UINT8 Pattern[8][8] = {
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}};
    return Blt16BPPBufferPixelateRectWithColor(pBuffer, uiDestPitchBYTES, area, Pattern, 0);
}

/**********************************************************************************************
 Blt8BPPDataTo16BPPBufferShadow

	Modifies the destination buffer. Darkens the destination pixels by 25%, using the source
	image as a mask. Any Non-zero index pixels are used to darken destination pixels.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411a60
BOOLEAN Blt8BPPDataTo16BPPBufferShadow(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                       HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2);
    LineSkip = (uiDestPitchBYTES - (usWidth * 2));

    NativeBltETRLE(SrcPtr, DestPtr, usHeight, 2, LineSkip,
        [](UINT8* dest, UINT8) { NativeWriteWord(dest, ShadeTable[NativeReadWord(dest)]); },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 Blt8BPPDataTo16BPPBufferTransparent

	Blits an image into the destination buffer, using an ETRLE brush as a source, and a 16-bit
	buffer as a destination.

**********************************************************************************************/

// FUNCTION: WIZ8 0x00411b80
BOOLEAN Blt8BPPDataTo16BPPBufferTransparent(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                            HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                            UINT16 usIndex)
{
    UINT16* p16BPPPalette;
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2);
    p16BPPPalette = hSrcVObject->pShadeCurrent;
    LineSkip = (uiDestPitchBYTES - (usWidth * 2));

    NativeBltETRLE(SrcPtr, DestPtr, usHeight, 2, LineSkip,
        [&](UINT8* dest, UINT8 index) { NativeWriteWord(dest, p16BPPPalette[index]); },
        [](UINT8*) {});

    return (TRUE);
}

// Blt8BPPDataTo16BPPBufferTransMirror
// Blits an 8bpp ETRLE to a 16-bit buffer, mirroring the image, with transparency.
// Returns BOOLEAN            - TRUE if successful
//  UINT16 *pBuffer           - 16bpp Destination buffer
// UINT32 uiDestPitchBYTES    - Destination pitch in bytes
// HVOBJECT hSrcVObject       - Source VOBJECT handle
// INT32 iX                   - X-location of blit
// INT32 iY                   - Y-location of blit
// UINT16 usIndex             - VOBJECT image index to blit from
// Created:  7/28/99 Derek Beland

// FUNCTION: WIZ8 0x00411cb0
BOOLEAN Blt8BPPDataTo16BPPBufferTransMirror(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                            HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                            UINT16 usIndex)
{
    UINT16* p16BPPPalette;
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 uiDestSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    //	iTempX = iX + pTrav->sOffsetX;
    iTempX = iX + usWidth - pTrav->sOffsetX - 1;
    iTempY = iY + pTrav->sOffsetY;

    // Validations
    CHECKF(iTempX >= 0);
    CHECKF(iTempY >= 0);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2);
    p16BPPPalette = hSrcVObject->pShadeCurrent;
    uiDestSkip = (uiDestPitchBYTES + (usWidth * 2));

    NativeBltETRLE(SrcPtr, DestPtr, usHeight, -2, uiDestSkip,
        [&](UINT8* dest, UINT8 index) { NativeWriteWord(dest, p16BPPPalette[index]); },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 Blt8BPPDataTo16BPPBufferTransparentClip

	Blits an image into the destination buffer, using an ETRLE brush as a source, and a 16-bit
	buffer as a destination. Clips the brush.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411de0
BOOLEAN Blt8BPPDataTo16BPPBufferTransparentClip(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                                HVOBJECT hSrcVObject, INT32 iX, INT32 iY,
                                                UINT16 usIndex, SGPRect* clipregion)
{
    UINT16* p16BPPPalette;
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // check if whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // check if whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip) * 2);
    p16BPPPalette = hSrcVObject->pShadeCurrent;
    LineSkip = (uiDestPitchBYTES - (BlitLength * 2));

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 2, LineSkip,
        [&](UINT8* dest, UINT8 index) { NativeWriteWord(dest, p16BPPPalette[index]); },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
 BltIsClipped

	Determines whether a given blit will need clipping or not. Returns TRUE/FALSE.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004120b0
BOOLEAN BltIsClipped(HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex, SGPRect* clipregion)
{
    UINT32 usHeight, usWidth;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    if (__min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth))
        return (TRUE);

    if (__min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth))
        return (TRUE);

    if (__min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight))
        return (TRUE);

    if (__min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight))
        return (TRUE);

    return (FALSE);
}

/**********************************************************************************************
 Blt8BPPDataTo16BPPBufferShadowClip

	Modifies the destination buffer. Darkens the destination pixels by 25%, using the source
	image as a mask. Any Non-zero index pixels are used to darken destination pixels. Blitter
	clips brush if it doesn't fit on the viewport.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004121e0
BOOLEAN Blt8BPPDataTo16BPPBufferShadowClip(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                           HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex,
                                           SGPRect* clipregion)
{
    UINT32 uiOffset;
    UINT32 usHeight, usWidth;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LineSkip;
    ETRLEObject* pTrav;
    INT32 iTempX, iTempY, LeftSkip, RightSkip, TopSkip, BottomSkip, BlitLength, BlitHeight;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);
    Assert(pBuffer != nullptr);

    // Get Offsets from Index into structure
    pTrav = &(hSrcVObject->pETRLEObject[usIndex]);
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;
    uiOffset = pTrav->uiDataOffset;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    if (clipregion == nullptr) {
        ClipX1 = ClippingRect.iLeft;
        ClipY1 = ClippingRect.iTop;
        ClipX2 = ClippingRect.iRight;
        ClipY2 = ClippingRect.iBottom;
    } else {
        ClipX1 = clipregion->iLeft;
        ClipY1 = clipregion->iTop;
        ClipX2 = clipregion->iRight;
        ClipY2 = clipregion->iBottom;
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = __min(ClipX1 - min(ClipX1, iTempX), (INT32)usWidth);
    RightSkip = __min(max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth);
    TopSkip = __min(ClipY1 - __min(ClipY1, iTempY), (INT32)usHeight);
    BottomSkip = __min(__max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight);

    // calculate the remaining rows and columns to blit
    BlitLength = ((INT32)usWidth - LeftSkip - RightSkip);
    BlitHeight = ((INT32)usHeight - TopSkip - BottomSkip);

    // whole thing is clipped
    if ((LeftSkip >= (INT32)usWidth) || (RightSkip >= (INT32)usWidth))
        return (TRUE);

    // whole thing is clipped
    if ((TopSkip >= (INT32)usHeight) || (BottomSkip >= (INT32)usHeight))
        return (TRUE);

    SrcPtr = hSrcVObject->pPixData.data() + uiOffset;
    DestPtr = (UINT8*)pBuffer + (uiDestPitchBYTES * (iTempY + TopSkip)) + ((iTempX + LeftSkip) * 2);
    LineSkip = (uiDestPitchBYTES - (BlitLength * 2));

    NativeBltETRLEClip(SrcPtr, DestPtr, TopSkip, LeftSkip, BlitLength, BlitHeight, 2, LineSkip,
        [](UINT8* dest, UINT8) { NativeWriteWord(dest, ShadeTable[NativeReadWord(dest)]); },
        [](UINT8*) {});

    return (TRUE);
}

/**********************************************************************************************
	Blt16BPPBufferShadowRect

		Darkens a rectangular area by 25%. This blitter is used by ShadowVideoObjectRect.

	pBuffer						Pointer to a 16BPP buffer
	uiDestPitchBytes	Pitch of the destination surface
	area							An SGPRect, the area to darken

*********************************************************************************************/
// FUNCTION: WIZ8 0x004124a0
BOOLEAN Blt16BPPBufferShadowRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SGPRect* area)
{
    INT32 width, height;
    UINT16* DestPtr;

    // Assertions
    Assert(pBuffer != nullptr);

    // Clipping
    if (area->iLeft < ClippingRect.iLeft)
        area->iLeft = ClippingRect.iLeft;
    if (area->iTop < ClippingRect.iTop)
        area->iTop = ClippingRect.iTop;
    if (area->iRight >= ClippingRect.iRight)
        area->iRight = ClippingRect.iRight - 1;
    if (area->iBottom >= ClippingRect.iBottom)
        area->iBottom = ClippingRect.iBottom - 1;
    //CHECKF(area->iLeft >= ClippingRect.iLeft );
    //CHECKF(area->iTop >= ClippingRect.iTop );
    //CHECKF(area->iRight <= ClippingRect.iRight );
    //CHECKF(area->iBottom <= ClippingRect.iBottom );

    DestPtr = (pBuffer + (area->iTop * (uiDestPitchBYTES / 2)) + area->iLeft);
    width = area->iRight - area->iLeft + 1;
    height = area->iBottom - area->iTop + 1;

    CHECKF(width >= 1);
    CHECKF(height >= 1);

    UINT8* dest = reinterpret_cast<UINT8*>(DestPtr);
    for (INT32 y = 0; y < height; ++y) {
        for (INT32 x = 0; x < width; ++x) {
            NativeWriteWord(dest + x * 2, ShadeTable[NativeReadWord(dest + x * 2)]);
        }
        dest += uiDestPitchBYTES;
    }

    return (TRUE);
}

/**********************************************************************************************
	Blt16BPPBufferShadowRect

		Darkens a rectangular area by 25%. This blitter is used by ShadowVideoObjectRect.

	pBuffer						Pointer to a 16BPP buffer
	uiDestPitchBytes	Pitch of the destination surface
	area							An SGPRect, the area to darken

*********************************************************************************************/
// FUNCTION: WIZ8 0x00412570
BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               SGPRect* area)
{
    INT32 width, height;
    UINT16* DestPtr;

    // Assertions
    Assert(pBuffer != nullptr);

    // Clipping
    if (area->iLeft < ClippingRect.iLeft)
        area->iLeft = ClippingRect.iLeft;
    if (area->iTop < ClippingRect.iTop)
        area->iTop = ClippingRect.iTop;
    if (area->iRight >= ClippingRect.iRight)
        area->iRight = ClippingRect.iRight - 1;
    if (area->iBottom >= ClippingRect.iBottom)
        area->iBottom = ClippingRect.iBottom - 1;
    //CHECKF(area->iLeft >= ClippingRect.iLeft );
    //CHECKF(area->iTop >= ClippingRect.iTop );
    //CHECKF(area->iRight <= ClippingRect.iRight );
    //CHECKF(area->iBottom <= ClippingRect.iBottom );

    DestPtr = (pBuffer + (area->iTop * (uiDestPitchBYTES / 2)) + area->iLeft);
    width = area->iRight - area->iLeft + 1;
    height = area->iBottom - area->iTop + 1;

    CHECKF(width >= 1);
    CHECKF(height >= 1);

    UINT8* dest = reinterpret_cast<UINT8*>(DestPtr);
    for (INT32 y = 0; y < height; ++y) {
        for (INT32 x = 0; x < width; ++x) {
            NativeWriteWord(dest + x * 2, IntensityTable[NativeReadWord(dest + x * 2)]);
        }
        dest += uiDestPitchBYTES;
    }

    return (TRUE);
}

// UTILITY FUNCTIONS FOR BLITTING

// FUNCTION: WIZ8 0x00412640
BOOLEAN FillRect16BPP(UINT16* pBuffer, UINT32 uiDestPitchBYTES, INT32 x1, INT32 y1, INT32 x2,
                      INT32 y2, UINT16 color)
{
    INT32 x1real, y1real, x2real, y2real;
    UINT32 linelength, lines;
    UINT16* startoffset;

    // check parameters
    Assert(pBuffer != nullptr);
    Assert(uiDestPitchBYTES > 0);
    Assert(x2 > x1);
    Assert(y2 > y1);

    // clip edges of rect if hanging off screen

    x1real = __max(0, x1);
    x2real = __min(639, x2);
    y1real = __max(0, y1);
    y2real = __min(479, y2);

    startoffset = pBuffer + (y1real * uiDestPitchBYTES / 2) + x1real;
    lines = y2real - y1real + 1;
    linelength = x2real - x1real + 1;

    UINT8* dest = reinterpret_cast<UINT8*>(startoffset);
    for (UINT32 y = 0; y < lines; ++y) {
        for (UINT32 x = 0; x < linelength; ++x) {
            NativeWriteWord(dest + x * 2, color);
        }
        dest += uiDestPitchBYTES;
    }
    return (TRUE);
}
