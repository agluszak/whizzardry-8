/* D:\srsdk1x\sources\corelib\Material.cpp */

#include "wiz8/render/material.h"

#include "glm/ext/vector_float4.hpp"
#include "wiz8/render/vertex_channels.h"

void Material::updateParms()
{
    parms.disabled_channels = 0;
    if (parms.diffuse.x == 0.0f && parms.diffuse.y == 0.0f && parms.diffuse.z == 0.0f) {
        parms.disabled_channels = (1UL << VertexChannels::CHANNEL_LIGHT_DIFFUSE);
    }
    if (parms.ambient.x == 0.0f && parms.ambient.y == 0.0f && parms.ambient.z == 0.0f) {
        parms.disabled_channels |= (1UL << VertexChannels::CHANNEL_LIGHT_AMBIENT);
    }
    if (parms.specular.x == 0.0f && parms.specular.y == 0.0f && parms.specular.z == 0.0f) {
        parms.disabled_channels |= (1UL << VertexChannels::CHANNEL_SPECULAR);
    }
    if (parms.fog_scale == 0.0f) {
        parms.disabled_channels |= (1UL << VertexChannels::CHANNEL_FOG);
    }
    dirty = 0;
}

Material& Material::operator=(const Material& other)
{
    if (&other != this) {
        name = other.name;
        parms = other.parms;
        operations = other.operations;
        uv_mapping = other.uv_mapping;
        uv_velocity = other.uv_velocity;
        dirty = 1;
        return *this;
    }
    return *this;
}

VertexChannels::MaterialInfo Material::getMaterialInfo()
{
    if (dirty != 0) {
        updateParms();
    }
    return parms;
}

void Material::reset()
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
    uv_mapping = UvMapping::Mesh;
    uv_velocity = glm::vec2(0);
}

glm::vec4 Material::getEmissive() const
{
    return parms.emissive;
}

void Material::setVector(glm::vec4& destination, const glm::vec4& source)
{
    destination = source;
    dirty = 1;
}

void Material::disable(e_oper operation)
{
    operations &= ~(1 << (operation & 0x1f));
    dirty = 1;
}

void Material::enable(e_oper operation)
{
    operations |= 1 << (operation & 0x1f);
    dirty = 1;
}
