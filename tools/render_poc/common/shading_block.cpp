#include "shading_block.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "wiz8/math.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/fog.h"
#include "wiz8/render/light.h"
#include "wiz8/render/material.h"
#include "wiz8/render/mesh.h"
#include "wiz8/render/mesh_instance.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/render/vertex_shading.h"

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

void packLight(const Light& light, std::span<glm::vec4> out)
{
    out[0] = glm::vec4(light.eye_location, lightFlags(light));
    out[1] = light.scaled_ambient;
    out[2] = light.scaled_diffuse;
    out[3] = light.scaled_specular;
    out[4] = glm::vec4(light.spot_direction_eye, light.spot_cutoff);
    // The range cull of a 3ds Max light uses attenuation_range, its scaled far end.
    out[5] = {light.scaled_near_start, light.near_attenuation, light.scaled_far_end,
              light.far_attenuation};
    out[6] = glm::vec4(light.opengl_attenuation, light.spot_exponent);
}

void packMaterial(Material& material, double frame_time, std::span<glm::vec4> out)
{
    const auto info = material.getMaterialInfo();
    // Scrolling is evaluated in double precision, then added in float.
    const auto scroll = glm::vec2(glm::fract(glm::dvec2(material.uv_velocity) * frame_time));
    out[0] = info.diffuse;
    out[1] = info.ambient;
    out[2] = info.specular;
    out[3] = info.emissive;
    out[4] = {info.shininess, info.fog_scale, float(material.operations),
              float(info.disabled_channels)};
    out[5] = {float(static_cast<int>(material.uv_mapping)), scroll.x, scroll.y, 0};
}

} // namespace

ViewBlock packView(const ShadingEnvironment& environment, std::span<Light* const> lights,
                   std::span<FogParams* const> fogs, const glm::vec4& fog_color)
{
    if (lights.size() > static_cast<std::size_t>(kMaxLights))
        throw std::length_error("Too many lights for one view");
    if (fogs.size() > static_cast<std::size_t>(kMaxFogs))
        throw std::length_error("Too many fog layers for one view");
    ViewBlock block;
    block.values[1] = {environment.environment_minimum, environment.environment_maximum,
                       environment.environment_scale, environment.environment_inverse_scale};
    block.values[2] = {float(lights.size()), float(fogs.size()), 0, 0};
    for (std::size_t slot = 0; slot < lights.size(); ++slot) {
        packLight(*lights[slot],
                  std::span(&block.values[kLightBase + slot * kLightVec4s], kLightVec4s));
        block.masks[slot].x = lights[slot]->group_mask;
    }
    for (std::size_t slot = 0; slot < fogs.size(); ++slot) {
        const auto& fog = *fogs[slot];
        // The normalization is computed in double precision, as on the CPU.
        const float normalization = fog.fog_end == fog.fog_start
                                        ? 1e8f
                                        : static_cast<float>(1.f / (fog.fog_end - fog.fog_start));
        block.values[kFogBase + slot] = {static_cast<float>(fog.fog_start), normalization,
                                         fog.density, static_cast<float>(fog.fog_end)};
        block.masks[kMaxLights + slot].x = fog.group_mask;
    }
    block.fog_color = fog_color;
    return block;
}

InstanceBlock packInstance(const MeshInstance& instance, const ResidentMesh& mesh,
                           const glm::vec4& scene_ambient)
{
    const auto frames =
        selectFrame(mesh.geometry, instance.frame_index, instance.frame_interpolation);
    return {.ambient = instanceAmbient(instance, scene_ambient),
            .frames = {float(frames.frame), float(frames.next), frames.interpolation, 0},
            .bounds = glm::vec4(mesh.bounds_center, mesh.bounds_radius),
            .overrides = {instance.diffuse_scale_enabled ? 1.f : 0.f, instance.diffuse_scale,
                          instance.emissive_override_enabled ? 1.f : 0.f,
                          instance.emissive_override},
            .masks = {instance.getExclusionMask(), 0, 0, 0}};
}

PassBlock packPass(const ResidentMesh& mesh, std::size_t pass, double frame_time)
{
    const auto& resident = mesh.passes[pass];
    if (resident.materials.size() > static_cast<std::size_t>(kMaxMaterials))
        throw std::length_error("Too many materials for one pass");
    PassBlock block;
    Material default_material;
    for (std::size_t slot = 0; slot < resident.materials.size(); ++slot) {
        auto* material = resident.materials[slot] ? resident.materials[slot] : &default_material;
        packMaterial(*material, frame_time,
                     std::span(&block.materials[slot * kMaterialVec4s], kMaterialVec4s));
    }
    block.mesh = {float(mesh.geometry.position_count), float(mesh.geometry.frame_count),
                  mesh.geometry.has_normals ? 1.f : 0.f, float(resident.draw_flags)};
    return block;
}

BatchBlock packBatch(std::uint32_t shader)
{
    DrawShader decoded;
    decoded.value = shader;
    const bool texturing = (shader & DrawShader::MASK_TEXTURING) != 0;
    return {.channels = {float(~shaderDisabledChannels(decoded) & 0xffffu), 0, 0, 0},
            .modes = {texturing ? 1.f : 0.f,
                      float((shader & DrawShader::MASK_GRADIENT) >> DrawShader::GRADIENT_SHIFT),
                      (shader & DrawShader::MASK_SECONDARY_GRADIENT) ? 1.f : 0.f,
                      float((shader & DrawShader::MASK_FOG) >> DrawShader::FOG_SHIFT)},
            .alpha_test = {(shader & DrawShader::MASK_ALPHATEST) ? 1.f : 0.f, 0, 0, 0}};
}

glm::vec4 instanceAmbient(const MeshInstance& instance, const glm::vec4& scene_ambient)
{
    // MeshInstance::RenderMeshes: baked models take their ambient from the
    // baked colours; others scale the scene ambient (alpha 1).
    if (instance.getModel()->vertex_lighting_ready) return glm::vec4(0);
    return glm::vec4(glm::vec3(scene_ambient) * instance.light_scale, 1);
}

std::uint32_t effectiveShader(const MeshInstance& instance, std::uint32_t shader)
{
    return instance.diffuse_scale_enabled ? fadingShader(shader) : shader;
}

std::uint32_t fadingShader(std::uint32_t shader)
{
    // MeshInstance::RenderMeshes: a diffuse-scaled instance alpha-blends.
    return (shader & 0xffffd7bfu) | 0x44a0u;
}

bool drawsSorted(const MeshInstance& instance)
{
    const auto* model = instance.getModel();
    return instance.diffuse_scale_enabled ||
           (model->render_control & (1u << Mesh::CONTROL_SORTED_RENDERING)) != 0;
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

void prepareLightAt(Light& light, const glm::mat4& model_view, float scene_brightness)
{
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
