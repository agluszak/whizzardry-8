#pragma once
#include "Types.h"
#include "compat/kernel32.h"
#include <SDL3/SDL_surface.h>
#include <memory>
#include <optional>
#include <span>
#include <vector>

using SDLSurfaceOwner = std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)>;

struct SurfaceColorKey
{
    UINT32 low, high;
};

// SDL owns the pixels. Only contracts SDL cannot express live alongside it.
struct CpuSurface
{
    SDLSurfaceOwner surface{nullptr, SDL_DestroySurface};
    UINT32 redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
    std::optional<SurfaceColorKey> sourceKey, destinationKey;
    std::optional<std::vector<RECT>> clipRegions;
    bool locked = false;
};

struct SurfaceLock
{
    void* pixels;
    int pitch, width, height;
};

std::unique_ptr<CpuSurface> CreateCpuSurface(UINT16 width, UINT16 height, UINT8 bits,
    UINT32 red = 0, UINT32 green = 0, UINT32 blue = 0, UINT32 alpha = 0);
SurfaceLock LockCpuSurface(CpuSurface& surface, const RECT* region = nullptr);
void UnlockCpuSurface(CpuSurface& surface);
void SetSurfaceClipRegions(CpuSurface& surface, std::span<const RECT> rectangles);
void FillCpuSurface(CpuSurface& destination, UINT32 color, const RECT* rectangle = nullptr);
void BlitCpuSurface(CpuSurface& destination, const RECT* destinationRect, CpuSurface& source,
    const RECT* sourceRect = nullptr, bool sourceKey = false, bool destinationKey = false);
