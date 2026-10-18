#include <cstddef>
#include <vector>

#include "layouts/game_status.h"
#include "soundman.h"
#include "wiz8/music_playlist.h"
#ifdef WIZ8_RUNTIME_TESTS
#include "runtime_instrumentation.h"
#endif
#include "Font.h"
#include "wiz8/application.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Video2.h"
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
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

/* GameloopExit names this unit; GameLoop is placed with that companion. */

W8ScreenStateRuntime g_current_screen_state;
W8ScreenStateRuntime g_pending_screen_state;
bool g_screen_return_requested;
std::vector<W8ScreenStateRuntime> g_screen_return_stack;

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
W8ScreenId g_previous_screen_id = W8_SCREEN_NONE;
W8ScreenId g_suspended_screen_id = W8_SCREEN_NONE;

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
    g_screen_return_stack.clear();
    ClearLocationVariables();
    DestroyGameplayObjects();
    ClearNpcMessageQueue();
    ClearNpcDialogueTranscript();
    ReleaseMessageStorage();
    FreeStringTable();
    g_status.buffers = {};
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
    state = g_current_screen_state.id;
    if (g_screen_return_requested) {
        g_previous_screen_id = state;
        if (!g_screen_handlers[g_current_screen_state.id].leave(1)) {
            gfProgramIsRunning = 0;
            g_current_screen_state.id = W8_SCREEN_NONE;
            return;
        }
        g_current_screen_state.id = W8_SCREEN_NONE;
        if (g_pending_screen_state.id == W8_SCREEN_NONE) {
            if (g_screen_return_stack.empty()) {
                gfProgramIsRunning = 0;
                return;
            }
            g_pending_screen_state = g_screen_return_stack.back();
            g_screen_return_stack.pop_back();
        }
        state = W8_SCREEN_NONE;
        g_current_screen_state.id = state;
        g_screen_return_requested = false;
    }
    if (g_pending_screen_state.id != W8_SCREEN_NONE && g_pending_screen_state.id != state) {
        /* Retail tests only the low byte of the count. */
        if (static_cast<unsigned char>(
                static_cast<int>(gXStatus.character_event_queue->active_events.size())) != 0) {
            gXStatus.character_event_queue->CompleteFirstActiveEvent();
            state = g_current_screen_state.id;
        }
        if (state != W8_SCREEN_NONE) {
            g_previous_screen_id = state;
            if (!g_screen_handlers[g_current_screen_state.id].leave(0)) {
                g_current_screen_state.id = W8_SCREEN_NONE;
                gfProgramIsRunning = 0;
                return;
            }
            g_suspended_screen_id = g_current_screen_state.id;
            g_screen_return_stack.push_back(g_current_screen_state);
        }
        state = g_pending_screen_state.id;
        g_current_screen_state = g_pending_screen_state;
        if (!EnterScreen(state)) {
            g_current_screen_state.id = W8_SCREEN_NONE;
            gfProgramIsRunning = 0;
            return;
        }
        state = g_current_screen_state.id;
        g_pending_screen_state.id = W8_SCREEN_NONE;
#ifdef WIZ8_RUNTIME_TESTS
        RuntimeObserve(RUNTIME_SCREEN_CHANGED, g_previous_screen_id, state, -1);
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
    if (g_current_screen_state.id != W8_SCREEN_NONE) {
        g_screen_handlers[g_current_screen_state.id].leave(1);
    }
    while (!g_screen_return_stack.empty()) {
        g_current_screen_state = g_screen_return_stack.back();
        g_screen_return_stack.pop_back();
        if (g_current_screen_state.id != W8_SCREEN_NONE) {
            g_screen_handlers[g_current_screen_state.id].leave(1);
        }
    }
    g_current_screen_state.id = W8_SCREEN_NONE;
    g_pending_screen_state.id = W8_SCREEN_NONE;
}
