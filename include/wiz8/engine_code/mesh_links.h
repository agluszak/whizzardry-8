#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace wiz8 {
class File;
}

// Native ownership of the .oct file's lookup offsets and zero-terminated object ids.
struct OctreeMeshLinks {
    std::vector<unsigned short> lookup;
    std::vector<unsigned short> records;

    std::span<const unsigned short> runAt(std::size_t start) const
    {
        if (start >= records.size()) {
            return {};
        }
        auto end = start;
        while (end < records.size() && records[end] != 0) {
            ++end;
        }
        return std::span<const unsigned short>(records).subspan(start, end - start);
    }
    std::span<const unsigned short> alwaysVisible() const
    {
        return runAt(0);
    }
    std::span<const unsigned short> forMesh(std::size_t mesh) const
    {
        return mesh < lookup.size() && lookup[mesh] != 0 ? runAt(lookup[mesh])
                                                         : std::span<const unsigned short>{};
    }
    void read(wiz8::File& file, std::size_t mesh_count);
    void write(wiz8::File& file, std::size_t mesh_count) const;

    template <class Object>
    void markVisible(std::size_t mesh, std::span<Object* const> loaded,
                     std::vector<bool>& visible) const
    {
        for (const auto id : forMesh(mesh)) {
            if (id <= loaded.size() && id <= visible.size() && loaded[id - 1]) {
                visible[id - 1] = true;
            }
        }
    }
};
