#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <stdexcept>
#include <string>
#include <string_view>

#include "input.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Video2.h"

// GLOBAL: WIZ8 0x006505a0
bool gfLoadAtStartup = false;
// GLOBAL: WIZ8 0x006505a1
bool gfUsingBoundsChecker = false;
// GLOBAL: WIZ8 0x006505a4
std::string gzStringDataOverride;
// GLOBAL: WIZ8 0x006505a8
bool gfCapturingVideo = false;
// GLOBAL: WIZ8 0x006f0630
bool gfApplicationActive = false;
// GLOBAL: WIZ8 0x006f0628
bool gfProgramIsRunning = false;
// GLOBAL: WIZ8 0x006505a9
bool gfGameInitialized = false;
// GLOBAL: WIZ8 0x006505ac
std::string gzErrorMsg;
// GLOBAL: WIZ8 0x00650dac
bool gfIgnoreMessages = false;

// FUNCTION: WIZ8 0x00401920
[[noreturn]] void ShutdownWithErrorBox(std::string_view message)
{
    gzErrorMsg = message;
    gfIgnoreMessages = true;
    throw std::runtime_error(gzErrorMsg);
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
        SDL_GetWindowFromEvent(&event) == reinterpret_cast<SDL_Window*>(ghWindow)) {
        gfProgramIsRunning = false;
        return;
    }
    if (gfIgnoreMessages) {
        return;
    }
    HandleInputEvent(event);
    if (!ghWindow || SDL_GetWindowFromEvent(&event) != reinterpret_cast<SDL_Window*>(ghWindow)) {
        return;
    }
    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED:
        if (event.window.data1 > 0 && event.window.data2 > 0) {
            VideoResizeWindow();
        }
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        if (restore_pending) {
            if (!VideoInspectorIsEnabled()) {
                RestoreVideoManager();
            }
            MoveTimer(TIMER_RESUME);
            restore_pending = false;
        }
        gfApplicationActive = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        if (!VideoInspectorIsEnabled()) {
            SuspendVideoManager();
        }
        MoveTimer(TIMER_SUSPEND);
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
