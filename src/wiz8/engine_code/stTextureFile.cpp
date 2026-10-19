#include "wiz8/engine_code/stTextureFile.h"

#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>

#include "surrender/srImageIO.h"
#include "surrender/srTexture.h"
#include "wiz8/filesystem.h"

void stTextureFile::releaseSurface()
{
    surface.reset();
    texture_flags_ |= DEFAULTS_PENDING;
}

stTextureFile::stTextureFile(std::filesystem::path file_name, bool preload)
    : file_name(std::move(file_name)), has_alpha(false)
{
    invalidate();
    setName(wiz8::path_to_utf8(this->file_name).c_str());
    if (preload) setupDefaultValues();
}

srTexture* stTextureFile::resolve()
{
    if (!surface && (texture_flags_ & LOAD_FAILED) == 0) loadSurface();
    return surface ? this : nullptr;
}

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

void stTextureFile::invalidate()
{
    releaseSurface();
    srTexture::invalidate();
    texture_flags_ &= ~LOAD_FAILED;
}

stTextureFile::~stTextureFile()
{
    invalidate();
}

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
            return wiz8::open_file(file_name, wiz8::OpenMode::read);
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
