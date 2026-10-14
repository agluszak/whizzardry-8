#include <cstddef>
#include <vector>

#include "wiz8/music_playlist.h"
#include "layouts/game_status.h"
#include "soundman.h"
#ifdef WIZ8_RUNTIME_TESTS
#include "runtime_instrumentation.h"
#endif
#include "wiz8/character_event_queue.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/xstatus.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/ui_input.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_screens/IntroScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/AutomapScreen.h"
#include "wiz8/local_screens/CreditsScreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/fonts.h"
#include "wiz8/sr_api.h"
#include "Font.h"
#include "wiz8/application.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/ReadMesh.h"
#include "wiz8/local_code/CombatSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/engine_code/Missile.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayInit.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/monster_generators.h"

/* GameloopExit names this unit; GameLoop is placed with that companion. */

// GLOBAL: WIZ8 0x0068ec78
W8ScreenStateRuntime g_current_screen_state;
// GLOBAL: WIZ8 0x0068ed10
W8ScreenStateRuntime g_pending_screen_state;
// GLOBAL: WIZ8 0x0068edac
bool g_screen_return_requested;
// GLOBAL: WIZ8 0x0068eda8
std::vector<W8ScreenStateRuntime> g_screen_return_stack;

// GLOBAL: WIZ8 0x00647bc8
W8ScreenStateHandlers g_screen_handlers[W8_SCREEN_COUNT] = {
    {ScreenLifecycleSuccess, IntroScreenEnter, IntroScreenFrame, IntroScreenLeave, ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, MainMenuScreenEnter, MainMenuScreenFrame, MainMenuScreenLeave, ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, ScreenLifecycleSuccess, GameStartRouterFrame, GameStartRouterLeave,
     ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, CharacterScreenEnter, CharacterScreenFrame, CharacterScreenLeave, ScreenLifecycleSuccess},
    {PleaseWaitScreenInitialize, PleaseWaitScreenEnter, PleaseWaitScreenFrame, PleaseWaitScreenLeave,
     ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, PartySelectionScreenEnter, PartySelectionScreenFrame, PartySelectionScreenLeave,
     ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, CampScreenEnter, CampScreenFrame, CampScreenLeave, ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, MainGameScreenEnter, MainGameScreenFrame, MainGameScreenLeave, ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, AutomapScreenEnter, AutomapScreenFrame, AutomapScreenLeave, AutomapScreenFinalize},
    {ScreenLifecycleSuccess, CreditsScreenEnter, CreditsScreenFrame, CreditsScreenLeave, ScreenLifecycleSuccess},
    {ScreenLifecycleSuccess, OptionsScreenEnter, OptionsScreenFrame, OptionsScreenLeave, ScreenLifecycleSuccess},
    {JournalScreenInitialize, JournalScreenEnter, JournalScreenFrame, JournalScreenLeave, JournalScreenFinalize},
    {ScreenLifecycleSuccess, ExitScreenEnter, ExitScreenFrame, MainMenuScreenLeave, ScreenLifecycleSuccess}};
// GLOBAL: WIZ8 0x00647bc0
W8ScreenId g_previous_screen_id = W8_SCREEN_NONE;
// GLOBAL: WIZ8 0x00647bc4
W8ScreenId g_suspended_screen_id = W8_SCREEN_NONE;

// FUNCTION: WIZ8 0x004e3290
void ShutdownGame(size_t initialized_screens, bool save_configuration)
{
    int index;

    if (gXStatus.character_event_queue) {
        gXStatus.character_event_queue->DestroyAllEvents();
    }
    ReleaseHitSoundDatabase();
    ReleaseGenericItemNames();
    ReleaseNpcStates();
    UnloadEncounterTables();
    ReleaseMissileDatabase();
    ReleaseSpellDatabase();
    DestroyItemDatabase();
    DestroyItemTables();
    FreeIfNotNull(0);
    DestroyNpcDatabase();
    DestroyFactDatabase();
    DestroyLevelDatabase();
    g_font_state_palettes = {};
    for (index = 0; index < static_cast<int>(initialized_screens); ++index) {
        g_screen_handlers[index].finalize();
    }
    g_screen_return_stack.clear();
    ReleaseAllTriggers();
    DestroyGameplayObjects();
    ReleaseReadMeshScratch();
    ClearNpcMessageQueue();
    ClearNpcDialogueTranscript();
    ReleaseMessageStorage();
    FreeStringTable();
    g_status.buffers = {};
    if (save_configuration) {
        SaveGameConfiguration();
    }
}

/* Screens own their input layers. A suspended screen keeps its layers, so
   every screen boundary disables all input; the entering screen enables its own.
   The main game also shows tooltips outside the dispatcher, so remove any. */
static bool EnterScreen(W8ScreenId state)
{
    VideoRemoveToolTip();
    ResetUiInput();
    return g_screen_handlers[state].enter();
}

// FUNCTION: WIZ8 0x004e3340
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
        if (static_cast<unsigned char>(static_cast<int>(gXStatus.character_event_queue->active_events.size())) != 0) {
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
    g_screen_handlers[state].frame();
}

// FUNCTION: WIZ8 0x004e34b0
void GameloopExit(unsigned char release_screens)
{
    W8ScreenId state;

    // Complete voice events before leaving gameplay unloads its UI.
    if (gXStatus.character_event_queue) {
        gXStatus.character_event_queue->DestroyAllEvents();
    }

    g_smfnt_font.resetPalette();
    g_calligraphy_font.resetPalette();
    g_calligraphy_shadow_font.resetPalette();
    g_wiz_text_font.resetPalette();
    g_button_font.resetPalette();
    g_wiz_text_font_secondary.resetPalette();
    g_wiz_text_bold_font.resetPalette();
    g_options_detail_font.resetPalette();
    if (!release_screens) {
        return;
    }
    for (;;) {
        if (g_current_screen_state.id != W8_SCREEN_NONE) {
            g_previous_screen_id = g_current_screen_state.id;
            g_screen_handlers[g_current_screen_state.id].leave(1);
            g_current_screen_state.id = W8_SCREEN_NONE;
        }
        if (g_screen_return_stack.empty()) {
            break;
        }
        g_pending_screen_state = g_screen_return_stack.back();
        g_screen_return_stack.pop_back();
        if (g_pending_screen_state.id != W8_SCREEN_NONE) {
            if (g_current_screen_state.id != W8_SCREEN_NONE) {
                srAssertFail("gCurrentScreen.iScreenId == NO_SCREEN",
                             "C:\\Projects\\Wizardry 8\\Local Code\\Gameloop.cpp", 0x276, 0);
            }
            state = g_pending_screen_state.id;
            g_current_screen_state = g_pending_screen_state;
            if (!EnterScreen(state)) {
                g_current_screen_state.id = W8_SCREEN_NONE;
            } else {
                g_pending_screen_state.id = W8_SCREEN_NONE;
#ifdef WIZ8_RUNTIME_TESTS
                RuntimeObserve(RUNTIME_SCREEN_CHANGED, g_previous_screen_id, state, -1);
                if (state == W8_SCREEN_MAIN_GAME) {
                    RuntimeObserve(RUNTIME_MAIN_GAME_ENTERED, state, 0, 0);
                }
#endif
            }
        }
    }
}
