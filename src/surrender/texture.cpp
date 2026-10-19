#include <memory>
#include <utility>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "surrender/srClass.h"
#include "surrender/srGERD.h"
#include "surrender/srTexture.h"

// FUNCTION: SURRENDER 0x1005E440
srTexture::srTexture(std::shared_ptr<SDL_Surface> pixels) : surface(std::move(pixels))
{
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
    texture_dimensions_.format = SDL_PIXELFORMAT_ARGB4444;
    texture_flags_ = 0;
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

// FUNCTION: SURRENDER 0x1005E570
srTexture::~srTexture()
{
    invalidate();
    srGERD::unbindTexture(this);
}

// FUNCTION: SURRENDER 0x1005E390
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

// FUNCTION: SURRENDER 0x1005EB50
void srTexture::setDimensions(const Dimensions& dimensions)
{
    texture_dimensions_ = dimensions;
    texture_flags_ &= ~(1 << FLAG_DIRTY_DEFAULTS);
}

// FUNCTION: SURRENDER 0x1005EBE0
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

// FUNCTION: SURRENDER 0x1005EB20

// FUNCTION: SURRENDER 0x1005EB40
void srTexture::setMipmapBias(float bias)
{
    parameters.mipmap_bias = bias;
}

// FUNCTION: SURRENDER 0x1005EC80
void srTexture::setMagFilter(e_filter value)
{
    parameters.mag = value;
}

// FUNCTION: SURRENDER 0x1005ECA0
void srTexture::setMinFilter(e_filter value)
{
    parameters.min = value;
}

// FUNCTION: SURRENDER 0x1005ECC0
void srTexture::setMipmap(e_mipmap value)
{
    parameters.mipmap = value;
}

// FUNCTION: SURRENDER 0x1005ECE0
void srTexture::setWrapS(e_wrap value)
{
    parameters.wrap_s = value;
}

// FUNCTION: SURRENDER 0x1005ED00
void srTexture::setWrapT(e_wrap value)
{
    parameters.wrap_t = value;
}

// FUNCTION: SURRENDER 0x1005ED80

// FUNCTION: SURRENDER 0x1005EDA0

// FUNCTION: SURRENDER 0x1005EDF0
srTexture* srTexture::resolve()
{
    return this;
}

// FUNCTION: SURRENDER 0x1005EE00
srTexture::Dimensions srTexture::getDimensions()
{
    if ((texture_flags_ & (1 << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    texture_dimensions_.generate_mipmaps = parameters.mipmap != MIPMAP_NONE;
    return texture_dimensions_;
}

// FUNCTION: SURRENDER 0x1005EEB0
srTexture::Parameters srTexture::getParameters()
{
    return parameters;
}

// FUNCTION: SURRENDER 0x1005F140

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
