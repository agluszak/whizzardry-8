#include <cstddef>
#include <utility>
#include <vector>

#include "layouts/game_status.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/music_playlist.h"
#ifdef WIZ8_RUNTIME_TESTS
#include "runtime_instrumentation.h"
#endif
#include "wiz8/application.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_generators.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

/* GameloopExit names this unit; GameLoop is placed with that companion. */

namespace {
struct ScreenRouting {
    W8ScreenStateRuntime current;
    W8ScreenStateRuntime pending;
    bool return_requested = false;
    std::vector<W8ScreenStateRuntime> return_stack;
    W8ScreenId previous = W8_SCREEN_NONE;
    W8ScreenId suspended = W8_SCREEN_NONE;
};
ScreenRouting routing;
}

void ResetScreenRouting()
{
    routing = {};
}
void RequestScreen(W8ScreenStateRuntime request, ScreenDeparture departure)
{
    routing.pending = std::move(request);
    if (departure == ScreenDeparture::Discard) routing.return_requested = true;
}
void ReturnToPreviousScreen()
{
    routing.return_requested = true;
}
const W8ScreenStateRuntime& CurrentScreenState()
{
    return routing.current;
}
W8ScreenId PreviousScreen()
{
    return routing.previous;
}
W8ScreenId SuspendedScreen()
{
    return routing.suspended;
}
W8ScreenId RequestedScreen()
{
    return routing.pending.id;
}
std::size_t SuspendedScreenCount()
{
    return routing.return_stack.size();
}
bool IsScreenTransitionPending()
{
    return routing.pending.id != W8_SCREEN_NONE || routing.return_requested;
}
W8ScreenId GetPendingScreenState()
{
    if (routing.pending.id != W8_SCREEN_NONE) return routing.pending.id;
    return routing.return_stack.empty() ? W8_SCREEN_NONE : routing.return_stack.back().id;
}

W8ScreenStateHandlers g_screen_handlers[W8_SCREEN_COUNT] = {
    {.enter = IntroScreenEnter, .frame = IntroScreenFrame, .leave = IntroScreenLeave},
    {.enter = MainMenuScreenEnter, .frame = MainMenuScreenFrame, .leave = MainMenuScreenLeave},
    {.frame = GameStartRouterFrame, .leave = GameStartRouterLeave},
    {.enter = CharacterScreenEnter, .frame = CharacterScreenFrame, .leave = CharacterScreenLeave},
    {.initialize = PleaseWaitScreenInitialize,
     .enter = PleaseWaitScreenEnter,
     .frame = PleaseWaitScreenFrame,
     .leave = PleaseWaitScreenLeave},
    {.enter = PartySelectionScreenEnter,
     .frame = PartySelectionScreenFrame,
     .leave = PartySelectionScreenLeave},
    {.enter = CampScreenEnter, .frame = CampScreenFrame, .leave = CampScreenLeave},
    {.enter = MainGameScreenEnter, .frame = MainGameScreenFrame, .leave = MainGameScreenLeave},
    {.enter = AutomapScreenEnter,
     .frame = AutomapScreenFrame,
     .leave = AutomapScreenLeave,
     .finalize = AutomapScreenFinalize},
    {.enter = CreditsScreenEnter, .frame = CreditsScreenFrame, .leave = CreditsScreenLeave},
    {.enter = OptionsScreenEnter, .frame = OptionsScreenFrame, .leave = OptionsScreenLeave},
    {.initialize = JournalScreenInitialize,
     .enter = JournalScreenEnter,
     .frame = JournalScreenFrame,
     .leave = JournalScreenLeave,
     .finalize = JournalScreenFinalize},
    {.enter = ExitScreenEnter, .frame = ExitScreenFrame, .leave = MainMenuScreenLeave},
};

void ShutdownGame(size_t initialized_screens, bool save_configuration)
{
    int index;

    if (gXStatus.character_event_queue) gXStatus.character_event_queue->DestroyAllEvents();
    ReleaseHitSoundDatabase();
    ReleaseGenericItemNames();
    ReleaseNpcStates();
    UnloadEncounterTables();
    ReleaseSpellDatabase();
    DestroyItemDatabase();
    DestroyItemTables();
    DestroyNpcDatabase();
    DestroyFactDatabase();
    DestroyLevelDatabase();
    g_font_state_palettes = {};
    for (index = 0; index < static_cast<int>(initialized_screens); ++index) {
        if (g_screen_handlers[index].finalize) g_screen_handlers[index].finalize();
    }
    routing.return_stack.clear();
    ClearLocationVariables();
    DestroyGameplayObjects();
    ClearNpcMessageQueue();
    ClearNpcDialogueTranscript();
    ReleaseMessageStorage();
    FreeStringTable();
    g_status.party = {};
    if (save_configuration) SaveGameConfiguration();
}

/* Screens own their input layers. The entering screen becomes the input
   owner; a suspended screen's layers keep their state but receive nothing.
   The main game also shows tooltips outside the dispatcher, so remove any. */
static bool EnterScreen(W8ScreenId state)
{
    VideoRemoveToolTip();
    SelectUiInputOwner(state);
    const auto enter = g_screen_handlers[state].enter;
    return !enter || enter();
}

void GameLoop(void)
{
    W8ScreenId state;

    wiz8::sound::service();
    ServiceMusicPlaylist();
    state = routing.current.id;
    if (routing.return_requested) {
        routing.previous = state;
        if (!g_screen_handlers[routing.current.id].leave(1)) {
            gfProgramIsRunning = 0;
            routing.current.id = W8_SCREEN_NONE;
            return;
        }
        routing.current.id = W8_SCREEN_NONE;
        if (routing.pending.id == W8_SCREEN_NONE) {
            if (routing.return_stack.empty()) {
                gfProgramIsRunning = 0;
                return;
            }
            routing.pending = routing.return_stack.back();
            routing.return_stack.pop_back();
        }
        state = W8_SCREEN_NONE;
        routing.current.id = state;
        routing.return_requested = false;
    }
    if (routing.pending.id != W8_SCREEN_NONE && routing.pending.id != state) {
        /* Retail tests only the low byte of the count. */
        if (static_cast<unsigned char>(
                static_cast<int>(gXStatus.character_event_queue->active_events.size())) != 0) {
            gXStatus.character_event_queue->CompleteFirstActiveEvent();
            state = routing.current.id;
        }
        if (state != W8_SCREEN_NONE) {
            routing.previous = state;
            if (state == W8_SCREEN_CAMP && g_camp_screen) {
                routing.current.character = g_camp_screen->reviewed;
            }
            if (!g_screen_handlers[routing.current.id].leave(0)) {
                routing.current.id = W8_SCREEN_NONE;
                gfProgramIsRunning = 0;
                return;
            }
            routing.suspended = routing.current.id;
            routing.return_stack.push_back(routing.current);
        }
        state = routing.pending.id;
        routing.current = routing.pending;
        if (!EnterScreen(state)) {
            routing.current.id = W8_SCREEN_NONE;
            gfProgramIsRunning = 0;
            return;
        }
        state = routing.current.id;
        routing.pending.id = W8_SCREEN_NONE;
#ifdef WIZ8_RUNTIME_TESTS
        RuntimeObserve(RUNTIME_SCREEN_CHANGED, routing.previous, state, -1);
        if (state == W8_SCREEN_MAIN_GAME) {
            RuntimeObserve(RUNTIME_MAIN_GAME_ENTERED, state, 0, 0);
        }
#endif
    }
    if (state == W8_SCREEN_NONE) {
        gfProgramIsRunning = 0;
        return;
    }
    SyncWorldAdvancement();
    g_screen_handlers[state].frame();
}

void GameloopExit()
{
    // Complete voice events before leaving gameplay unloads its UI.
    if (gXStatus.character_event_queue) gXStatus.character_event_queue->DestroyAllEvents();

    g_smfnt_font.resetPalette();
    g_calligraphy_font.resetPalette();
    g_calligraphy_shadow_font.resetPalette();
    g_wiz_text_font.resetPalette();
    g_button_font.resetPalette();
    g_wiz_text_font_secondary.resetPalette();
    g_wiz_text_bold_font.resetPalette();
    g_options_detail_font.resetPalette();
    // Discard the current screen, then every suspended one without
    // re-entering it: a full leave releases what a suspended screen retains.
    if (routing.current.id != W8_SCREEN_NONE) {
        g_screen_handlers[routing.current.id].leave(1);
    }
    while (!routing.return_stack.empty()) {
        routing.current = routing.return_stack.back();
        routing.return_stack.pop_back();
        if (routing.current.id != W8_SCREEN_NONE) {
            g_screen_handlers[routing.current.id].leave(1);
        }
    }
    routing.current.id = W8_SCREEN_NONE;
    routing.pending.id = W8_SCREEN_NONE;
}
