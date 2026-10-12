#ifndef WIZ8_SR_API_H
#define WIZ8_SR_API_H

#include <cstdint>
#include <string>

typedef void(__cdecl* srAssertHandler)(const char* expression, const char* source_path, std::int32_t line,
                                      const char* message);

std::int32_t __cdecl srInit(void);
std::int32_t __cdecl srExit(void);
void __cdecl srAssertSetFunc(srAssertHandler handler);
void __cdecl srAssertFail(const char* expression, const char* source_path,
                        std::int32_t line, const char* message, ...);

inline void srAssertFail(const char* expression, const char* source_path, std::int32_t line,
                         const std::string& message)
{
    srAssertFail(expression, source_path, line, "%s", message.c_str());
}

#endif
