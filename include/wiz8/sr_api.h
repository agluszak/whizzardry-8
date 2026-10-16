#ifndef WIZ8_SR_API_H
#define WIZ8_SR_API_H

#include <string>

typedef void (*srAssertHandler)(const char* expression, const char* source_path, w8_long line,
                                const char* message);

int srInit(void);
int srExit(void);
void srAssertSetFunc(srAssertHandler handler);
void srAssertFail(const char* expression, const char* source_path, w8_long line,
                  const char* message, ...);

inline void srAssertFail(const char* expression, const char* source_path, w8_long line,
                         const std::string& message)
{
    srAssertFail(expression, source_path, line, "%s", message.c_str());
}

#endif
