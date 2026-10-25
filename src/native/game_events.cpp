#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_events.h>

#include "wiz8/application.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"

// GLOBAL: WIZ8 0x006f0630
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

void HandleGameEvent(const SDL_Event& event, std::function_ref<void(bool)> focus_changed)
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
            focus_changed(true);
            restore_pending = false;
        }
        gfApplicationActive = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!restore_pending) focus_changed(false);
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
bool PumpGameEvents(std::function_ref<void(bool)> focus_changed, bool wait)
{
    SDL_Event event{};
    bool received = false;
    if (wait && SDL_WaitEventTimeout(&event, 10)) {
        HandleGameEvent(event, focus_changed);
        received = true;
    }
    while (SDL_PollEvent(&event)) {
        HandleGameEvent(event, focus_changed);
        received = true;
    }
    return received;
}
