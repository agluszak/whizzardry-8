#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/application.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/music_playlist.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

namespace {
/* A screen waiting under the current one, with the request it resumes with. A
   screen rebuilt on every visit leaves only its request. */
struct SuspendedEntry {
    ScreenRequest request;
    std::unique_ptr<Screen> screen;
};

struct ScreenRouter {
    ScreenFactory factory = CreateScreen;
    // The session the screens begin and end games in.
    GameSession* games = nullptr;
    ScreenRequest current;
    std::unique_ptr<Screen> screen;
    ScreenRequest pending;
    /* The suspended screen a return popped, entered with `pending`. */
    std::unique_ptr<Screen> resuming;
    bool return_requested = false;
    std::vector<SuspendedEntry> suspended;
    W8ScreenId previous = W8_SCREEN_NONE;
    W8ScreenId last_suspended = W8_SCREEN_NONE;

    void show(ScreenRequest request) { current = std::move(request); }

    // No screen is current until the next one enters.
    void show_none() { current.id = W8_SCREEN_NONE; }

    void stop()
    {
        gfProgramIsRunning = false;
        show_none();
        screen.reset();
    }
};
ScreenRouter router;

/* Screens own their input layers. The entering screen becomes the input
   owner; a suspended screen's layers keep their state but receive nothing.
   The main game also shows tooltips outside the dispatcher, so remove any. */
bool EnterScreen()
{
    VideoRemoveToolTip();
    SelectUiInputOwner(router.current.id);
    if (router.resuming) {
        router.screen = std::move(router.resuming);
        return router.screen->resume(router.current);
    }
    router.screen = router.factory(router.current.id, *router.games);
    return router.screen->enter(router.current);
}

/* Apply a requested return: the current screen is discarded and, without an
   explicit request, the most recently suspended screen becomes pending. */
bool ApplyReturn()
{
    router.previous = router.current.id;
    const bool continuing = router.screen->leave(ScreenDeparture::Discard);
    router.screen.reset();
    router.show_none();
    if (!continuing) return false;
    if (router.pending.id == W8_SCREEN_NONE) {
        if (router.suspended.empty()) return false;
        auto& top = router.suspended.back();
        router.pending = std::move(top.request);
        router.resuming = std::move(top.screen);
        router.suspended.pop_back();
    }
    router.return_requested = false;
    return true;
}

/* Apply a pending request: suspend the current screen, then enter the
   destination. */
bool ApplyRequest()
{
    /* Retail tests only the low byte of the count. */
    if (auto* game = router.games->game();
        game && static_cast<unsigned char>(game->speech.active_events.size()) != 0) {
        game->speech.CompleteFirstActiveEvent();
    }
    if (router.current.id != W8_SCREEN_NONE) {
        router.previous = router.current.id;
        if (!router.screen->leave(ScreenDeparture::Suspend)) return false;
        router.last_suspended = router.current.id;
        if (auto rebuild = router.screen->rebuildRequest(router.current)) {
            router.screen.reset();
            router.suspended.push_back({std::move(*rebuild), nullptr});
        } else {
            router.suspended.push_back({std::move(router.current), std::move(router.screen)});
        }
    }
    router.show(std::exchange(router.pending, {}));
    return EnterScreen();
}
} // namespace

void ResetScreenRouting(GameSession& games, ScreenFactory factory)
{
    router = {};
    router.factory = factory;
    router.games = &games;
}

void RequestScreen(ScreenRequest request, ScreenDeparture departure)
{
    router.pending = std::move(request);
    if (departure == ScreenDeparture::Discard) router.return_requested = true;
}

void ReturnToPreviousScreen()
{
    router.return_requested = true;
}

const ScreenRequest& CurrentScreenState()
{
    return router.current;
}

Screen* CurrentScreen()
{
    return router.screen.get();
}

Screen* FindScreen(W8ScreenId id)
{
    if (router.current.id == id) return router.screen.get();
    for (const auto& entry : router.suspended) {
        if (entry.request.id == id && entry.screen) return entry.screen.get();
    }
    return nullptr;
}

W8ScreenId PreviousScreen()
{
    return router.previous;
}

W8ScreenId SuspendedScreen()
{
    return router.last_suspended;
}

W8ScreenId PendingScreen()
{
    if (router.pending.id != W8_SCREEN_NONE) return router.pending.id;
    return router.suspended.empty() ? W8_SCREEN_NONE : router.suspended.back().request.id;
}

std::size_t SuspendedScreenCount()
{
    return router.suspended.size();
}

bool IsScreenTransitionPending()
{
    return router.pending.id != W8_SCREEN_NONE || router.return_requested;
}

void GameLoop()
{
    wiz8::sound::service();
    ServiceMusicPlaylist();
    if (router.return_requested && !ApplyReturn()) {
        router.stop();
        return;
    }
    /* A request for the screen that is already current waits. */
    if (router.pending.id != W8_SCREEN_NONE && router.pending.id != router.current.id &&
        !ApplyRequest()) {
        router.stop();
        return;
    }
    if (router.current.id == W8_SCREEN_NONE) {
        router.stop();
        return;
    }
    router.screen->frame();
}

void GameloopExit()
{
    // Complete voice events before leaving gameplay unloads its UI.
    if (auto* game = router.games ? router.games->game() : nullptr) game->speech.DestroyAllEvents();

    ResetMenuFontPalettes();
    // Discard the current screen, then every suspended one without
    // resuming it: a full leave releases what a suspended screen retains.
    if (router.screen) router.screen->leave(ScreenDeparture::Discard);
    router.screen.reset();
    router.resuming.reset();
    while (!router.suspended.empty()) {
        auto entry = std::move(router.suspended.back());
        router.suspended.pop_back();
        if (!entry.screen) continue;
        /* Teardown reads the current screen, as it does for the one on top, so the
           leaving screen becomes current and can still find itself. */
        router.show(std::move(entry.request));
        router.screen = std::move(entry.screen);
        router.screen->leave(ScreenDeparture::Discard);
        router.screen.reset();
    }
    router.show({});
    router.pending = {};
    router.return_requested = false;
}
