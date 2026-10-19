#pragma once

#include <source_location>
#include <string_view>

// Fatal invariant failure; the application boundary reports the diagnostic.
[[noreturn]] void Fail(std::string_view expression, std::string_view message = {},
                       std::source_location location = std::source_location::current());
