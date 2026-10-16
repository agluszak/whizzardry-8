#include "wiz8/engine_code/stTextureFile.h"

#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <SDL3/SDL_pixels.h>

#include "asset_paths.h"
#include "surrender/srImageIO.h"
#include "surrender/srTexture.h"
#include "wiz8/filesystem.h"

// VTABLE: WIZ8 0x005EC5F8
// class stTextureFile

// FUNCTION: WIZ8 0x0047BBD0
void stTextureFile::releaseSurface()
{
    surface.reset();
    texture_flags_ |= DEFAULTS_PENDING;
}

// FUNCTION: WIZ8 0x0047C630
stTextureFile::stTextureFile(const char* file_name, int preload) : has_alpha(0)
{
    invalidate();
    setFileName(file_name ? file_name : "");
    if (file_name != 0) {
        setName(file_name);
    }
    if (preload != 0 && file_name != 0) {
        setupDefaultValues();
    }
}

stTextureFile& stTextureFile::operator=(const stTextureFile& other)
{
    if (this != &other) {
        srTexture::operator=(other);
        setFileName(other.file_name);
    }
    return *this;
}

// FUNCTION: WIZ8 0x0047C5F0
srTexture* stTextureFile::resolve()
{
    if (!surface && (texture_flags_ & LOAD_FAILED) == 0) loadSurface();
    return surface ? this : nullptr;
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
    srTexture::invalidate();
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
