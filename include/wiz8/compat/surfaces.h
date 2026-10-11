#pragma once
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>
#include <cstddef>

using SDLSurfaceOwner = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

struct SurfaceColorKey
{
    uint32_t low, high;
};

// SDL owns the pixels. Only contracts SDL cannot express live alongside it.
struct CpuSurface
{
    SDLSurfaceOwner surface{nullptr, SDL_DestroySurface};
    uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
    std::optional<SurfaceColorKey> sourceKey, destinationKey;
    std::optional<std::vector<SDL_Rect>> clipRegions;
    bool locked = false;
};

struct SurfaceLock
{
    void* pixels;
    int pitch, width, height;
};

std::unique_ptr<CpuSurface> CreateCpuSurface(uint16_t width, uint16_t height, uint8_t bits,
    uint32_t red = 0, uint32_t green = 0, uint32_t blue = 0, uint32_t alpha = 0);
SurfaceLock LockCpuSurface(CpuSurface& surface, const SDL_Rect* region = nullptr);
void UnlockCpuSurface(CpuSurface& surface);
void SetSurfaceClipRegions(CpuSurface& surface, std::span<const SDL_Rect> rectangles);
void FillCpuSurface(CpuSurface& destination, uint32_t color, const SDL_Rect* rectangle = nullptr);
void BlitCpuSurface(CpuSurface& destination, const SDL_Rect* destinationRect, CpuSurface& source,
    const SDL_Rect* sourceRect = nullptr, bool sourceKey = false, bool destinationKey = false);

// Retain an SDL surface without changing ownership of caller-provided pixels.
CpuSurface ShareCpuSurface(SDL_Surface& surface);
// Conversion of byte-channel formats; snapshots overlapping input before SDL writes.
void ConvertCpuPixels(void* destination, const void* source, size_t count,
                      SDL_PixelFormat sourceFormat, SDL_PixelFormat destinationFormat);

// Omit pitch to allocate; a nonnegative pitch borrows pixels, including a null view.
SDLSurfaceOwner CreateCpuStorage(int width, int height, SDL_PixelFormat format,
                                 void* pixels = nullptr, int pitch = -1);
SDLSurfaceOwner CopyCpuStorage(SDL_Surface& source);

void FlipCpuSurface(CpuSurface& surface, SDL_FlipMode flip, const SDL_Rect* region = nullptr);

// Clip an unscaled blit while preserving the source-to-destination offset.
bool ClipCpuBlit(SDL_Rect& destination, SDL_Rect& source,
                 const SDL_Rect& destinationBounds, const SDL_Rect& sourceBounds);
