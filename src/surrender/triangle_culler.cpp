#include <algorithm>
#include <cmath>
#include <array>
#include <numeric>
#include <cassert>
#include <cstddef>
#include <span>

#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/srTriangleCuller.h"
#include "surrender/srClip.h"
#include "surrender/srMath.h"
#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"

// FUNCTION: SURRENDER 0x10029740
glm::vec4 srTriangleCuller::transformClipPlane(const glm::vec4& plane,
                                               const glm::mat4& plane_matrix)
{
    const glm::vec4 transformed = plane_matrix * plane;
    const float length = glm::length(glm::vec3(transformed));
    return length == 0 ? transformed : transformed / length;
}

// FUNCTION: SURRENDER 0x10029BF0
int srTriangleCuller::setClipFlags(w8_ulong* clip_flags, float* distances,
                                   const srVector3* vertices, const glm::vec4& plane,
                                   w8_ulong shift, w8_ulong count, int first)
{
    w8_ulong collected = 0;
    for (w8_ulong i = 0; i < count; ++i) {
        distances[i] = glm::dot(vertices[i].glmValue(), glm::vec3(plane)) + plane.w;
        const w8_ulong flag = std::signbit(distances[i]) ? 1u << shift : 0;
        clip_flags[i] = first ? flag : clip_flags[i] | flag;
        collected |= flag;
    }
    return collected != 0;
}

// FUNCTION: SURRENDER 0x10029DE0
srTriangleCuller::ClipFlagsResult srTriangleCuller::setClipFlagsObjectSpace(
    w8_ulong* clip_flags, const srVector3* vertices, const glm::vec4* planes,
    const w8_ulong* plane_bits, w8_ulong plane_count, w8_ulong vertex_count)
{
    std::array<float, 256> distances;
    w8_ulong shared = 0xffffffff;
    bool clipped = false;
    for (w8_ulong base = 0; base < vertex_count; base += distances.size()) {
        const auto count = std::min(w8_ulong(distances.size()), vertex_count - base);
        for (w8_ulong plane = 0; plane < plane_count; ++plane)
            clipped |= setClipFlags(clip_flags + base, distances.data(), vertices + base,
                                     planes[plane], plane_bits[plane], count, plane == 0) != 0;
        for (w8_ulong i = 0; i < count; ++i)
            shared &= clip_flags[base + i];
    }
    return {shared == 0, clipped};
}

// FUNCTION: SURRENDER 0x10029FC0
srTriangleCuller::ClipMask srTriangleCuller::getClipMask(const glm::vec3& center, float radius,
                                                         const glm::vec4* planes, w8_ulong mask)
{
    w8_ulong clip = 0;
    float visible = 1;
    const glm::vec4 point(center, 1);
    for (unsigned i = 0; i < 32; ++i) {
        const w8_ulong bit = 1u << i;
        if (!(mask & bit))
            continue;
        const float distance = glm::dot(planes[i], point);
        if (distance > radius)
            continue;
        clip |= bit;
        if (i < 4) {
            const float fraction = radius > 0      ? (distance + radius) / (2 * radius)
                                   : distance >= 0 ? 1.0f
                                                   : 0.0f;
            visible *= std::clamp(fraction, 0.0f, 1.0f);
        }
    }
    return {clip, 1 - visible};
}

// FUNCTION: SURRENDER 0x1002A260
w8_ulong srTriangleCuller::cullNoClip(w8_ulong* indices, const glm::vec4* projected,
                                      const glm::vec4& constant, w8_ulong count)
{
    return srRender::collectOutside({indices, count}, constant, {projected, count});
}

// FUNCTION: SURRENDER 0x1002A290
w8_ulong srTriangleCuller::cullNoClipAPT(w8_ulong* indices, const w8_ulong* active,
                                              const glm::vec4* projected, const glm::vec4& constant,
                                              w8_ulong count)
{
    w8_ulong collected = 0;
    for (w8_ulong i = 0; i < count; ++i) {
        const auto index = active[i];
        if (glm::dot(projected[index], constant) < 0)
            indices[collected++] = index;
    }
    return collected;
}

// FUNCTION: SURRENDER 0x1002A3E0
w8_ulong srTriangleCuller::collectCF(w8_ulong* indices, const w8_ulong* clip_flags,
                                          const srVector3i* triangles, w8_ulong count)
{
    w8_ulong collected = 0;
    for (w8_ulong i = 0; i < count; ++i) {
        const auto index = indices[i];
        const auto& triangle = triangles[index];
        if (!(clip_flags[triangle.x] & clip_flags[triangle.y] & clip_flags[triangle.z]))
            indices[collected++] = index;
    }
    return collected;
}

// FUNCTION: SURRENDER 0x1002A600
w8_ulong srTriangleCuller::clip(w8_ulong* indices, const w8_ulong* clip_flags,
                                     const srVector3i* triangles, w8_ulong count)
{
    w8_ulong collected = 0;
    for (w8_ulong index = 0; index < count; ++index) {
        if ((clip_flags[triangles[index].x] & clip_flags[triangles[index].z] &
             clip_flags[triangles[index].y]) == 0) {
            indices[collected] = index;
            ++collected;
        }
    }
    return collected;
}

// FUNCTION: SURRENDER 0x1002A650
void srTriangleCuller::setupLinearArray(w8_ulong* indices, w8_ulong count)
{
    if (count)
        std::iota(indices, indices + count, w8_ulong{0});
}

// FUNCTION: SURRENDER 0x1002A6C0
w8_ulong srTriangleCuller::buildAVT(w8_ulong* avt, w8_ulong* vertex_scratch,
                                         const w8_ulong* indices, const srVector3i* triangles,
                                         w8_ulong triangle_count, w8_ulong vertex_count)
{
    std::fill_n(vertex_scratch, vertex_count, w8_ulong{0});
    for (w8_ulong i = 0; i < triangle_count; ++i) {
        const auto& triangle = triangles[indices[i]];
        vertex_scratch[triangle.x] = vertex_scratch[triangle.y] = vertex_scratch[triangle.z] = 1;
    }
    w8_ulong count = 0;
    for (w8_ulong i = 0; i < vertex_count; ++i)
        if (vertex_scratch[i])
            avt[count++] = i;
    for (w8_ulong i = 0; i < count; ++i)
        vertex_scratch[avt[i]] = i;
    return count;
}

// FUNCTION: SURRENDER 0x1002A720
int srTriangleCuller::cull(Output& output, const Input& input)
{
    output.linear = 1;
    output.triangle_count = output.vertex_count = 0;
    if (!input.triangle_count)
        return false;
    const auto& inverse_model_view = *input.inverse_model_view;
    const glm::vec4 facing = input.cull_mode == 0 ? -inverse_model_view[3] : inverse_model_view[3];
    auto* clip_flags = output.vertex_remap;
    if (input.active_triangles) {
        if (input.cull_mode == 2) {
            if (output.indices != input.active_triangles)
                std::copy_n(input.active_triangles, input.active_triangle_count, output.indices);
            output.triangle_count = input.active_triangle_count;
        } else {
            output.triangle_count =
                cullNoClipAPT(output.indices, input.active_triangles, input.projected_vertices,
                              facing, input.active_triangle_count);
        }
    } else {
        bool clipped = false;
        if (input.clip_mask) {
            const auto plane_matrix = glm::transpose(*input.model_view);
            std::array<glm::vec4, 32> planes;
            std::array<w8_ulong, 32> bits;
            w8_ulong count = 0;
            for (unsigned i = 0; i < 32; ++i)
                if (input.clip_mask & (1u << i)) {
                    planes[count] = transformClipPlane(input.clip_planes[i], plane_matrix);
                    bits[count++] = i;
                }
            const auto flags = setClipFlagsObjectSpace(
                clip_flags, input.vertices, planes.data(), bits.data(), count, input.vertex_count);
            if (!flags.visible)
                return false;
            clipped = flags.clipped;
        }
        if (input.cull_mode == 2) {
            if (clipped)
                output.triangle_count =
                    clip(output.indices, clip_flags, input.triangles, input.triangle_count);
            else {
                setupLinearArray(output.indices, input.triangle_count);
                output.triangle_count = input.triangle_count;
            }
        } else {
            output.triangle_count =
                cullNoClip(output.indices, input.projected_vertices, facing, input.triangle_count);
            if (clipped)
                output.triangle_count =
                    collectCF(output.indices, clip_flags, input.triangles, output.triangle_count);
        }
    }
    if (!output.triangle_count)
        return false;
    output.vertex_count = buildAVT(output.avt, clip_flags, output.indices, input.triangles,
                                   output.triangle_count, input.vertex_count);
    return true;
}

// FUNCTION: SURRENDER 0x10068DE0
SRDWORD srRender::collectOutside(std::span<SRDWORD> destination, const glm::vec4& constant,
                                 std::span<const glm::vec4> vectors)
{
    assert(vectors.size() >= destination.size());
    const std::size_t count = destination.size();
    SRDWORD collected = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (glm::dot(vectors[i], constant) < 0.0f)
            destination[collected++] = static_cast<SRDWORD>(i);
    }
    return collected;
}
