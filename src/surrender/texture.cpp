#include "surrender/stream_scope.h"
#include <cstddef>
#include <ostream>
#include <memory>
#include <string>

#include "surrender/srTexture.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "surrender/srGERD.h"
#include "surrender/srPixelConvert.h"
#include "compat/compiler.h"
#include "surrender/srColorSurfaceIFace.h"
#include "surrender/srFilter.h"
#include "surrender/srTextureIFace.h"
#include "surrender/srTypeRegistry.h"

// GLOBAL: SURRENDER 0x100A4A20
// Lazy e_flag names, "GENERATESURFACE_FAILURE,DIRTY_DEFAULTS"
static const char* s_flag_names3;

// GLOBAL: SURRENDER 0x100A4A1C
// srTexture::_frameHandle
w8_ulong srTexture::_frameHandle;

// FUNCTION: SURRENDER 0x1005EDE0
const char* srTexture::sGetClassName()
{
    return "srTexture";
}

// FUNCTION: SURRENDER 0x1005E440
srTexture::srTexture()
{
    texture_dimensions_.format.fourcc = 0;
    texture_dimensions_.hints = 0;
    texture_dimensions_.width = 0x40;
    texture_dimensions_.height = 0x40;
    texture_dimensions_.palette = srCore.getPalette();
    texture_dimensions_.filter = srCore.getFilter();
    texture_dimensions_.hints = 0;
    texture_dimensions_.compression = srTextureIFace::COMPRESSION_DEFAULT;
    texture_dimensions_.format = srPixelConvert::pixelFormat(srPixelConvert::SURFACE_ARGB4444);
    texture_flags_ = 0;
    if (s_flag_names3 == 0) {
        s_flag_names3 = "GENERATESURFACE_FAILURE,DIRTY_DEFAULTS";
    }
    mipmap_bias = 0.0f;
    packed_state = 0xe43;
    texture_priority = 0.5f;
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
        texture_priority = other.texture_priority;
        texture_dimensions_ = other.texture_dimensions_;
        texture_flags_ = other.texture_flags_;
    }
    return *this;
}

static const char* filterName(srTexture::e_filter filter)
{
    switch (filter) {
    case srTexture::FILTER_NONE:
        return "NONE";
    case srTexture::FILTER_FASTEST:
        return "FASTEST";
    case srTexture::FILTER_GOOD:
        return "GOOD";
    case srTexture::FILTER_BEST:
        return "BEST";
    default:
        return "DEFAULT";
    }
}

// FUNCTION: SURRENDER 0x1005E690
void srTexture::dump(std::ostream& stream)
{
    srClass::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Filter: " << getFilter()->getName() << '\n';
    const char* name;
    e_correction correction = getCorrection();
    e_filter mag_filter = getMagFilter();
    e_filter min_filter = getMinFilter();
    e_mipmap mipmap = getMipmap();
    e_wrap wrap_s = getWrapS();
    e_wrap wrap_t = getWrapT();
    stream.width(0x20);
    stream << "  Correction: ";
    switch (correction) {
    case CORRECTION_FASTEST:
        name = "FASTEST";
        break;
    case CORRECTION_GOOD:
        name = "GOOD";
        break;
    case CORRECTION_BEST:
        name = "BEST";
        break;
    default:
        name = "DEFAULT";
        break;
    }
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Mag. filter: ";
    name = filterName(mag_filter);
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Min. filter: ";
    name = filterName(min_filter);
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Mipmap: ";
    switch (mipmap) {
    case MIPMAP_NONE:
        name = "NONE";
        break;
    case MIPMAP_FASTEST:
        name = "FASTEST";
        break;
    case MIPMAP_BEST:
        name = "BEST";
        break;
    default:
        name = "DEFAULT";
        break;
    }
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Wrap S: ";
    if (wrap_s == WRAP_REPEAT) {
        name = "REPEAT";
    } else {
        name = "CLAMP";
    }
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Wrap T: ";
    if (wrap_t == WRAP_REPEAT) {
        name = "REPEAT";
    } else {
        name = "CLAMP";
    }
    stream << name << '\n';
    stream.width(0x20);
    stream << "  Mipmap bias: " << mipmap_bias << '\n';
    stream.width(0x20);
    stream << "  Priority: ";
    stream.width(0x20);
    stream << getPriority() << '\n';
    stream.width(0x20);
    stream << "  Flags: ";
    srDumpFlags(stream, texture_flags_, s_flag_names3);
    stream << '\n';
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
    for (srGERD* device = srGERD::getFirstOpen(); device != 0; device = device->getNextOpen()) {
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
void srTexture::setupDefaultValuesFromSurface(srColorSurfaceIFace* surface)
{
    if (surface != 0) {
        texture_dimensions_.width = surface->getWidth();
        texture_dimensions_.height = surface->getHeight();
        texture_dimensions_.format = surface->getPixelFormat();
        texture_dimensions_.palette = surface->getPalette();
    } else {
        texture_dimensions_.width = 1;
        texture_dimensions_.height = 1;
        texture_dimensions_.palette = 0;
    }
}

// FUNCTION: SURRENDER 0x1005EB00
srFilter* srTexture::getFilter() const
{
    return texture_dimensions_.filter;
}

// FUNCTION: SURRENDER 0x1005EB20
float srTexture::getPriority()
{
    return texture_priority;
}

// FUNCTION: SURRENDER 0x1005EAC0
void srTexture::setPriority(float priority)
{
    if (priority <= 0.0f) {
        texture_priority = 0.0f;
    } else if (priority >= 1.0f) {
        texture_priority = 1.0f;
    } else {
        texture_priority = priority;
    }
}

// FUNCTION: SURRENDER 0x1005EB10
float srTexture::getMipmapBias() const
{
    return mipmap_bias;
}

// FUNCTION: SURRENDER 0x1005EB30
void srTexture::setFilter(srFilter* filter)
{
    texture_dimensions_.filter = filter;
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

void srTexture::copyMipmapChain(srColorSurfaceIFace& source, const MultiRequest& request)
{
    if (request.destinations.empty()) {
        return;
    }
    if (auto* first = request.destinations.front()) {
        first->copy(source);
    }
    for (std::size_t level = 1; level < request.destinations.size(); ++level) {
        auto* destination = request.destinations[level];
        auto* previous = request.destinations[level - 1];
        if (destination && previous) {
            destination->copy(*previous);
        }
    }
}
