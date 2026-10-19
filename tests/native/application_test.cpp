#include <array>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "scoped_asset_roots.h"
#include "temporary_directory.h"
#include "wiz8/application.h"
#include "wiz8/asset_paths.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_input.h"

static void CheckStopped()
{
    REQUIRE(SDL_WasInit(0) == 0);
    REQUIRE((!gfProgramIsRunning && !gfGameInitialized && !gfApplicationActive));
    REQUIRE(ghWindow == nullptr);
    REQUIRE(LiveUiTargetCount() == 0);
}

TEST_CASE("application_test")
{
    ScopedTemporaryDirectory directory{"wiz8-application"};
    auto roots = w8_native::path_roots();
    roots.assets = (directory.path / "missing-assets").string();
    roots.user = (directory.path / "user").string();
    ScopedAssetRoots scoped_roots{roots};

    for (const auto option : {"/STRINGDATA", "/STRINGDATA="}) {
        const std::array<std::string_view, 1> arguments{option};
        bool rejected = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::invalid_argument& failure) {
            rejected = std::string_view(failure.what()).find("requires a filename") !=
                       std::string_view::npos;
        }
        REQUIRE(rejected);
        CheckStopped();
    }

    const std::array<std::string_view, 2> arguments{"/STRINGDATA=string data.wiz", "/window"};
    for (int attempt = 0; attempt < 2; ++attempt) {
        bool failed = false;
        try {
            wiz8::Application application(arguments);
        } catch (const std::runtime_error&) {
            failed = true;
        }
        REQUIRE(failed);
        CheckStopped();
    }

    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER",
                                       "wiz8-invalid-driver", true));
    bool failed = false;
    try {
        wiz8::Application application;
    } catch (const std::runtime_error&) {
        failed = true;
    }
    REQUIRE(failed);
    CheckStopped();
    REQUIRE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "SDL_VIDEODRIVER", "dummy", true));

    bool reported = false;
    try {
        ShutdownWithErrorBox("native initialization failure");
    } catch (const std::runtime_error& failure) {
        reported = std::string_view(failure.what()) == "native initialization failure";
    }
    REQUIRE(reported);
    CheckStopped();
}
