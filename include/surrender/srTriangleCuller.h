#pragma once

#include <optional>
#include <span>
#include <vector>

#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/ext/vector_int3.hpp"
#include "srMath.h"

class srTriangleCuller {
public:
    struct Output {
        std::vector<w8_ulong> indices;
        std::vector<w8_ulong> avt;
        std::vector<w8_ulong> vertex_remap;

        w8_ulong triangleCount() const
        {
            return static_cast<w8_ulong>(indices.size());
        }
        w8_ulong vertexCount() const
        {
            return static_cast<w8_ulong>(avt.size());
        }
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
        std::optional<std::span<const w8_ulong>> active_triangles;
        int cull_mode = 0;
        w8_ulong clip_mask = 0;
    };

    /* Apply a covector transform and normalize the plane normal. */
    static SR_DLL_IMPORT glm::vec4 transformClipPlane(const glm::vec4& plane,
                                                      const glm::mat4& plane_matrix);
    struct ClipMask {
        w8_ulong mask;
        float depth;
    };
    /* Sphere-vs-plane-mask test over active clip planes, including the
       0..1 penetration fraction across side planes. */
    static SR_DLL_IMPORT ClipMask getClipMask(const glm::vec3& center, float radius,
                                              std::span<const glm::vec4> planes, w8_ulong mask);
    // Owns the surviving triangle indices, ordered active vertices and inverse remap.
    static SR_DLL_IMPORT Output cull(const Input& input);
};
