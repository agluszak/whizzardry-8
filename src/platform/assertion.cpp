#include "wiz8/platform/assertion.h"

#include <source_location>
#include <stdexcept>
#include <string_view>

#include <fmt/format.h>

[[noreturn]] void Fail(std::string_view expression, std::string_view message,
                       std::source_location location)
{
    throw std::runtime_error(fmt::format("Assertion failed at {}:{}: {}{}{}", location.file_name(),
                                         location.line(), expression, message.empty() ? "" : "\n",
                                         message));
}
