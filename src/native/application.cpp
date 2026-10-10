#include "native/input_events.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Video2.h"
#include "input.h"

#include "compat/kernel32.h"
#include "compat/video.h"
#include "Font.h"
#include "vobject.h"
#include "vsurface.h"
#include "Button System.h"
#include "mousesystem.h"
#include "soundman.h"
#include "random.h"
#include "timer.h"
#include "wiz8/filesystem.h"
#include "wiz8/game_init.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/engine_code/Octree.h"
#include <stdexcept>
#include <cstdio>

namespace {

void ParseArguments(std::span<const std::string_view> arguments)
{
    for (size_t index = 0; index < arguments.size(); ++index) {
        const auto separator = arguments[index].find('=');
        const std::string option(arguments[index].substr(0, separator));
        if (SDL_strcasecmp(option.c_str(), "/NOSOUND") == 0)
            SoundEnableSound(FALSE);
        else if (SDL_strcasecmp(option.c_str(), "/INSPECTOR") == 0)
            VideoInspectorEnable();
        else if (SDL_strcasecmp(option.c_str(), "/LOAD") == 0)
            gfLoadAtStartup = TRUE;
        else if (SDL_strcasecmp(option.c_str(), "/WINDOW") == 0)
            VideoFullScreen(FALSE);
        else if (SDL_strcasecmp(option.c_str(), "/BC") == 0)
            gfUsingBoundsChecker = TRUE;
        else if (SDL_strcasecmp(option.c_str(), "/CAPTURE") == 0)
            gfCapturingVideo = TRUE;
        else if (SDL_strcasecmp(option.c_str(), "/NOOCT") == 0)
            NoOct();
        else if (SDL_strcasecmp(option.c_str(), "/VIDEOCFG") == 0 ||
                 SDL_strcasecmp(option.c_str(), "/STRINGDATA") == 0) {
            const auto value = separator != std::string_view::npos
                ? arguments[index].substr(separator + 1)
                : ++index < arguments.size() ? arguments[index] : std::string_view{};
            if (value.empty())
                throw std::invalid_argument(option + " requires a filename");
            if (SDL_strcasecmp(option.c_str(), "/VIDEOCFG") == 0) {
                std::string filename(value);
                VideoSetConfigFile(filename.data());
            } else
                gzStringDataOverride = value;
        }
    }
}
}

wiz8::Application::Application(std::span<const std::string_view> arguments)
{
    if (gfGameInitialized)
        throw std::logic_error("The game is already running");
    try {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            const std::string failure(SDL_GetError());
            SDL_Quit();
            throw std::runtime_error(failure);
        }
        sdl_initialized_ = true;
        gfIgnoreMessages = FALSE;
        gfLoadAtStartup = gfUsingBoundsChecker = gfCapturingVideo = FALSE;
        gzStringDataOverride.clear();
        gzErrorMsg[0] = '\0';
        SoundEnableSound(TRUE);
        ParseArguments(arguments);
        wiz8::refresh_asset_archives();
        gbPixelDepth = W8ReadProfileInt("sgp.ini", "SGP", "PIXEL_DEPTH", PIXEL_DEPTH);
        if (!InitializeInputManager())
            throw std::runtime_error("Input initialization failed");
        if (!InitializeVideoManager())
            throw std::runtime_error("Video initialization failed");
        if (!InitializeVideoObjectManager() || !InitializeVideoSurfaceManager())
            throw std::runtime_error("Video resource initialization failed");
        InitializeClockManager();
        if (!InitializeFontManager(CreateEnglishTransTable()))
            throw std::runtime_error("Font initialization failed");
        if (!InitializeSoundManager())
            throw std::runtime_error("Sound initialization failed");
        InitializeRandom();
        game_started_ = true;
        if (!InitializeGame(initialized_screens_))
            throw std::runtime_error("Game initialization failed");
        gfGameInitialized = gfApplicationActive = gfProgramIsRunning = TRUE;
    } catch (...) {
        shutdown();
        throw;
    }
}

wiz8::Application::~Application()
{
    shutdown();
}

void wiz8::Application::run()
{
    while (gfProgramIsRunning) {
        PumpGameEvents(!gfApplicationActive);
        if (!gfProgramIsRunning)
            break;
        if (gfApplicationActive) {
            GameLoop();
            gfSGPInputReceived = FALSE;
        }
    }
}

void wiz8::Application::shutdown() noexcept
{
    if (!sdl_initialized_)
        return;
    gfProgramIsRunning = FALSE;
    const bool complete = gfGameInitialized;
    if (complete) {
        try {
            bool release_screens = true;
#ifdef _DEBUG
            release_screens = !gfIgnoreMessages;
#endif
            GameloopExit(release_screens);
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Leaving game: %s", failure.what());
        }
    }
    gfGameInitialized = FALSE;
    ShutdownSoundManager();
    if (game_started_) {
        try {
            ShutdownGame(initialized_screens_, complete);
        } catch (const std::exception& failure) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Game shutdown: %s", failure.what());
        }
    }
    ShutdownButtonSystem();
    MSYS_Shutdown();
    ShutdownFontManager();
    ShutdownClockManager();
    ShutdownVideoSurfaceManager();
    ShutdownVideoObjectManager();
    ShutdownVideoManager();
    wiz8::clear_asset_archives();
    W8VideoShowCursor(TRUE);
    VideoDumpMemoryLeaks();
    gfApplicationActive = FALSE;
    SDL_Quit();
    sdl_initialized_ = false;
}
