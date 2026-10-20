#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/application.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/fonts.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/music_playlist.h"
#include "wiz8/render/frame.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

namespace {
/* A screen waiting under the current one, with the request it resumes with. */
struct SuspendedEntry {
    ScreenRequest request;
    std::unique_ptr<Screen> screen;
};

struct ScreenRouter {
    ScreenFactory factory = CreateScreen;
    ScreenRequest current;
    std::unique_ptr<Screen> screen;
    ScreenRequest pending;
    /* The suspended screen a return popped, entered with `pending`. */
    std::unique_ptr<Screen> resuming;
    bool return_requested = false;
    std::vector<SuspendedEntry> suspended;
    W8ScreenId previous = W8_SCREEN_NONE;
    W8ScreenId last_suspended = W8_SCREEN_NONE;

    void stop()
    {
        gfProgramIsRunning = false;
        current.id = W8_SCREEN_NONE;
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
    router.screen = router.factory(router.current.id);
    return router.screen->enter(router.current);
}

/* Apply a requested return: the current screen is discarded and, without an
   explicit request, the most recently suspended screen becomes pending. */
bool ApplyReturn()
{
    router.previous = router.current.id;
    const bool continuing = router.screen->leave(ScreenDeparture::Discard);
    router.screen.reset();
    router.current.id = W8_SCREEN_NONE;
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
    if (static_cast<unsigned char>(gXStatus.character_event_queue->active_events.size()) != 0) {
        gXStatus.character_event_queue->CompleteFirstActiveEvent();
    }
    if (router.current.id != W8_SCREEN_NONE) {
        router.previous = router.current.id;
        if (!router.screen->leave(ScreenDeparture::Suspend)) return false;
        router.last_suspended = router.current.id;
        router.suspended.push_back({std::move(router.current), std::move(router.screen)});
    }
    router.current = std::exchange(router.pending, {});
    return EnterScreen();
}
} // namespace

void ResetScreenRouting(ScreenFactory factory)
{
    router = {};
    router.factory = factory;
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
        if (entry.request.id == id) return entry.screen.get();
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
    SyncWorldAdvancement();
    router.screen->frame();
}

void GameloopExit()
{
    // Complete voice events before leaving gameplay unloads its UI.
    if (gXStatus.character_event_queue) gXStatus.character_event_queue->DestroyAllEvents();

    ResetMenuFontPalettes();
    // Discard the current screen, then every suspended one without
    // resuming it: a full leave releases what a suspended screen retains.
    if (router.screen) router.screen->leave(ScreenDeparture::Discard);
    router.screen.reset();
    router.resuming.reset();
    while (!router.suspended.empty()) {
        auto entry = std::move(router.suspended.back());
        router.suspended.pop_back();
        // Teardown reads the current screen id, as it does for the one on top.
        router.current = std::move(entry.request);
        entry.screen->leave(ScreenDeparture::Discard);
    }
    router.current = {};
    router.pending = {};
    router.return_requested = false;
}
