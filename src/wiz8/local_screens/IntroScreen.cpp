#include "wiz8/local_screens/IntroScreen.h"

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include <fmt/format.h>

#include "asset_paths.h"
#include "wiz8/bink_video.h"
#include "wiz8/filesystem.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"

void ContinueAfterDarkEndingVideo(void)
{
    BeginEndgameSequence();
}

namespace {
constexpr std::array<std::string_view, 7> intro_video_names = {
    "Wizardry8.bik", "unaligned.bik", "Umpani.bik",  "T'Rang.bik",
    "virgin.bik",    "darkend.bik",   "sirtech.bik",
};

std::string IntroVideoPath(W8IntroVideo video)
{
    return fmt::format("Data\\Flics\\Intro\\{}", intro_video_names[video]);
}

bool IntroVideoExists(const std::string& path)
{
    const auto status = wiz8::file_status(wiz8::asset_path(path));
    return status && status->info.type == SDL_PATHTYPE_FILE;
}

class IntroScreen final : public Screen {
public:
    bool enter(const ScreenRequest& request) override
    {
        video_ = std::get<W8IntroVideo>(request.mode);
        ClearSurfaceRect(0, 0, 0x280, 0x1e0);
        if (video_ == W8_INTRO_VIDEO_WIZARDRY && g_settings.intro_seen && !g_status.intro_shown) {
            return true;
        }
        const std::string path = IntroVideoPath(video_);
        if (!IntroVideoExists(path)) return true;
        StopMusicPlaylist(true);
        DisableCursorScene();
        player_ = std::make_unique<W8BinkVideo>();
        player_->SetTarget(BeginVideoPresentation());
        if (!player_->Open(path.c_str(), 0)) {
            player_.reset();
            /* Hand presentation back, or a failed race intro leaves rendering off
               for the rest of the game. */
            FinishVideoPresentation();
        }
        return true;
    }

    void frame() override
    {
        while (auto next_input = PollInputEvent()) {
            const InputEvent input = *next_input;
            switch (input.type) {
            case InputEventType::KeyDown:
                if (input.key != SDLK_ESCAPE) break;
                advance();
                return;
            case InputEventType::LeftButtonUp:
            case InputEventType::RightButtonUp:
                advance();
                return;
            default:
                break;
            }
        }
        if (!player_ || player_->UpdateFrame()) advance();
    }

    bool leave(ScreenDeparture) override
    {
        if (player_) {
            FinishVideoPresentation();
            player_.reset();
        }
        return true;
    }

private:
    /* The film ended or was skipped: the publisher logo continues into the
       title film on a first run; otherwise route to where the film led. */
    void advance()
    {
        bool first_video_available = false;
        if (video_ == W8_INTRO_VIDEO_SIRTECH && !g_settings.intro_seen) {
            video_ = W8_INTRO_VIDEO_WIZARDRY;
            const std::string path = IntroVideoPath(video_);
            first_video_available = IntroVideoExists(path);
            if (first_video_available && player_ && player_->Open(path.c_str(), 0)) return;
        }
        /* A valid first-video path also closes the presentation when no video is open. */
        if (player_ || first_video_available) FinishVideoPresentation();
        player_.reset();
        ReturnToPreviousScreen();
        switch (video_) {
        case W8_INTRO_VIDEO_WIZARDRY:
        case W8_INTRO_VIDEO_SIRTECH:
            RequestScreen({.id = W8_SCREEN_MAIN_MENU});
            g_settings.intro_seen = true;
            break;
        case W8_INTRO_VIDEO_UNALIGNED:
        case W8_INTRO_VIDEO_UMPANI:
        case W8_INTRO_VIDEO_TRANG:
        case W8_INTRO_VIDEO_VIRGIN:
            if (!g_status.intro_shown) {
                RequestScreen({.id = W8_SCREEN_PLEASE_WAIT, .mode = W8_PLEASE_WAIT_NEW_GAME});
            } else {
                g_status.intro_shown = false;
                if (PendingScreen() != W8_SCREEN_MAIN_GAME) {
                    RequestScreen({.id = W8_SCREEN_MAIN_GAME});
                }
            }
            break;
        case W8_INTRO_VIDEO_DARK_ENDING:
            BeginScreenFade(0, 0, 1, ContinueAfterDarkEndingVideo, true, 1);
            break;
        }
        EnableCursorScene();
    }

    W8IntroVideo video_ = W8_INTRO_VIDEO_SIRTECH;
    std::unique_ptr<W8BinkVideo> player_;
};
} // namespace

std::unique_ptr<Screen> CreateIntroScreen()
{
    return std::make_unique<IntroScreen>();
}
