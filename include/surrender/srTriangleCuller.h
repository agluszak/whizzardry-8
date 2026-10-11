#pragma once

#include "srMath.h"
#include <vector>

class srTriangleCuller {
public:
    struct Output {
        std::vector<w8_ulong> indices;
        std::vector<w8_ulong> avt;
        std::vector<w8_ulong> vertex_remap;

        w8_ulong triangleCount() const { return static_cast<w8_ulong>(indices.size()); }
        w8_ulong vertexCount() const { return static_cast<w8_ulong>(avt.size()); }
    };

    struct Input {
        w8_ulong triangle_count;
        w8_ulong vertex_count;
        w8_ulong active_triangle_count;
        int cull_mode;
        const w8_ulong* active_triangles;
        const glm::vec4* projected_vertices;
        const srVector3i* triangles;
        const srVector3T<float>* vertices;
        const glm::vec4* clip_planes;
        const glm::mat4* model_view;
        const glm::mat4* inverse_model_view;
        w8_ulong clip_mask;
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
                                              const glm::vec4* planes, w8_ulong mask);
    // Owns the surviving triangle indices, ordered active vertices and inverse remap.
    static SR_DLL_IMPORT Output cull(const Input& input);
};
