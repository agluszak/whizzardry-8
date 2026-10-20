#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "wiz8/filesystem.h"

namespace wiz8 {
// Bounded typed reads on the existing stream; no ownership or path handling.
class BinaryReader {
    File& file_;

public:
    explicit BinaryReader(File& file) : file_(file) {}
    void require(std::size_t bytes) const
    {
        const auto remaining = file_.size() - file_.tell();
        if (remaining < 0 || bytes > static_cast<std::uint64_t>(remaining))
            throw std::runtime_error("truncated binary record");
    }
    template<class T> T record()
    {
        static_assert(std::is_trivially_copyable_v<T>);
        require(sizeof(T));
        T value{};
        file_.read_exact(&value, sizeof(value));
        return value;
    }
    template<class T> std::vector<T> array(std::size_t count)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::runtime_error("binary array too large");
        require(count * sizeof(T));
        std::vector<T> values(count);
        file_.read_exact(values.data(), count * sizeof(T));
        return values;
    }
};
}
