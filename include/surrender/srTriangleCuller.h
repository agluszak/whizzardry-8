#pragma once

#include "srRendererDefs.h"

#include "srMath.h"

class srTriangleCuller {
public:
    struct Output {
        w8_ulong* indices;
        w8_ulong* avt;
        w8_ulong* vertex_remap;
        w8_ulong triangle_count;
        w8_ulong vertex_count;
        int linear;
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
    /* Fills distances with plane·vertex, then shifts each distance's IEEE
       sign bit into clip_flags at bit position shift. first assigns the
       flags; otherwise they are OR-merged. Returns whether any sign bit
       was set. */
    static SR_DLL_IMPORT int setClipFlags(w8_ulong* clip_flags, float* distances,
                                          const srVector3T<float>* vertices,
                                          const glm::vec4& plane, w8_ulong shift,
                                          w8_ulong count, int first);

    struct ClipMask {
        w8_ulong mask;
        float depth;
    };
    /* Sphere-vs-plane-mask test over active clip planes, including the
       0..1 penetration fraction across side planes. */
    static SR_DLL_IMPORT ClipMask getClipMask(const glm::vec3& center, float radius,
                                              const glm::vec4* planes, w8_ulong mask);
    /* Builds the active-vertex table: marks the vertices referenced by the
       surviving triangle indices, collects their indices into avt, then
       rewrites vertex_scratch as the vertex->avt inverse remap. Returns the
       active vertex count. */
    static SR_DLL_IMPORT w8_ulong
    buildAVT(w8_ulong* avt, w8_ulong* vertex_scratch, const w8_ulong* indices,
             const srVector3i* triangles, w8_ulong triangle_count, w8_ulong vertex_count);
    static SR_DLL_IMPORT void setupLinearArray(w8_ulong* indices, w8_ulong count);
    static SR_DLL_IMPORT int cull(Output& output, const Input& input);

private:
    /* Chunks vertices by 0x100, runs setClipFlags per plane/bit pair, and
       ANDs the per-vertex masks. shared_out ORs each plane's any-outside
       result. Returns 0 when every vertex shares an outside bit (fully
       clipped). */
    static int setClipFlagsObjectSpace(w8_ulong* clip_flags, const srVector3T<float>* vertices,
                                       const glm::vec4* planes,
                                       const w8_ulong* plane_bits, w8_ulong plane_count,
                                       w8_ulong vertex_count, w8_ulong& shared_out);
    static w8_ulong cullNoClip(w8_ulong* indices, const glm::vec4* projected,
                               const glm::vec4& constant, w8_ulong count);
    static w8_ulong cullNoClipAPT(w8_ulong* indices, const w8_ulong* active,
                                       const glm::vec4* projected, const glm::vec4& constant, w8_ulong count);
    /* Filters the index list in place, keeping triangles whose three vertex
       clip masks share no set bit. */
    static w8_ulong collectCF(w8_ulong* indices, const w8_ulong* clip_flags,
                                   const srVector3i* triangles, w8_ulong count);
    /* Same keep-test as collectCF but walks the triangle table directly. */
    static w8_ulong clip(w8_ulong* indices, const w8_ulong* clip_flags,
                              const srVector3i* triangles, w8_ulong count);
};

