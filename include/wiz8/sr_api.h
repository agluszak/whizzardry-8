#ifndef WIZ8_SR_API_H
#define WIZ8_SR_API_H

typedef void(__cdecl* srAssertHandler)(const char* expression, const char* source_path, w8_long line,
                                      const char* message);

int __cdecl srInit(void);
int __cdecl srExit(void);
void __cdecl srAssertSetFunc(srAssertHandler handler);
void __cdecl srAssertFail(const char* expression, const char* source_path,
                        w8_long line, const char* message, ...);

#endif
