#include "shading_block.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/vertex_channels.h"

namespace render_poc {
namespace {

// #embed is standard C++26; Clang 23 still labels it an extension.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
constexpr char vertex_source[] = {
#embed "shaders/wizardry_vertex.glsl"
    , 0};
constexpr char fragment_source[] = {
#embed "shaders/wizardry_fragment.glsl"
    , 0};
#pragma clang diagnostic pop

// The same per-object rejections shadeVertices applies before its vertex loop.
bool lightAffects(const Light& light, const InstanceShading& instance)
{
    if (light.group_mask & instance.exclusion_mask) return false;
    const auto offset = instance.eye_bounds.center - light.eye_location;
    const float distance_squared = glm::dot(offset, offset);
    const float radius = instance.eye_bounds.radius;
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

bool fogAffects(const FogParams& fog, const InstanceShading& instance)
{
    if ((fog.group_mask & instance.exclusion_mask) || fog.density <= 0) return false;
    const auto& sphere = instance.eye_bounds;
    return !(sphere.radius <= fog.fog_start &&
             glm::dot(sphere.center, sphere.center) <
                 (fog.fog_start - sphere.radius) * (fog.fog_start - sphere.radius));
}

float lightFlags(const Light& light)
{
    using enum VertexChannels::e_channel;
    std::uint32_t flags = light.derived_flags & 0x3fu;
    if (light.enable_flags & (1u << Light::ENABLE_RANGE_NEAR)) flags |= 1u << 6;
    if (light.enable_flags & (1u << Light::ENABLE_RANGE_FAR)) flags |= 1u << 7;
    flags |= (static_cast<std::uint32_t>(light.attenuation_model) & 3u) << 8;
    if (light.channel_mask & (1u << CHANNEL_LIGHT_AMBIENT)) flags |= 1u << 10;
    if (light.channel_mask & (1u << CHANNEL_LIGHT_DIFFUSE)) flags |= 1u << 11;
    if (light.channel_mask & (1u << CHANNEL_SPECULAR)) flags |= 1u << 12;
    return static_cast<float>(flags);
}

} // namespace

ShadingBlock packShading(const ShadingEnvironment& environment, const InstanceShading& instance,
                         std::span<Light* const> lights, std::span<FogParams* const> fogs,
                         std::span<Material* const> materials)
{
    ShadingBlock block{};
    block[0] = environment.ambient_light;
    block[1] = {environment.environment_minimum, environment.environment_maximum,
                environment.environment_scale, environment.environment_inverse_scale};

    int light_count = 0;
    for (auto* light : lights) {
        if (!lightAffects(*light, instance)) continue;
        if (light_count == kMaxLights) throw std::length_error("Too many lights for one draw");
        auto* out = &block[kLightBase + light_count++ * kLightVec4s];
        out[0] = glm::vec4(light->eye_location, lightFlags(*light));
        out[1] = light->scaled_ambient;
        out[2] = light->scaled_diffuse;
        out[3] = light->scaled_specular;
        out[4] = glm::vec4(light->spot_direction_eye, light->spot_cutoff);
        out[5] = {light->scaled_near_start, light->near_attenuation, light->scaled_far_end,
                  light->far_attenuation};
        out[6] = glm::vec4(light->opengl_attenuation, light->spot_exponent);
    }

    int fog_count = 0;
    for (auto* fog : fogs) {
        if (!fogAffects(*fog, instance)) continue;
        if (fog_count == kMaxFogs) throw std::length_error("Too many fog layers for one draw");
        // Range limits and normalization are double-precision on the CPU.
        const double limit = instance.eye_bounds.radius + fog->fog_end;
        const bool ramped =
            glm::dot(instance.eye_bounds.center, instance.eye_bounds.center) <= limit * limit;
        const float normalization = fog->fog_end == fog->fog_start
                                        ? 1e8f
                                        : static_cast<float>(1.f / (fog->fog_end - fog->fog_start));
        block[kFogBase + fog_count++] = {static_cast<float>(fog->fog_start), normalization,
                                         fog->density, ramped ? 1.f : 0.f};
    }
    block[2] = {float(light_count), float(fog_count), 0, 0};

    if (materials.size() > static_cast<std::size_t>(kMaxMaterials))
        throw std::length_error("Too many materials for one draw");
    Material default_material;
    for (std::size_t slot = 0; slot < materials.size(); ++slot) {
        auto* material = materials[slot] ? materials[slot] : &default_material;
        auto info = material->getMaterialInfo();
        if (instance.diffuse_scale) info.diffuse.w *= *instance.diffuse_scale;
        if (instance.emissive_override)
            info.emissive = glm::vec4(glm::vec3(*instance.emissive_override), 1);
        // Scrolling is evaluated in double precision, then added in float.
        const auto scroll =
            glm::vec2(glm::fract(glm::dvec2(material->uv_velocity) * environment.frame_time));
        auto* out = &block[kMaterialBase + slot * kMaterialVec4s];
        out[0] = info.diffuse;
        out[1] = info.ambient;
        out[2] = info.specular;
        out[3] = info.emissive;
        out[4] = {info.shininess, info.fog_scale, float(material->operations),
                  float(info.disabled_channels)};
        out[5] = {float(static_cast<int>(material->uv_mapping)), scroll.x, scroll.y, 0};
    }
    return block;
}

DrawBlock packDraw(const DrawState& state)
{
    const auto shader = state.shader;
    const bool texturing = (shader & DrawShader::MASK_TEXTURING) != 0;
    return {glm::vec4(float(state.final_channels & 0xffffu), float(state.draw_flags),
                      state.has_normals ? 1.f : 0.f, float(state.position_count)),
            glm::vec4(float(state.frame), float(state.next_frame), state.interpolation,
                      float(state.frame_count)),
            glm::vec4(texturing ? 1.f : 0.f,
                      float((shader & DrawShader::MASK_GRADIENT) >> DrawShader::GRADIENT_SHIFT),
                      (shader & DrawShader::MASK_SECONDARY_GRADIENT) ? 1.f : 0.f,
                      float((shader & DrawShader::MASK_FOG) >> DrawShader::FOG_SHIFT)),
            state.fog_color,
            glm::vec4((shader & DrawShader::MASK_ALPHATEST) ? 1.f : 0.f, 0, 0, 0),
            glm::vec4(0)};
}

RasterState rasterState(std::uint32_t shader)
{
    // The same decoding as GpuRenderer::gpuPipeline.
    RasterState state;
    state.depth_compare = shader & DrawShader::PASS_MASK;
    state.depth_write = (shader & DrawShader::MASK_DEPTH_WRITE) != 0;
    state.depth_test = state.depth_compare != DrawShader::PASS_ALWAYS || state.depth_write;
    state.color_write = (shader & DrawShader::MASK_COLOR_WRITE) != 0;
    state.source = shader >> DrawShader::SRCBLEND_SHIFT & 3;
    state.destination = shader >> DrawShader::DSTBLEND_SHIFT & 7;
    state.blend = !(state.source == DrawShader::SRCBLEND_ONE &&
                    state.destination == DrawShader::DSTBLEND_ZERO);
    return state;
}

std::string_view wizardryVertexGlsl()
{
    return vertex_source;
}

std::string_view wizardryFragmentGlsl()
{
    return fragment_source;
}

void prepareLight(Light& light, const glm::mat4& view, float scene_brightness)
{
    const glm::mat4 model_view = view * light.transform.matrix<glm::mat4>();
    light.derived_flags = Light::DERIVED_ACTIVE;
    light.channel_mask = 0;
    const struct Channel {
        const glm::vec3& color;
        glm::vec4& scaled;
        VertexChannels::e_channel channel;
    } channels[] = {{light.ambient, light.scaled_ambient, VertexChannels::CHANNEL_LIGHT_AMBIENT},
                    {light.diffuse, light.scaled_diffuse, VertexChannels::CHANNEL_LIGHT_DIFFUSE},
                    {light.specular, light.scaled_specular, VertexChannels::CHANNEL_SPECULAR}};
    for (const auto& channel : channels) {
        if (channel.color == glm::vec3(0)) continue;
        light.channel_mask |= 1u << channel.channel;
        channel.scaled = glm::vec4(channel.color * light.intensity * scene_brightness, 0);
    }
    if (light.channel_mask == 0) light.derived_flags &= ~Light::DERIVED_ACTIVE;
    if (light.enable_flags & (1u << Light::ENABLE_DIRECTIONAL)) {
        light.derived_flags |= Light::DERIVED_DIRECTIONAL;
        light.eye_location = normalizedOrZero(-glm::vec3(model_view[2]));
        return;
    }
    if ((light.enable_flags & (1u << Light::ENABLE_SPOT)) && light.spot_angle > 0 &&
        light.spot_exponent != 0) {
        light.derived_flags |= Light::DERIVED_SPOT;
        light.spot_cutoff = static_cast<float>(std::cos(light.spot_angle));
        light.spot_direction_eye = normalizedOrZero(
            glm::transpose(glm::mat3(glm::inverse(model_view))) * light.spot_direction);
    }
    if (light.attenuation_model == Light::ATTENUATION_OPENGL) {
        const auto& coefficients = light.opengl_attenuation;
        light.derived_flags |= Light::DERIVED_OPENGL_ATTENUATION;
        if (std::fabs(coefficients.y) < 5.9604645e-08f &&
            std::fabs(coefficients.z) < 5.9604645e-08f) {
            if (coefficients.x == 1 || std::fabs(coefficients.x) < 5.9604645e-08f)
                light.derived_flags &= ~Light::DERIVED_OPENGL_ATTENUATION;
            light.derived_flags |= Light::DERIVED_CONSTANT_ATTENUATION;
        }
    } else if (light.attenuation_model == Light::ATTENUATION_3DSTUDIO_MAX) {
        const glm::mat3 basis(model_view);
        const float scale =
            std::max({glm::length(basis[0]), glm::length(basis[1]), glm::length(basis[2])});
        light.scaled_near_start = scale * static_cast<float>(light.near_start);
        light.scaled_far_end = scale * static_cast<float>(light.far_end);
        light.near_attenuation =
            light.near_start == light.near_end
                ? 1.f
                : static_cast<float>(1.0 / (scale * (light.near_end - light.near_start)));
        light.far_attenuation =
            light.far_start == light.far_end
                ? 1.f
                : static_cast<float>(1.0 / (scale * (light.far_end - light.far_start)));
        if (light.enable_flags & (1u << Light::ENABLE_RANGE_FAR)) {
            light.attenuation_range = light.scaled_far_end;
            light.derived_flags |= Light::DERIVED_RANGE_CULL;
        }
    }
    light.eye_location = glm::vec3(model_view[3]);
}

} // namespace render_poc
