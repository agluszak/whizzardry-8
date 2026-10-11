/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>

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
std::uint16_t NativeReadWord(const std::uint8_t* data)
{
    std::uint16_t value;
    memcpy(&value, data, sizeof(value));
    return value;
}
void NativeWriteWord(std::uint8_t* data, std::uint16_t value)
{
    memcpy(data, &value, sizeof(value));
}

} // namespace


// GLOBAL: WIZ8 0x00600078
SDL_Rect ClippingRect = {0, 0, 640, 480};

// Ordinary buffer copies share the CPU surface backend. The legacy entry points
// deliberately propagate writes through overlapping input; SDL snapshots instead,
// so only that unsupported case uses the shared forward-copy loop below.
static void BlitPackedBuffers(void* destination, std::uint32_t destinationPitch,
                              void* source, std::uint32_t sourcePitch, std::uint32_t width,
                              std::uint32_t height, unsigned bytes,
                              std::optional<SurfaceColorKey> key = std::nullopt)
{
    if (!width || !height)
        return;
    const auto dest = reinterpret_cast<uintptr_t>(destination);
    const auto src = reinterpret_cast<uintptr_t>(source);
    if (dest < src + size_t(height - 1) * sourcePitch + width * bytes &&
        src < dest + size_t(height - 1) * destinationPitch + width * bytes) {
        auto* output = static_cast<std::uint8_t*>(destination);
        const auto* input = static_cast<const std::uint8_t*>(source);
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
                        std::uint32_t value;
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
bool Blt16BPPTo16BPP(std::uint16_t* pDest, std::uint32_t uiDestPitch, std::uint16_t* pSrc, std::uint32_t uiSrcPitch,
                        std::int32_t iDestXPos, std::int32_t iDestYPos, std::int32_t iSrcXPos, std::int32_t iSrcYPos,
                        std::uint32_t uiWidth, std::uint32_t uiHeight)
{
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<std::uint8_t*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 2;
    auto* destination = reinterpret_cast<std::uint8_t*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 2;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 2);
    return true;
}

/**********************************************************************************************
	Blt16BPPTo16BPPTrans

	Copies a rect of 16 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip. Transparent color is
	not copied.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004114c0
bool Blt16BPPTo16BPPTrans(std::uint16_t* pDest, std::uint32_t uiDestPitch, std::uint16_t* pSrc, std::uint32_t uiSrcPitch,
                             std::int32_t iDestXPos, std::int32_t iDestYPos, std::int32_t iSrcXPos, std::int32_t iSrcYPos,
                             std::uint32_t uiWidth, std::uint32_t uiHeight, std::uint16_t usTrans)
{
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<std::uint8_t*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 2;
    auto* destination = reinterpret_cast<std::uint8_t*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 2;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 2, SurfaceColorKey{usTrans, usTrans});
    return true;
}

/**********************************************************************************************
	Blt16BPPTo16BPPMirror

	Copies a rect of 16 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411540
bool Blt16BPPTo16BPPMirror(std::uint16_t* pDest, std::uint32_t uiDestPitch, std::uint16_t* pSrc, std::uint32_t uiSrcPitch,
                              std::int32_t iDestXPos, std::int32_t iDestYPos, std::int32_t iSrcXPos, std::int32_t iSrcYPos,
                              std::uint32_t uiWidth, std::uint32_t uiHeight)
{
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    const SDL_Rect destination{iDestXPos, iDestYPos, int(uiWidth), int(uiHeight)};
    const SDL_Rect bounds{0, 0, 640, 480};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&destination, &bounds, &visible))
        return true;
    // This entry point ignores the source origin and propagates overlapping writes
    // from left to right while writing the destination from right to left.
    const int topSkip = visible.y - destination.y;
    const int rightSkip = destination.x + destination.w - visible.x - visible.w;
    const auto* source = reinterpret_cast<const std::uint8_t*>(pSrc) + topSkip * uiSrcPitch + rightSkip * 2;
    auto* dest = reinterpret_cast<std::uint8_t*>(pDest) + visible.y * uiDestPitch +
                 (visible.x + visible.w - 1) * 2;
    for (int y = 0; y < visible.h; ++y, source += uiSrcPitch, dest += uiDestPitch)
        for (int x = 0; x < visible.w; ++x)
            NativeWriteWord(dest - x * 2, NativeReadWord(source + x * 2));
    return true;
}

/***********************************************************************************************
	Blt8BPPTo8BPP

	Copies a rect of an 8 bit data from a video buffer to a buffer position of the brush
	in the data area, for later blitting. Used to copy background information for mercs
	etc. to their unblit buffer, for later reblitting. Does NOT clip.

**********************************************************************************************/
// FUNCTION: WIZ8 0x004116c0
bool Blt8BPPTo8BPP(std::uint8_t* pDest, std::uint32_t uiDestPitch, std::uint8_t* pSrc, std::uint32_t uiSrcPitch,
                      std::int32_t iDestXPos, std::int32_t iDestYPos, std::int32_t iSrcXPos, std::int32_t iSrcYPos,
                      std::uint32_t uiWidth, std::uint32_t uiHeight)
{
    Assert(pDest != nullptr);
    Assert(pSrc != nullptr);
    auto* source = reinterpret_cast<std::uint8_t*>(pSrc) + iSrcYPos * uiSrcPitch + iSrcXPos * 1;
    auto* destination = reinterpret_cast<std::uint8_t*>(pDest) + iDestYPos * uiDestPitch + iDestXPos * 1;
    BlitPackedBuffers(destination, uiDestPitch, source, uiSrcPitch, uiWidth, uiHeight, 1);
    return true;
}



/**********************************************************************************************
 Blt8BPPDataSubTo16BPPBuffer

	Blits a subrect from a flat 8 bit surface to a 16-bit buffer.

**********************************************************************************************/
// FUNCTION: WIZ8 0x00411730
bool Blt8BPPDataSubTo16BPPBuffer(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                                    SGPVSurface& hSrcVSurface, std::uint8_t* pSrcBuffer, std::uint32_t uiSrcPitch,
                                    std::int32_t iX, std::int32_t iY, SDL_Rect* pRect)
{
    std::uint16_t* p16BPPPalette;
    std::uint8_t *SrcPtr, *DestPtr;
    std::uint32_t LeftSkip, TopSkip, BlitLength, BlitHeight;
    std::int32_t iTempX, iTempY;

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

    SrcPtr = (std::uint8_t*)(pSrcBuffer + TopSkip + LeftSkip);
    DestPtr = ((std::uint8_t*)pBuffer + (uiDestPitchBYTES * iTempY) + (iTempX * 2));
    p16BPPPalette = hSrcVSurface.p16BPPPalette.data();

    // Every index writes its LUT word.
    for (std::uint32_t y = 0; y < BlitHeight; ++y) {
        for (std::uint32_t x = 0; x < BlitLength; ++x) {
            NativeWriteWord(DestPtr + x * 2, p16BPPPalette[SrcPtr[x]]);
        }
        SrcPtr += uiSrcPitch;
        DestPtr += uiDestPitchBYTES;
    }

    return (true);
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
bool BltSpriteTo16BPPBuffer(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                               const SpriteFrame& frame, const std::uint16_t* palette, std::int32_t iX, std::int32_t iY,
                               const SDL_Rect* clipregion, bool fMirror, SpriteDrawOp op,
                               std::uint16_t mono_foreground, std::uint16_t mono_background, std::uint16_t mono_shadow)
{
    Assert(pBuffer != nullptr);

    const SDL_Rect* clip = clipregion ? clipregion : &ClippingRect;
    // Mirroring flips the sprite about its anchor, so the offset sign flips
    // too and clipping (not rejection) handles edges that leave the buffer.
    const SDL_Rect sprite{fMirror ? iX - frame.sOffsetX : iX + frame.sOffsetX,
                          iY + frame.sOffsetY, frame.usWidth, frame.usHeight};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&sprite, clip, &visible)) {
        return (true);
    }

    if (op == SpriteDrawOp::Transparent) {
        auto* source = frame.coloredSurface(std::span<const std::uint16_t, 256>(palette, 256), fMirror);
        auto destination = CreateCpuStorage(visible.x + visible.w, visible.y + visible.h,
                                           SDL_PIXELFORMAT_RGB565, pBuffer, uiDestPitchBYTES);
        if (!source || !destination) return false;
        SDL_Rect sourceRect{visible.x - sprite.x, visible.y - sprite.y, visible.w, visible.h};
        return SDL_BlitSurface(source, &sourceRect, destination.get(), &visible);
    }

    for (std::int32_t dy = visible.y; dy < visible.y + visible.h; ++dy) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(frame.indices->pixels) + (dy - sprite.y) * frame.indices->pitch);
        std::uint8_t* dest = reinterpret_cast<std::uint8_t*>(pBuffer) + dy * uiDestPitchBYTES + visible.x * 2;
        const std::int32_t left = visible.x - sprite.x;
        for (std::int32_t dx = 0; dx < visible.w; ++dx, dest += 2) {
            const std::int32_t sx = fMirror ? frame.usWidth - 1 - left - dx : left + dx;
            const std::uint16_t index = row[sx];
            switch (op) {
            case SpriteDrawOp::Transparent:
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
    return (true);
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
bool Blt16BPPBufferPixelateRectWithColor(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area,
                                            std::uint8_t Pattern[8][8], std::uint16_t usColor)
{
    Assert(pBuffer != nullptr);
    Assert(Pattern != nullptr);

    // These old SGP rectangles store inclusive endpoint deltas, not SDL sizes.
    const SDL_Rect requested{area->x, area->y, area->w + 1, area->h + 1};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&requested, &ClippingRect, &visible))
        return false;

    auto* dest = reinterpret_cast<std::uint8_t*>(pBuffer) + visible.y * uiDestPitchBYTES + visible.x * 2;
    for (int y = 0; y < visible.h; ++y) {
        for (int x = 0; x < visible.w; ++x) {
            // The first column of every row uses pattern[0][0].
            if (x == 0 ? Pattern[0][0] : Pattern[y & 7][x & 7])
                NativeWriteWord(dest + x * 2, usColor);
        }
        dest += uiDestPitchBYTES;
    }
    return true;
}

//Uses black hatch color
// FUNCTION: WIZ8 0x00411930
bool Blt16BPPBufferHatchRect(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area)
{
    std::uint8_t Pattern[8][8] = {
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0},
        {0, 1, 0, 1, 0, 1, 0, 1}, {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1},
        {1, 0, 1, 0, 1, 0, 1, 0}, {0, 1, 0, 1, 0, 1, 0, 1}};
    return Blt16BPPBufferPixelateRectWithColor(pBuffer, uiDestPitchBYTES, area, Pattern, 0);
}
// FUNCTION: WIZ8 0x004120b0
bool BltIsClipped(SGPVObject& object, std::int32_t x, std::int32_t y, std::uint16_t index, SDL_Rect* clipregion)
{
    const SpriteFrame& frame = object.sprites[index];
    if (frame.usWidth == 0 || frame.usHeight == 0)
        return false;

    const SDL_Rect sprite{x + frame.sOffsetX, y + frame.sOffsetY,
                          frame.usWidth, frame.usHeight};
    const SDL_Rect& clip = clipregion ? *clipregion : ClippingRect;
    SDL_Rect visible;
    return !SDL_GetRectIntersection(&sprite, &clip, &visible) ||
           !SDL_RectsEqual(&sprite, &visible);
}
// FUNCTION: WIZ8 0x004124a0
/* Keep the legacy inclusive rectangle and palette transforms in one place. */
static bool Blt16BPPBufferShadowRectWithTable(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                                                 SDL_Rect* area, const std::uint16_t* table)
{
    Assert(pBuffer != nullptr);

    // Preserve the legacy inclusive endpoint deltas, including mutation of
    // the caller's rectangle after clipping.
    const SDL_Rect requested{area->x, area->y, area->w + 1, area->h + 1};
    SDL_Rect visible;
    if (!SDL_GetRectIntersection(&requested, &ClippingRect, &visible))
        return false;
    *area = {visible.x, visible.y, visible.w - 1, visible.h - 1};

    auto* dest = reinterpret_cast<std::uint8_t*>(pBuffer) + visible.y * uiDestPitchBYTES + visible.x * 2;
    for (int y = 0; y < visible.h; ++y) {
        for (int x = 0; x < visible.w; ++x) {
            std::uint8_t* pixel = dest + x * 2;
            NativeWriteWord(pixel, table[NativeReadWord(pixel)]);
        }
        dest += uiDestPitchBYTES;
    }
    return true;
}

bool Blt16BPPBufferShadowRect(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area)
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
bool Blt16BPPBufferShadowRectAlternateTable(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                                               SDL_Rect* area)
{
    return Blt16BPPBufferShadowRectWithTable(pBuffer, uiDestPitchBYTES, area, IntensityTable);
}

// UTILITY FUNCTIONS FOR BLITTING

// FUNCTION: WIZ8 0x00412640
bool FillRect16BPP(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, std::int32_t x1, std::int32_t y1, std::int32_t x2,
                      std::int32_t y2, std::uint16_t color)
{
    Assert(pBuffer != nullptr);
    Assert(uiDestPitchBYTES > 0);
    // This legacy entry point uses inclusive coordinates on the 640x480 buffer.
    const SDL_Rect rectangle{std::max(0, x1), std::max(0, y1),
                            std::min(639, x2) - std::max(0, x1) + 1,
                            std::min(479, y2) - std::max(0, y1) + 1};
    if (SDL_RectEmpty(&rectangle))
        return true;
    auto storage = CreateCpuStorage(uiDestPitchBYTES / 2, 480, SDL_PIXELFORMAT_RGB565,
                                    pBuffer, uiDestPitchBYTES);
    auto destination = ShareCpuSurface(*storage);
    FillCpuSurface(destination, color, &rectangle);
    return true;
}
