/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_pixels.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>

#include "Types.h"
#include "vsurface.h"
#include "himage.h"
#include "vobject.h"
#include "WCheck.h"
#include "vobject_blitters.h"
#include "shading.h"

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
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    const SDL_Rect destination{iDestXPos, iDestYPos, int(uiWidth), int(uiHeight)};
    const SDL_Rect bounds{0, 0, 640, 480};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&destination, &bounds, &visible))
        return TRUE;
    // This entry point ignores the source origin and propagates overlapping writes
    // from left to right while writing the destination from right to left.
    const int topSkip = visible.y - destination.y;
    const int rightSkip = destination.x + destination.w - visible.x - visible.w;
    const auto* source = reinterpret_cast<const UINT8*>(pSrc) + topSkip * uiSrcPitch + rightSkip * 2;
    auto* dest = reinterpret_cast<UINT8*>(pDest) + visible.y * uiDestPitch +
                 (visible.x + visible.w - 1) * 2;
    for (int y = 0; y < visible.h; ++y, source += uiSrcPitch, dest += uiDestPitch)
        for (int x = 0; x < visible.w; ++x)
            NativeWriteWord(dest - x * 2, NativeReadWord(source + x * 2));
    return TRUE;
}

/**********************************************************************************************
 Blt8BPPDataSubTo16BPPBuffer

	Blits a subrect from a flat 8 bit surface to a 16-bit buffer.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411730
BOOLEAN Blt8BPPDataSubTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                    const Palette16& palette, const UINT8* pSrcBuffer,
                                    UINT32 uiSrcPitch, INT32 iX, INT32 iY, const SDL_Rect& source)
{
    Assert(pSrcBuffer != nullptr);
    Assert(pBuffer != nullptr);
    CHECKF(iX >= 0);
    CHECKF(iY >= 0);

    const UINT8* src = pSrcBuffer + source.y * uiSrcPitch + source.x;
    auto* dest = reinterpret_cast<UINT8*>(pBuffer) + uiDestPitchBYTES * iY + iX * 2;
    for (int y = 0; y < source.h; ++y, src += uiSrcPitch, dest += uiDestPitchBYTES)
        for (int x = 0; x < source.w; ++x)
            NativeWriteWord(dest + x * 2, palette[src[x]]);
    return TRUE;
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
    Assert(pBuffer != nullptr);
    Assert(Pattern != nullptr);

    // These old SGP rectangles store inclusive endpoint deltas, not SDL sizes.
    const SDL_Rect requested{area->x, area->y, area->w + 1, area->h + 1};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&requested, &ClippingRect, &visible))
        return FALSE;

    auto* dest = reinterpret_cast<UINT8*>(pBuffer) + visible.y * uiDestPitchBYTES + visible.x * 2;
    for (int y = 0; y < visible.h; ++y) {
        for (int x = 0; x < visible.w; ++x) {
            // The first column of every row uses pattern[0][0].
            if (x == 0 ? Pattern[0][0] : Pattern[y & 7][x & 7])
                NativeWriteWord(dest + x * 2, usColor);
        }
        dest += uiDestPitchBYTES;
    }
    return TRUE;
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
BOOLEAN BltIsClipped(SGPVObject& object, INT32 x, INT32 y, UINT16 index, SDL_Rect* clipregion)
{
    const SpriteFrame& frame = object.sprites[index];
    if (frame.usWidth == 0 || frame.usHeight == 0)
        return FALSE;

    const SDL_Rect sprite{x + frame.sOffsetX, y + frame.sOffsetY,
                          frame.usWidth, frame.usHeight};
    const SDL_Rect& clip = clipregion ? *clipregion : ClippingRect;
    SDL_Rect visible;
    return !SDL_GetRectIntersection(&sprite, &clip, &visible) ||
           !SDL_RectsEqual(&sprite, &visible);
}
/**********************************************************************************************
	Blt16BPPBufferShadowRect

		Darkens a rectangular area through ShadeTable. This blitter is used by
		ShadowVideoSurfaceRect.

	pBuffer						Pointer to a 16BPP buffer
	uiDestPitchBytes	Pitch of the destination surface
	area							An SDL_Rect, the area to darken

*********************************************************************************************/
// FUNCTION: WIZ8 0x004124a0
BOOLEAN Blt16BPPBufferShadowRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area)
{
    Assert(pBuffer != nullptr);

    // Preserve the legacy inclusive endpoint deltas, including mutation of
    // the caller's rectangle after clipping.
    const SDL_Rect requested{area->x, area->y, area->w + 1, area->h + 1};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&requested, &ClippingRect, &visible))
        return FALSE;
    *area = {visible.x, visible.y, visible.w - 1, visible.h - 1};

    auto* dest = reinterpret_cast<UINT8*>(pBuffer) + visible.y * uiDestPitchBYTES + visible.x * 2;
    for (int y = 0; y < visible.h; ++y) {
        for (int x = 0; x < visible.w; ++x) {
            UINT8* pixel = dest + x * 2;
            NativeWriteWord(pixel, ShadeTable[NativeReadWord(pixel)]);
        }
        dest += uiDestPitchBYTES;
    }
    return TRUE;
}
