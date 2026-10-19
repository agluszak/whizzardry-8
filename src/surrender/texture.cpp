#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "surrender/srClass.h"
#include "surrender/srGERD.h"
#include "surrender/srTexture.h"

srTexture::srTexture(std::shared_ptr<SDL_Surface> pixels) : surface(std::move(pixels))
{
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
    texture_dimensions_.format = SDL_PIXELFORMAT_ARGB4444;
    texture_flags_ = 0;
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

srTexture::~srTexture()
{
    invalidate();
    srGERD::unbindTexture(this);
}

srTexture& srTexture::operator=(const srTexture& other)
{
    if (this != &other) {
        invalidate();
        srClass::operator=(other);
        parameters = other.parameters;
        texture_dimensions_ = other.texture_dimensions_;
        texture_flags_ = other.texture_flags_;
        surface = other.surface;
    }
    return *this;
}

void srTexture::setDimensions(const Dimensions& dimensions)
{
    texture_dimensions_ = dimensions;
    texture_flags_ &= ~(1 << FLAG_DIRTY_DEFAULTS);
}

void srTexture::setupDefaultValuesFromSurface(SDL_Surface* surface)
{
    if (surface != 0) {
        texture_dimensions_.width = surface->w;
        texture_dimensions_.height = surface->h;
        texture_dimensions_.format = surface->format;
    } else {
        texture_dimensions_.width = 1;
        texture_dimensions_.height = 1;
    }
}

void srTexture::setMipmapBias(float bias)
{
    parameters.mipmap_bias = bias;
}

void srTexture::setMagFilter(e_filter value)
{
    parameters.mag = value;
}

void srTexture::setMinFilter(e_filter value)
{
    parameters.min = value;
}

void srTexture::setMipmap(e_mipmap value)
{
    parameters.mipmap = value;
}

void srTexture::setWrapS(e_wrap value)
{
    parameters.wrap_s = value;
}

void srTexture::setWrapT(e_wrap value)
{
    parameters.wrap_t = value;
}

srTexture* srTexture::resolve()
{
    return this;
}

srTexture::Dimensions srTexture::getDimensions()
{
    if ((texture_flags_ & (1 << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    texture_dimensions_.generate_mipmaps = parameters.mipmap != MIPMAP_NONE;
    return texture_dimensions_;
}

srTexture::Parameters srTexture::getParameters()
{
    return parameters;
}

void srTexture::invalidate()
{
    if (auto* renderer = srGERD::getActive()) renderer->invalidateTexture(this);
}

void srTexture::setSurfacePtr(std::shared_ptr<SDL_Surface> pixels)
{
    if (pixels == surface) return;
    invalidate();
    surface = std::move(pixels);
    setupDefaultValues();
}

void srTexture::setupDefaultValues()
{
    setupDefaultValuesFromSurface(surface.get());
    texture_flags_ &= ~(1u << FLAG_DIRTY_DEFAULTS);
}
