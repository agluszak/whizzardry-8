#pragma once

#include "compat/compiler.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <numeric>
#include <vector>

struct srTriangleRun {
    std::size_t first;
    std::size_t count;
    w8_ulong texture_set;
};

// An empty order uses contiguous input; otherwise runs follow its permutation.
template<class Visit>
inline void visitTriangleRuns(std::span<const w8_ulong> texture_sets,
                              std::span<const w8_ulong> order,
                              std::size_t maximum_run, Visit visit)
{
    assert(maximum_run > 0);
    const auto count = order.empty() ? texture_sets.size() : order.size();
    const auto textureAt = [&](std::size_t index) {
        return texture_sets[order.empty() ? index : order[index]];
    };
    for (std::size_t first = 0; first < count;) {
        const auto texture = textureAt(first);
        const auto limit = std::min(maximum_run, count - first);
        std::size_t length = 1;
        while (length < limit && textureAt(first + length) == texture) ++length;
        visit(srTriangleRun{first, length, texture});
        first += length;
    }
}

// Preserve equal-depth submission order and the renderer's sorted key table.
inline std::vector<w8_ulong> sortedTriangleOrder(std::span<w8_ulong> depth_keys)
{
    std::vector<w8_ulong> order(depth_keys.size());
    std::iota(order.begin(), order.end(), w8_ulong{0});
    std::stable_sort(order.begin(), order.end(), [&](w8_ulong first, w8_ulong second) {
        return depth_keys[first] < depth_keys[second];
    });
    std::vector<w8_ulong> sorted_keys;
    sorted_keys.reserve(depth_keys.size());
    for (auto index : order) sorted_keys.push_back(depth_keys[index]);
    std::copy(sorted_keys.begin(), sorted_keys.end(), depth_keys.begin());
    return order;
}
