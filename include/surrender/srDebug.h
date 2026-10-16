#pragma once

#include <string>

// Fatal invariant failure; the application boundary reports the diagnostic.
[[noreturn]] void srAssertFail(const char* expression, const char* source_path, int line,
                               const char* message, ...);

[[noreturn]] inline void srAssertFail(const char* expression, const char* source_path, int line,
                                      const std::string& message)
{
    srAssertFail(expression, source_path, line, "%s", message.c_str());
}
