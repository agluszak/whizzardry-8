#pragma once

#include <ostream>

w8_long srDebugPrintf(w8_ulong level, const char* format, ...) W8_PRINTF(2, 3);
w8_long srPrintf(const char* format, ...);
w8_long srStreamPrintf(std::ostream& stream, const char* format, ...) W8_PRINTF(2, 3);
const char* srBoolToString(int value);

// Print comma-separated bit names, falling back to the index for unnamed bits.
void srDumpFlags(std::ostream& stream, w8_ulong flags, const char* names);

/* Consumers see the fixed-arity form declared in wiz8's sr_api.h. */
void srAssertFail(const char* expression, const char* source_path, w8_long line,
                  const char* message, ...);

typedef void (*srAssertHandler)(const char* expression, const char* source_path, w8_long line,
                                const char* message);

srAssertHandler srAssertGetFunc();
void srAssertSetFunc(srAssertHandler handler);
void srDefaultAssertFailFunc(const char* expression, const char* source_path, w8_long line,
                             const char* message);
