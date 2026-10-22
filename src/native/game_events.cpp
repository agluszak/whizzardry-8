#include <SDL3/SDL_events.h>

#include "wiz8/application.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"

// GLOBAL: WIZ8 0x006f0630
bool gfApplicationActive = false;
bool gfProgramIsRunning = false;
bool gfGameInitialized = false;

namespace {
bool restore_pending = false;
}

void HandleGameEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT) {
        gfProgramIsRunning = false;
        return;
    }
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ghWindow &&
        SDL_GetWindowFromEvent(&event) == ghWindow) {
        gfProgramIsRunning = false;
        return;
    }
    HandleInputEvent(event);
    if (!ghWindow || SDL_GetWindowFromEvent(&event) != ghWindow) return;
    switch (event.type) {
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (event.window.data1 > 0 && event.window.data2 > 0) VideoResizeWindow();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (restore_pending) {
            if (auto* screen = CurrentScreen()) screen->resetStepClock();
            MoveTimer(W8_MOVE_TIMER_RESUME_MAIN_GAME);
            restore_pending = false;
        }
        gfApplicationActive = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!restore_pending) MoveTimer(W8_MOVE_TIMER_SUSPEND);
        gfApplicationActive = false;
        restore_pending = true;
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        if (!g_video_inspector_enabled) SuspendVideoManager();
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        if (!g_video_inspector_enabled) RestoreVideoManager();
        break;
    }
}
bool PumpGameEvents(bool wait)
{
    SDL_Event event{};
    bool received = false;
    if (wait && SDL_WaitEventTimeout(&event, 10)) {
        HandleGameEvent(event);
        received = true;
    }
    while (SDL_PollEvent(&event)) {
        HandleGameEvent(event);
        received = true;
    }
    return received;
}
