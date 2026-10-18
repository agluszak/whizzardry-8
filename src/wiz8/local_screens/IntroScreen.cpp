#include "wiz8/local_screens/IntroScreen.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "compat/compiler.h"
#include "input.h"
#include "wiz8/bink_video.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"

/* The canonical state-zero row owns this enter/frame/leave bundle. */

#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"

// GLOBAL: WIZ8 0x0064d8ac
W8IntroVideo g_intro_video_index = W8_INTRO_VIDEO_SIRTECH;
// GLOBAL: WIZ8 0x0064D8B0
static constexpr std::array<std::string_view, 7> g_intro_video_names = {
    "Wizardry8.bik", "unaligned.bik", "Umpani.bik",  "T'Rang.bik",
    "virgin.bik",    "darkend.bik",   "sirtech.bik",
};

// FUNCTION: WIZ8 0x005AE770
void ContinueAfterDarkEndingVideo(void)
{
    BeginEndgameSequence();
}
// GLOBAL: WIZ8 0x0069C258
static std::unique_ptr<W8BinkVideo> g_intro_video;

// FUNCTION: WIZ8 0x005ae510
bool IntroScreenEnter(void)
{
    std::string path;

    ClearSurfaceRect(0, 0, 0x280, 0x1e0);
    if (g_intro_video_index == W8_INTRO_VIDEO_WIZARDRY && g_settings.intro_seen && !g_status.intro_shown) {
        return 1;
    }
    path = fmt::format("Data\\Flics\\Intro\\{}", g_intro_video_names[g_intro_video_index]);
    if (![&]() {
            const auto status = wiz8::file_status(wiz8::asset_path(path));
            return status && status->info.type == SDL_PATHTYPE_FILE;
        }()) {
        return 1;
    }
    StopMusicPlaylist(true);
    DisableCursorScene();
    g_intro_video = std::make_unique<W8BinkVideo>();
    g_intro_video->SetTarget(BeginVideoPresentation());
    if (!g_intro_video->Open(path.c_str(), 0)) {
        g_intro_video.reset();
        /* Hand presentation back, or a failed race intro leaves rendering off
           for the rest of the game. */
        FinishVideoPresentation();
    }
    return 1;
}

static void AdvanceIntroScreen(void);

// FUNCTION: WIZ8 0x005ae6f0
void IntroScreenFrame(void)
{

    while (auto next_input = PollInputEvent()) {
        const InputEvent input = *next_input;
        switch (input.type) {
        case InputEventType::KeyDown:
            if (input.key != SDLK_ESCAPE) break;
            AdvanceIntroScreen();
            return;
        case InputEventType::LeftButtonUp:
        case InputEventType::RightButtonUp:
            AdvanceIntroScreen();
            return;
        default:
            break;
        }
    }
    if (!g_intro_video || g_intro_video->UpdateFrame()) {
        AdvanceIntroScreen();
    }
}

// FUNCTION: WIZ8 0x005ae780
static void AdvanceIntroScreen(void)
{
    std::string path;
    bool first_video_available = false;

    if (g_intro_video_index == W8_INTRO_VIDEO_SIRTECH && !g_settings.intro_seen) {
        g_intro_video_index = W8_INTRO_VIDEO_WIZARDRY;
        path = fmt::format("Data\\Flics\\Intro\\{}", g_intro_video_names[g_intro_video_index]);
        first_video_available = [&]() {
            const auto status = wiz8::file_status(wiz8::asset_path(path));
            return status && status->info.type == SDL_PATHTYPE_FILE;
        }();
        if (first_video_available && g_intro_video && g_intro_video->Open(path.c_str(), 0)) {
            return;
        }
    }
    /* A valid first-video path also closes the presentation when no video is open. */
    if (g_intro_video || first_video_available) {
        FinishVideoPresentation();
    }
    g_intro_video.reset();
    RequestScreenTransition();
    switch (g_intro_video_index) {
    case W8_INTRO_VIDEO_WIZARDRY:
    case W8_INTRO_VIDEO_SIRTECH:
        g_pending_screen_state.id = W8_SCREEN_MAIN_MENU;
        g_settings.intro_seen = true;
        break;
    case W8_INTRO_VIDEO_UNALIGNED:
    case W8_INTRO_VIDEO_UMPANI:
    case W8_INTRO_VIDEO_TRANG:
    case W8_INTRO_VIDEO_VIRGIN:
        if (!g_status.intro_shown) {
            g_pending_screen_state.mode = W8_PLEASE_WAIT_NEW_GAME;
            g_pending_screen_state.id = W8_SCREEN_PLEASE_WAIT;
        } else {
            g_status.intro_shown = false;
            if (GetPendingScreenState() != 7) {
                g_pending_screen_state.id = W8_SCREEN_MAIN_GAME;
            }
        }
        break;
    case W8_INTRO_VIDEO_DARK_ENDING:
        BeginScreenFade(0, 0, 1, ContinueAfterDarkEndingVideo, true, 1);
        break;
    }
    EnableCursorScene();
}

// FUNCTION: WIZ8 0x005ae940
bool IntroScreenLeave(int)
{
    if (g_intro_video) {
        FinishVideoPresentation();
        g_intro_video.reset();
    }
    return 1;
}

// FUNCTION: WIZ8 0x005AE9C0
void SetIntroVideoIndex(W8IntroVideo value)
{
    g_intro_video_index = value;
}
