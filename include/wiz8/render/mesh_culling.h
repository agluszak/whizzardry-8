#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "wiz8/math.h"

namespace meshCulling {
struct Output {
    std::vector<std::uint32_t> indices;
    std::vector<std::uint32_t> avt;
    std::vector<std::uint32_t> vertex_remap;

    std::uint32_t triangleCount() const { return static_cast<std::uint32_t>(indices.size()); }
    std::uint32_t vertexCount() const { return static_cast<std::uint32_t>(avt.size()); }
};

struct Input {
    Input(const glm::mat4& model_view, const glm::mat4& inverse_model_view)
        : model_view(model_view), inverse_model_view(inverse_model_view)
    {
    }
    const glm::mat4& model_view;
    const glm::mat4& inverse_model_view;
    std::span<const glm::ivec3> triangles;
    std::span<const glm::vec3> vertices;
    std::span<const glm::vec4> polygon_planes;
    std::span<const glm::vec4> clip_planes;
    // Absent draws every triangle; engaged empty draws none.
    std::optional<std::span<const std::uint32_t>> active_triangles;
    int cull_mode = 0;
    std::uint32_t clip_mask = 0;
};

/* Apply a covector transform and normalize the plane normal. */
glm::vec4 transformClipPlane(const glm::vec4& plane, const glm::mat4& plane_matrix);
struct ClipMask {
    std::uint32_t mask;
    float depth;
};
/* Sphere-vs-plane-mask test over active clip planes, including the
       0..1 penetration fraction across side planes. */
ClipMask getClipMask(const glm::vec3& center, float radius, std::span<const glm::vec4> planes,
                     std::uint32_t mask);
// Owns the surviving triangle indices, ordered active vertices and inverse remap.
Output cull(const Input& input);
} // namespace meshCulling
