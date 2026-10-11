#pragma once

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>
#include "glm/ext/vector_float3.hpp"
#include "glm/geometric.hpp"
#include "srMath.h"
#include "compat/compiler.h"

namespace srMeshMath {
inline glm::vec3 polygonNormal(std::span<const glm::vec3> positions, const srVector3i& triangle)
{
    const auto origin = positions[triangle.x];
    return glm::cross(positions[triangle.y] - origin, positions[triangle.z] - origin);
}

// Accumulate area-weighted polygon normals through optional shade groups. Keep
// normalization at the consumer: static normals and signed-byte frame normals
// have different storage but must sum the same corners in the same order.
template<glm::length_t Components>
std::vector<glm::vec3> vertexNormalSums(std::span<const srVector3i> triangles,
                                      std::span<const glm::vec<Components, float>> normals,
                                      std::size_t vertex_count,
                                      std::span<const w8_ulong> shade_indices = {})
{
    std::vector<glm::vec3> sums(vertex_count, glm::vec3(0));
    for (std::size_t polygon = 0; polygon < triangles.size(); ++polygon) {
        const auto& triangle = triangles[polygon];
        const glm::vec3 normal(normals[polygon]);
        for (const auto vertex : {triangle.x, triangle.y, triangle.z})
            sums[shade_indices.empty() ? vertex : shade_indices[vertex]] += normal;
    }
    if (shade_indices.empty()) return sums;
    std::vector<glm::vec3> result(vertex_count);
    for (std::size_t vertex = 0; vertex < vertex_count; ++vertex)
        result[vertex] = sums[shade_indices[vertex]];
    return result;
}
} // namespace srMeshMath
