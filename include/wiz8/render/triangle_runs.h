#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

struct TriangleRun {
    std::size_t first;
    std::size_t count;
    std::uint32_t texture_set;
};

// An empty order uses contiguous input; otherwise runs follow its permutation.
template<class Visit>
inline void visitTriangleRuns(std::span<const std::uint32_t> texture_sets,
                              std::span<const std::uint32_t> order, std::size_t maximum_run,
                              Visit visit)
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
        visit(TriangleRun{first, length, texture});
        first += length;
    }
}

// Preserve equal-depth submission order and the renderer's sorted key table.
inline std::vector<std::uint32_t> sortedTriangleOrder(std::span<std::uint32_t> depth_keys)
{
    std::vector<std::uint32_t> order(depth_keys.size());
    std::iota(order.begin(), order.end(), std::uint32_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::uint32_t first, std::uint32_t second) {
        return depth_keys[first] < depth_keys[second];
    });
    std::vector<std::uint32_t> sorted_keys;
    sorted_keys.reserve(depth_keys.size());
    for (auto index : order) sorted_keys.push_back(depth_keys[index]);
    std::copy(sorted_keys.begin(), sorted_keys.end(), depth_keys.begin());
    return order;
}
