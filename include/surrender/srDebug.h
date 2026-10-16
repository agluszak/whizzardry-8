#pragma once

/* Consumers see the fixed-arity form declared in wiz8's sr_api.h. */
SR_DLL_IMPORT void __cdecl srAssertFail(const char* expression, const char* source_path,
                                        w8_long line, const char* message, ...);

typedef void(__cdecl* srAssertHandler)(const char* expression, const char* source_path,
                                       w8_long line, const char* message);

SR_DLL_IMPORT srAssertHandler __cdecl srAssertGetFunc();
SR_DLL_IMPORT void __cdecl srAssertSetFunc(srAssertHandler handler);
SR_DLL_IMPORT void __cdecl srDefaultAssertFailFunc(const char* expression, const char* source_path,
                                                   w8_long line, const char* message);
