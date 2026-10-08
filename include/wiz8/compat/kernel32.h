#pragma once

/* The Win32 subset the native lane provides: the plain data types that game
   structures and signatures use, and the few kernel32/winmm services with
   exact portable equivalents (tick counts, sleeping, debugger output).
   Window, file, graphics and audio APIs are replaced at their use sites. */
#if defined(WIZ8_NATIVE)
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

typedef uint32_t DWORD;
typedef DWORD* LPDWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int INT;
typedef unsigned int UINT;
typedef int BOOL;
typedef char CHAR;
typedef char* LPSTR;
typedef const char* LPCSTR;
typedef void* LPVOID;
typedef int32_t HRESULT;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef void* HANDLE;
typedef struct HWND__* HWND;
typedef struct HINSTANCE__* HINSTANCE;
typedef HINSTANCE HMODULE;
typedef struct HDC__* HDC;
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define MAX_PATH 260

typedef struct tagRECT {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT, *LPRECT;

typedef struct tagPOINT {
    LONG x;
    LONG y;
} POINT;

/* 100 ns intervals since 1601, as stored in save games and SLF directories. */
typedef struct _FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME;

typedef struct _SYSTEMTIME {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} SYSTEMTIME;

typedef struct tagMSG {
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG;

/* Virtual-key codes: the game's keyboard vocabulary. The native input layer
   translates SDL key events into these values. */
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E

/* windows.h defines min and max unless NOMINMAX is set; SGP relies on them.
   As macros they would break the C++ library headers, so C++ gets templates
   with the same result type as the conditional expression. */
#ifndef NOMINMAX
#ifdef __cplusplus
template <class A, class B> inline auto max(A a, B b) -> decltype(a > b ? a : b)
{
    return a > b ? a : b;
}
template <class A, class B> inline auto min(A a, B b) -> decltype(a < b ? a : b)
{
    return a < b ? a : b;
}
#else
#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#endif
#endif

inline uint32_t GetTickCount()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000u + now.tv_nsec / 1000000);
}

inline uint32_t timeGetTime()
{
    return GetTickCount();
}

inline void Sleep(uint32_t milliseconds)
{
    usleep(milliseconds * 1000u);
}

inline void OutputDebugString(const char* text)
{
    fputs(text, stderr);
}
#else
#include <windows.h>
#endif
