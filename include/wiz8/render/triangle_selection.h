#pragma once

#include <cstdint>
#include <span>

#include "glm/ext/vector_int3.hpp"

namespace clipMath {
// Supports both table order and an existing active list; compaction may be in place.
template<class Indices, class Keep>
std::uint32_t selectTriangles(std::span<std::uint32_t> destination, const Indices& indices,
                              Keep keep)
{
    std::uint32_t count = 0;
    for (std::uint32_t index : indices)
        if (keep(index)) destination[count++] = index;
    return count;
}

// Clip flags may refer to original vertices or a compact active-vertex table.
template<class Flag>
bool triangleVisible(const glm::ivec3& triangle, const Flag* flags,
                     const std::uint32_t* remap = nullptr)
{
    const auto vertex = [remap](int index) { return remap ? remap[index] : index; };
    const auto first = flags[vertex(triangle.x)];
    return first == 0 || (first & flags[vertex(triangle.z)] & flags[vertex(triangle.y)]) == 0;
}
} // namespace clipMath
