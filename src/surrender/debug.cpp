#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "surrender/srDebug.h"

[[noreturn]] void Fail(std::string_view expression, std::string_view message,
                       const std::source_location& location)
{
    std::string diagnostic = fmt::format("Invariant failed at {}:{}:\n\n[ {} ]\n",
                                         location.file_name(), location.line(), expression);
    if (!message.empty()) {
        diagnostic += "\n";
        diagnostic += message;
        diagnostic += "\n";
    }
    throw std::runtime_error(diagnostic);
}
