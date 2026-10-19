#include "wiz8/render/vertex_shading.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include "compat/compiler.h"
#include "glm/common.hpp"
#include "glm/ext/vector_double2.hpp"
#include "glm/ext/vector_float2.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/lighting_math.h"
#include "wiz8/render/material.h"
#include "wiz8/render/packed_color.h"
#include "wiz8/render/vertex_channels.h"

std::uint32_t shaderDisabledChannels(const DrawShader& shader)
{
    w8_ulong disable = 0;
    if ((shader.value & DrawShader::MASK_FOG) == 0) {
        disable = 1 << VertexChannels::CHANNEL_FOG;
    }
    if ((shader.value & DrawShader::MASK_GRADIENT) == 0) {
        disable |= (1 << VertexChannels::CHANNEL_DIFFUSE) | (1 << VertexChannels::CHANNEL_ALPHA);
    } else if ((shader.value & DrawShader::MASK_ALPHATEST) == 0 &&
               ((shader.value >> DrawShader::DSTBLEND_SHIFT) & 7) !=
                   DrawShader::DSTBLEND_SRC_ALPHA &&
               ((shader.value >> DrawShader::DSTBLEND_SHIFT) & 7) !=
                   DrawShader::DSTBLEND_ONE_MINUS_SRC_ALPHA &&
               ((shader.value >> DrawShader::SRCBLEND_SHIFT) & 3) !=
                   DrawShader::SRCBLEND_SRC_ALPHA &&
               ((shader.value >> DrawShader::SRCBLEND_SHIFT) & 3) !=
                   DrawShader::SRCBLEND_ONE_MINUS_SRC_ALPHA) {
        disable |= 1 << VertexChannels::CHANNEL_ALPHA;
    }
    if ((shader.value & DrawShader::MASK_SECONDARY_GRADIENT) == 0) {
        disable |= 1 << VertexChannels::CHANNEL_SPECULAR;
    }
    if ((shader.value & DrawShader::MASK_TEXTURING) == 0) {
        return disable | (1 << VertexChannels::CHANNEL_ST0) | (1 << VertexChannels::CHANNEL_ST1) |
               (1 << VertexChannels::CHANNEL_Q0) | (1 << VertexChannels::CHANNEL_Q1);
    }
    if ((shader.value & (DrawShader::MASK_DETAILCOLOR0 | DrawShader::MASK_DETAILALPHA0 |
                         DrawShader::MASK_DETAILCOLOR1 | DrawShader::MASK_DETAILALPHA1)) == 0) {
        disable |= (1 << VertexChannels::CHANNEL_ST1) | (1 << VertexChannels::CHANNEL_Q1);
    }
    return disable;
}

std::uint32_t shaderDisabledChannels(const DrawShader* shaders, const w8_ulong* indices,
                                     w8_ulong count)
{
    std::uint32_t available = 0;
    for (w8_ulong index = 0; index < count; ++index)
        available |= ~shaderDisabledChannels(shaders[indices[index]]);
    return ~available;
}

glm::vec4 DrawMaterial::ColorSource::colorAt(w8_ulong index) const
{
    if (!colors) return glm::vec4(0);
    if (format == FORMAT_ARGB) {
        const auto color = static_cast<const PackedColor*>(colors)[index];
        return glm::vec4(color.red, color.green, color.blue, color.alpha) * (1.f / 255.f);
    }
    if (format == FORMAT_VECTOR3) return glm::vec4(static_cast<const glm::vec3*>(colors)[index], 1);
    return static_cast<const glm::vec4*>(colors)[index];
}

namespace {
constexpr auto bit(VertexChannels::e_channel channel)
{
    return 1u << channel;
}
float ramp(float distance, float origin, float normalization, float amplitude, bool decreasing)
{
    const float value = decreasing ? origin - distance : distance - origin;
    return glm::clamp(value * normalization, 0.f, 1.f) * amplitude;
}
float depthCue(const VertexShadingInput& input, float distance)
{
    const float near_value = 1 - input.environment_scale;
    const float far_value = 1 - input.environment_inverse_scale;
    if (input.environment_minimum == input.environment_maximum)
        return distance < input.environment_minimum ? near_value : far_value;
    return ramp(distance, input.environment_maximum,
                1 / (input.environment_maximum - input.environment_minimum), near_value - far_value,
                true) +
           (1 - near_value);
}
bool lightAffects(const Light& light, const VertexShadingInput& input)
{
    if (light.group_mask & input.exclusion_mask) return false;
    const auto offset = input.eye_bounds.center - light.eye_location;
    const float distance_squared = glm::dot(offset, offset);
    const float radius = input.eye_bounds.radius;
    if ((light.derived_flags & Light::DERIVED_RANGE_CULL) &&
        (light.attenuation_range + radius) * (light.attenuation_range + radius) <= distance_squared)
        return false;
    if ((light.derived_flags & Light::DERIVED_SPOT) && radius * radius < distance_squared &&
        glm::dot(offset, light.spot_direction_eye) / std::sqrt(distance_squared) +
                radius / std::sqrt(radius * radius + distance_squared) <
            light.spot_cutoff)
        return false;
    return true;
}
bool fogAffects(const FogParams& fog, const VertexShadingInput& input)
{
    if ((fog.group_mask & input.exclusion_mask) || fog.density <= 0) return false;
    const auto& sphere = input.eye_bounds;
    return !(sphere.radius <= fog.fog_start &&
             glm::dot(sphere.center, sphere.center) <
                 (fog.fog_start - sphere.radius) * (fog.fog_start - sphere.radius));
}
float attenuationAt(const Light& light, const lightingMath::DirectionDistance& sample)
{
    float attenuation = 1;
    if (light.attenuation_model == Light::ATTENUATION_3DSTUDIO_MAX) {
        if (light.enable_flags & (1u << Light::ENABLE_RANGE_NEAR))
            attenuation =
                ramp(sample.distance, light.scaled_near_start, light.near_attenuation, 1, false);
        if (light.enable_flags & (1u << Light::ENABLE_RANGE_FAR))
            attenuation *=
                ramp(sample.distance, light.scaled_far_end, light.far_attenuation, 1, true);
    } else if (light.attenuation_model == Light::ATTENUATION_OPENGL &&
               (light.derived_flags & Light::DERIVED_OPENGL_ATTENUATION)) {
        const auto coefficients = light.opengl_attenuation;
        const float denominator =
            light.derived_flags & Light::DERIVED_CONSTANT_ATTENUATION
                ? coefficients.x
                : (coefficients.z * sample.distance + coefficients.y) * sample.distance +
                      coefficients.x;
        attenuation = glm::clamp(1.f / denominator, 0.f, 1.f);
    }
    if (light.derived_flags & Light::DERIVED_SPOT) {
        float spot = std::max(
            0.f, (glm::dot(sample.direction, -light.spot_direction_eye) - light.spot_cutoff) /
                     (1 - light.spot_cutoff));
        if (light.spot_exponent != 1) spot = std::pow(spot, std::min(light.spot_exponent, 127.f));
        attenuation *= spot;
    }
    return attenuation;
}
}

void shadeVertices(const VertexShadingInput& input)
{
    using namespace VertexChannels;
    if (input.active_vertices.empty()) return;
    if (input.vertex_arrays.empty() || input.vertex_arrays.size() < input.records.size())
        throw std::invalid_argument("Shading requires an output array for every pass");
    // Validate the whole submission before writing output, including direct-index draws.
    for (w8_ulong vertex = 0; vertex < input.active_vertices.size(); ++vertex) {
        const auto index = input.geometryIndex(vertex);
        if (index >= input.positions.size() ||
            (!input.normals.empty() && index >= input.normals.size()))
            throw std::out_of_range("Shading geometry index exceeds its source channel");
    }
    std::vector<const Light*> lights;
    for (auto* light : input.lights)
        if (lightAffects(*light, input)) lights.push_back(light);
    std::vector<const FogParams*> fogs;
    for (auto* fog : input.fogs)
        if (fogAffects(*fog, input)) fogs.push_back(fog);
    Material default_material;
    for (std::size_t pass = 0; pass < input.records.size(); ++pass) {
        const auto& draw = input.records[pass];
        const auto& output = input.vertex_arrays[pass];
        const unsigned final_channels = ~draw.channels;
        for (w8_ulong vertex = 0; vertex < input.active_vertices.size(); ++vertex) {
            const auto source = input.geometryIndex(vertex);
            const auto attribute = input.active_vertices[vertex];
            const auto eye = input.model_view * glm::vec4(input.positions[source], 1);
            const auto normal =
                input.normals.empty()
                    ? glm::vec3(0, 0, -1)
                    : glm::vec3(input.normal_matrix * glm::vec4(input.normals[source], 1));
            const float eye_distance = glm::length(glm::vec3(eye));
            Material* material = draw.flags & DrawMaterial::HAS_VERTEX_MATERIALS
                                     ? (*draw.materials)[attribute].get()
                                     : draw.material;
            if (!material) material = &default_material;
            auto info = material->getMaterialInfo();
            if (draw.diffuse_scale) info.diffuse.w *= *draw.diffuse_scale;
            if (draw.emissive_override)
                info.emissive = glm::vec4(glm::vec3(*draw.emissive_override), 1);
            unsigned channels = final_channels & ~info.disabled_channels;
            if (!(channels & bit(CHANNEL_DIFFUSE)))
                channels &= ~(bit(CHANNEL_LIGHT_DIFFUSE) | bit(CHANNEL_LIGHT_AMBIENT));
            if (!(channels & (bit(CHANNEL_LIGHT_DIFFUSE) | bit(CHANNEL_LIGHT_AMBIENT))))
                channels &= ~bit(CHANNEL_DIFFUSE);
            const auto operations = material->operations;
            if (operations &
                ((1u << Material::OPER_DIFFUSE_SPECULAR) | (1u << Material::OPER_DIFFUSE))) {
                channels |=
                    bit(CHANNEL_DIFFUSE) | bit(CHANNEL_LIGHT_DIFFUSE) | bit(CHANNEL_LIGHT_AMBIENT);
                if (operations & (1u << Material::OPER_DIFFUSE_SPECULAR))
                    channels |= bit(CHANNEL_SPECULAR);
                else
                    channels &= ~bit(CHANNEL_SPECULAR);
            } else if (operations & (1u << Material::OPER_SPECULAR)) {
                channels &= ~(bit(CHANNEL_DIFFUSE) | bit(CHANNEL_LIGHT_DIFFUSE) |
                              bit(CHANNEL_LIGHT_AMBIENT));
                channels |= bit(CHANNEL_SPECULAR);
            }
            glm::vec4 diffuse(0), specular(0);
            float alpha = 1, fog = 0;
            bool diffuse_ready = false, specular_ready = false, alpha_ready = false,
                 fog_ready = false;
            const auto ensureDiffuse = [&]() -> glm::vec4& {
                if (!diffuse_ready) {
                    diffuse = input.ambient_light * info.ambient + info.emissive;
                    if (draw.flags & DrawMaterial::HAS_COLORS)
                        diffuse += draw.color_source.colorAt(attribute);
                    diffuse_ready = true;
                }
                return diffuse;
            };
            const auto ensureSpecular = [&]() -> glm::vec4& {
                specular_ready = true;
                return specular;
            };
            for (const auto* light : lights) {
                const unsigned enabled = channels & light->channel_mask;
                if (!enabled) continue;
                const bool directional = light->derived_flags & Light::DERIVED_DIRECTIONAL;
                const auto sample =
                    directional
                        ? lightingMath::DirectionDistance{light->eye_location, 0}
                        : lightingMath::directionAndDistance(light->eye_location - glm::vec3(eye));
                const float attenuation = directional ? 1 : attenuationAt(*light, sample);
                if (attenuation == 0) continue;
                if (enabled & bit(CHANNEL_LIGHT_AMBIENT))
                    ensureDiffuse() += (light->scaled_ambient * info.ambient) * attenuation;
                if (!(enabled & (bit(CHANNEL_LIGHT_DIFFUSE) | bit(CHANNEL_SPECULAR)))) continue;
                const float dot = directional
                                      ? glm::clamp(glm::dot(normal, sample.direction), 0.f, 1.f)
                                      : std::max(0.f, glm::dot(normal, sample.direction));
                if (dot == 0) continue;
                if (enabled & bit(CHANNEL_LIGHT_DIFFUSE))
                    ensureDiffuse() += ((light->scaled_diffuse * info.diffuse) * attenuation) * dot;
                if (enabled & bit(CHANNEL_SPECULAR)) {
                    const auto eye_direction =
                        lightingMath::directionAndDistance(
                            glm::vec3(eye), lightingMath::ZeroLengthDirection::divide)
                            .direction;
                    const auto halfway =
                        lightingMath::directionAndDistance(sample.direction - eye_direction)
                            .direction;
                    float highlight = std::max(0.f, glm::dot(normal, halfway));
                    if (info.shininess > 1)
                        highlight = std::pow(highlight, std::min(info.shininess, 127.f));
                    highlight *= dot;
                    ensureSpecular() +=
                        ((light->scaled_specular * info.specular) * attenuation) * highlight;
                }
            }
            if (channels & bit(CHANNEL_FOG)) {
                for (const auto* layer : fogs) {
                    const double limit = input.eye_bounds.radius + layer->fog_end;
                    const float density =
                        glm::dot(input.eye_bounds.center, input.eye_bounds.center) <= limit * limit
                            ? ramp(eye_distance, layer->fog_start,
                                   layer->fog_end == layer->fog_start
                                       ? 1e8f
                                       : 1.f / (layer->fog_end - layer->fog_start),
                                   layer->density, false)
                            : layer->density;
                    fog = fog * (1 - density) + density;
                    fog_ready = true;
                }
            }
            bool depth_cue = operations & (1u << Material::OPER_DEPTH_CUE);
            const float cue = depthCue(input, eye_distance);
            if (operations & (1u << Material::OPER_DIFFUSE_SPECULAR)) {
                ensureDiffuse();
                ensureSpecular();
                std::swap(diffuse, specular);
            } else if (operations & (1u << Material::OPER_DIFFUSE)) {
                if (depth_cue) {
                    ensureDiffuse() *= cue;
                    depth_cue = false;
                }
                ensureSpecular() = ensureDiffuse();
                channels |= bit(CHANNEL_SPECULAR);
            } else if (operations & (1u << Material::OPER_SPECULAR)) {
                if (depth_cue) {
                    ensureSpecular() *= cue;
                    depth_cue = false;
                }
                ensureDiffuse() = ensureSpecular();
                channels |= bit(CHANNEL_DIFFUSE);
            }
            if (depth_cue) {
                if (channels & bit(CHANNEL_DIFFUSE)) ensureDiffuse() *= cue;
                if (channels & bit(CHANNEL_SPECULAR)) ensureSpecular() *= cue;
            }
            if ((operations & (1u << Material::OPER_ALPHA)) && (channels & bit(CHANNEL_ALPHA))) {
                alpha *= cue;
                alpha_ready = true;
            }
            diffuse_ready &= (final_channels & bit(CHANNEL_DIFFUSE)) != 0;
            specular_ready &= (final_channels & bit(CHANNEL_SPECULAR)) != 0;
            alpha_ready &= (final_channels & bit(CHANNEL_ALPHA)) != 0;
            fog_ready &= (final_channels & bit(CHANNEL_FOG)) != 0;
            if (final_channels & (bit(CHANNEL_DIFFUSE) | bit(CHANNEL_ALPHA))) {
                if (!diffuse_ready && !alpha_ready &&
                    !(draw.flags & (DrawMaterial::HAS_COLORS | DrawMaterial::HAS_ALPHA))) {
                    diffuse = input.ambient_light * info.ambient + info.emissive;
                    diffuse.w = info.diffuse.w;
                    diffuse = glm::clamp(diffuse, 0.f, 1.f);
                } else {
                    if (final_channels & bit(CHANNEL_DIFFUSE)) ensureDiffuse();
                    if (final_channels & bit(CHANNEL_ALPHA)) {
                        if (draw.flags & DrawMaterial::HAS_ALPHA) {
                            alpha = alpha_ready ? alpha * draw.alpha_source[attribute]
                                                : draw.alpha_source[attribute];
                            alpha_ready = true;
                        }
                        diffuse.w = !alpha_ready ? glm::clamp(info.diffuse.w, 0.f, 1.f)
                                    : final_channels & bit(CHANNEL_DIFFUSE)
                                        ? info.diffuse.w * alpha
                                        : info.diffuse.w * glm::clamp(alpha, 0.f, 1.f);
                    }
                    if (final_channels & bit(CHANNEL_DIFFUSE))
                        diffuse = glm::clamp(diffuse, 0.f, 1.f);
                }
                if (draw.flags & DrawMaterial::HAS_DIFFUSE_MULTIPLIERS)
                    diffuse *= draw.spec_for_diffuse[attribute];
                if (output.diffuse) output.diffuse[vertex] = diffuse;
            }
            if (final_channels & (bit(CHANNEL_SPECULAR) | bit(CHANNEL_FOG))) {
                if (!specular_ready && !fog_ready)
                    specular = glm::vec4(0);
                else if (!(final_channels & bit(CHANNEL_SPECULAR)))
                    specular.w = fog_ready ? info.fog_scale * glm::clamp(fog, 0.f, 1.f) : 0;
                else {
                    if (!specular_ready) specular = glm::vec4(0);
                    if (final_channels & bit(CHANNEL_FOG)) specular.w = info.fog_scale * fog;
                    specular = glm::clamp(specular, 0.f, 1.f);
                }
                if (draw.flags & DrawMaterial::HAS_SPECULAR_MULTIPLIERS)
                    specular *= draw.spec_for_specular[attribute];
                if (output.specular) output.specular[vertex] = specular;
            }
            for (unsigned stage = 0; stage < 2; ++stage) {
                if (!(final_channels & (bit(CHANNEL_ST0) << stage))) continue;
                glm::vec2 uv =
                    draw.flags & (DrawMaterial::HAS_TEXCOORD0 << stage) && draw.st_source[stage]
                        ? draw.st_source[stage][attribute]
                        : glm::vec2(0);
                if (stage == 0 && (channels & bit(CHANNEL_ST0))) {
                    if (material->uv_mapping == Material::UvMapping::EyeNormals)
                        uv = (glm::vec2(normal) + 1.f) * .5f;
                    else if (material->uv_mapping == Material::UvMapping::Scroll)
                        uv += glm::vec2(
                            glm::fract(glm::dvec2(material->uv_velocity) * input.frame_time));
                }
                auto* coordinates = stage == 0 ? output.st0 : output.st1;
                auto* q = stage == 0 ? output.q0 : output.q1;
                if (coordinates) coordinates[vertex] = uv;
                if (q) q[vertex] = 1;
            }
            if (output.eye_locations) output.eye_locations[vertex] = eye;
            unsigned attributes = 0;
            if (final_channels & (bit(CHANNEL_DIFFUSE) | bit(CHANNEL_ALPHA)))
                attributes |= ShadedVertexArrays::ATTRIBUTE_DIFFUSE;
            if (final_channels & bit(CHANNEL_SPECULAR))
                attributes |= ShadedVertexArrays::ATTRIBUTE_SPECULAR;
            if (final_channels & bit(CHANNEL_FOG))
                attributes |= ShadedVertexArrays::ATTRIBUTE_SPECULAR_ALPHA;
            if (final_channels & bit(CHANNEL_ST0)) attributes |= ShadedVertexArrays::ATTRIBUTE_ST0;
            if (final_channels & bit(CHANNEL_ST1)) attributes |= ShadedVertexArrays::ATTRIBUTE_ST1;
            if (output.attributes)
                output.attributes[vertex] = static_cast<unsigned char>(attributes);
        }
    }
}
