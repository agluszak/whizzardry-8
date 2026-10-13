/* D:\srsdk1x\sources\corelib\srMaterial.cpp */

#include <algorithm>
#include <cstddef>
#include <ostream>

#include "surrender/srMaterial.h"
#include "surrender/stream_scope.h"
#include "surrender/srCore.h"
#include "surrender/srDebug.h"
#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srFlags.h"
#include "surrender/srMaterialIFace.h"
#include "surrender/srShader.h"
#include "surrender/srTypeRegistry.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"

/* Comma-separated operation names srMaterial::dump walks while printing the operations bits; never
   assigned. */
// GLOBAL: SURRENDER 0x100A48C0
static const char* s_oper_names;

// Retail assert location; kept so failure messages match the original build.
constexpr const char* kMaterialSourcePath = "D:\\srsdk1x\\sources\\corelib\\srMaterial.cpp";

// FUNCTION: SURRENDER 0x100335D0
void srMaterial::verify(srRuntimeClass::e_verify mode)
{
    srClass::verify(mode);
    // Fails only outside [0, upper]; NaN passes, matching the original explicit comparisons.
    const auto checkRange = [](double value, double upper, const char* condition, int line) {
        if (value < 0.0 || value > upper) {
            srAssertFail(condition, kMaterialSourcePath, line, 0);
        }
    };
    checkRange(parms.ambient.x, 1.0, "parms.ambient.x >= 0.0 && parms.ambient.x <= 1.0", 0x29);
    checkRange(parms.ambient.y, 1.0, "parms.ambient.y >= 0.0 && parms.ambient.y <= 1.0", 0x2a);
    checkRange(parms.ambient.z, 1.0, "parms.ambient.z >= 0.0 && parms.ambient.z <= 1.0", 0x2b);
    checkRange(parms.ambient.w, 1.0, "parms.ambient.w >= 0.0 && parms.ambient.w <= 1.0", 0x2c);
    checkRange(parms.diffuse.x, 1.0, "parms.diffuse.x >= 0.0 && parms.diffuse.x <= 1.0", 0x2d);
    checkRange(parms.diffuse.y, 1.0, "parms.diffuse.y >= 0.0 && parms.diffuse.y <= 1.0", 0x2e);
    checkRange(parms.diffuse.z, 1.0, "parms.diffuse.z >= 0.0 && parms.diffuse.z <= 1.0", 0x2f);
    checkRange(parms.diffuse.w, 1.0, "parms.diffuse.w >= 0.0 && parms.diffuse.w <= 1.0", 0x30);
    checkRange(parms.emissive.x, 1.0, "parms.emissive.x >= 0.0 && parms.emissive.x <= 1.0", 0x31);
    checkRange(parms.emissive.y, 1.0, "parms.emissive.y >= 0.0 && parms.emissive.y <= 1.0", 0x32);
    checkRange(parms.emissive.z, 1.0, "parms.emissive.z >= 0.0 && parms.emissive.z <= 1.0", 0x33);
    checkRange(parms.emissive.w, 1.0, "parms.emissive.w >= 0.0 && parms.emissive.w <= 1.0", 0x34);
    checkRange(parms.specular.x, 1.0, "parms.specular.x >= 0.0 && parms.specular.x <= 1.0", 0x35);
    checkRange(parms.specular.y, 1.0, "parms.specular.y >= 0.0 && parms.specular.y <= 1.0", 0x36);
    checkRange(parms.specular.z, 1.0, "parms.specular.z >= 0.0 && parms.specular.z <= 1.0", 0x37);
    checkRange(parms.specular.w, 1.0, "parms.specular.w >= 0.0 && parms.specular.w <= 1.0", 0x38);
    checkRange(parms.shininess, 127.0, "parms.shininess >= 0.0 && parms.shininess <= 127.0", 0x39);
    checkRange(parms.translucency, 1.0, "parms.translucency >= 0.0 && parms.translucency <= 1.0", 0x3a);
}

// FUNCTION: SURRENDER 0x100339B0
void srMaterial::updateParms()
{
    parms.disabled_channels = 0;
    if (parms.diffuse.x == 0.0f && parms.diffuse.y == 0.0f && parms.diffuse.z == 0.0f) {
        parms.disabled_channels = (1UL << srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
    }
    if (parms.ambient.x == 0.0f && parms.ambient.y == 0.0f && parms.ambient.z == 0.0f) {
        parms.disabled_channels |= (1UL << srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
    }
    if (parms.specular.x == 0.0f && parms.specular.y == 0.0f && parms.specular.z == 0.0f) {
        parms.disabled_channels |= (1UL << srVertexProcessor::CHANNEL_SPECULAR);
    }
    if (parms.fog_scale == 0.0f) {
        parms.disabled_channels |= (1UL << srVertexProcessor::CHANNEL_FOG);
    }
    dirty = 0;
}

// FUNCTION: SURRENDER 0x10033A80
srMaterial& srMaterial::operator=(const srMaterial& other)
{
    if (&other != this) {
        srMaterialIFace::operator=(other);
        parms = other.parms;
        operations.value = other.operations.value;
        mapper = other.mapper;
        dirty = 1;
        return *this;
    }
    return *this;
}

// FUNCTION: SURRENDER 0x10033B50
srVertexProcessor::MaterialInfo srMaterial::getMaterialInfo()
{
    if (dirty != 0) {
        updateParms();
    }
    return parms;
}

// FUNCTION: SURRENDER 0x10033C00
void srMaterial::preProcess(srVertexPipe& pipe)
{
    if ((operations.value & (1UL << OPER_DIFFUSE_SPECULAR)) != 0) {
        pipe.enableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        pipe.enableChannel(srVertexProcessor::CHANNEL_SPECULAR);
        return;
    }
    if ((operations.value & (1UL << OPER_DIFFUSE)) != 0) {
        pipe.enableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        pipe.disableChannel(srVertexProcessor::CHANNEL_SPECULAR);
        return;
    }
    if ((operations.value & (1UL << OPER_SPECULAR)) != 0) {
        pipe.disableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
        pipe.disableChannel(srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        pipe.disableChannel(srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        pipe.enableChannel(srVertexProcessor::CHANNEL_SPECULAR);
    }
}

// FUNCTION: SURRENDER 0x10033C90
void srMaterial::postProcess(srVertexPipe& pipe)
{
    if (mapper != nullptr && mapper->isActive(pipe)) {
        mapper->process(pipe);
    }
    const auto operations = this->operations.value;
    bool depth_cue = (operations & (1u << OPER_DEPTH_CUE)) != 0;
    const auto cueColor = [&](bool specular) {
        const auto depth = pipe.getDepthCue();
        const auto color = specular ? pipe.getSpecular() : pipe.getDiffuse();
        for (w8_ulong i = 0; i < pipe.vertex_count; ++i) {
            color[i] *= depth[i];
        }
    };

    if ((operations & (1u << OPER_DIFFUSE_SPECULAR)) != 0) {
        pipe.swapDiffuseAndSpecular();
    } else if ((operations & (1u << OPER_DIFFUSE)) != 0) {
        if (depth_cue) {
            cueColor(false);
            depth_cue = false;
        }
        pipe.copyDiffuseToSpecular();
        pipe.enableChannel(srVertexProcessor::CHANNEL_SPECULAR);
    } else if ((operations & (1u << OPER_SPECULAR)) != 0) {
        if (depth_cue) {
            cueColor(true);
            depth_cue = false;
        }
        pipe.copySpecularToDiffuse();
        pipe.enableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
    }
    if (depth_cue) {
        if ((pipe.channel_mask & (1u << srVertexProcessor::CHANNEL_DIFFUSE)) != 0) {
            cueColor(false);
        }
        if ((pipe.channel_mask & (1u << srVertexProcessor::CHANNEL_SPECULAR)) != 0) {
            cueColor(true);
        }
    }
    if ((operations & (1u << OPER_ALPHA)) != 0 &&
        (pipe.channel_mask & (1u << srVertexProcessor::CHANNEL_ALPHA)) != 0) {
        const auto depth = pipe.getDepthCue();
        const auto alpha = pipe.getAlpha();
        for (w8_ulong i = 0; i < pipe.vertex_count; ++i) {
            alpha[i] *= depth[i];
        }
    }
}

// FUNCTION: SURRENDER 0x10033F60
void srMaterial::reset()
{
    parms.ambient = glm::vec4(0.2f, 0.2f, 0.2f, 1.0f);
    parms.diffuse = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    parms.specular = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    parms.emissive = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    parms.shininess = 1.0f;
    parms.translucency = 0.0f;
    parms.disabled_channels = 0;
    parms.fog_scale = 1.0f;
    parms.value_38 = 0.0f;
    dirty = 1;
    operations.value = 0;
    mapper = 0;
}

// FUNCTION: SURRENDER 0x10033FC0
void srMaterial::dump(std::ostream& stream)
{
    srClass::dump(stream);
    const srDumpStreamScope formatting(stream);
    stream.width(0x20);
    stream << "  Ambient: " << '{' << parms.ambient.x << ',' << parms.ambient.y << ','
           << parms.ambient.z << ',' << parms.ambient.w << '}' << '\n';
    stream.width(0x20);
    stream << "  Diffuse: " << '{' << parms.diffuse.x << ',' << parms.diffuse.y << ','
           << parms.diffuse.z << ',' << parms.diffuse.w << '}' << '\n';
    stream.width(0x20);
    stream << "  Emissive: " << '{' << parms.emissive.x << ',' << parms.emissive.y << ','
           << parms.emissive.z << ',' << parms.emissive.w << '}' << '\n';
    stream.width(0x20);
    stream << "  Specular: " << '{' << parms.specular.x << ',' << parms.specular.y << ','
           << parms.specular.z << ',' << parms.specular.w << '}' << '\n';
    stream.width(0x20);
    stream << "  Shininess: " << parms.shininess << '\n';
    stream.width(0x20);
    stream << "  Translucency: " << parms.translucency << '\n';
    stream.width(0x20);
    stream << "  Op. Control Flags: ";
    srDumpFlags(stream, operations.value, s_oper_names);
    stream << '\n';
    stream.width(0x20);
    stream << "  Texture mapper: " << mapper << '\n';
    stream.width(0x20);
    stream << "  Dirty: " << srBoolToString(dirty) << '\n';
}

// FUNCTION: SURRENDER 0x100343B0
const char* srMaterial::sGetClassName()
{
    return "srMaterial";
}

// FUNCTION: SURRENDER 0x10034640
glm::vec4 srMaterial::getAmbient() const
{
    return parms.ambient;
}

// FUNCTION: SURRENDER 0x10034670
glm::vec4 srMaterial::getDiffuse() const
{
    return parms.diffuse;
}

// FUNCTION: SURRENDER 0x100346A0
glm::vec4 srMaterial::getEmissive() const
{
    return parms.emissive;
}

// FUNCTION: SURRENDER 0x100346D0
glm::vec4 srMaterial::getSpecular() const
{
    return parms.specular;
}

// FUNCTION: SURRENDER 0x100348B0
float srMaterial::getShininess() const
{
    return parms.shininess;
}

// FUNCTION: SURRENDER 0x100348C0
float srMaterial::getTranslucency() const
{
    return parms.translucency;
}

// FUNCTION: SURRENDER 0x100348D0
float srMaterial::getOpacity() const
{
    return parms.diffuse.w;
}

// FUNCTION: SURRENDER 0x100348E0
void srMaterial::setAmbientAndDiffuse(const glm::vec4& color)
{
    parms.ambient = color;
    dirty = 1;
    parms.diffuse = color;
    dirty = 1;
}

// FUNCTION: SURRENDER 0x100349F0
void srMaterial::setShininess(double shininess)
{
    parms.shininess = static_cast<float>(shininess);
    if (static_cast<float>(shininess) < 1.0f) {
        parms.shininess = 1.0f;
    }
    if (parms.shininess > 127.0f) {
        parms.shininess = 127.0f;
    }
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034A30
void srMaterial::setTranslucency(double translucency)
{
    float value = static_cast<float>(translucency);
    if (value <= 0.0f) {
        dirty = 1;
        parms.translucency = 0.0f;
        return;
    }
    if (value >= 1.0f) {
        value = 1.0f;
    }
    parms.translucency = value;
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034AA0
void srMaterial::setVector(glm::vec4& destination, const glm::vec4& source)
{
    destination = source;
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034AD0
void srMaterial::disable(e_oper operation)
{
    operations.value &= ~(1 << (operation & 0x1f));
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034B00
void srMaterial::enable(e_oper operation)
{
    operations.value |= 1 << (operation & 0x1f);
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034B20
int srMaterial::isEnabled(e_oper operation) const
{
    return (operations.value & (1 << (operation & 0x1f))) != 0;
}

// FUNCTION: SURRENDER 0x10034B50
srVertexProcessor* srMaterial::getMapper() const
{
    return mapper;
}

// FUNCTION: SURRENDER 0x10034DA0
const char* srMaterialIFace::sGetClassName()
{
    return "srMaterialIFace";
}

// Shader fields print as names. A value with no name prints nothing, as the old switches did.
struct NamedValue {
    w8_ulong value;
    const char* name;
};

template<std::size_t Count>
static void dumpNamed(std::ostream& stream, w8_ulong value, const NamedValue (&names)[Count])
{
    if (const auto found = std::ranges::find(names, value, &NamedValue::value); found != std::end(names))
        stream << found->name;
}

constexpr NamedValue kDetailAlphaNames[] = {
    {srShader::DETAILALPHA_DISABLE, "DETAILALPHA_DISABLE"},
    {srShader::DETAILALPHA_DETAIL, "DETAILALPHA_DETAIL"},
    {srShader::DETAILALPHA_SCALE, "DETAILALPHA_SCALE"},
    {srShader::DETAILALPHA_INVSCALE, "DETAILALPHA_INVSCALE"},
};

constexpr NamedValue kDetailColorNames[] = {
    {srShader::DETAILCOLOR_DISABLE, "DETAILCOLOR_DISABLE"},
    {srShader::DETAILCOLOR_DETAIL, "DETAILCOLOR_DETAIL"},
    {srShader::DETAILCOLOR_SCALE, "DETAILCOLOR_SCALE"},
    {srShader::DETAILCOLOR_INVSCALE, "DETAILCOLOR_INVSCALE"},
    {srShader::DETAILCOLOR_ADD, "DETAILCOLOR_ADD"},
    {srShader::DETAILCOLOR_SUB, "DETAILCOLOR_SUB"},
    {srShader::DETAILCOLOR_SUBR, "DETAILCOLOR_SUBR"},
    {srShader::DETAILCOLOR_BLEND, "DETAILCOLOR_BLEND"},
    {srShader::DETAILCOLOR_DETAILBLEND, "DETAILCOLOR_DETAILBLEND"},
};

constexpr NamedValue kPassNames[] = {
    {srShader::PASS_NEVER, "PASS_NEVER"},
    {srShader::PASS_LESS, "PASS_LESS"},
    {srShader::PASS_EQUAL, "PASS_EQUAL"},
    {srShader::PASS_LEQUAL, "PASS_LEQUAL"},
    {srShader::PASS_GREATER, "PASS_GREATER"},
    {srShader::PASS_NOTEQUAL, "PASS_NOTEQUAL"},
    {srShader::PASS_GEQUAL, "PASS_GEQUAL"},
    {srShader::PASS_ALWAYS, "PASS_ALWAYS"},
};

constexpr NamedValue kDepthWriteNames[] = {
    {srShader::DEPTH_WRITE_DISABLE, "DEPTH_WRITE_DISABLE"},
    {srShader::DEPTH_WRITE_ENABLE, "DEPTH_WRITE_ENABLE"},
};

constexpr NamedValue kColorWriteNames[] = {
    {srShader::COLOR_WRITE_DISABLE, "COLOR_WRITE_DISABLE"},
    {srShader::COLOR_WRITE_ENABLE, "COLOR_WRITE_ENABLE"},
};

constexpr NamedValue kAlphaTestNames[] = {
    {srShader::ALPHATEST_DISABLE, "ALPHATEST_DISABLE"},
    {srShader::ALPHATEST_ENABLE, "ALPHATEST_ENABLE"},
};

constexpr NamedValue kDitherNames[] = {
    {srShader::DITHER_DISABLE, "DITHER_DISABLE"},
    {srShader::DITHER_ENABLE, "DITHER_ENABLE"},
};

constexpr NamedValue kSrcBlendNames[] = {
    {srShader::SRCBLEND_ZERO, "SRCBLEND_ZERO"},
    {srShader::SRCBLEND_ONE, "SRCBLEND_ONE"},
    {srShader::SRCBLEND_SRC_ALPHA, "SRCBLEND_SRC_ALPHA"},
    {srShader::SRCBLEND_ONE_MINUS_SRC_ALPHA, "SRCBLEND_ONE_MINUS_SRC_ALPHA"},
};

constexpr NamedValue kDstBlendNames[] = {
    {srShader::DSTBLEND_ZERO, "DSTBLEND_ZERO"},
    {srShader::DSTBLEND_ONE, "DSTBLEND_ONE"},
    {srShader::DSTBLEND_SRC_COLOR, "DSTBLEND_SRC_COLOR"},
    {srShader::DSTBLEND_ONE_MINUS_SRC_COLOR, "DSTBLEND_ONE_MINUS_SRC_COLOR"},
    {srShader::DSTBLEND_SRC_ALPHA, "DSTBLEND_SRC_ALPHA"},
    {srShader::DSTBLEND_ONE_MINUS_SRC_ALPHA, "DSTBLEND_ONE_MINUS_SRC_ALPHA"},
};

constexpr NamedValue kFogNames[] = {
    {srShader::FOG_DISABLE, "FOG_DISABLE"},
    {srShader::FOG_ENABLE, "FOG_ENABLE"},
    {srShader::FOG_SCALE_FRAGMENT, "FOG_SCALE_FRAGMENT"},
    {srShader::FOG_WHITE, "FOG_WHITE"},
};

constexpr NamedValue kGradientNames[] = {
    {srShader::GRADIENT_DISABLE, "GRADIENT_DISABLE"},
    {srShader::GRADIENT_MODULATE, "GRADIENT_MODULATE"},
    {srShader::GRADIENT_ADD, "GRADIENT_ADD"},
};

constexpr NamedValue kSecondaryGradientNames[] = {
    {srShader::SECONDARY_GRADIENT_DISABLE, "SECONDARY_GRADIENT_DISABLE"},
    {srShader::SECONDARY_GRADIENT_ENABLE, "SECONDARY_GRADIENT_ENABLE"},
};

constexpr NamedValue kTexturingNames[] = {
    {srShader::TEXTURING_DISABLE, "TEXTURING_DISABLE"},
    {srShader::TEXTURING_ENABLE, "TEXTURING_ENABLE"},
};

// FUNCTION: SURRENDER 0x10034DB0
std::ostream& operator<<(std::ostream& stream, const srShader& shader)
{
    dumpNamed(stream, shader.value & srShader::PASS_MASK, kPassNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DEPTH_WRITE_SHIFT) & 0x1, kDepthWriteNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::COLOR_WRITE_SHIFT) & 0x1, kColorWriteNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DETAILALPHA0_SHIFT) & 0x7, kDetailAlphaNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DETAILCOLOR0_SHIFT) & 0xf, kDetailColorNames);
    stream << '/';
    dumpNamed(stream, shader.value >> srShader::DETAILALPHA1_SHIFT, kDetailAlphaNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DETAILCOLOR1_SHIFT) & 0xf, kDetailColorNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::ALPHATEST_SHIFT) & 0x1, kAlphaTestNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DITHER_SHIFT) & 0x1, kDitherNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::SRCBLEND_SHIFT) & 0x3, kSrcBlendNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::DSTBLEND_SHIFT) & 0x7, kDstBlendNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::FOG_SHIFT) & 0x3, kFogNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::GRADIENT_SHIFT) & 0x3, kGradientNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::SECONDARY_GRADIENT_SHIFT) & 0x1, kSecondaryGradientNames);
    stream << '/';
    dumpNamed(stream, (shader.value >> srShader::TEXTURING_SHIFT) & 0x1, kTexturingNames);
    return stream;
}
