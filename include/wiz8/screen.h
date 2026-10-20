#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "wiz8/layouts/save_screenshot.h"

class GameSession;
struct W8Character;

enum W8ScreenId {
    W8_SCREEN_NONE = -1,
    W8_SCREEN_INTRO = 0,
    W8_SCREEN_MAIN_MENU = 1,
    W8_SCREEN_GAME_START_ROUTER = 2,
    W8_SCREEN_CHARACTER = 3,
    W8_SCREEN_PLEASE_WAIT = 4,
    W8_SCREEN_PARTY_SELECTION = 5,
    W8_SCREEN_CAMP = 6,
    W8_SCREEN_MAIN_GAME = 7,
    W8_SCREEN_AUTOMAP = 8,
    W8_SCREEN_CREDITS = 9,
    W8_SCREEN_OPTIONS = 10,
    W8_SCREEN_JOURNAL = 11,
    W8_SCREEN_EXIT = 12,
    W8_SCREEN_COUNT = 13
};

/* The destination-specific values a request's mode carries. */
enum W8PleaseWaitMode {
    W8_PLEASE_WAIT_NEW_GAME = 0,
    W8_PLEASE_WAIT_LOAD_GAME = 1,
    W8_PLEASE_WAIT_SAVE_GAME = 2,
    W8_PLEASE_WAIT_CHANGE_LEVEL = 3
};

enum W8OptionsMode {
    W8_OPTIONS_DEFAULT = 0,
    W8_OPTIONS_LOAD = 1,
    W8_OPTIONS_SAVE = 2,
    W8_OPTIONS_IN_GAME = 3
};

enum W8CharacterScreenMode {
    W8_CHARACTER_CREATE = 0,
    W8_CHARACTER_EDIT = 1,
    W8_CHARACTER_LEVEL_UP = 2,
    W8_CHARACTER_LEVEL_UP_NOTICE = 3
};

/* The seven intro films, in the same order as the intro screen's filename table. */
enum W8IntroVideo : unsigned {
    W8_INTRO_VIDEO_WIZARDRY = 0,
    W8_INTRO_VIDEO_UNALIGNED = 1,
    W8_INTRO_VIDEO_UMPANI = 2,
    W8_INTRO_VIDEO_TRANG = 3,
    W8_INTRO_VIDEO_VIRGIN = 4,
    W8_INTRO_VIDEO_DARK_ENDING = 5,
    W8_INTRO_VIDEO_SIRTECH = 6
};

/* The mode variant carries only the domain selected by the request's id, so a
   screen cannot interpret another screen's mode as its own. */
using W8ScreenMode = std::variant<std::monostate, W8PleaseWaitMode, W8OptionsMode,
                                  W8CharacterScreenMode, W8IntroVideo>;

/* What a screen is entered with. A suspended screen keeps its request and is
   resumed with it. */
struct ScreenRequest {
    W8ScreenId id = W8_SCREEN_NONE;
    W8ScreenMode mode;
    int parameter = 0;
    int parameter_2 = 0;
    /* The character the camp and review-character transitions hand to the
       character screen. */
    W8Character* character = nullptr;
    /* The screenshot the Please Wait screen's save mode hands to SaveGame. */
    std::optional<W8SaveScreenshot> save_screenshot;
    /* Identifying PC for camp entry; null when camp opens without an
       identify/use-item handoff. The entry mode lives on the items page. */
    W8Character* parameter_4 = nullptr;
    std::string name;
    /* Previous user save slot, removed only after a successful renamed save. */
    std::string previous_save_name;
};

enum class ScreenDeparture { Suspend, Discard };

/* One screen of the game. The router creates a screen when it is first
   requested, keeps it while it is suspended under other screens, and destroys
   it when it is discarded. A screen rebuilt on every visit is destroyed when it
   is suspended too, and a new one is entered in its place when it would be
   resumed. Entering and leaving report failure, which stops the program. */
class Screen {
public:
    virtual ~Screen() = default;
    Screen() = default;
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    /* The screen becomes current for the first time. */
    virtual bool enter(const ScreenRequest& request) = 0;
    /* The screen becomes current again after a suspension, with the request it
       was first entered with. Screens that rebuild from live state on every
       visit leave this as a plain entry. */
    virtual bool resume(const ScreenRequest& request) { return enter(request); }
    virtual void frame() = 0;
    /* The screen stops being current. A suspended screen is resumed later; a
       discarded one is destroyed afterwards. */
    virtual bool leave(ScreenDeparture departure) = 0;
    /* For a screen rebuilt on every visit, the request that rebuilds it after a
       suspension, given the one it was entered with; screens kept while
       suspended return nothing. */
    virtual std::optional<ScreenRequest> rebuildRequest(const ScreenRequest&) const
    {
        return std::nullopt;
    }
};

using ScreenFactory = std::unique_ptr<Screen> (*)(W8ScreenId id, GameSession& games);
/* The game's screens, by id. The screens that begin and end games keep `games`. */
std::unique_ptr<Screen> CreateScreen(W8ScreenId id, GameSession& games);

/* Screen routing. Requests and returns take effect on the next GameLoop step:
   the current screen leaves, then the destination enters. A request suspends
   the current screen under the destination unless it asks to discard it; a
   return discards the current screen and resumes the most recently suspended
   one. Returning with nothing suspended, or a request that fails to enter, ends
   the program. */
/* Routing starts afresh, building screens with `factory` and handing them `games`, which
   the caller keeps alive while it routes. */
void ResetScreenRouting(GameSession& games, ScreenFactory factory = CreateScreen);
void RequestScreen(ScreenRequest request, ScreenDeparture departure = ScreenDeparture::Suspend);
void ReturnToPreviousScreen();
/* The request the current screen was entered with. */
const ScreenRequest& CurrentScreenState();
Screen* CurrentScreen();
/* The current or a suspended screen with this id, or null. Screens rebuilt on
   every visit exist only while they are current. */
Screen* FindScreen(W8ScreenId id);
template<class T> T* FindScreen()
{
    return static_cast<T*>(FindScreen(T::id));
}
/* The screen that was current before the last transition. */
W8ScreenId PreviousScreen();
/* The screen most recently suspended by a request. */
W8ScreenId SuspendedScreen();
/* The screen the next transition enters: the requested one, else the one a
   return would resume. */
W8ScreenId PendingScreen();
std::size_t SuspendedScreenCount();
bool IsScreenTransitionPending();

/* One step of the game: service audio, apply a pending transition, then run
   the current screen's frame. */
void GameLoop();
/* Discard every screen, current and suspended, without resuming any. */
void GameloopExit();
