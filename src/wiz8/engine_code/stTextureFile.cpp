#include "wiz8/engine_code/stTextureFile.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_pixels.h>

#include "asset_paths.h"
#include "compat/compiler.h"
#include "surrender/srImageIO.h"
#include "surrender/srTexture.h"
#include "wiz8/filesystem.h"

// VTABLE: WIZ8 0x005EC5F8
// class stTextureFile

// VTABLE: WIZ8 0x005EC5D8
// class srClientSupport<srPalette,10496>

// FUNCTION: WIZ8 0x0047BBD0
void stTextureFile::releaseSurface()
{
    surface.reset();
    texture_flags_ |= DEFAULTS_PENDING;
}

// FUNCTION: WIZ8 0x0047C630
stTextureFile::stTextureFile(const char* file_name, int cached)
    : cached(0), surface(nullptr), frame_handle(getNewFrameHandle()), has_alpha(0)
{
    /* Retail stores 0 then conditionally stores 1: the authored value is the
       normalized predicate, not the raw parameter. */
    this->cached = (cached != 0);
    invalidate();
    setFileName(file_name ? file_name : "");
    if (file_name != 0) {
        setName(file_name);
    }
    if (cached != 0 && file_name != 0) {
        setupDefaultValues();
    }
}

stTextureFile& stTextureFile::operator=(const stTextureFile& other)
{
    if (this != &other) {
        srTexture::operator=(other);
        setFileName(other.file_name);
        cached = other.cached;
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047C5F0
w8_ulong stTextureFile::getTextureFrameHandle()
{
    if ((texture_flags_ & LOAD_FAILED) != 0) {
        return 0;
    }
    return frame_handle;
}

// FUNCTION: WIZ8 0x0047C600
void stTextureFile::setupDefaultValues()
{
    if ((texture_flags_ & DEFAULTS_PENDING) == 0) {
        return;
    }

    texture_flags_ &= ~DEFAULTS_PENDING;
    if (surface == 0) {
        loadSurface();
    }
    setupDefaultValuesFromSurface(surface.get());
}

// FUNCTION: WIZ8 0x0047C830
void stTextureFile::setFileName(std::string file_name)
{
    invalidate();
    this->file_name = std::move(file_name);
    texture_flags_ &= ~LOAD_FAILED;
    texture_flags_ |= DEFAULTS_PENDING;
}

// FUNCTION: WIZ8 0x0047C8B0
void stTextureFile::invalidate()
{
    releaseSurface();
    invalidateFrameHandle(frame_handle);
    texture_flags_ &= ~LOAD_FAILED;
}

// FUNCTION: WIZ8 0x0047C8E0
stTextureFile::~stTextureFile()
{
    invalidate();
}

// FUNCTION: WIZ8 0x0047BBF0
void stTextureFile::loadSurface()
{
    if (surface != 0) {
        invalidate();
    }
    /* A missing file name lands on the same LOAD_FAILED tail as a failed
       load; retail has no silent early return here. */
    if (file_name.empty()) {
        texture_flags_ |= LOAD_FAILED;
        return;
    }

    surface = 0;
    std::unique_ptr<wiz8::File> handle = [&]() {
        try {
            return wiz8::open_file(wiz8::asset_path(file_name), wiz8::OpenMode::read);
        } catch (const std::exception&) {
            return std::unique_ptr<wiz8::File>{};
        }
    }();
    if (handle != 0) {
        surface = srImage::loadTga(*handle);
        if (handle) handle->close();
        handle.reset();
    }

    if (surface == 0) {
        texture_flags_ |= LOAD_FAILED;
        return;
    }

    setupDefaultValues();
    /* Retail reads the alpha channel count straight out of the surface's
       pixel format (unsigned SETA): the authored comparison is `> 0`. */
    has_alpha = (SDL_ISPIXELFORMAT_ALPHA(surface->format));
}

// FUNCTION: WIZ8 0x0047CA50
void stTextureFile::getMipmapData(const MultiRequest& request)
{
    if (!surface) loadSurface();
    if (!surface) return;
    copyMipmapChain(*surface, request);
    if ((cached & 1) == 0) releaseSurface();
}

void stTextureFile::getMipmapLevelPartial(PartialRequest&) {}
