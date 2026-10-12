#pragma once

#include <cstdint>
#include <ostream>

SR_DLL_IMPORT std::int32_t __cdecl srDebugPrintf(std::uint32_t level, const char* format, ...) W8_PRINTF(2, 3);
SR_DLL_IMPORT std::int32_t __cdecl srPrintf(const char* format, ...);
SR_DLL_IMPORT std::int32_t __cdecl srStreamPrintf(std::ostream& stream, const char* format, ...) W8_PRINTF(2, 3);
SR_DLL_IMPORT const char* __cdecl srBoolToString(std::int32_t value);

// Print comma-separated bit names, falling back to the index for unnamed bits.
void srDumpFlags(std::ostream& stream, std::uint32_t flags, const char* names);

/* Consumers see the fixed-arity form declared in wiz8's sr_api.h. */
SR_DLL_IMPORT void __cdecl srAssertFail(const char* expression, const char* source_path, std::int32_t line,
                                        const char* message, ...);

typedef void(__cdecl* srAssertHandler)(const char* expression, const char* source_path, std::int32_t line,
                                       const char* message);

SR_DLL_IMPORT srAssertHandler __cdecl srAssertGetFunc();
SR_DLL_IMPORT void __cdecl srAssertSetFunc(srAssertHandler handler);
SR_DLL_IMPORT void __cdecl srDefaultAssertFailFunc(const char* expression, const char* source_path,
                                                   std::int32_t line, const char* message);
