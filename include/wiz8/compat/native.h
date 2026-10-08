#pragma once

/* Native (non-Windows) lane.  Maps Microsoft compiler keywords and CRT
   spellings onto their standard equivalents.  Windows API types and calls do
   not belong here: each one is replaced at its use under WIZ8_NATIVE. */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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
