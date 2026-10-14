#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <span>
#include <stdexcept>
#include <vector>

#include "compat/compiler.h"
#include "glm/ext/matrix_float4x4.hpp"
#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_float4.hpp"
#include "glm/geometric.hpp"
#include "glm/matrix.hpp"
#include "surrender/plane_math.h"
#include "surrender/srClip.h"
#include "surrender/srMath.h"
#include "surrender/srTriangleCuller.h"
#include "surrender/triangle_selection.h"

// FUNCTION: SURRENDER 0x10029740
glm::vec4 srTriangleCuller::transformClipPlane(const glm::vec4& plane,
                                               const glm::mat4& plane_matrix)
{
    return srPlaneMath::normalized(plane_matrix * plane);
}

static void validateClipPlanes(std::span<const glm::vec4> planes, w8_ulong mask)
{
    if (planes.size() < 32 && (mask >> planes.size()) != 0)
        throw std::invalid_argument("Incomplete clip-plane span");
}

struct ClipFlagsResult {
    bool visible;
    bool clipped;
};

static ClipFlagsResult SetClipFlags(std::span<w8_ulong> flags, const srTriangleCuller::Input& input)
{
    validateClipPlanes(input.clip_planes, input.clip_mask);
    const auto plane_matrix = glm::transpose(input.model_view);
    bool first = true;
    bool clipped = false;
    for (unsigned plane = 0; plane < 32; ++plane) {
        if (!(input.clip_mask & (1u << plane))) continue;
        const auto equation =
            srTriangleCuller::transformClipPlane(input.clip_planes[plane], plane_matrix);
        for (std::size_t vertex = 0; vertex < flags.size(); ++vertex) {
            const float distance =
                glm::dot(input.vertices[vertex], glm::vec3(equation)) + equation.w;
            const w8_ulong flag = std::signbit(distance) ? 1u << plane : 0;
            flags[vertex] = first ? flag : flags[vertex] | flag;
            clipped |= flag != 0;
        }
        first = false;
    }
    w8_ulong shared = 0xffffffff;
    for (auto flag : flags) shared &= flag;
    return {shared == 0, clipped};
}

// FUNCTION: SURRENDER 0x10029FC0
srTriangleCuller::ClipMask srTriangleCuller::getClipMask(const glm::vec3& center, float radius,
                                                         std::span<const glm::vec4> planes,
                                                         w8_ulong mask)
{
    validateClipPlanes(planes, mask);
    w8_ulong clip = 0;
    float visible = 1;
    const glm::vec4 point(center, 1);
    for (unsigned i = 0; i < 32; ++i) {
        const w8_ulong bit = 1u << i;
        if (!(mask & bit)) continue;
        const float distance = glm::dot(planes[i], point);
        if (distance > radius) continue;
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

// FUNCTION: SURRENDER 0x1002A720
srTriangleCuller::Output srTriangleCuller::cull(const Input& input)
{
    if (input.triangles.empty() || (input.active_triangles && input.active_triangles->empty()))
        return {};
    if (input.cull_mode != 2 && input.polygon_planes.size() < input.triangles.size())
        throw std::invalid_argument("Incomplete polygon-plane span");
    Output output;
    output.indices.resize(input.active_triangles ? input.active_triangles->size()
                                                 : input.triangles.size());
    output.vertex_remap.resize(input.vertices.size());
    const auto& inverse_model_view = input.inverse_model_view;
    const glm::vec4 facing = input.cull_mode == 0 ? -inverse_model_view[3] : inverse_model_view[3];
    bool clipped = false;
    // An explicit active list has already undergone clipping.
    if (!input.active_triangles && input.clip_mask) {
        const auto flags = SetClipFlags(output.vertex_remap, input);
        if (!flags.visible) return {};
        clipped = flags.clipped;
    }
    const auto keep = [&](w8_ulong index) {
        if (index >= input.triangles.size())
            throw std::invalid_argument("Active triangle index outside mesh");
        const auto& triangle = input.triangles[index];
        for (const auto vertex : {triangle.x, triangle.y, triangle.z})
            if (vertex < 0 || static_cast<std::size_t>(vertex) >= input.vertices.size())
                throw std::invalid_argument("Triangle vertex index outside mesh");
        return (input.cull_mode == 2 || glm::dot(input.polygon_planes[index], facing) < 0) &&
               (!clipped ||
                srRender::triangleVisible(input.triangles[index], output.vertex_remap.data()));
    };
    const auto count =
        input.active_triangles
            ? srRender::selectTriangles(output.indices, *input.active_triangles, keep)
            : srRender::selectTriangles(
                  output.indices,
                  std::views::iota(w8_ulong{0}, static_cast<w8_ulong>(input.triangles.size())),
                  keep);
    output.indices.resize(count);
    if (!count) return {};

    std::fill(output.vertex_remap.begin(), output.vertex_remap.end(), 0);
    for (auto index : output.indices) {
        const auto& triangle = input.triangles[index];
        output.vertex_remap[triangle.x] = output.vertex_remap[triangle.y] =
            output.vertex_remap[triangle.z] = 1;
    }
    output.avt.reserve(input.vertices.size());
    for (w8_ulong vertex = 0; vertex < input.vertices.size(); ++vertex)
        if (output.vertex_remap[vertex]) output.avt.push_back(vertex);
    for (w8_ulong vertex = 0; vertex < output.avt.size(); ++vertex)
        output.vertex_remap[output.avt[vertex]] = vertex;
    return output;
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
