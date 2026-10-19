#pragma once

#include <concepts>
#include <stdexcept>
#include <utility>

namespace wiz8 {
// Validate counts at fixed-width format and legacy API boundaries. Runtime
// sizes and clocks should retain their native width instead of using this.
template<std::integral To> constexpr To checked_integer(std::integral auto value)
{
    if (!std::in_range<To>(value)) throw std::overflow_error("Integer value is out of range");
    return static_cast<To>(value);
}
} // namespace wiz8
