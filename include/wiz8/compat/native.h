#pragma once

/* Native (non-Windows) lane.  Maps Microsoft compiler keywords and CRT
   spellings onto standard equivalents or onto w8_* implementations with the
   Microsoft semantics the recovered code expects.  Windows API calls do not
   belong here: each one is replaced at its use or wrapped in compat/platform.h.

   wchar_t is two bytes (-fshort-wchar), as on Windows, so the C library's
   wide-character functions, which assume four, are never called: every wide
   function the game uses maps to a w8_* implementation.  System headers are
   included first so their declarations keep the library names. */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <wchar.h>
#include <wctype.h>
#ifdef __cplusplus
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#endif

#define __declspec(attribute)
#define __cdecl
#define __stdcall
#define __fastcall
#define __forceinline inline __attribute__((always_inline))
#define __int64 long long

#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define _finite(value) isfinite(value)
#define _isnan(value) isnan(value)

#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256

/* <stdlib.h> and windows.h helpers. */
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#define __max(a, b) (((a) > (b)) ? (a) : (b))

#ifdef __cplusplus
extern "C" {
#endif

/* Narrow CRT extensions and path-aware file entry points. */
FILE* w8_fopen(const char* path, const char* mode);
int w8_rename(const char* source, const char* destination);
/* Kept explicit: a remove macro would also rewrite C++ member names. */
int w8_remove(const char* path);
char* w8_strupr(char* text);
char* w8_strlwr(char* text);
int w8_access(const char* path, int mode);
int w8_chmod(const char* path, int mode);
int w8_chdir(const char* path);
char* w8_getcwd(char* buffer, int size);
void w8_splitpath(const char* path, char* drive, char* directory, char* name, char* extension);

/* Wide strings: two-byte wchar_t, Microsoft semantics. */
size_t w8_wcslen(const wchar_t* text);
wchar_t* w8_wcscpy(wchar_t* destination, const wchar_t* source);
wchar_t* w8_wcsncpy(wchar_t* destination, const wchar_t* source, size_t count);
wchar_t* w8_wcscat(wchar_t* destination, const wchar_t* source);
wchar_t* w8_wcsncat(wchar_t* destination, const wchar_t* source, size_t count);
int w8_wcscmp(const wchar_t* first, const wchar_t* second);
int w8_wcsncmp(const wchar_t* first, const wchar_t* second, size_t count);
int w8_wcsicmp(const wchar_t* first, const wchar_t* second);
int w8_wcsnicmp(const wchar_t* first, const wchar_t* second, size_t count);
wchar_t* w8_wcschr(const wchar_t* text, wchar_t character);
wchar_t* w8_wcsrchr(const wchar_t* text, wchar_t character);
wchar_t* w8_wcsstr(const wchar_t* text, const wchar_t* pattern);
size_t w8_wcscspn(const wchar_t* text, const wchar_t* reject);
size_t w8_wcsspn(const wchar_t* text, const wchar_t* accept);
wchar_t* w8_wcstok(wchar_t* text, const wchar_t* delimiters);
wchar_t* w8_wcsdup(const wchar_t* text);
int w8_wtoi(const wchar_t* text);
/* Microsoft swprintf: no buffer size; %s/%c take wide arguments, %S/%hs narrow. */
int w8_swprintf(wchar_t* buffer, const wchar_t* format, ...);
int w8_vswprintf(wchar_t* buffer, const wchar_t* format, va_list arguments);

#ifdef __cplusplus
}
#endif

#define fopen w8_fopen
#define rename w8_rename

#define _strupr w8_strupr
#define strupr w8_strupr
#define _strlwr w8_strlwr
#define strlwr w8_strlwr
#define _access w8_access
#define _chmod w8_chmod
#define _chdir w8_chdir
#define _getcwd w8_getcwd
#define _splitpath w8_splitpath
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR

#define wcslen w8_wcslen
#define wcscpy w8_wcscpy
#define wcsncpy w8_wcsncpy
#define wcscat w8_wcscat
#define wcsncat w8_wcsncat
#define wcscmp w8_wcscmp
#define wcsncmp w8_wcsncmp
#define _wcsicmp w8_wcsicmp
#define _wcsnicmp w8_wcsnicmp
#define wcschr w8_wcschr
#define wcsrchr w8_wcsrchr
#define wcsstr w8_wcsstr
#define wcscspn w8_wcscspn
#define wcsspn w8_wcsspn
#define wcstok w8_wcstok
#define _wcsdup w8_wcsdup
#define _wtoi w8_wtoi
#define swprintf w8_swprintf
#define vswprintf w8_vswprintf
