/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07.
   Distributed under the accompanying SFI Source Code license agreement. */
#ifndef __VOBJECT_BLITTERS
#define __VOBJECT_BLITTERS

#include "shading.h"
#include "vsurface.h"
#include "vobject.h"

#ifdef __cplusplus
#include <span>

extern "C" {
#endif

extern SDL_Rect ClippingRect;

extern void SetClippingRect(SDL_Rect* clip);
void GetClippingRect(SDL_Rect* clip);

BOOLEAN BltIsClipped(HVOBJECT hSrcVObject, INT32 iX, INT32 iY, UINT16 usIndex, SDL_Rect* clipregion);

// Per-pixel operation applied by BltSpriteTo16BPPBuffer to each opaque sprite
// pixel. MonoShadow additionally paints mono_background into transparent
// pixels when it is nonzero.
enum class SpriteDrawOp { Transparent, Shadow, MonoShadow };

// Draws one decoded sprite frame into a 16-bit destination. The sprite rect
// (position plus frame offsets) is intersected with clipregion, or with the
// global ClippingRect when clipregion is nullptr, so clipped, unclipped, and
// mirrored draws all take the same path. mono_* colors apply only to
// SpriteDrawOp::MonoShadow.
BOOLEAN BltSpriteTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                               const SpriteFrame& frame, const UINT16* palette, INT32 iX, INT32 iY,
                               const SDL_Rect* clipregion, bool fMirror, SpriteDrawOp op,
                               UINT16 mono_foreground = 0, UINT16 mono_background = 0,
                               UINT16 mono_shadow = 0);



BOOLEAN Blt8BPPTo8BPP(UINT8* pDest, UINT32 uiDestPitch, UINT8* pSrc, UINT32 uiSrcPitch,
                      INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                      UINT32 uiWidth, UINT32 uiHeight);
BOOLEAN Blt16BPPTo16BPP(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                        INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                        UINT32 uiWidth, UINT32 uiHeight);
BOOLEAN Blt16BPPTo16BPPTrans(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                             INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                             UINT32 uiWidth, UINT32 uiHeight, UINT16 usTrans);
BOOLEAN Blt16BPPTo16BPPMirror(UINT16* pDest, UINT32 uiDestPitch, UINT16* pSrc, UINT32 uiSrcPitch,
                              INT32 iDestXPos, INT32 iDestYPos, INT32 iSrcXPos, INT32 iSrcYPos,
                              UINT32 uiWidth, UINT32 uiHeight);

BOOLEAN Blt16BPPBufferPixelateRectWithColor(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area,
                                            UINT8 Pattern[8][8], UINT16 usColor);
//A wrapper for the Blt16BPPBufferPixelateRect that automatically passes a hatch pattern.
BOOLEAN Blt16BPPBufferHatchRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area);
BOOLEAN Blt16BPPBufferShadowRect(UINT16* pBuffer, UINT32 uiDestPitchBYTES, SDL_Rect* area);

BOOLEAN Blt8BPPDataSubTo16BPPBuffer(UINT16* pBuffer, UINT32 uiDestPitchBYTES,
                                    HVSURFACE hSrcVSurface, UINT8* pSrcBuffer, UINT32 uiSrcPitch,
                                    INT32 iX, INT32 iY, SDL_Rect* pRect);

// ATE: New blitters for showing an outline at color 254

// ATE: New blitter for included shadow, but pixellate if obscured by z
BOOLEAN FillRect16BPP(UINT16* pBuffer, UINT32 uiDestPitchBYTES, INT32 x1, INT32 y1, INT32 x2,
                      INT32 y2, UINT16 color);

#ifdef __cplusplus
}

// Decode every ETRLE frame once from the compressed source into SpriteFrame
// pixels (transparent runs become SpriteFrame::kTransparent, literal index 0
// stays opaque). The object keeps only the decoded representation; decoding
// is transactional, so hVObject->sprites is replaced only on success.
// Returns FALSE on malformed or truncated data.
BOOLEAN DecodeVideoObjectSprites(HVOBJECT hVObject, std::span<const ETRLEObject> frames,
                                 std::span<const UINT8> data);
#endif

#endif
