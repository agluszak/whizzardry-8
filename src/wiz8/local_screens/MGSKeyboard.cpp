#include "wiz8/local_code/ButtonSound.h"
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <exception>
#include <string>

#include "wiz8/fonts.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "input.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/Cursor3d.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/ui_layer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/Traps.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/mipe.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/ui_input.h"
#include "wiz8/text_input.h"
#include "wiz8/version.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"
#include "wiz8/filesystem.h"
#include "camera_motion.h"
#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "layouts/screen_state.h"
#include "layouts/targeting.h"
#include "mgs_commands.h"
#include "surrender/srMath.h"

struct W8ItemInstance;

/* Local Screens\MGSKeyboard.cpp owns the binding vector, its command-keyed
   lookup and the singleton ResetMGSKeyboardBindings installs.
   MGSKeyboard::LoadDefaults moved to Local Code\InputMapper.cpp. */

// GLOBAL: WIZ8 0x0069b7e4
MGSKeyboard* g_mgs_keyboard;

// Retail set 0x26, regions 180-193.

// FUNCTION: WIZ8 0x0055d260
int MGSKeyboard::FindBinding(W8MGSCommand command) const
{
    int count = static_cast<int>(m_bindings.size());
    int index;
    for (index = 0; index < count; ++index) {
        MGSKeyBinding* binding = m_bindings[index].get();
        if (binding->command == command) {
            return index;
        }
    }
    return -1;
}

// FUNCTION: WIZ8 0x0055d2a0
W8MGSCommand MGSKeyboard::FindCommandForEvent(const InputEvent* event) const
{
    int count = static_cast<int>(m_bindings.size());
    int index;
    for (index = 0; index < count; ++index) {
        MGSKeyBinding* binding = m_bindings[index].get();
        if (binding->active != 0 && event->type == InputEventType::KeyDown &&
            binding->modifiers == event->modifiers &&
            binding->key == event->key) {
            return binding->command;
        }
    }
    return W8_MGS_COMMAND_NONE;
}

// FUNCTION: WIZ8 0x0055d300
MGSKeyBinding* MGSKeyboard::GetBinding(int index) const
{
    if (index < 0 || m_bindings.empty()) {
        return nullptr;
    }
    // Preserve the binding table's fallback to its first entry.
    if (std::cmp_greater_equal(index, m_bindings.size())) {
        return m_bindings.front().get();
    }
    return m_bindings[index].get();
}

// FUNCTION: WIZ8 0x0055d320
bool MGSKeyboard::IsCommandPressed(W8MGSCommand command) const
{
    const auto found = m_command_index.find(command);
    if (found == m_command_index.end()) {
        return false;
    }
    const auto* binding = found->second;
    if (IsKeyHeld(binding->key)) {
        unsigned short modifiers = 0;
        if (IsKeyHeld(VK_SHIFT)) {
            modifiers |= SHIFT_DOWN;
        }
        if (IsKeyHeld(VK_MENU)) {
            modifiers |= ALT_DOWN;
        }
        if (IsKeyHeld(VK_CONTROL)) {
            modifiers |= CTRL_DOWN;
        }
        if (modifiers == binding->modifiers) {
            return true;
        }
    }
    return false;
}

/* Discard every queued input atom. MainGameScreenEnter calls this so stale
   presses do not fire against the freshly entered screen state. */
// FUNCTION: WIZ8 0x0055D3C0
void DrainInputEventQueue(void)
{
    InputEvent event;

    while (DequeueEvent(&event)) {
    }
}

// FUNCTION: WIZ8 0x0055D3F0
void MGSKeyboard::Clear()
{
    m_command_index.clear();
    m_bindings.clear();
}

// FUNCTION: WIZ8 0x0055d590
unsigned char MGSKeyboard::Load(wiz8::File& handle, bool clear)
try
{
    int count;

    if (clear) {
        Clear();
    }
    handle.read_exact(&count, sizeof(count));
    for (int index = 0; index < count; ++index) {
        auto binding = std::make_unique<MGSKeyBinding>();
        handle.read_exact(binding.get(), sizeof(*binding));

        int old_index = FindBinding(binding->command);
        if (old_index != -1) {
            m_command_index.erase(binding->command);
            m_bindings.erase(m_bindings.begin() + old_index);
        }
        auto* borrowed = binding.get();
        m_bindings.push_back(std::move(binding));
        // Copy the packed disk field before unordered_map binds its arguments.
        m_command_index.emplace(static_cast<unsigned int>(borrowed->command), borrowed);
    }
    return 1;
}
catch (const std::exception&) { return false; }

// FUNCTION: WIZ8 0x0055d7a0
unsigned char MGSKeyboard::Save(wiz8::File& handle) const
{
    int count = static_cast<int>(m_bindings.size());
    handle.write(&count, sizeof(count));
    for (int index = 0; index < count; ++index) {
        handle.write(m_bindings[index].get(), sizeof(MGSKeyBinding));
    }
    return 1;
}

// FUNCTION: WIZ8 0x00592BE0
void ResetMGSKeyboardBindings()
{
    if (g_mgs_keyboard == 0) {
        g_mgs_keyboard = new MGSKeyboard;
    } else {
        g_mgs_keyboard->Clear();
    }
    g_mgs_keyboard->LoadDefaults("Data\\Strings\\MGSKeyboard.ini");
}


/* Route one non-mouse input atom: text entry, NPC dialogue and the trap text
   box consume key presses first; the MIPE editor and the record-mode console
   gate on their flags; everything else resolves to a bound MGS command.
   Attribution-gap body leading the MGSKeyboard.cpp hull at 0x00591960. */
// FUNCTION: WIZ8 0x00591890
unsigned char HandleMainGameInputEvent(const InputEvent* input)
{
    auto* text_fields = TextFieldGroup::Focused();
    if (IsTypingEvent(*input) && text_fields && text_fields->HandleInput(*input)) {
        return 1;
    }
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) && gXStatus.fNpcDialogueMode) {
        HandleNpcDialogueKeyEvent(input);
        return 1;
    }
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) &&
        gXStatus.fTrapInteractMode && TextBoxHandleKey(input) != 0) {
        return 1;
    }
    if (IsMipeActive() && HandleMipeKey(input) != 0) {
        return 1;
    }
    if (IsRecordModeActive()) {
        if (!IsKeyHeld(VK_CONTROL) && HandleRecordModeKey(input, PromptRecordModeEntry) == 1) {
            ApplyRecordModeLine();
            return 1;
        }
    } else {
        DispatchMGSCommand(g_mgs_keyboard->FindCommandForEvent(input));
    }
    return 1;
}

// FUNCTION: WIZ8 0x00591960
void DispatchMGSCommand(W8MGSCommand command)
{
    switch (command) {
    case W8_MGS_COMMAND_CANCEL:
        if (IsWorldCursorVisible()) {
            ToggleWorldCursor();
        } else if (gXStatus.fSurprisePossible) {
            AcknowledgeSurprise();
        } else if (gXStatus.fSpellCastMode) {
            ResetSpellCastingSelection();
        } else if (gXStatus.fLockInteractMode) {
            EndLockInteractMode(0);
        } else if (gXStatus.fItemSelectMode) {
            CloseUseItemSelection();
        } else if (gXStatus.iTargetingMode != W8_TARGET_NEED_NONE) {
            SetTargetingMode(W8_TARGET_NEED_NONE);
        } else if (gXStatus.fReviewCharacterMode) {
            CloseFormationPanel();
        } else if (IsNpcScriptSessionActive()) {
            TryFinishNpcVoicePlayback(true);
        } else if (gXStatus.character_event_queue->HasActiveEvents()) {
            gXStatus.character_event_queue->CompleteFirstActiveEvent();
        } else {
            if (gXStatus.fCombatMode) {
                ToggleCombatMode();
                if (!gXStatus.fCombatMode) {
                    InvalidateRegion(0xa8, 0x16e, 0x1c4, 0x1ba);
                    return;
                }
            }
            ShowMainGameNoticeLine(g_localized_strings[0x779], OnLeaveGameConfirmClosed, true, true);
        }
        InvalidateRegion(0xa8, 0x16e, 0x1c4, 0x1ba);
        break;
    case W8_MGS_COMMAND_QUIT_GAME:
        ShowMainGameNoticeLine(g_localized_strings[0x832], OnQuitGameDialogClosed, true, true);
        break;
    case W8_MGS_COMMAND_TOGGLE_FULLSCREEN:
        ResetTransientRenderScenes();
        SetOverlayRenderMode();
        SetRendererModePair();
        VideoFullScreen(VideoIsFullScreen() == 0);
        break;
    case W8_MGS_COMMAND_LOAD_OPTIONS:
        if (IsScreenInputBlocked() == 0 && CanInterruptLevelMovement()) {
            g_pending_screen_state.mode = 1;
            SetPendingScreenState(W8_SCREEN_OPTIONS);
        }
        break;
    case W8_MGS_COMMAND_SAVE_OPTIONS:
        if (IsScreenInputBlocked() == 0 && CanInterruptLevelMovement()) {
            g_pending_screen_state.mode = 2;
            SetPendingScreenState(W8_SCREEN_OPTIONS);
        }
        break;
    case W8_MGS_COMMAND_COMBAT_DELAY_UP: {
        unsigned int delay = g_settings.combat_delay_ms;
        if (delay < 0x1388) {
            if (delay % 0xfa == 0) {
                delay += 0xfa;
            } else {
                delay += 0xfa - delay % 0xfa;
            }
            g_settings.combat_delay_ms = delay;
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} {:d}", g_localized_strings[0x7ba], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", g_localized_strings[0x7bb], 0x14 - delay / 0xfa));
        }
        break;
    }
    case W8_MGS_COMMAND_COMBAT_DELAY_DOWN: {
        unsigned int delay = g_settings.combat_delay_ms;
        if (delay > 0) {
            if (delay % 0xfa == 0) {
                delay -= 0xfa;
            } else {
                delay -= delay % 0xfa;
            }
            g_settings.combat_delay_ms = delay;
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} {:d}", g_localized_strings[0x7ba], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", g_localized_strings[0x7bc], 0x14 - delay / 0xfa));
        }
        break;
    }
    case W8_MGS_COMMAND_TEXT_DELAY_UP: {
        unsigned int delay = g_settings.text_display_delay_ms;
        if (delay < 0x1388) {
            if (delay % 0xfa == 0) {
                delay += 0xfa;
            } else {
                delay += 0xfa - delay % 0xfa;
            }
            g_settings.text_display_delay_ms = delay;
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} {:d}", g_localized_strings[0x7b7], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", g_localized_strings[0x7b8], 0x14 - delay / 0xfa));
        }
        break;
    }
    case W8_MGS_COMMAND_TEXT_DELAY_DOWN: {
        unsigned int delay = g_settings.text_display_delay_ms;
        if (delay > 0) {
            if (delay % 0xfa == 0) {
                delay -= 0xfa;
            } else {
                delay -= delay % 0xfa;
            }
            g_settings.text_display_delay_ms = delay;
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} {:d}", g_localized_strings[0x7b7], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", g_localized_strings[0x7b9], 0x14 - delay / 0xfa));
        }
        break;
    }
    case W8_MGS_COMMAND_SHOW_VERSION: {
        std::string version_text;
        version_text = FormatVersionBanner(true, true, true);
        ShowNotice(W8_FONT_PALETTE_BEIGE, version_text);
        break;
    }
    case W8_MGS_COMMAND_LOOK_LEVEL:
        if (IsWorldCursorVisible()) {
            UpdateWorldCursorPlacement();
        } else {
            LevelCamera();
        }
        break;
    case W8_MGS_COMMAND_PAUSE_GAME:
        ToggleMainGamePause();
        break;
    case W8_MGS_COMMAND_NEXT_LAYOUT:
        if (gXStatus.fNpcDialogueMode || gXStatus.fCampMode) {
            break;
        }
        if (IsScreenInputBlocked() != 0) {
            ApplyMainGameModeFlag(static_cast<W8MainUiMode>((g_level_block->main_ui_mode - 1) & 1),
                                  true);
        } else {
            ApplyMainGameModeFlag(static_cast<W8MainUiMode>((g_level_block->main_ui_mode + 1) % 3),
                                  true);
        }
        break;
    case W8_MGS_COMMAND_PREV_LAYOUT:
        if (IsScreenInputBlocked() != 0) {
            ApplyMainGameModeFlag(static_cast<W8MainUiMode>((g_level_block->main_ui_mode - 1) & 1),
                                  true);
        } else {
            ApplyMainGameModeFlag(static_cast<W8MainUiMode>((g_level_block->main_ui_mode + 2) % 3),
                                  true);
        }
        break;
    case W8_MGS_COMMAND_AUTOMAP:
        if (IsScreenInputBlocked() == 0) {
            OpenAutomapScreen();
        }
        break;
    case W8_MGS_COMMAND_OPTIONS:
        if (gXStatus.fNpcDialogueMode || CanOpenNpcDialogue() || gXStatus.fLockInteractMode ||
            gXStatus.fTrapInteractMode || gXStatus.fCampMode || gXStatus.fLockInteract ||
            gXStatus.fTrapInteract) {
            break;
        }
        if (!CanInterruptLevelMovement()) {
            break;
        }
        ClearScreenWait();
        break;
    case W8_MGS_COMMAND_JOURNAL:
        TryMGSActionKey(W8_MGS_ACTION_JOURNAL);
        break;
    case W8_MGS_COMMAND_INVENTORY:
        if (CanOpenNpcDialogue() || gXStatus.fCampMode || g_status.selected_character == -1 ||
            (gXStatus.fNpcDialogueMode &&
             g_npc_interaction_state->dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT)) {
            break;
        }
        OpenCharacterScreenForPartySlot(g_status.selected_character, false);
        break;
    case W8_MGS_COMMAND_USE_ITEM:
        TryMGSActionKey(W8_MGS_ACTION_USE_ITEM_VIEW);
        break;
    case W8_MGS_COMMAND_USE_LAST_ITEM:
        TryMGSActionKey(W8_MGS_ACTION_USE_RECORDED_ITEM);
        break;
    case W8_MGS_COMMAND_CAST_SPELL:
        TryMGSActionKey(W8_MGS_ACTION_SPELL_VIEW);
        break;
    case W8_MGS_COMMAND_CAST_LAST_SPELL:
        TryMGSActionKey(W8_MGS_ACTION_CAST_RECORDED_SPELL);
        break;
    case W8_MGS_COMMAND_CAMP:
        RequestCamp();
        break;
    case W8_MGS_COMMAND_TOGGLE_SEARCH:
        if (IsScreenInputBlocked() == 0) {
            ToggleSearchMode();
        }
        break;
    case W8_MGS_COMMAND_TOGGLE_COMBAT:
        if (IsScreenInputBlocked() == 0) {
            ToggleCombatMode();
        }
        break;
    case W8_MGS_COMMAND_QUICK_SAVE: {
        if (IsScreenInputBlocked() != 0 || !CanInterruptLevelMovement()) {
            break;
        }
        if (g_status.iron_man && !g_dev_mode) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x82c]);
            break;
        }
        if (gXStatus.fCombatMode) {
            if (g_dev_mode) {
                EndCombat(false);
            } else {
                ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x774]);
                break;
            }
        }
        const auto slot_name = SelectQuickSaveSlotForWrite();
        if (slot_name && SaveGame(*slot_name, 0)) {
            SetLastSaveName(*slot_name);
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x6f5]);
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x6f6]);
        }
        break;
    }
    case W8_MGS_COMMAND_QUICK_LOAD: {
        if (IsScreenInputBlocked() != 0) {
            break;
        }
        if (g_status.iron_man && !g_dev_mode) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x82c]);
            break;
        }
        if (const auto slot_name = FindStartupQuickSave()) {
            SetLastSaveName(*slot_name);
            ClearHeldItemDisplay();
            g_pending_screen_state.mode = 1;
            g_pending_screen_state.name = *slot_name;
            g_pending_screen_state.parameter = GetSaveGameLevel(g_pending_screen_state.name);
            CloseMainGameOverlays();
            SetMainGameMode(W8_MAIN_GAME_DEFAULT);
            SetPendingScreenState(W8_SCREEN_PLEASE_WAIT);
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE, g_localized_strings[0x786]);
        }
        break;
    }
    case W8_MGS_COMMAND_RADAR_ZOOM:
        if (g_level_block->radar_map_visible != 0) {
            ToggleRadarMapZoom();
        }
        break;
    case W8_MGS_COMMAND_REPLAY_QUOTE:
        RequeueSelectedPortraitEvent();
        break;
    case W8_MGS_COMMAND_SWAP_WEAPONS:
        if (IsScreenInputBlocked() == 0 && g_status.selected_character != -1) {
            BindCharacterItems(g_status.selected_character, true);
        }
        break;
    case W8_MGS_COMMAND_SWAP_ALL_WEAPONS:
        if (IsScreenInputBlocked() == 0) {
            BindEveryPartyItem();
        }
        break;
    case W8_MGS_COMMAND_SELECT_RECRUITED_1:
    case W8_MGS_COMMAND_SELECT_RECRUITED_2:
    case W8_MGS_COMMAND_SELECT_PC_1:
    case W8_MGS_COMMAND_SELECT_PC_2:
    case W8_MGS_COMMAND_SELECT_PC_3:
    case W8_MGS_COMMAND_SELECT_PC_4:
    case W8_MGS_COMMAND_SELECT_PC_5:
    case W8_MGS_COMMAND_SELECT_PC_6: {
        int slot = command - W8_MGS_COMMAND_SELECT_RECRUITED_1;
        if (!g_status.buffers.XChar[slot].fOccupied) {
            break;
        }
        if (g_status.selected_character != slot) {
            SelectPartyCharacter(slot);
            break;
        }
        if (g_settings.main_ui_mode == W8_MAIN_UI_MODE_PORTRAITS) {
            break;
        }
        if (g_level_block->portrait_refresh_pending[slot] != 0) {
            ClearPortraitRefreshSlot(slot);
        } else {
            RefreshSelectedPartyPortrait(slot);
            gXStatus.monster_manager_entries[slot].portrait_refresh_pinned = true;
        }
        break;
    }
    case W8_MGS_COMMAND_TEXTBOX_PAGE_UP:
        ScrollTextBoxUp(7);
        break;
    case W8_MGS_COMMAND_TEXTBOX_PAGE_DOWN:
        ScrollTextBoxDown(7);
        break;
    case W8_MGS_COMMAND_TEXTBOX_TOP:
        ScrollTextBoxTo(0);
        break;
    case W8_MGS_COMMAND_TEXTBOX_BOTTOM:
        ScrollDialogueTextBoxToLine();
        break;
    case W8_MGS_COMMAND_TEXTBOX_CLEAR:
        if (!gXStatus.fSpellCastMode && !gXStatus.fItemSelectMode) {
            ResetEditorStatusLine(-1);
        }
        break;
    case W8_MGS_COMMAND_START_COMBAT_ROUND:
        if (!gXStatus.fCombatMode || gXStatus.fSpellCastMode || gXStatus.fItemSelectMode) {
            break;
        }
        if (g_combat_state->execution_active == 0) {
            BeginCombatExecution();
        } else if (gXStatus.fPartyMovementUi && !CanPartyMove() && !IsLevelMovementStopped()) {
            BeginFreeTurnPhase();
        }
        break;
    case W8_MGS_COMMAND_CONTINUOUS_COMBAT:
        if (IsScreenInputBlocked() == 0) {
            TogglePartyCombatStance();
        }
        break;
    case W8_MGS_COMMAND_CYCLE_TARGET:
        if (g_status.selected_character != -1) {
            CycleToNextTarget(g_status.selected_character);
        }
        break;
    case W8_MGS_COMMAND_ATTACK:
        TryMGSActionKey(W8_MGS_ACTION_ATTACK);
        break;
    case W8_MGS_COMMAND_BERSERK:
        TryMGSActionKey(W8_MGS_ACTION_BERSERK);
        break;
    case W8_MGS_COMMAND_BREATHE:
        TryMGSActionKey(W8_MGS_ACTION_BREATHE);
        break;
    case W8_MGS_COMMAND_TURN_UNDEAD:
        TryMGSActionKey(W8_MGS_ACTION_TURN_UNDEAD);
        break;
    case W8_MGS_COMMAND_PRAY:
        TryMGSActionKey(W8_MGS_ACTION_PRAY);
        break;
    case W8_MGS_COMMAND_DEFEND:
        TryMGSActionKey(W8_MGS_ACTION_DEFEND);
        break;
    case W8_MGS_COMMAND_PROTECT:
        TryMGSActionKey(W8_MGS_ACTION_PROTECT);
        break;
    case W8_MGS_COMMAND_EQUIP:
        TryMGSActionKey(W8_MGS_ACTION_EQUIP);
        break;
    case W8_MGS_COMMAND_PARTY_WALK:
        TryMGSActionKey(W8_MGS_ACTION_WALK);
        break;
    case W8_MGS_COMMAND_PARTY_RUN:
        TryMGSActionKey(W8_MGS_ACTION_RUN);
        break;
    case W8_MGS_COMMAND_REPEAT_ACTION:
        TryMGSActionKey(W8_MGS_ACTION_REPEAT);
        break;
    case W8_MGS_COMMAND_DEBUG_AUDIT_QUOTES:
        AuditNpcScriptQuotes();
        break;
    case W8_MGS_COMMAND_DEBUG_NUMERIC_HP:
        ToggleNumericHitPoints();
        break;
    case W8_MGS_COMMAND_DEBUG_INJECT_CLICK:
        if (IsCursorImageInsideViewport()) {
            const SDL_Point position = GetGameMousePosition();
            QueueEvent(InputEventType::LeftButtonDown, position);
            QueueEvent(InputEventType::LeftButtonUp, position);
        }
        break;
    case W8_MGS_COMMAND_TOGGLE_AUTO_ADVANCE:
        g_settings.auto_advance_character ^= 1;
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   g_localized_strings[g_settings.auto_advance_character != 0 ? 0x1f68 / 4 : 0x1f6c / 4]);
        break;
    case W8_MGS_COMMAND_TEXTBOX_SCROLL_UP:
        ScrollTextBoxUp(1);
        break;
    case W8_MGS_COMMAND_TEXTBOX_SCROLL_DOWN:
        ScrollTextBoxDown(1);
        break;
    case W8_MGS_COMMAND_DEBUG_TOGGLE_FLAG_271:
        g_level_block->text_box_visible ^= 1;
        RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
        break;
    case W8_MGS_COMMAND_DEBUG_MONSTER_SCRIPT:
        if (g_level_block->highlighted_item != -1) {
            FindMonster(g_level_block->highlighted_item);
        }
        break;
    default:
        break;
    }
}

// GLOBAL: WIZ8 0x0064c1cc
srVector2i g_keyboard_row_positions[13] = {
    {5, 55}, {23, 55}, {41, 55}, {59, 55}, {59, 37}, {5, 37},  {5, 19},
    {5, 1},  {23, 1},  {41, 1},  {59, 1},  {59, 19}, {32, 28},
};

namespace {
struct KeyboardMenuRow {
    int menu = -1;
    int item = -1;
    bool enabled = true;
    HitTarget* target = nullptr;
    ButtonInteraction press;
    ButtonAppearance appearance;
};
struct KeyboardMenu {
    UiLayer input{0x26};
    SDL_Point origin;
    std::array<KeyboardMenuRow, 13> rows;
    bool dirty = true;
};
std::unique_ptr<KeyboardMenu> keyboard_menu;
}

static void KeyboardMenuSelectAttack(void);
static void KeyboardMenuSelectBerserk(void);
static void KeyboardMenuSelectBreathe(void);
static void KeyboardMenuSelectPray(void);
static void KeyboardMenuSelectTurnUndead(void);
static void KeyboardMenuSelectDefend(void);
static void KeyboardMenuSelectProtect(void);
static void KeyboardMenuOpenUseItemView(void);
static void KeyboardMenuSelectEquip(void);
static void KeyboardMenuOpenSpellView(void);
static void KeyboardMenuCastRecordedSpell(void);
static void KeyboardMenuUseRecordedItem(void);

// FUNCTION: WIZ8 0x00592C70
void OpenKeyboardMenuForSlot(int slot)
{
    g_selected_party_slot = slot;
    keyboard_menu.reset();
    g_level_block->keyboard_menu_open = true;
    SelectPartyCharacter(g_selected_party_slot);
    if (BuildKeyboardMenu() == 0) {
        CloseKeyboardMenu();
        return;
    }
    UpdateScreenOverlays(0);
    gXStatus.monster_manager_entries[slot].keyboard_menu_open = true;
    DisableMainGamePortraitSlot(slot);

    g_main_input->edge_targets[slot]->setEnabled(false);
    g_main_input->combat_targets[slot]->setEnabled(false);
    EnableKeyboardMenuInput();
    RequestRedraw(1 << slot);
}

// FUNCTION: WIZ8 0x00592E60
void CloseKeyboardMenu(void)
{
    gXStatus.monster_manager_entries[g_selected_party_slot].keyboard_menu_open = false;
    g_level_block->keyboard_menu_open = false;
    g_level_block->combat_slot = -1;
    g_level_block->hover_combat_slot = g_selected_party_slot;
    g_level_block->cursor_grace = 0;
    RefreshPartySlotRegions();
    if (gXStatus.fCombatMode) {
        g_main_input->combat.setEnabled(true);
    } else {
        g_main_input->combat.setEnabled(false);
    }
    if (keyboard_menu && g_level_block->portrait_refresh_pending[g_selected_party_slot] == 0) {
        ClearSurfaceRect(keyboard_menu->origin.x, keyboard_menu->origin.y,
                         keyboard_menu->origin.x + 0x52,
                         keyboard_menu->origin.y + 0x4a);
        InvalidateRegion(keyboard_menu->origin.x, keyboard_menu->origin.y,
                         keyboard_menu->origin.x + 0x52,
                         keyboard_menu->origin.y + 0x4a);
    }
    keyboard_menu.reset();
    RequestRedraw(1 << g_selected_party_slot);
}

static std::string KeyboardMenuTooltip(const KeyboardMenuRow& row)
{
    if (row.menu == -1) return g_localized_strings[0x11];
    if (row.menu == W8_SUBMENU_SPELLS && row.item == 1) {
        if (!row.enabled) return g_localized_strings[0x5a];
        const auto& spell = g_spell_records[g_status.buffers.XChar[g_selected_party_slot].spell_id];
        return fmt::format("{}: {} ({:d})", g_localized_strings[0x5a], spell.display_name,
                           GetAffordableSpellPowerLevel(g_selected_party_slot));
    }
    if (row.menu == W8_SUBMENU_ITEMS && row.item == 2) {
        if (!row.enabled) return g_localized_strings[0x5d];
        const auto& party = g_status.buffers.XChar[g_selected_party_slot];
        auto* item = FindCharacterItemAt(g_selected_party_slot, party.item_origin, party.item_slot);
        return fmt::format("{}: {}", g_localized_strings[0x5d], FormatItemDisplayName(item, false));
    }
    return g_localized_strings[g_submenu_entry_help_ids[row.menu * 5 + row.item]];
}

unsigned char BuildKeyboardMenu(void)
{
    int left, top, unused_4, unused_5, unused_6, unused_7;
    GetPartySlotMenuAnchor(g_selected_party_slot, &left, &top, &unused_4, &unused_5, &unused_6,
                           &unused_7, 0);
    keyboard_menu = std::make_unique<KeyboardMenu>();
    keyboard_menu->origin = {left + 0x17, top};
    static void (*const actions[4][5])() = {
        {KeyboardMenuSelectAttack, KeyboardMenuSelectBerserk, KeyboardMenuSelectBreathe,
         KeyboardMenuSelectTurnUndead, KeyboardMenuSelectPray},
        {KeyboardMenuSelectDefend, KeyboardMenuSelectProtect},
        {KeyboardMenuSelectEquip, KeyboardMenuOpenUseItemView, KeyboardMenuUseRecordedItem},
        {KeyboardMenuOpenSpellView, KeyboardMenuCastRecordedSpell}
    };
    int index = 0;
    for (int menu = 0; menu < 4; ++menu) for (int item = 0; item < 5; ++item) {
        if (g_submenu_entry_message_ids[menu * 5 + item] == -1) continue;
        keyboard_menu->rows[index].menu = menu;
        keyboard_menu->rows[index++].item = item;
    }
    for (int i = 0; i < 13; ++i) {
        auto& row = keyboard_menu->rows[i];
        row.target = &keyboard_menu->input.add(
            SDL_Rect{left + 0x17 + g_keyboard_row_positions[i].x,
                     top + g_keyboard_row_positions[i].y, 19, 19},
            [&row](const UiPointerEvent& event) {
                keyboard_menu->dirty = true;
                const ButtonPolicy policy{.controller_activation = true};
                if (event.action == UiPointerAction::Leave) row.press.leave(policy);
                if (event.action == UiPointerAction::Enter &&
                    ((row.menu == W8_SUBMENU_ITEMS && row.item == 2) ||
                     (row.menu == W8_SUBMENU_SPELLS && row.item == 1)))
                    SetUiHelpForced(true);
                if (!row.enabled) {
                    PushButtonSoundScheme(0, true);
                    return UiResult::Handled;
                }
                const auto mask = event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
                if (event.action == UiPointerAction::Down) row.press.press(mask, policy);
                if (event.action == UiPointerAction::Up && event.inside &&
                    (event.captured_buttons & mask) && row.press.release(mask, policy).activate) {
                    auto action = event.button == UiPointerButton::Right || row.menu == -1
                        ? CloseKeyboardMenu : actions[row.menu][row.item];
                    if (action) AfterUiDispatch(action);
                }
                return UiResult::Handled;
            });
        row.target->setTooltip([&row] { return KeyboardMenuTooltip(row); });
    }
    keyboard_menu->rows[12].appearance.quick_image = ButtonGraphics::Load(
        CatalogArtwork::Load(0x89, 0).spriteSheet(), -1, 0xbd, 0xbf, 0xbd, 0xbf);
    keyboard_menu->input.add(SDL_Rect{left + 0x17, top, 0x53, 0x4b},
        [](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Right)
                AfterUiDispatch(CloseKeyboardMenu);
            return UiResult::Handled;
        });
    RefreshKeyboardMenuRows();
    return 1;
}

void EnableKeyboardMenuInput(void)
{
    keyboard_menu->input.setEnabled(true);
}

bool KeyboardMenuContainsCursor(void)
{
    const auto [x, y] = keyboard_menu->origin;
    return IsCursorInRectangle(x, y, x + 0x52, y + 0x4a);
}

void RefreshKeyboardMenuRows(void)
{
    const auto art = CatalogArtwork::Load(0x89, 0);
    for (auto& row : keyboard_menu->rows) {
        if (row.menu == -1) continue;
        const int base = g_submenu_entry_message_ids[row.menu * 5 + row.item];
        int normal = base;
        int hover = base + 2;
        row.enabled = true;
        switch (GetSubMenuEntryState(row.menu, row.item, g_selected_party_slot)) {
        case W8_SUBMENU_ENTRY_USABLE: break;
        case W8_SUBMENU_ENTRY_USABLE_SELECTED: normal += 1; break;
        case W8_SUBMENU_ENTRY_UNUSABLE: normal += 3; hover += 3; break;
        case W8_SUBMENU_ENTRY_UNUSABLE_SELECTED: normal += 4; hover += 3; break;
        case W8_SUBMENU_ENTRY_UNAVAILABLE: row.enabled = false; break;
        }
        int offset = 0;
        if (row.menu == W8_SUBMENU_SPELLS && row.item == 1)
            offset = g_spell_records[g_status.buffers.XChar[g_selected_party_slot].spell_id].realm * 7;
        else if (row.menu == W8_SUBMENU_ATTACK && row.item == 0)
            offset = GetAttackMenuWeaponOffset(g_status.buffers.Char[g_selected_party_slot].Hand[0].weapon_skill) * 7;
        row.appearance.quick_image = ButtonGraphics::Load(art.spriteSheet(), base + 6,
            normal + offset, hover + offset, normal + offset, hover + offset);
    }
    keyboard_menu->dirty = true;
}

void RedrawKeyboardMenuPanel(bool invalidate)
{
    if (!invalidate && !keyboard_menu->dirty) return;
    const auto [x, y] = keyboard_menu->origin;
    auto& target = GetFrameBufferSurface();
    CatalogArtwork::Load(0xa6, 0).drawAndInvalidate(target, 0, x, y);
    for (int index = 0; index < 13; ++index) {
        const auto& row = keyboard_menu->rows[index];
        DrawButtonAppearance(target, row.appearance,
            {x + g_keyboard_row_positions[index].x, y + g_keyboard_row_positions[index].y, 18, 18},
            {.enabled = row.enabled, .pressed = row.press.pressed,
             .hovered = row.target->hovered(), .menu_artwork = true});
    }
    keyboard_menu->dirty = false;
}

static void ChooseKeyboardMenuAction(W8ActionKind action)
{
    ChooseAction(g_selected_party_slot, action, -1, 0, false, 1);
    RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
    CloseKeyboardMenu();
}

// FUNCTION: WIZ8 0x00593710
static void KeyboardMenuSelectAttack(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_ATTACK);
}

// FUNCTION: WIZ8 0x00593860
static void KeyboardMenuSelectBerserk(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_BERSERK);
}

// FUNCTION: WIZ8 0x005939B0
static void KeyboardMenuSelectBreathe(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_BREATHE);
}

// FUNCTION: WIZ8 0x00593B00
static void KeyboardMenuSelectTurnUndead(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_TURN_UNDEAD);
}

// FUNCTION: WIZ8 0x00593C50
static void KeyboardMenuSelectPray(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_PRAY);
}

// FUNCTION: WIZ8 0x00593DA0
static void KeyboardMenuSelectDefend(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_DEFEND);
}

// FUNCTION: WIZ8 0x00593EF0
static void KeyboardMenuSelectProtect(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_PROTECT);
}

// FUNCTION: WIZ8 0x00594040
static void KeyboardMenuOpenSpellView(void)
{

    CloseKeyboardMenu();
    if (!gXStatus.fSpellCastMode) {
        OpenSpellCastingView(g_selected_party_slot);
    }
}

// FUNCTION: WIZ8 0x00594180
static void KeyboardMenuCastRecordedSpell(void)
{

    CloseKeyboardMenu();
    if (CanPartySlotCastRecordedSpell(g_selected_party_slot)) {
        StartCharacterSpellCast(g_selected_party_slot, 0);
        RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
    }
}

// FUNCTION: WIZ8 0x00594390
static void KeyboardMenuSelectEquip(void)
{

    ChooseKeyboardMenuAction(W8_ACTION_EQUIP);
}

// FUNCTION: WIZ8 0x005944E0
static void KeyboardMenuOpenUseItemView(void)
{

    CloseKeyboardMenu();
    if (!gXStatus.fItemSelectMode) {
        OpenUseItemSelectView(g_selected_party_slot);
    }
}

// FUNCTION: WIZ8 0x00594620
static void KeyboardMenuUseRecordedItem(void)
{

    CloseKeyboardMenu();
    StartCharacterItemUse(g_selected_party_slot);
    RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
}

// FUNCTION: WIZ8 0x005929d0
void HandleManualCameraHotkeys(void)
{
    if (g_modal_owner == 0 && !gXStatus.fNpcDialogueMode) {
        if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_CAMERA_LOCK)) {
            BeginManualCameraControl();
        }
        ApplyCameraMotionHotkeys();
    }
}

// FUNCTION: WIZ8 0x00592a10
void ApplyCameraMotionHotkeys(void)
{
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_TURN_LEFT) ||
        g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_TURN_LEFT_ALT)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_TURN_LEFT;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_TURN_RIGHT) ||
        g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_TURN_RIGHT_ALT)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_TURN_RIGHT;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_MOVE_FORWARD)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_MOVE_FORWARD_RUN)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_MOVE_BACKWARD)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_BACKWARD;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_MOVE_BACKWARD_RUN)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_BACKWARD | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_LOOK_UP)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_LOOK_UP;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_LOOK_DOWN)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_LOOK_DOWN;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_STRAFE_LEFT)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_LEFT;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_STRAFE_RIGHT)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_RIGHT;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_STRAFE_LEFT_RUN)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_LEFT | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard->IsCommandPressed(W8_MGS_COMMAND_STRAFE_RIGHT_RUN)) {
        g_level_block->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_RIGHT | W8_CAMERA_MOTION_FAST;
    }
}
