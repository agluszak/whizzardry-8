#include <stdexcept>

#include <fmt/format.h>

#include "surrender/srDebug.h"

[[noreturn]] void srAssertFail(std::string_view expression, std::string_view message,
                               std::source_location location)
{
    throw std::runtime_error(fmt::format("Assertion failed at {}:{}: {}{}{}", location.file_name(),
                                         location.line(), expression, message.empty() ? "" : "\n",
                                         message));
}
