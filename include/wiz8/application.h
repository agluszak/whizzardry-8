#pragma once

#include "Types.h"
#include <span>
#include <string>
#include <string_view>

extern "C" {
extern BOOLEAN gfProgramIsRunning;
extern UINT8 gbPixelDepth;
extern BOOLEAN gfLoadAtStartup;
extern std::string gzStringDataOverride;
extern BOOLEAN gfUsingBoundsChecker;
extern BOOLEAN gfCapturingVideo;
extern BOOLEAN gfApplicationActive;
extern BOOLEAN gfGameInitialized;
extern BOOLEAN gfIgnoreMessages;
extern CHAR8 gzErrorMsg[2048];

union SDL_Event;
void HandleGameEvent(const SDL_Event& event);
bool PumpGameEvents(bool wait = false);
[[noreturn]] void ShutdownWithErrorBox(const CHAR8* message);
}

namespace wiz8 {
class Application {
public:
    explicit Application(std::span<const std::string_view> arguments = {});
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    void run();

private:
    void shutdown() noexcept;
    bool sdl_initialized_ = false;
    bool game_started_ = false;
    size_t initialized_screens_ = 0;
};
}
