#pragma once

/* Native lane. Maps Microsoft compiler keywords and CRT
   spellings onto standard equivalents or onto w8_* implementations with the
   Microsoft semantics the recovered code expects.  Windows API calls do not
   belong here: game file operations use wiz8/filesystem.h.

   Application strings use UTF-8; retail Unicode decoding is explicit. */

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#endif
#ifdef __cplusplus
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#endif

#ifndef _WIN32
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
#else
#define stricmp _stricmp
#define strnicmp _strnicmp
#endif

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

/* Narrow CRT spelling extensions. */
char* w8_strupr(char* text);
char* w8_strlwr(char* text);
void w8_splitpath(const char* path, char* drive, char* directory, char* name, char* extension);

#ifdef __cplusplus
}
#endif


#define _strupr w8_strupr
#define strupr w8_strupr
#define _strlwr w8_strlwr
#define strlwr w8_strlwr
#define _splitpath w8_splitpath
#ifndef _WIN32
#define _S_IREAD S_IRUSR
#define _S_IWRITE S_IWUSR
#else
#define S_IRUSR _S_IREAD
#define S_IWUSR _S_IWRITE
#define S_IWGRP 0
#define S_IWOTH 0
#define S_ISREG(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#endif
