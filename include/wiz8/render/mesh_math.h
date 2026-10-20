#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "glm/ext/vector_int3.hpp"
#include "glm/geometric.hpp"
#include "wiz8/render/plane_math.h"

namespace meshMath {
// UV tables may keep indices outside the geometry permutation. Other callers
// require every corner to have a corresponding entry in the map.
inline glm::ivec3 remapTriangle(glm::ivec3 triangle, std::span<const std::uint32_t> indices,
                                bool retain_external = false)
{
    for (int corner = 0; corner < 3; ++corner) {
        const auto index = static_cast<std::uint32_t>(triangle[corner]);
        if (index < indices.size())
            triangle[corner] = indices[index];
        else if (!retain_external)
            throw std::out_of_range("Mesh corner index out of range");
    }
    return triangle;
}

inline glm::vec3 polygonNormal(std::span<const glm::vec3> positions, const glm::ivec3& triangle)
{
    return planeMath::triangleNormal(positions[triangle.x], positions[triangle.y],
                                     positions[triangle.z]);
}

// Accumulate area-weighted polygon normals through optional shade groups. Keep
// normalization at the consumer: static normals and signed-byte frame normals
// have different storage but must sum the same corners in the same order.
template<glm::length_t Components>
std::vector<glm::vec3> vertexNormalSums(std::span<const glm::ivec3> triangles,
                                        std::span<const glm::vec<Components, float>> normals,
                                        std::size_t vertex_count,
                                        std::span<const std::uint32_t> shade_indices = {})
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
} // namespace meshMath
