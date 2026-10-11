/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_pixels.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "Types.h"
#include "vsurface.h"
#include "wiz8/compat/surfaces.h"
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

// Decode every ETRLE frame once into SpriteFrame pixels, each strictly
// inside its own recorded byte range. Transparent runs become
// SpriteFrame::kTransparent so a literal index 0 stays distinguishable.
// Every scanline must account for the full frame width before its
// terminator. Decoding is transactional: hVObject.sprites is replaced
// only after all frames validate.
BOOLEAN DecodeVideoObjectSprites(SGPVObject& hVObject, std::span<const ETRLEObject> frames,
                                 std::span<const UINT8> data)
{
    if (!frames.empty() && data.empty()) {
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
    hVObject.sprites = std::move(decoded);
    return (TRUE);
}

// Ordinary buffer copies share the CPU surface backend. The legacy entry points
// deliberately propagate writes through overlapping input; SDL snapshots instead,
// so only that unsupported case uses the shared forward-copy loop below.
static void BlitPackedBuffers(void* destination, UINT32 destinationPitch,
                              void* source, UINT32 sourcePitch, UINT32 width,
                              UINT32 height, unsigned bytes,
                              std::optional<SurfaceColorKey> key = std::nullopt)
{
    if (!width || !height)
        return;
    const auto dest = reinterpret_cast<uintptr_t>(destination);
    const auto src = reinterpret_cast<uintptr_t>(source);
    if (dest < src + size_t(height - 1) * sourcePitch + width * bytes &&
        src < dest + size_t(height - 1) * destinationPitch + width * bytes) {
        auto* output = static_cast<UINT8*>(destination);
        const auto* input = static_cast<const UINT8*>(source);
        const unsigned rowBytes = width * bytes;
        for (unsigned y = 0; y < height; ++y, output += destinationPitch, input += sourcePitch) {
            if (key) {
                for (unsigned x = 0; x < rowBytes; x += 2)
                    if (NativeReadWord(input + x) != key->low)
                        NativeWriteWord(output + x, NativeReadWord(input + x));
            } else {
                // MOVSB/MOVSW prefix followed by forward MOVSD groups.
                unsigned offset = 0;
                for (unsigned chunk : {1u, 2u, 4u}) {
                    const unsigned end = chunk == 4 ? rowBytes : offset + (rowBytes & chunk);
                    for (; offset < end; offset += chunk) {
                        UINT32 value;
                        memcpy(&value, input + offset, chunk);
                        memcpy(output + offset, &value, chunk);
                    }
                }
            }
        }
        return;
    }
    const auto format = bytes == 1 ? SDL_PIXELFORMAT_INDEX8 : SDL_PIXELFORMAT_RGB565;
    auto outputStorage = CreateCpuStorage(width, height, format, destination, destinationPitch);
    auto inputStorage = CreateCpuStorage(width, height, format, source, sourcePitch);
    auto output = ShareCpuSurface(*outputStorage);
    auto input = ShareCpuSurface(*inputStorage);
    input.sourceKey = key;
    BlitCpuSurface(output, nullptr, input, nullptr, key.has_value());
}

// FUNCTION: WIZ8 0x00411430
BOOLEAN Blt16BPPTo16BPP(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                        INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                        UINT32 uiWidth, UINT32 uiHeight)
{
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<UINT8*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 2;
    auto* destination = reinterpret_cast<UINT8*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 2;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 2);
    return TRUE;
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
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<UINT8*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 2;
    auto* destination = reinterpret_cast<UINT8*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 2;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 2, SurfaceColorKey{usTrans, usTrans});
    return TRUE;
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
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<UINT8*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 1;
    auto* destination = reinterpret_cast<UINT8*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 1;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 1);
    return TRUE;
}



/**********************************************************************************************
 Blt8BPPDataSubTo16BPPBuffer

	Blits a subrect from a flat 8 bit surface to a 16-bit buffer.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411730
BOOLEAN Blt8BPPDataSubTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                    SGPVSurface& hSrcVSurface, UINT8* pSrcBuffer, UINT32 uiSrcPitch,
                                    INT32 iX, INT32 iY, SDL_Rect* pRect)
{
    UINT16* p16BPPPalette;
    UINT8 *SrcPtr, *DestPtr;
    UINT32 LeftSkip, TopSkip, BlitLength, BlitHeight;
    INT32 iTempX, iTempY;

    // Assertions
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
    p16BPPPalette = hSrcVSurface.p16BPPPalette.data();

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
// FUNCTION: WIZ8 0x004124a0
/* Keep the legacy inclusive rectangle and palette transforms in one place. */
static BOOLEAN Blt16BPPBufferShadowRectWithTable(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                                 SDL_Rect* area, const UINT16* table)
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
            NativeWriteWord(pixel, table[NativeReadWord(pixel)]);
        }
        dest += uiDestPitchBYTES;
    }
    return TRUE;
}

BOOLEAN Blt16BPPBufferShadowRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area)
{
    return Blt16BPPBufferShadowRectWithTable(pBuffer, uiDestPitchBYTES, area, ShadeTable);
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
    return Blt16BPPBufferShadowRectWithTable(pBuffer, uiDestPitchBYTES, area, IntensityTable);
}

// UTILITY FUNCTIONS FOR BLITTING

// FUNCTION: WIZ8 0x00412640
BOOLEAN FillRect16BPP(UINT16* pBuffer, UINT32 uiDestPitchBYTES, INT32 x1, INT32 y1, INT32 x2,
                      INT32 y2, UINT16 color)
{
    Assert(pBuffer != nullptr);
    Assert(uiDestPitchBYTES > 0);
    // This legacy entry point uses inclusive coordinates on the 640x480 buffer.
    const SDL_Rect rectangle{std::max(0, x1), std::max(0, y1),
                            std::min(639, x2) - std::max(0, x1) + 1,
                            std::min(479, y2) - std::max(0, y1) + 1};
    if (SDL_RectEmpty(&rectangle))
        return TRUE;
    auto storage = CreateCpuStorage(uiDestPitchBYTES / 2, 480, SDL_PIXELFORMAT_RGB565,
                                    pBuffer, uiDestPitchBYTES);
    auto destination = ShareCpuSurface(*storage);
    FillCpuSurface(destination, color, &rectangle);
    return TRUE;
}
