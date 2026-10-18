#pragma once

#include <source_location>
#include <string>
#include <string_view>

// Unconditional invariant failure. The application boundary reports the diagnostic.
[[noreturn]] void Fail(std::string_view expression, std::string_view message = {},
                       const std::source_location& location = std::source_location::current());
