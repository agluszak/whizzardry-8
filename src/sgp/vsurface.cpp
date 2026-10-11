#include <cstdint>
#include <algorithm>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <cstddef>
#include <memory>
#include <string_view>
#include <optional>

/* Modified for the Wizardry 8 reconstruction: 2026-10-03, 2026-10-06, 2026-10-07, 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
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

static bool SetVideoSurfacePalette(SGPVSurface& surface, const Palette8& colors);

// Fills an rectangular area with a specified color value.

// FUNCTION: WIZ8 0x00402fa0
bool ColorFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, std::int32_t iDestX1, std::int32_t iDestY1,
                                  std::int32_t iDestX2, std::int32_t iDestY2, std::uint16_t Color16BPP)
{
    SDL_Rect clip;
    GetClippingRect(&clip);
    SDL_Rect area{iDestX1, iDestY1, iDestX2 - iDestX1, iDestY2 - iDestY1};
    if (!SDL_GetRectIntersection(&area, &clip, &area))
        return false;
    FillCpuSurface(uiDestVSurface.cpu(), Color16BPP, &area);
    return true;
}
// Fills an rectangular area with a specified image value.

// FUNCTION: WIZ8 0x00403150
bool ImageFillVideoSurfaceArea(SGPVSurface& uiDestVSurface, std::int32_t iDestX1, std::int32_t iDestY1,
                                  std::int32_t iDestX2, std::int32_t iDestY2, SGPVObject& BkgrndImg, std::uint16_t Index,
                                  std::int16_t Ox, std::int16_t Oy)
{
    if (Index >= BkgrndImg.sprites.size()) return false;
    const auto& frame = BkgrndImg.sprites[Index];
    const int width = frame.usWidth + frame.sOffsetX;
    const int height = frame.usHeight + frame.sOffsetY;
    if (width <= 0 || height <= 0) return false;
    SDL_Rect visible{iDestX1, iDestY1, iDestX2 - iDestX1, iDestY2 - iDestY1};
    const auto& storage = *uiDestVSurface.cpu().surface;
    const SDL_Rect bounds{0, 0, storage.w, storage.h};
    if (!SDL_GetRectIntersection(&visible, &ClippingRect, &visible) ||
        !SDL_GetRectIntersection(&visible, &bounds, &visible)) return false;
    auto* source = frame.coloredSurface(BkgrndImg.currentPalette());
    if (!source) return false;
    std::uint32_t key;
    if (!SDL_GetSurfaceColorKey(source, &key)) return false;
    auto destination = CreateCpuStorage(storage.w, storage.h, SDL_PIXELFORMAT_RGB565,
                                        storage.pixels, storage.pitch);
    if (!destination) return false;
    const auto modulo = [](int value, int period) { return (value % period + period) % period; };
    const int startX = iDestX1 + modulo(Ox, width) - width;
    const int startY = iDestY1 + modulo(Oy, height) - height;
    const int lastX = iDestX2 - 1 - modulo(iDestX2 - 1 - startX, width);
    const int lastY = iDestY2 - 1 - modulo(iDestY2 - 1 - startY, height);
    // Negative frame offsets overlap later tiles. At the finite grid's right
    // and bottom edges those neighbors do not exist, so keep separate edge cells.
    const int xs[]{visible.x,
                   std::clamp(lastX + width + std::min<int>(frame.sOffsetX, 0),
                              visible.x, visible.x + visible.w),
                   visible.x + visible.w};
    const int ys[]{visible.y,
                   std::clamp(lastY + height + std::min<int>(frame.sOffsetY, 0),
                              visible.y, visible.y + visible.h),
                   visible.y + visible.h};
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
            const SDL_Rect area{xs[column], ys[row],
                                xs[column + 1] - xs[column], ys[row + 1] - ys[row]};
            if (area.w == 0 || area.h == 0) continue;
            const int cellWidth = column ? area.w : std::min(width, area.w);
            const int cellHeight = row ? area.h : std::min(height, area.h);
            auto tile = CreateCpuStorage(cellWidth, cellHeight, SDL_PIXELFORMAT_RGB565);
            if (!tile || !SDL_FillSurfaceRect(tile.get(), nullptr, key) ||
                !SDL_SetSurfaceColorKey(tile.get(), true, key)) return false;
            const int lowerX = area.x - frame.sOffsetX - frame.usWidth + 1;
            const int lowerY = area.y - frame.sOffsetY - frame.usHeight + 1;
            const int firstX = std::max(startX, lowerX + modulo(startX - lowerX, width));
            const int firstY = std::max(startY, lowerY + modulo(startY - lowerY, height));
            const int endX = std::min(lastX, area.x + cellWidth - 1 - frame.sOffsetX);
            const int endY = std::min(lastY, area.y + cellHeight - 1 - frame.sOffsetY);
            // Compose only one phase-aligned cell, including overlapping frames.
            for (int y = firstY; y <= endY; y += height) {
                for (int x = firstX; x <= endX; x += width) {
                    const SDL_Rect position{x + frame.sOffsetX - area.x,
                                            y + frame.sOffsetY - area.y,
                                            frame.usWidth, frame.usHeight};
                    if (!SDL_BlitSurface(source, nullptr, tile.get(), &position)) return false;
                }
            }
            if (!SDL_SetSurfaceRLE(tile.get(), true) ||
                !SDL_BlitSurfaceTiled(tile.get(), nullptr, destination.get(), &area)) return false;
        }
    }
    return true;
}
// Video Surface Manipulation Functions

// FUNCTION: WIZ8 0x004033d0
std::unique_ptr<SGPVSurface> CreateVideoSurface(std::uint16_t width, std::uint16_t height, std::uint8_t bits)
{
    CHECKN(width && height && (bits == 8 || bits == 16));
    std::uint32_t red = 0, green = 0, blue = 0;
    if (bits == 16)
        CHECKN(GetPrimaryRGBDistributionMasks(&red, &green, &blue));
    auto result = std::make_unique<SGPVSurface>();
    result->ownedSurface = CreateCpuSurface(width, height, bits, red, green, blue);
    CHECKN(result->ownedSurface != nullptr);
    result->usWidth = width;
    result->usHeight = height;
    result->ubBitDepth = bits;
    SetVideoSurfaceTransparencyColor(*result, FROMRGB(0, 0, 0));
    return result;
}

std::unique_ptr<SGPVSurface> LoadVideoSurface(std::string_view filename)
{
    auto image = LoadImage(filename);
    if (!image) return nullptr;
    auto result = CreateVideoSurface(image->usWidth, image->usHeight, image->ubBitDepth);
    if (!result) return nullptr;
    if (image->pPalette && !SetVideoSurfacePalette(*result, *image->pPalette))
        return nullptr;

    const auto& storage = *result->cpu().surface;
    const auto bits = result->ubBitDepth;
    const auto format = bits == 8 ? BUFFER_8BPP : BUFFER_16BPP;
    const SDL_Rect bounds{0, 0, result->usWidth, result->usHeight};
    if (!CopyImageToBuffer(*image, format,
            {static_cast<std::uint8_t*>(storage.pixels), std::size_t(storage.pitch) * result->usHeight},
            storage.pitch / (bits / 8), result->usHeight, 0, 0, bounds)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error copying image data to video surface");
        return nullptr;
    }
    return result;
}
// Indexed surfaces use SDL palettes; custom RGB555 blitters retain their table.
static bool SetVideoSurfacePalette(SGPVSurface& hVSurface, const Palette8& pSrcPalette)
{
    if (hVSurface.ubBitDepth == 8) {
        SDL_Color colors[256];
        for (unsigned i = 0; i < 256; ++i)
            colors[i] = {pSrcPalette[i].peRed, pSrcPalette[i].peGreen, pSrcPalette[i].peBlue, 255};
        CHECKF(SDL_SetPaletteColors(SDL_GetSurfacePalette(hVSurface.cpu().surface.get()),
                                   colors, 0, 256));
    }
    hVSurface.p16BPPPalette = Create16BPPPalette(pSrcPalette);
    return true;
}

// Transparency uses the game's packed source key.
// colorkey value.
void SetVideoSurfaceTransparencyColor(SGPVSurface& surface, COLORVAL color)
{
    const std::uint32_t key = surface.ubBitDepth == 16 ? Get16BPPColor(color) : color;
    surface.cpu().sourceKey = SurfaceColorKey{key, key};
}

// FUNCTION: WIZ8 0x00403b10
bool BltVideoSurfaceToVideoSurface(SGPVSurface& hDestVSurface, SGPVSurface& hSrcVSurface,
                                      std::int32_t iDestX, std::int32_t iDestY, std::int32_t fBltFlags,
                                      const SDL_Rect* sourceRect)
{
    SDL_Rect SrcRect;
    std::uint16_t *pDestSurface16, *pSrcSurface16;
    std::uint32_t uiSrcPitch, uiDestPitch;

    if (sourceRect) {
        SrcRect = *sourceRect;
    } else {
        CHECKF(hDestVSurface.usHeight >= hSrcVSurface.usHeight);
        CHECKF(hDestVSurface.usWidth >= hSrcVSurface.usWidth);
        SrcRect = {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight};
    }

    // Once here, assert valid Src

    const SDL_Rect bounds{0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight};
    const SDL_Rect destination{iDestX, iDestY, SrcRect.w, SrcRect.h};
    if (!SDL_HasRectIntersection(&destination, &bounds))
        return false;

    CHECKF(hDestVSurface.ubBitDepth == hSrcVSurface.ubBitDepth &&
           (hDestVSurface.ubBitDepth == 8 || hDestVSurface.ubBitDepth == 16));
    if (hDestVSurface.ubBitDepth == 16 && (fBltFlags & VS_BLT_MIRROR_Y)) {
        const auto& source_storage = *hSrcVSurface.cpu().surface;
        uiSrcPitch = source_storage.pitch;
        pSrcSurface16 = static_cast<std::uint16_t*>(source_storage.pixels);
        CHECKF(pSrcSurface16);
        const auto& destination_storage = *hDestVSurface.cpu().surface;
        uiDestPitch = destination_storage.pitch;
        pDestSurface16 = static_cast<std::uint16_t*>(destination_storage.pixels);
        if (!pDestSurface16) {
            return false;
        }
        Blt16BPPTo16BPPMirror(pDestSurface16, uiDestPitch, pSrcSurface16, uiSrcPitch,
                             iDestX, iDestY, SrcRect.x, SrcRect.y, SrcRect.w, SrcRect.h);
        return true;
    }
    SDL_Rect clippedDestination = destination;
    if (fBltFlags & VS_BLT_FAST) {
        CHECKF(iDestX >= 0 && iDestY >= 0);
        CHECKF(!hDestVSurface.cpu().clipRegions);
        CHECKF((destination.x + destination.w) <= hDestVSurface.usWidth &&
               (destination.y + destination.h) <= hDestVSurface.usHeight);
    } else if (!ClipCpuBlit(clippedDestination, SrcRect,
                            {0, 0, hDestVSurface.usWidth, hDestVSurface.usHeight},
                            {0, 0, hSrcVSurface.usWidth, hSrcVSurface.usHeight})) {
        return true;
    }
    if (clippedDestination.w == 0 || clippedDestination.h == 0)
        return true;
    BlitCpuSurface(hDestVSurface.cpu(), &clippedDestination, hSrcVSurface.cpu(), &SrcRect,
                   fBltFlags & VS_BLT_USECOLORKEY, fBltFlags & VS_BLT_USEDESTCOLORKEY);
    return true;
}

bool Blt16BPPBufferShadowRectAlternateTable(std::uint16_t* pBuffer, std::uint32_t uiDestPitchBYTES,
                                               SDL_Rect* area);

// FUNCTION: WIZ8 0x004045b0
bool InternalShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, std::int32_t X1, std::int32_t Y1, std::int32_t X2,
                                       std::int32_t Y2, bool fLowPercentShadeTable)
{
    std::uint16_t* pBuffer;
    std::uint32_t uiPitch;
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
        return false;

    const auto& destination_storage = *uiDestVSurface.cpu().surface;
    uiPitch = destination_storage.pitch;
    pBuffer = static_cast<std::uint16_t*>(destination_storage.pixels);


    if (pBuffer == nullptr) {
        return (false);
    }

    const bool result = fLowPercentShadeTable
        ? Blt16BPPBufferShadowRectAlternateTable(pBuffer, uiPitch, &area)
        : Blt16BPPBufferShadowRect(pBuffer, uiPitch, &area);
    return result;
}

// FUNCTION: WIZ8 0x004048a0
bool ShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, std::int32_t X1, std::int32_t Y1, std::int32_t X2, std::int32_t Y2)
{
    return (InternalShadowVideoSurfaceRect(uiDestVSurface, X1, Y1, X2, Y2, false));
}

// FUNCTION: WIZ8 0x004048d0
std::unique_ptr<SGPVSurface> MakeVSurfaceFromVObject(SGPVObject& object, std::uint16_t subIndex)
{
    if (subIndex >= object.sprites.size())
        return nullptr;
    auto surface = CreateVideoSurface(object.sprites[subIndex].usWidth,
                                      object.sprites[subIndex].usHeight, PIXEL_DEPTH);
    if (!surface || !BltVideoObject(*surface, object, subIndex, 0, 0,
                                    VO_BLT_SRCTRANSPARENCY, nullptr))
        return nullptr;
    return surface;
}
