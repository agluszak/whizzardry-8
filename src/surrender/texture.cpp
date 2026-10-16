#include <cstddef>
#include <string>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>

#include "compat/compiler.h"
#include "surrender/srClass.h"
#include "surrender/srGERD.h"
#include "surrender/srTexture.h"
#include "surrender/srTextureIFace.h"
#include "wiz8/surface_storage.h"

// GLOBAL: SURRENDER 0x100A4A20
// Lazy e_flag names, "GENERATESURFACE_FAILURE,DIRTY_DEFAULTS"

// GLOBAL: SURRENDER 0x100A4A1C
// srTexture::_frameHandle
w8_ulong srTexture::_frameHandle;
// FUNCTION: SURRENDER 0x1005E440
srTexture::srTexture()
{
    texture_dimensions_.hints = 0;
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
    texture_dimensions_.hints = 0;
    texture_dimensions_.compression = srTextureIFace::COMPRESSION_DEFAULT;
    texture_dimensions_.format = SDL_PIXELFORMAT_ARGB4444;
    texture_flags_ = 0;
    mipmap_bias = 0.0f;
    packed_state = 0xe43;
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

// FUNCTION: SURRENDER 0x1005E570
srTexture::~srTexture() = default;

// FUNCTION: SURRENDER 0x1005E390
srTexture& srTexture::operator=(const srTexture& other)
{
    if (this != &other) {
        invalidate();
        srClass::operator=(other);
        packed_state = other.packed_state;
        mipmap_bias = other.mipmap_bias;
        texture_dimensions_ = other.texture_dimensions_;
        texture_flags_ = other.texture_flags_;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x1005EEF0
w8_ulong srTexture::getNewFrameHandle()
{
    _frameHandle = _frameHandle + 1;
    return _frameHandle;
}

// FUNCTION: SURRENDER 0x1005EA90
void srTexture::invalidateFrameHandle(w8_ulong handle)
{
    if (auto* device = srGERD::getActive()) {
        device->invalidateTextureByFrameHandle(handle);
    }
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

// FUNCTION: SURRENDER 0x1005EAC0

// FUNCTION: SURRENDER 0x1005EB10
float srTexture::getMipmapBias() const
{
    return mipmap_bias;
}

// FUNCTION: SURRENDER 0x1005EBD0
void srTexture::setDimensionsDirty()
{
    texture_flags_ |= 1 << FLAG_DIRTY_DEFAULTS;
}

// FUNCTION: SURRENDER 0x1005EB40
void srTexture::setMipmapBias(float bias)
{
    mipmap_bias = bias;
}

// Packed state keeps each field at a fixed shift; these helpers read and replace one field.
template<class Enum>
static w8_ulong withField(w8_ulong packed, w8_ulong field_mask, unsigned shift, Enum value)
{
    return (packed & ~field_mask) | (static_cast<w8_ulong>(value) << shift);
}

static w8_ulong readField(w8_ulong packed, unsigned shift, w8_ulong width_mask)
{
    return (packed >> shift) & width_mask;
}

// FUNCTION: SURRENDER 0x1005EC60
void srTexture::setCorrection(e_correction correction)
{
    packed_state = (packed_state & ~Parameters::CORRECTION_MASK) | correction;
}

// FUNCTION: SURRENDER 0x1005EC80
void srTexture::setMagFilter(e_filter filter)
{
    packed_state =
        withField(packed_state, Parameters::MAG_FILTER_MASK, Parameters::MAG_FILTER_SHIFT, filter);
}

// FUNCTION: SURRENDER 0x1005ECA0
void srTexture::setMinFilter(e_filter filter)
{
    packed_state =
        withField(packed_state, Parameters::MIN_FILTER_MASK, Parameters::MIN_FILTER_SHIFT, filter);
}

// FUNCTION: SURRENDER 0x1005ECC0
void srTexture::setMipmap(e_mipmap mipmap)
{
    packed_state =
        withField(packed_state, Parameters::MIPMAP_MASK, Parameters::MIPMAP_SHIFT, mipmap);
}

// FUNCTION: SURRENDER 0x1005ECE0
void srTexture::setWrapS(e_wrap wrap)
{
    packed_state = withField(packed_state, Parameters::WRAP_S_MASK, Parameters::WRAP_S_SHIFT, wrap);
}

// FUNCTION: SURRENDER 0x1005ED00
void srTexture::setWrapT(e_wrap wrap)
{
    packed_state = withField(packed_state, Parameters::WRAP_T_MASK, Parameters::WRAP_T_SHIFT, wrap);
}

// FUNCTION: SURRENDER 0x1005ED20
srTextureIFace::e_correction srTexture::getCorrection() const
{
    return static_cast<e_correction>(packed_state & Parameters::CORRECTION_MASK);
}

// FUNCTION: SURRENDER 0x1005ED30
srTextureIFace::e_filter srTexture::getMagFilter() const
{
    return static_cast<e_filter>(readField(packed_state, Parameters::MAG_FILTER_SHIFT, 7));
}

// FUNCTION: SURRENDER 0x1005ED40
srTextureIFace::e_filter srTexture::getMinFilter() const
{
    return static_cast<e_filter>(readField(packed_state, Parameters::MIN_FILTER_SHIFT, 7));
}

// FUNCTION: SURRENDER 0x1005ED50
srTextureIFace::e_mipmap srTexture::getMipmap() const
{
    return static_cast<e_mipmap>(readField(packed_state, Parameters::MIPMAP_SHIFT, 3));
}

// FUNCTION: SURRENDER 0x1005ED60
srTextureIFace::e_wrap srTexture::getWrapS() const
{
    return static_cast<e_wrap>(readField(packed_state, Parameters::WRAP_S_SHIFT, 1));
}

// FUNCTION: SURRENDER 0x1005ED70
srTextureIFace::e_wrap srTexture::getWrapT() const
{
    return static_cast<e_wrap>(readField(packed_state, Parameters::WRAP_T_SHIFT, 1));
}

// FUNCTION: SURRENDER 0x1005ED80
void srTexture::enableHint(e_hint hint)
{
    texture_dimensions_.hints |= 1 << hint;
}

// FUNCTION: SURRENDER 0x1005EDA0
void srTexture::disableHint(e_hint hint)
{
    texture_dimensions_.hints &= ~(1 << hint);
}

// FUNCTION: SURRENDER 0x1005EDC0
int srTexture::isHintEnabled(e_hint hint) const
{
    return (texture_dimensions_.hints & (1 << hint)) != 0;
}

// FUNCTION: SURRENDER 0x1005EED0
srTextureIFace::e_compression srTexture::getCompression() const
{
    return texture_dimensions_.compression;
}

// FUNCTION: SURRENDER 0x1005EEE0
void srTexture::setCompression(e_compression compression)
{
    texture_dimensions_.compression = compression;
}

// FUNCTION: SURRENDER 0x1005EF00
w8_ulong srTexture::getNewFrameHandles(w8_ulong count)
{
    w8_ulong first = _frameHandle + 1;
    _frameHandle = _frameHandle + count;
    return first;
}

// FUNCTION: SURRENDER 0x1005EDF0
w8_ulong srTexture::getTextureFrameHandle()
{
    return 0;
}

// FUNCTION: SURRENDER 0x1005EE00
srTextureIFace::Dimensions srTexture::getDimensions()
{
    if ((texture_flags_ & (1 << FLAG_DIRTY_DEFAULTS)) != 0) {
        setupDefaultValues();
    }
    return texture_dimensions_;
}

// FUNCTION: SURRENDER 0x1005EE90
void srTexture::getMipmapData(const MultiRequest& request) {}

// FUNCTION: SURRENDER 0x1005EEA0
void srTexture::getMipmapLevelPartial(PartialRequest& request) {}

// FUNCTION: SURRENDER 0x1005EEB0
srTextureIFace::Parameters srTexture::getParameters()
{
    return {packed_state, mipmap_bias};
}

srTextureIFace::~srTextureIFace()
{
    srGERD::unbindTexture(this);
}

// FUNCTION: SURRENDER 0x1005F140
const char* srTextureIFace::getTextureName()
{
    return getName().c_str();
}

void srTexture::copyMipmapChain(SDL_Surface& source, const MultiRequest& request)
{
    if (request.destinations.empty()) return;
    if (auto* first = request.destinations.front())
        StretchCpuSurface(source, *first, SDL_SCALEMODE_LINEAR);
    for (std::size_t level = 1; level < request.destinations.size(); ++level) {
        auto* destination = request.destinations[level];
        auto* previous = request.destinations[level - 1];
        if (destination && previous)
            StretchCpuSurface(*previous, *destination, SDL_SCALEMODE_LINEAR);
    }
}
