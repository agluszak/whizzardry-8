#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_events.h>

#include "wiz8/application.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/platform/input.h"

bool gfLoadAtStartup = false;
std::string gzStringDataOverride;
bool gfApplicationActive = false;
bool gfProgramIsRunning = false;
bool gfGameInitialized = false;

[[noreturn]] void ShutdownWithErrorBox(std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

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
    case SDL_EVENT_WINDOW_RESIZED:
        if (event.window.data1 > 0 && event.window.data2 > 0) VideoResizeWindow();
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (restore_pending) {
            if (!g_video_inspector_enabled) {
                RestoreVideoManager();
            }
            MoveTimer(W8_MOVE_TIMER_RESUME_MAIN_GAME);
            restore_pending = false;
        }
        gfApplicationActive = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!g_video_inspector_enabled) SuspendVideoManager();
        MoveTimer(W8_MOVE_TIMER_SUSPEND);
        gfApplicationActive = false;
        restore_pending = true;
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
