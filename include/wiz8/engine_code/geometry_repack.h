#pragma once

#include <array>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <span>
#include <vector>
#include <type_traits>
#include "wiz8/engine_code/OctPreTree.h"
#include "wiz8/engine_code/OctBuildPreTree.h"

namespace geometryRepack {
using CornerIndices = std::array<w8_ulong, 3>;

// Build tables keep index zero as a sentinel and one extra allocation slot.
// Own replacements until all allocations succeed, then transfer to the context.
template <class Record> auto copyTable(std::span<const Record> records)
{
    static_assert(std::is_trivially_copyable_v<Record>);
    std::unique_ptr<Record, decltype(&std::free)> table(
        static_cast<Record*>(std::calloc(records.size() + 1, sizeof(Record))), &std::free);
    if (table) {
        std::ranges::copy(records, table.get());
    }
    return table;
}

template <class Record, class Keep>
std::vector<Record> compactTable(std::span<const Record> records, Keep keep)
{
    std::vector<Record> result(1);
    for (const auto& record : records | std::views::drop(1)) {
        if (keep(record)) {
            result.push_back(record);
        }
    }
    return result;
}

inline void repointCorners(std::span<W8OctRegionPolygon> polygons,
                           std::span<const CornerIndices> indices, W8OctPreTreeVertex* vertices)
{
    for (std::size_t face = 1; face < polygons.size(); ++face) {
        for (unsigned corner = 0; corner < 3; ++corner) {
            polygons[face].vertices[corner] = vertices + indices[face][corner];
        }
    }
}
} // namespace geometryRepack
