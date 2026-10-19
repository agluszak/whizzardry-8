#pragma once
#include <cstdint>
#include <optional>
#include <span>

#include "wiz8/render/draw_shader.h"
#include "wiz8/render/mesh_resources.h"
#include "wiz8/render/vertex_channels.h"
#include "wiz8/spatial/bounding_sphere.h"

class Material;
class Light;
class FogParams;

struct DrawMaterial {
    enum {
        HAS_COLORS = 0x01u,
        HAS_DIFFUSE_MULTIPLIERS = 0x02u,
        HAS_SPECULAR_MULTIPLIERS = 0x04u,
        HAS_ALPHA = 0x08u,
        HAS_TEXCOORD0 = 0x10u,
        HAS_TEXCOORD1 = 0x20u,
        HAS_VERTEX_MATERIALS = 0x40u
    };
    w8_ulong flags = 0;
    w8_ulong channels = 0;
    Material* material = nullptr;
    std::optional<float> diffuse_scale;
    std::optional<float> emissive_override;
    struct ColorSource {
        enum e_format { FORMAT_ARGB = 0, FORMAT_VECTOR3 = 1, FORMAT_VECTOR4 = 2 };
        const void* colors = nullptr;
        e_format format = FORMAT_VECTOR4;

        glm::vec4 colorAt(w8_ulong index) const;
    };
    ColorSource color_source{};
    const glm::vec4* spec_for_diffuse = nullptr;
    const glm::vec4* spec_for_specular = nullptr;
    const float* alpha_source = nullptr;
    const glm::vec2* st_source[2]{};
    const MeshResources<Material>* materials = nullptr;
};
struct VertexShadingInput {
    VertexShadingInput(const glm::mat4& model_view, const glm::mat4& normal_matrix)
        : model_view(model_view), normal_matrix(normal_matrix)
    {
    }

    std::span<const w8_ulong> active_vertices;
    bool direct_vertex_indices = false;
    std::span<const glm::vec3> positions;
    std::span<const glm::vec3> normals;
    BoundingSphere eye_bounds;
    const glm::mat4& model_view;
    const glm::mat4& normal_matrix;
    std::span<ShadedVertexArrays> vertex_arrays;
    w8_ulong exclusion_mask = 0;
    glm::vec4 ambient_light{0};
    std::span<const DrawMaterial> records;
    std::span<Light* const> lights;
    std::span<FogParams* const> fogs;
    double frame_time = 0;
    float environment_minimum = 0;
    float environment_maximum = 0;
    float environment_scale = 0;
    float environment_inverse_scale = 0;

    w8_ulong geometryIndex(w8_ulong processed_index) const
    {
        return direct_vertex_indices ? processed_index : active_vertices[processed_index];
    }
};

std::uint32_t shaderDisabledChannels(const DrawShader& shader);
std::uint32_t shaderDisabledChannels(const DrawShader* shaders, const w8_ulong* indices,
                                     w8_ulong count);
void shadeVertices(const VertexShadingInput& input);
