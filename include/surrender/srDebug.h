#pragma once

/* Consumers see the fixed-arity form declared in wiz8's sr_api.h. */
void srAssertFail(const char* expression, const char* source_path, w8_long line,
                  const char* message, ...);

typedef void (*srAssertHandler)(const char* expression, const char* source_path, w8_long line,
                                const char* message);

srAssertHandler srAssertGetFunc();
void srAssertSetFunc(srAssertHandler handler);
void srDefaultAssertFailFunc(const char* expression, const char* source_path, w8_long line,
                             const char* message);
