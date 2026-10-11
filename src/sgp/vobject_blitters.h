#ifndef __VOBJECT_BLITTERS
#define __VOBJECT_BLITTERS

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */

#include <cstdint>
#include <SDL3/SDL_rect.h>

#include "vobject.h"
#include "himage.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SDL_Rect ClippingRect;

extern void SetClippingRect(SDL_Rect* clip);
void GetClippingRect(SDL_Rect* clip);

bool BltIsClipped(SGPVObject& hSrcVObject, std::int32_t iX, std::int32_t iY, std::uint16_t usIndex, SDL_Rect* clipregion);

// Per-pixel operation applied by BltSpriteTo16BPPBuffer to each opaque sprite
// pixel. MonoShadow additionally paints mono_background into transparent
// pixels when it is nonzero.
enum class SpriteDrawOp { Transparent, Shadow, MonoShadow };

// Draws one decoded sprite frame into a 16-bit destination. The sprite rect
// (position plus frame offsets) is intersected with clipregion, or with the
// global ClippingRect when clipregion is nullptr, so clipped, unclipped, and
// mirrored draws all take the same path. mono_* colors apply only to
// SpriteDrawOp::MonoShadow.
bool BltSpriteTo16BPPBuffer(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                               const SpriteFrame& frame, const std::uint16_t* palette, std::int32_t iX, std::int32_t iY,
                               const SDL_Rect* clipregion, bool fMirror, SpriteDrawOp op,
                               std::uint16_t mono_foreground = 0, std::uint16_t mono_background = 0,
                               std::uint16_t mono_shadow = 0);



bool Blt16BPPTo16BPPMirror(std::uint16_t* pDest, std::uint32_t uiDestPitch, std::uint16_t* pSrc, std::uint32_t uiSrcPitch,
                              std::int32_t iDestXPos, std::int32_t iDestYPos, std::int32_t iSrcXPos, std::int32_t iSrcYPos,
                              std::uint32_t uiWidth, std::uint32_t uiHeight);

bool Blt16BPPBufferPixelateRectWithColor(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area,
                                            std::uint8_t Pattern[8][8], std::uint16_t usColor);
//A wrapper for the Blt16BPPBufferPixelateRect that automatically passes a hatch pattern.
bool Blt16BPPBufferHatchRect(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area);
bool Blt16BPPBufferShadowRect(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES, SDL_Rect* area);

// Converts the source rectangle of 8-bit indices through palette into the
// destination at (iX, iY). Neither side is clipped.
bool Blt8BPPDataSubTo16BPPBuffer(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                                    const Palette16& palette, const std::uint8_t* pSrcBuffer,
                                    std::uint32_t uiSrcPitch, std::int32_t iX, std::int32_t iY, const SDL_Rect& source);

#ifdef __cplusplus
}

#endif

#endif
