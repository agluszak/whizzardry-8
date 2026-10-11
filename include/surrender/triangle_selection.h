#pragma once

#include <span>
#include "srMath.h"
#include "compat/compiler.h"

namespace srRender {
// Supports both table order and an existing active list; compaction may be in place.
template<class Indices, class Keep>
w8_ulong selectTriangles(std::span<w8_ulong> destination, const Indices& indices, Keep keep)
{
    w8_ulong count = 0;
    for (w8_ulong index : indices)
        if (keep(index)) destination[count++] = index;
    return count;
}

// Clip flags may refer to original vertices or a compact active-vertex table.
template<class Flag>
bool triangleVisible(const srVector3i& triangle, const Flag* flags,
                     const w8_ulong* remap = nullptr)
{
    const auto vertex = [remap](int index) { return remap ? remap[index] : index; };
    const auto first = flags[vertex(triangle.x)];
    return first == 0 || (first & flags[vertex(triangle.z)] & flags[vertex(triangle.y)]) == 0;
}
} // namespace srRender
