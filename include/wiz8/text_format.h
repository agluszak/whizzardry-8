#pragma once

#include <fmt/printf.h>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace wiz8::text {
// Retail localization uses runtime printf patterns. {fmt} preserves that
// vocabulary while checking argument types and owning the complete result.
template<class T>
decltype(auto) format_argument(const T& value)
{
    if constexpr (std::is_enum_v<T>) return static_cast<std::underlying_type_t<T>>(value);
    else return (value);
}

template<class... Args>
std::string format(std::string_view pattern, const Args&... args)
{
    if constexpr (sizeof...(Args) == 0) return std::string(pattern);
    auto values = std::tuple<decltype(format_argument(args))...>{format_argument(args)...};
    return std::apply([&](const auto&... values) {
        return fmt::vsprintf(fmt::string_view(pattern.data(), pattern.size()),
                            fmt::make_printf_args(values...));
    }, values);
}
} // namespace wiz8::text
