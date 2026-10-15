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
#include <direct.h>
#include <io.h>
#endif
#ifdef __cplusplus
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#endif

#ifndef _WIN32
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

#define _MAX_PATH 260

#ifdef __cplusplus
extern "C" {
#endif

/* The game's one monotonic clock; runtime tests can substitute a virtual one. */
uint64_t w8_clock_us(void);
/* Milliseconds on the same clock, 64-bit so deadlines never wrap. */
uint64_t w8_get_ticks(void);

/* Narrow CRT spelling extensions. */
char* w8_strupr(char* text);
void w8_splitpath(const char* path, char* drive, char* directory, char* name, char* extension);

#ifdef __cplusplus
}
#endif

#define _strupr w8_strupr
#define _splitpath w8_splitpath
