/* Modified for the Wizardry 8 reconstruction: 2026-10-09.
   Distributed under the accompanying SFI Source Code license agreement. */
#include "timer.h"
#include "compat/kernel32.h"

UINT32 guiStartupTime;
UINT32 guiCurrentTime;

namespace
{
bool running = false;
}
void UpdateClockManager()
{
    if (running)
        guiCurrentTime = w8_get_ticks() - guiStartupTime;
}
BOOLEAN InitializeClockManager()
{
    guiStartupTime = w8_get_ticks();
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
