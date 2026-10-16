#pragma once

// Fatal invariant failure; the application boundary reports the diagnostic.
[[noreturn]] void srAssertFail(const char* expression, const char* source_path, int line,
                               const char* message, ...);
