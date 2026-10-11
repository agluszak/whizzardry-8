#include <SDL3/SDL_pixels.h>
#include <cstdint>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <vector>
#include <memory>
#include <variant>
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
    auto destination = CreateCpuStorage(storage.w, storage.h, g_sgp_pixel_format,
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
            auto tile = CreateCpuStorage(cellWidth, cellHeight, g_sgp_pixel_format);
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
    const auto* details = bits == 16 ? SDL_GetPixelFormatDetails(g_sgp_pixel_format) : nullptr;
    CHECKN(bits != 16 || details);
    const SDL_PixelFormat format = bits == 8 ? SDL_PIXELFORMAT_INDEX8 :
        SDL_GetPixelFormatForMasks(16, details->Rmask, details->Gmask, details->Bmask, 0);
    CHECKN(format != SDL_PIXELFORMAT_UNKNOWN);
    auto result = std::make_unique<SGPVSurface>();
    result->ownedSurface = CreateCpuSurface(width, height, format);
    CHECKN(result->ownedSurface != nullptr);
    result->cpu().sourceKey = SurfaceColorKey{0, 0};
    return result;
}

Palette16 SGPVSurface::packedPalette() const
{
    Palette8 colors{};
    if (const SDL_Palette* palette = SDL_GetSurfacePalette(ownedSurface->surface.get()))
        std::copy_n(palette->colors, std::min<int>(palette->ncolors, colors.size()), colors.begin());
    return Create16BPPPalette(colors);
}

std::unique_ptr<SGPVSurface> LoadVideoSurface(std::string_view filename)
{
    auto decoded = LoadImage(filename);
    auto* image = decoded ? std::get_if<BitmapImage>(&*decoded) : nullptr;
    if (!image) return nullptr;
    auto result = CreateVideoSurface(image->width, image->height, image->bitDepth);
    if (!result) return nullptr;
    if (image->palette) {
        CHECKN(image->bitDepth == 8);
        CHECKN(SDL_SetPaletteColors(SDL_GetSurfacePalette(result->cpu().surface.get()),
                                    image->palette->data(), 0, 256));
    }

    const auto& storage = *result->cpu().surface;
    const std::size_t row_bytes = image->width * (image->bitDepth / 8);
    // FUNCTION: WIZ8 0x0040fad0
    // FUNCTION: WIZ8 0x0040ff30
    // FUNCTION: WIZ8 0x00410050
    for (int row = 0; row < storage.h; ++row)
        std::memcpy(static_cast<std::uint8_t*>(storage.pixels) + row * storage.pitch,
                    image->pixels.data() + row * row_bytes, row_bytes);
    return result;
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
        CHECKF(hDestVSurface.height() >= hSrcVSurface.height());
        CHECKF(hDestVSurface.width() >= hSrcVSurface.width());
        SrcRect = {0, 0, hSrcVSurface.width(), hSrcVSurface.height()};
    }

    // Once here, assert valid Src

    const SDL_Rect bounds{0, 0, hDestVSurface.width(), hDestVSurface.height()};
    const SDL_Rect destination{iDestX, iDestY, SrcRect.w, SrcRect.h};
    if (!SDL_HasRectIntersection(&destination, &bounds))
        return false;

    CHECKF(hDestVSurface.bitDepth() == hSrcVSurface.bitDepth() &&
           (hDestVSurface.bitDepth() == 8 || hDestVSurface.bitDepth() == 16));
    if (hDestVSurface.bitDepth() == 16 && (fBltFlags & VS_BLT_MIRROR_Y)) {
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
        CHECKF((destination.x + destination.w) <= hDestVSurface.width() &&
               (destination.y + destination.h) <= hDestVSurface.height());
    } else if (!ClipCpuBlit(clippedDestination, SrcRect,
                            {0, 0, hDestVSurface.width(), hDestVSurface.height()},
                            {0, 0, hSrcVSurface.width(), hSrcVSurface.height()})) {
        return true;
    }
    if (clippedDestination.w == 0 || clippedDestination.h == 0)
        return true;
    BlitCpuSurface(hDestVSurface.cpu(), &clippedDestination, hSrcVSurface.cpu(), &SrcRect,
                   fBltFlags & VS_BLT_USECOLORKEY, fBltFlags & VS_BLT_USEDESTCOLORKEY);
    return true;
}

// FUNCTION: WIZ8 0x004045b0
// FUNCTION: WIZ8 0x004048a0
bool ShadowVideoSurfaceRect(SGPVSurface& uiDestVSurface, std::int32_t X1, std::int32_t Y1, std::int32_t X2, std::int32_t Y2)
{
    // The shadow blitter adds the final pixel on each axis; keep its inclusive
    // endpoint convention while intersecting the edge-based area here.
    const SDL_Rect bounds{0, 0, uiDestVSurface.width() - 1, uiDestVSurface.height() - 1};
    SDL_Rect area{X1, Y1, X2 - X1, Y2 - Y1};
    if (!SDL_GetRectIntersection(&area, &bounds, &area))
        return false;

    const auto& storage = *uiDestVSurface.cpu().surface;
    if (!storage.pixels)
        return false;
    return Blt16BPPBufferShadowRect(static_cast<std::uint16_t*>(storage.pixels), storage.pitch, &area);
}

// FUNCTION: WIZ8 0x004048d0
std::unique_ptr<SGPVSurface> MakeVSurfaceFromVObject(SGPVObject& object, std::uint16_t subIndex)
{
    if (subIndex >= object.sprites.size())
        return nullptr;
    auto surface = CreateVideoSurface(object.sprites[subIndex].usWidth,
                                      object.sprites[subIndex].usHeight, PIXEL_DEPTH);
    if (!surface || !BltVideoObject(*surface, object, subIndex, 0, 0,
                                    VO_BLT_SRCTRANSPARENCY))
        return nullptr;
    return surface;
}
