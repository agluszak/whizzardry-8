#ifndef WIZ8_SR_API_H
#define WIZ8_SR_API_H

#include <string>

#include "surrender/srDebug.h" // IWYU pragma: export

int srInit(void);
int srExit(void);

[[noreturn]] inline void srAssertFail(const char* expression, const char* source_path, int line,
                                      const std::string& message)
{
    srAssertFail(expression, source_path, line, "%s", message.c_str());
}

#endif
