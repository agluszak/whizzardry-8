/* Modified for the Wizardry 8 reconstruction: 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "timer.h"
#include <SDL3/SDL_timer.h>

UINT32 guiStartupTime;
UINT32 guiCurrentTime;

namespace
{
bool running = false;
}
void UpdateClockManager()
{
    if (running)
        guiCurrentTime = UINT32(SDL_GetTicks()) - guiStartupTime;
}
BOOLEAN InitializeClockManager()
{
    guiStartupTime = UINT32(SDL_GetTicks());
    guiCurrentTime = 0;
    running = true;
    return TRUE;
}
void ShutdownClockManager() { running = false; }
TIMER GetClock() { return guiCurrentTime; }
TIMER SetCountdownClock(UINT32 delay) { return guiCurrentTime + delay; }
UINT32 ClockIsTicking(TIMER timer)
{
    const INT32 remaining = INT32(timer - guiCurrentTime);
    return remaining > 0 ? UINT32(remaining) : 0;
}
