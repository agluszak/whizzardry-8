#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/asset_paths.h"
#include "temporary_directory.h"
#include "Button System.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
    std::exit(1); \
} } while (0)

static void CheckStopped()
{
    CHECK(SDL_WasInit(0) == 0);
    CHECK(!gfProgramIsRunning && !gfGameInitialized && !gfApplicationActive);
    CHECK(ghWindow == nullptr);
    CHECK(std::all_of(ButtonList.begin(), ButtonList.end(), [](const auto& button) {
        return !button;
    }));
}

int main()
{
    const auto temporary = make_temporary_directory("wiz8-application");
    auto roots = w8_native::path_roots();
    roots.assets = temporary + "/missing-assets";
    roots.user = temporary;
    w8_native::configure_paths(roots);

    for (const auto option : {"/VIDEOCFG", "/STRINGDATA", "/VIDEOCFG="}) {
        const std::array<std::string_view, 1> arguments{option};
        bool rejected = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::invalid_argument& failure) {
            rejected = std::string_view(failure.what()).find("requires a filename") !=
                std::string_view::npos;
        }
        CHECK(rejected);
        CheckStopped();
    }

    const std::array<std::string_view, 4> arguments{
        "/STRINGDATA=string data.wiz", "/window", "/vIdEoCfG", "C:\\missing video.cfg"};
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool failed = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        CHECK(failed);
        CHECK(gzStringDataOverride == "string data.wiz");
        CHECK(std::string_view(VideoGetConfigFile()) == "C:\\missing video.cfg");
        CheckStopped();
    }

    CHECK(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER",
                                     "wiz8-invalid-driver", true));
    bool failed = false;
    try {
        wiz8::Application application;
    } catch (const std::runtime_error&) {
        failed = true;
    }
    CHECK(failed);
    CheckStopped();
    CHECK(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true));

    bool reported = false;
    try {
        ShutdownWithErrorBox("native initialization failure");
    } catch (const std::runtime_error& failure) {
        reported = std::string_view(failure.what()) == "native initialization failure";
    }
    CHECK(reported && gfIgnoreMessages);
    CheckStopped();
    std::filesystem::remove_all(temporary);
    return 0;
}
