#include <SDL3/SDL_rect.h>
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
#include <algorithm>

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

} // namespace


// GLOBAL: WIZ8 0x00600078
SDL_Rect ClippingRect = {0, 0, 640, 480};

// Decode every ETRLE frame once into SpriteFrame pixels, each strictly
// inside its own recorded byte range. Transparent runs become
// SpriteFrame::kTransparent so a literal index 0 stays distinguishable.
// Every scanline must account for the full frame width before its
// terminator. Decoding is transactional: hVObject->sprites is replaced
// only after all frames validate.
BOOLEAN DecodeVideoObjectSprites(HVOBJECT hVObject, std::span<const ETRLEObject> frames,
                                 std::span<const UINT8> data)
{
    if (!hVObject || (!frames.empty() && data.empty())) {
        return (FALSE);
    }
    std::vector<SpriteFrame> decoded;
    decoded.reserve(frames.size());
    constexpr size_t max_pixels = 256 * 1024 * 1024 / sizeof(UINT16);
    size_t remaining_pixels = max_pixels;
    for (const ETRLEObject& frame : frames) {
        SpriteFrame& out = decoded.emplace_back();
        out.sOffsetX = frame.sOffsetX;
        out.sOffsetY = frame.sOffsetY;
        out.usWidth = frame.usWidth;
        out.usHeight = frame.usHeight;
        if (frame.uiDataOffset > data.size() ||
            frame.uiDataLength > data.size() - frame.uiDataOffset)
            return (FALSE);
        const size_t pixel_count = size_t(frame.usWidth) * frame.usHeight;
        if (pixel_count > remaining_pixels)
            return FALSE;
        remaining_pixels -= pixel_count;
        if (pixel_count == 0)
            continue;
        const UINT8* src = data.data() + frame.uiDataOffset;
        const UINT8* const end = src + frame.uiDataLength;
        out.pixels.assign(static_cast<size_t>(frame.usWidth) * frame.usHeight,
                          SpriteFrame::kTransparent);
        UINT16* row = out.pixels.data();
        for (UINT32 y = 0; y < frame.usHeight; ++y) {
            UINT32 x = 0;
            for (;;) {
                if (src >= end) {
                    return (FALSE);
                }
                const UINT8 control = *src++;
                if (control == 0) {
                    break;
                }
                const UINT32 count = control & 0x7f;
                if (count == 0 || count > frame.usWidth - x) {
                    return (FALSE);
                }
                if (control & 0x80) {
                    x += count;
                } else {
                    if (count > static_cast<UINT32>(end - src)) {
                        return (FALSE);
                    }
                    for (UINT32 n = 0; n < count; ++n)
                        row[x++] = *src++;
                }
            }
            if (x != frame.usWidth) {
                return (FALSE);
            }
            row += frame.usWidth;
        }
    }
    hVObject->sprites = std::move(decoded);
    return (TRUE);
}

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
    SDL_Rect* clipregion = nullptr;

    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);

    // Add to start position of dest buffer
    iTempX = iDestXPos;
    iTempY = iDestYPos;

    if (clipregion == nullptr) {
        ClipX1 = 0;   //ClippingRect.x;
        ClipY1 = 0;   //ClippingRect.y;
        ClipX2 = 640; //(ClippingRect.x + ClippingRect.w);
        ClipY2 = 480; //(ClippingRect.y + ClippingRect.h);
    } else {
        ClipX1 = clipregion->x;
        ClipY1 = clipregion->y;
        ClipX2 = (clipregion->x + clipregion->w);
        ClipY2 = (clipregion->y + clipregion->h);
    }

    // Calculate rows hanging off each side of the screen
    LeftSkip = std::min(ClipX1 - std::min(ClipX1, iTempX), (INT32)uiWidth);
    RightSkip = std::min(std::max(ClipX2, (iTempX + (INT32)uiWidth)) - ClipX2, (INT32)uiWidth);
    TopSkip = std::min(ClipY1 - std::min(ClipY1, iTempY), (INT32)uiHeight);
    BottomSkip = std::min(std::max(ClipY2, (iTempY + (INT32)uiHeight)) - ClipY2, (INT32)uiHeight);

    iTempX = std::max(ClipX1, iDestXPos);
    iTempY = std::max(ClipY1, iDestYPos);

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



/**********************************************************************************************
 Blt8BPPDataSubTo16BPPBuffer

	Blits a subrect from a flat 8 bit surface to a 16-bit buffer.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411730
BOOLEAN Blt8BPPDataSubTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                    HVSURFACE hSrcVSurface, UINT8* pSrcBuffer, UINT32 uiSrcPitch,
                                    INT32 iX, INT32 iY, SDL_Rect* pRect)
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

    LeftSkip = pRect->x;
    TopSkip = pRect->y * uiSrcPitch;
    BlitLength = pRect->w;
    BlitHeight = pRect->h;

    SrcPtr = (UINT8*)(pSrcBuffer + TopSkip + LeftSkip);
    DestPtr = ((UINT8*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2));
    p16BPPPalette = hSrcVSurface->p16BPPPalette.get();

    // Every index writes its LUT word.
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
void SetClippingRect(SDL_Rect* clip)
{
    Assert(clip != nullptr);
    Assert(clip->w > 0);
    Assert(clip->h > 0);

    memcpy(&ClippingRect, clip, sizeof(SDL_Rect));
}

// FUNCTION: WIZ8 0x00411820
void GetClippingRect(SDL_Rect* clip)
{
    Assert(clip != nullptr);

    memcpy(clip, &ClippingRect, sizeof(SDL_Rect));
}

/**********************************************************************************************
 BltSpriteTo16BPPBuffer

	Draws one decoded sprite frame into a 16-bit destination. The sprite rect
	(position plus frame offsets) is intersected with clipregion, or with the
	global ClippingRect when clipregion is nullptr; clipped, unclipped, and
	mirrored draws all take this same path.

	op selects the per-pixel effect: Transparent draws palette-indexed pixels,
	Shadow darkens destination pixels through the sprite mask, and MonoShadow
	turns the sprite into a mono stencil (index 1 = shadow, other opaque
	indices = foreground, transparent = background when nonzero).

**********************************************************************************************/
BOOLEAN BltSpriteTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                               const SpriteFrame& frame, const UINT16* palette, INT32 iX, INT32 iY,
                               const SDL_Rect* clipregion, bool fMirror, SpriteDrawOp op,
                               UINT16 mono_foreground, UINT16 mono_background, UINT16 mono_shadow)
{
    Assert(pBuffer != nullptr);

    const SDL_Rect* clip = clipregion ? clipregion : &ClippingRect;
    // Mirroring flips the sprite about its anchor, so the offset sign flips
    // too and clipping (not rejection) handles edges that leave the buffer.
    const SDL_Rect sprite{fMirror ? iX - frame.sOffsetX : iX + frame.sOffsetX,
                          iY + frame.sOffsetY, frame.usWidth, frame.usHeight};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&sprite, clip, &visible)) {
        return (TRUE);
    }

    const UINT16* const pixels = frame.pixels.data();
    for (INT32 dy = visible.y; dy < visible.y + visible.h; ++dy) {
        const UINT16* row = pixels + (dy - sprite.y) * frame.usWidth;
        UINT8* dest = reinterpret_cast<UINT8*>(pBuffer) + dy * uiDestPitchBYTES + visible.x * 2;
        const INT32 left = visible.x - sprite.x;
        for (INT32 dx = 0; dx < visible.w; ++dx, dest += 2) {
            const INT32 sx = fMirror ? frame.usWidth - 1 - left - dx : left + dx;
            const UINT16 index = row[sx];
            switch (op) {
            case SpriteDrawOp::Transparent:
                if (index != SpriteFrame::kTransparent)
                    NativeWriteWord(dest, palette[index]);
                break;
            case SpriteDrawOp::Shadow:
                if (index != SpriteFrame::kTransparent)
                    NativeWriteWord(dest, ShadeTable[NativeReadWord(dest)]);
                break;
            case SpriteDrawOp::MonoShadow:
                if (index == 1) {
                    if (mono_shadow)
                        NativeWriteWord(dest, mono_shadow);
                } else if (index != SpriteFrame::kTransparent && index != 0)
                    NativeWriteWord(dest, mono_foreground);
                else if (mono_background)
                    NativeWriteWord(dest, mono_background);
                break;
            }
        }
    }
    return (TRUE);
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
BOOLEAN Blt16BPPBufferPixelateRectWithColor(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area,
                                            UINT8 Pattern[8][8], UINT16 usColor)
{
    INT32 width, height;
    UINT16* DestPtr;
    INT32 iLeft, iTop, iRight, iBottom;

    // Assertions
    Assert(pBuffer != nullptr);
    Assert(Pattern != nullptr);

    iLeft = std::max(ClippingRect.x, area->x);
    iTop = std::max(ClippingRect.y, area->y);
    iRight = std::min((ClippingRect.x + ClippingRect.w) - 1, (area->x + area->w));
    iBottom = std::min((ClippingRect.y + ClippingRect.h) - 1, (area->y + area->h));

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
BOOLEAN Blt16BPPBufferHatchRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area)
{
    UINT8 Pattern[8][8] = {
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}};
    return Blt16BPPBufferPixelateRectWithColor(pBuffer, uiDestPitchBYTES, area, Pattern, 0);
}
// FUNCTION: WIZ8 0x004120b0
BOOLEAN BltIsClipped(HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex, SDL_Rect* clipregion)
{
    UINT32 usHeight, usWidth;
    const SpriteFrame* pTrav;
    INT32 iTempX, iTempY;
    INT32 ClipX1, ClipY1, ClipX2, ClipY2;

    // Assertions
    Assert(hSrcVObject != nullptr);

    // Get Offsets from Index into structure
    pTrav = &hSrcVObject->sprites[usIndex];
    usHeight = (UINT32)pTrav->usHeight;
    usWidth = (UINT32)pTrav->usWidth;

    // Add to start position of dest buffer
    iTempX = iX + pTrav->sOffsetX;
    iTempY = iY + pTrav->sOffsetY;

    const SDL_Rect& clip = clipregion ? *clipregion : ClippingRect;
    ClipX1 = clip.x;
    ClipY1 = clip.y;
    ClipX2 = clip.x + clip.w;
    ClipY2 = clip.y + clip.h;

    // Calculate rows hanging off each side of the screen
    if (std::min(ClipX1 - std::min(ClipX1, iTempX), (INT32)usWidth))
        return (TRUE);

    if (std::min(std::max(ClipX2, (iTempX + (INT32)usWidth)) - ClipX2, (INT32)usWidth))
        return (TRUE);

    if (std::min(ClipY1 - std::min(ClipY1, iTempY), (INT32)usHeight))
        return (TRUE);

    if (std::min(std::max(ClipY2, (iTempY + (INT32)usHeight)) - ClipY2, (INT32)usHeight))
        return (TRUE);

    return (FALSE);
}
// FUNCTION: WIZ8 0x004124a0
BOOLEAN Blt16BPPBufferShadowRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area)
{
    INT32 width, height;
    UINT16* DestPtr;

    // Assertions
    Assert(pBuffer != nullptr);

    // Clipping
    if (area->x < ClippingRect.x) {
        area->w += area->x - ClippingRect.x;
        area->x = ClippingRect.x;
    }
    if (area->y < ClippingRect.y) {
        area->h += area->y - ClippingRect.y;
        area->y = ClippingRect.y;
    }
    if ((area->x + area->w) >= (ClippingRect.x + ClippingRect.w))
        area->w = ((ClippingRect.x + ClippingRect.w) - 1) - area->x;
    if ((area->y + area->h) >= (ClippingRect.y + ClippingRect.h))
        area->h = ((ClippingRect.y + ClippingRect.h) - 1) - area->y;
    //CHECKF(area->x >= ClippingRect.x );
    //CHECKF(area->y >= ClippingRect.y );
    //CHECKF((area->x + area->w) <= (ClippingRect.x + ClippingRect.w) );
    //CHECKF((area->y + area->h) <= (ClippingRect.y + ClippingRect.h) );

    DestPtr = (pBuffer + (area->y * (uiDestPitchBYTES / 2)) + area->x);
    width = area->w + 1;
    height = area->h + 1;

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
	area							An SDL_Rect, the area to darken

*********************************************************************************************/
// FUNCTION: WIZ8 0x00412570
BOOLEAN Blt16BPPBufferShadowRectAlternateTable(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                               SDL_Rect* area)
{
    INT32 width, height;
    UINT16* DestPtr;

    // Assertions
    Assert(pBuffer != nullptr);

    // Clipping
    if (area->x < ClippingRect.x) {
        area->w += area->x - ClippingRect.x;
        area->x = ClippingRect.x;
    }
    if (area->y < ClippingRect.y) {
        area->h += area->y - ClippingRect.y;
        area->y = ClippingRect.y;
    }
    if ((area->x + area->w) >= (ClippingRect.x + ClippingRect.w))
        area->w = ((ClippingRect.x + ClippingRect.w) - 1) - area->x;
    if ((area->y + area->h) >= (ClippingRect.y + ClippingRect.h))
        area->h = ((ClippingRect.y + ClippingRect.h) - 1) - area->y;
    //CHECKF(area->x >= ClippingRect.x );
    //CHECKF(area->y >= ClippingRect.y );
    //CHECKF((area->x + area->w) <= (ClippingRect.x + ClippingRect.w) );
    //CHECKF((area->y + area->h) <= (ClippingRect.y + ClippingRect.h) );

    DestPtr = (pBuffer + (area->y * (uiDestPitchBYTES / 2)) + area->x);
    width = area->w + 1;
    height = area->h + 1;

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

    x1real = std::max(0, x1);
    x2real = std::min(639, x2);
    y1real = std::max(0, y1);
    y2real = std::min(479, y2);

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
