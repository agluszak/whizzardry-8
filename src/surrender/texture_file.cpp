#include <cstdint>
#include "surrender/stream_scope.h"
#include <ostream>
#include <utility>
#include <memory>
#include <string>

#include "surrender/srTextureFile.h"
#include "surrender/srImageIO.h"
#include "compat/compiler.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srTexture.h"

// FUNCTION: SURRENDER 0x1005F8E0
srTextureFile::srTextureFile(std::string file_name, std::int32_t cached)
    : cached(cached != 0), surface(nullptr), frame_handle(getNewFrameHandle())
{
    setFileName(std::move(file_name));
    if (!this->file_name.empty()) {
        setName(this->file_name.c_str());
        if (cached != 0) {
            setupDefaultValues();
        }
    }
}

// FUNCTION: SURRENDER 0x1005F780
srTextureFile& srTextureFile::operator=(const srTextureFile& other)
{
    if (this != &other) {
        srTexture::operator=(other);
        setFileName(other.file_name);
        cached = other.cached;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1005FAC0
srTextureFile::~srTextureFile()
{
    invalidate();
}

// FUNCTION: SURRENDER 0x1005F9F0
const std::string& srTextureFile::getFileName() const
{
    return file_name;
}

// FUNCTION: SURRENDER 0x1005FA00
void srTextureFile::setFileName(std::string file_name)
{
    invalidate();
    this->file_name = std::move(file_name);
    texture_flags_ &= ~1;
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

// FUNCTION: SURRENDER 0x1005FAA0
void srTextureFile::setCached(std::int32_t cached)
{
    if (cached != 0) {
        this->cached |= 1;
        return;
    }
    this->cached &= ~1;
}

// FUNCTION: SURRENDER 0x1005FF00
bool srTextureFile::isSurfaceLoaded() const
{
    return surface != 0;
}

// FUNCTION: SURRENDER 0x1005F7E0
void srTextureFile::loadSurface()
{
    if (surface != 0) {
        invalidate();
    }
    if (!file_name.empty()) {
        surface = 0;
        try {
            surface = srImage::load(file_name.c_str());
        } catch (...) {
            texture_flags_ |= 1 << FLAG_GENERATESURFACE_FAILURE;
            surface = 0;
        }
        if (surface != 0) {
            setupDefaultValues();
            surface->setFilter(getFilter());
        }
    }
}

// FUNCTION: SURRENDER 0x1005F7C0
void srTextureFile::releaseSurface()
{
    if (surface != 0) {
        surface = 0;
    }
}

// FUNCTION: SURRENDER 0x1005F8A0
std::uint32_t srTextureFile::getTextureFrameHandle()
{
    if ((texture_flags_ & (1 << FLAG_GENERATESURFACE_FAILURE)) != 0) {
        return 0;
    }
    return frame_handle;
}

// FUNCTION: SURRENDER 0x1005FA80
void srTextureFile::invalidate()
{
    releaseSurface();
    invalidateFrameHandle(frame_handle);
    texture_flags_ &= ~1;
}

// FUNCTION: SURRENDER 0x1005F8B0
void srTextureFile::setupDefaultValues()
{
    if ((texture_flags_ & (1 << FLAG_DIRTY_DEFAULTS)) != 0) {
        texture_flags_ &= ~(1 << FLAG_DIRTY_DEFAULTS);
        if (surface == 0) {
            loadSurface();
        }
        setupDefaultValuesFromSurface(surface.get());
    }
}

// FUNCTION: SURRENDER 0x1005FCB0
void srTextureFile::getMipmapData(const MultiRequest& request)
{
    if (!surface) loadSurface();
    if (!surface || request.destinations.empty() || !request.destinations.front()) return;
    copyMipmapChain(*surface, request);
    if ((cached & 1) == 0) releaseSurface();
}

// FUNCTION: SURRENDER 0x1005FD30
void srTextureFile::getMipmapLevelPartial(PartialRequest& request) {}

// FUNCTION: SURRENDER 0x1005FBD0
void srTextureFile::dump(std::ostream& stream)
{
    srTexture::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Filename: " << file_name << '\n';
    stream.width(0x20);
    stream << "  Caching enabled: " << ((cached & 1) != 0 ? "yes\n" : "no\n");
    stream.width(0x20);
    stream << "  Cached: " << (surface != 0 ? "yes\n" : "no\n");
}
