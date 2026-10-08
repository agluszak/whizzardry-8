#pragma once

/* Native implementations of the few kernel32/winmm services that the game and
   SGP call in hundreds of places and that have exact portable equivalents:
   millisecond tick counts, sleeping and debugger output.  Window, file,
   graphics and audio APIs are replaced at their use sites instead. */
#if defined(WIZ8_NATIVE)
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

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
