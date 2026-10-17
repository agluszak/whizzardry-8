/* D:\srsdk1x\sources\corelib\srMaterial.cpp */

#include "compat/compiler.h"
#include "glm/ext/vector_float4.hpp"
#include "surrender/srClass.h"
#include "surrender/srMaterial.h"
#include "surrender/srVertexPipe.h"
#include "surrender/srVertexProcessor.h"

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
        srClass::operator=(other);
        parms = other.parms;
        operations = other.operations;
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
    if ((operations & (1UL << OPER_DIFFUSE_SPECULAR)) != 0) {
        pipe.enableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        pipe.enableChannel(srVertexProcessor::CHANNEL_SPECULAR);
        return;
    }
    if ((operations & (1UL << OPER_DIFFUSE)) != 0) {
        pipe.enableChannel(srVertexProcessor::CHANNEL_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_DIFFUSE);
        pipe.enableChannel(srVertexProcessor::CHANNEL_LIGHT_AMBIENT);
        pipe.disableChannel(srVertexProcessor::CHANNEL_SPECULAR);
        return;
    }
    if ((operations & (1UL << OPER_SPECULAR)) != 0) {
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
    const auto operations = this->operations;
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
    operations = 0;
    mapper = 0;
}

// FUNCTION: SURRENDER 0x100346A0
glm::vec4 srMaterial::getEmissive() const
{
    return parms.emissive;
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
    operations &= ~(1 << (operation & 0x1f));
    dirty = 1;
}

// FUNCTION: SURRENDER 0x10034B00
void srMaterial::enable(e_oper operation)
{
    operations |= 1 << (operation & 0x1f);
    dirty = 1;
}
