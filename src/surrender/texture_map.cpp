#include <memory>
#include <utility>

#include <SDL3/SDL_surface.h>

#include "compat/compiler.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureMap.h"

// FUNCTION: SURRENDER 0x10060270
srTextureMap::srTextureMap(std::shared_ptr<SDL_Surface> surface)
{
    frame_handle = getNewFrameHandle();
    setSurfacePtr(std::move(surface));
}

// FUNCTION: SURRENDER 0x10060210
srTextureMap& srTextureMap::operator=(const srTextureMap& other)
{
    if (this != &other) {
        srTexture::operator=(other);
        surface = other.surface;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x10060380
srTextureMap::~srTextureMap()
{
    invalidate();
}

// FUNCTION: SURRENDER 0x100601C0
void srTextureMap::getMipmapData(const MultiRequest& request)
{
    if (surface) copyMipmapChain(*surface, request);
}

// FUNCTION: SURRENDER 0x100604E0
void srTextureMap::setSurfacePtr(std::shared_ptr<SDL_Surface> surface)
{
    if (surface != this->surface) {
        invalidate();
        this->surface = std::move(surface);
        setupDefaultValues();
    }
}

// FUNCTION: SURRENDER 0x10060490
SDL_Surface* srTextureMap::getSurfacePtr() const
{
    return surface.get();
}

// FUNCTION: SURRENDER 0x100604A0
void srTextureMap::invalidate()
{
    invalidateFrameHandle(frame_handle);
}

// FUNCTION: SURRENDER 0x100604B0
w8_ulong srTextureMap::getTextureFrameHandle()
{
    return frame_handle;
}

// FUNCTION: SURRENDER 0x100604C0
void srTextureMap::setupDefaultValues()
{
    setupDefaultValuesFromSurface(surface.get());
    texture_flags_ &= ~(1 << FLAG_DIRTY_DEFAULTS);
}
