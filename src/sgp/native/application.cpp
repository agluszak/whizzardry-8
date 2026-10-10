#include "native/input_events.h"
#include "sgp.h"
#include "wiz8/engine_code/GameData.h"

void HandleGameEvent(const SDL_Event& event)
{
    static bool restore_pending = false;
    if (event.type == SDL_EVENT_QUIT)
    {
        gfProgramIsRunning = FALSE;
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ghWindow &&
        SDL_GetWindowFromEvent(&event) == reinterpret_cast<SDL_Window*>(ghWindow))
    {
        gfProgramIsRunning = FALSE;
        return;
    }
    if (gfIgnoreMessages)
        return;
    HandleInputEvent(event);
    if (!ghWindow || SDL_GetWindowFromEvent(&event) != reinterpret_cast<SDL_Window*>(ghWindow))
        return;
    switch (event.type)
    {
    case SDL_EVENT_WINDOW_RESIZED:
        if (event.window.data1 > 0 && event.window.data2 > 0)
            VideoResizeWindow();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (restore_pending)
        {
            if (!VideoInspectorIsEnabled())
            {
                RestoreVideoManager();
                RestoreVideoSurfaces();
            }
            MoveTimer(TIMER_RESUME);
            restore_pending = false;
        }
        gfApplicationActive = TRUE;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!VideoInspectorIsEnabled())
            SuspendVideoManager();
        MoveTimer(TIMER_SUSPEND);
        gfApplicationActive = FALSE;
        restore_pending = true;
        break;
    }
}
bool PumpGameEvents(bool wait)
{
    SDL_Event event{};
    bool received = false;
    if (wait && SDL_WaitEventTimeout(&event, 10))
    {
        HandleGameEvent(event);
        received = true;
    }
    while (SDL_PollEvent(&event))
    {
        HandleGameEvent(event);
        received = true;
    }
    UpdateClockManager();
    return received;
}
