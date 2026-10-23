#include "wiz8/local_screens/MGSKeyboard.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>

#include "camera_motion.h"
#include "glm/ext/vector_int2.hpp"
#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "mgs_commands.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/chunk.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/cursor.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/key_bindings.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSRadarMap.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/version.h"
#include "wiz8/world_cursor.h"

struct W8ItemInstance;

/* Local Screens\MGSKeyboard.cpp owns the binding vector, its command-keyed
   lookup and the singleton ResetMGSKeyboardBindings installs.
   MGSKeyboard::LoadDefaults moved to Local Code\InputMapper.cpp. */

MGSKeyboard g_mgs_keyboard;

// Retail set 0x26, regions 180-193.

int MGSKeyboard::FindBinding(W8MGSCommand command) const
{
    const auto found = std::ranges::find(m_bindings, command, &MGSKeyBinding::command);
    return found == m_bindings.end() ? -1 : static_cast<int>(found - m_bindings.begin());
}

W8MGSCommand MGSKeyboard::FindCommandForEvent(const InputEvent* event) const
{
    if (event->type != InputEventType::KeyDown || event->key == SDLK_UNKNOWN)
        return W8_MGS_COMMAND_NONE;
    for (const auto& binding : m_bindings)
        if (binding.active && binding.key == event->key && binding.modifiers == event->modifiers)
            return binding.command;
    return W8_MGS_COMMAND_NONE;
}

MGSKeyBinding* MGSKeyboard::GetBinding(int index)
{
    if (index < 0 || m_bindings.empty()) {
        return nullptr;
    }
    // Preserve the binding table's fallback to its first entry.
    if (std::cmp_greater_equal(index, m_bindings.size())) {
        return &m_bindings.front();
    }
    return &m_bindings[index];
}

bool MGSKeyboard::IsCommandPressed(W8MGSCommand command) const
{
    const int index = FindBinding(command);
    return index >= 0 && IsKeyHeld(m_bindings[index].key) &&
           m_bindings[index].modifiers == GetInputState().modifiers;
}

/* Discard every queued input atom. MainGameScreenEnter calls this so stale
   presses do not fire against the freshly entered screen state. */
void DrainInputEventQueue()
{
    while (PollInputEvent()) {
    }
}

void MGSKeyboard::Clear()
{
    m_bindings.clear();
}

void MGSKeyboard::SetBinding(MGSKeyBinding binding)
{
    const int index = FindBinding(binding.command);
    if (index >= 0) m_bindings.erase(m_bindings.begin() + index);
    m_bindings.push_back(binding);
}

namespace {
// Exact retail layout at the file boundary; runtime bindings have normal C++ layout.
#pragma pack(push, 1)
struct RetailKeyBinding {
    std::uint16_t key;
    std::uint16_t modifiers;
    std::uint16_t active;
    std::uint32_t command;
};
#pragma pack(pop)
static_assert(sizeof(RetailKeyBinding) == 10);
} // namespace

unsigned char MGSKeyboard::Load(W8Chunk& handle, bool clear)
try {
    if (clear) Clear();
    std::uint32_t count;
    handle.ReadBytes(&count, sizeof(count));
    count = SDL_Swap32LE(count);
    if (count > (handle.RemainingBytes()) / sizeof(RetailKeyBinding)) return false;
    for (std::uint32_t index = 0; index < count; ++index) {
        RetailKeyBinding record;
        handle.ReadBytes(&record, sizeof(record));
        const auto retail_key = SDL_Swap16LE(record.key);
        const auto key = KeyFromRetailBinding(retail_key);
        SetBinding({key, SDL_Swap16LE(record.modifiers), SDL_Swap16LE(record.active),
                    static_cast<W8MGSCommand>(SDL_Swap32LE(record.command)),
                    key == SDLK_UNKNOWN ? retail_key : std::uint16_t(0)});
    }
    return true;
} catch (const std::exception&) {
    return false;
}

unsigned char MGSKeyboard::Save(wiz8::File& handle) const
{
    const auto count = SDL_Swap32LE(static_cast<std::uint32_t>(m_bindings.size()));
    handle.write(&count, sizeof(count));
    for (const auto& binding : m_bindings) {
        const RetailKeyBinding record{SDL_Swap16LE(binding.key == SDLK_UNKNOWN
                                                       ? binding.unmapped_retail_key
                                                       : KeyToRetailBinding(binding.key)),
                                      SDL_Swap16LE(binding.modifiers), SDL_Swap16LE(binding.active),
                                      SDL_Swap32LE(static_cast<std::uint32_t>(binding.command))};
        handle.write(&record, sizeof(record));
    }
    return true;
}

void ResetMGSKeyboardBindings()
{
    g_mgs_keyboard.Clear();
    g_mgs_keyboard.LoadDefaults("Data\\Strings\\MGSKeyboard.ini");
}

/* Route one non-mouse input atom: text entry, NPC dialogue and the trap text
   box consume key presses first; everything else resolves to a bound MGS
   command.
   Attribution-gap body leading the MGSKeyboard.cpp hull at 0x00591960. */
bool HandleMainGameInputEvent(const InputEvent* input)
{
    auto* text_fields = TextFieldGroup::Focused();
    if (IsTypingEvent(*input) && text_fields && text_fields->HandleInput(*input)) {
        return true;
    }
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) &&
        CurrentGame().activity.conversation) {
        HandleNpcDialogueKeyEvent(input);
        return true;
    }
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) &&
        IsTrapInteractionActive() && TextBoxHandleKey(input) != 0) {
        return true;
    }

    DispatchMGSCommand(g_mgs_keyboard.FindCommandForEvent(input));
    return true;
}

void DispatchMGSCommand(W8MGSCommand command)
{
    switch (command) {
    case W8_MGS_COMMAND_CANCEL:
        if (IsWorldCursorVisible()) {
            ToggleWorldCursor();
        } else if (CurrentGame().surprise_possible) {
            AcknowledgeSurprise();
        } else if (auto* picker = SpellPicker()) {
            picker->Cancel();
        } else if (IsLockInteractionActive()) {
            EndLockInteractMode(false);
        } else if (UseItemPicker()) {
            MainGame()->closeItemPicker();
        } else if (ScreenPointer().targeting() != W8_TARGET_NEED_NONE) {
            SetTargetingMode(W8_TARGET_NEED_NONE);
        } else if (FormationPanelOpen()) {
            CloseFormationPanel();
        } else if (IsNpcScriptSessionActive()) {
            TryFinishNpcVoicePlayback(CurrentGame().status, true);
        } else if (CurrentGame().speech.HasActiveEvents()) {
            CurrentGame().speech.CompleteFirstActiveEvent();
        } else {
            if (CurrentGame().combat.has_value()) {
                ToggleCombatMode();
                if (!CurrentGame().combat.has_value()) {
                    InvalidateRegion(0xa8, 0x16e, 0x1c4, 0x1ba);
                    return;
                }
            }
            ShowMainGameNoticeLine(Data().strings[0x779], OnLeaveGameConfirmClosed, true, true);
        }
        InvalidateRegion(0xa8, 0x16e, 0x1c4, 0x1ba);
        break;
    case W8_MGS_COMMAND_QUIT_GAME:
        ShowMainGameNoticeLine(Data().strings[0x832], OnQuitGameDialogClosed, true, true);
        break;
    case W8_MGS_COMMAND_TOGGLE_FULLSCREEN:
        ResetTransientRenderScenes();
        VideoFullScreen(g_fullscreen == 0);
        break;
    case W8_MGS_COMMAND_LOAD_OPTIONS:
        if (IsScreenInputBlocked() == 0 && CanInterruptLevelMovement()) {
            RequestScreen({.id = W8_SCREEN_OPTIONS, .mode = W8_OPTIONS_LOAD});
        }
        break;
    case W8_MGS_COMMAND_SAVE_OPTIONS:
        if (IsScreenInputBlocked() == 0 && CanInterruptLevelMovement()) {
            RequestScreen({.id = W8_SCREEN_OPTIONS, .mode = W8_OPTIONS_SAVE});
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
                       fmt::format("{} {:d}", Data().strings[0x7ba], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", Data().strings[0x7bb], 0x14 - delay / 0xfa));
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
                       fmt::format("{} {:d}", Data().strings[0x7ba], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", Data().strings[0x7bc], 0x14 - delay / 0xfa));
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
                       fmt::format("{} {:d}", Data().strings[0x7b7], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", Data().strings[0x7b8], 0x14 - delay / 0xfa));
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
                       fmt::format("{} {:d}", Data().strings[0x7b7], 0x14 - delay / 0xfa));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       fmt::format("{} ({:d})", Data().strings[0x7b9], 0x14 - delay / 0xfa));
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
        if (CurrentGame().activity.conversation || CurrentGame().activity.conversation_aside) {
            break;
        }
        MainGame()->stepLayoutMode(1);
        break;
    case W8_MGS_COMMAND_PREV_LAYOUT:
        MainGame()->stepLayoutMode(-1);
        break;
    case W8_MGS_COMMAND_AUTOMAP:
        if (IsScreenInputBlocked() == 0) {
            OpenAutomapScreen();
        }
        break;
    case W8_MGS_COMMAND_OPTIONS:
        if (CurrentGame().activity.conversation ||
            (CurrentGame().activity.conversation &&
             CurrentGame().npc_interaction.scripted_dialogue) ||
            IsLockInteractionActive() || IsTrapInteractionActive() ||
            CurrentGame().activity.conversation_aside || LockInteractionSuspended() ||
            TrapInteractionSuspended()) {
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
        if ((CurrentGame().activity.conversation &&
             CurrentGame().npc_interaction.scripted_dialogue) ||
            CurrentGame().activity.conversation_aside ||
            CurrentGame().status.selected_character == -1 ||
            (CurrentGame().activity.conversation &&
             CurrentGame().npc_interaction.dialogue_layout == W8_DIALOGUE_LAYOUT_TRANSCRIPT)) {
            break;
        }
        OpenCharacterScreenForPartySlot(CurrentGame().status.selected_character, false);
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
            CurrentGame().search.toggle();
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
        if (CurrentGame().status.iron_man) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x82c]);
            break;
        }
        if (CurrentGame().combat.has_value()) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x774]);
            break;
        }
        const auto slot_name = SelectQuickSaveSlotForWrite();
        if (slot_name && SaveGame(*slot_name, nullptr)) {
            g_options_last_save_name = (*slot_name);
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x6f5]);
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x6f6]);
        }
        break;
    }
    case W8_MGS_COMMAND_QUICK_LOAD: {
        if (IsScreenInputBlocked() != 0) {
            break;
        }
        if (CurrentGame().status.iron_man) {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x82c]);
            break;
        }
        if (const auto slot_name = FindStartupQuickSave()) {
            g_options_last_save_name = (*slot_name);
            ClearHeldItemDisplay();
            ScreenRequest request;
            request.mode = W8_PLEASE_WAIT_LOAD_GAME;
            request.name = *slot_name;
            request.parameter = GetSaveGameLevel(request.name);
            SetMainGameMode(W8_MAIN_GAME_DEFAULT);
            request.id = W8_SCREEN_PLEASE_WAIT;
            RequestScreen(std::move(request));
        } else {
            ShowNotice(W8_FONT_PALETTE_BEIGE, Data().strings[0x786]);
        }
        break;
    }
    case W8_MGS_COMMAND_RADAR_ZOOM:
        if (MainGame()->radar_map) {
            MainGame()->radar_map->toggleZoom();
        }
        break;
    case W8_MGS_COMMAND_REPLAY_QUOTE:
        RequeueSelectedPortraitEvent();
        break;
    case W8_MGS_COMMAND_SWAP_WEAPONS:
        if (IsScreenInputBlocked() == 0 && CurrentGame().status.selected_character != -1) {
            CurrentGame().inventory.requestWeaponSwap(CurrentGame().status.selected_character,
                                                      true);
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
        if (!CurrentGame().party.members[slot].has_value()) {
            break;
        }
        if (CurrentGame().status.selected_character != slot) {
            SelectPartyCharacter(slot);
            break;
        }
        if (MainGame()->layout().mode == W8_MAIN_UI_MODE_PORTRAITS) {
            break;
        }
        if (MainGameHud()->portrait_refresh_pending[slot] != 0) {
            ClearPortraitRefreshSlot(slot);
        } else {
            RefreshSelectedPartyPortrait(slot);
            CurrentGame().monster_manager_entries[slot].portrait_refresh_pinned = true;
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
        if (!SpellPicker() && !UseItemPicker()) {
            ResetEditorStatusLine(-1);
        }
        break;
    case W8_MGS_COMMAND_START_COMBAT_ROUND:
        if (!CurrentGame().combat.has_value() || SpellPicker() || UseItemPicker()) {
            break;
        }
        if (CurrentGame().combat->execution_active == 0) {
            BeginCombatExecution();
        } else if (PartyMovementPanelShown() && !CanPartyMove() && !IsLevelMovementStopped()) {
            BeginFreeTurnPhase();
        }
        break;
    case W8_MGS_COMMAND_CONTINUOUS_COMBAT:
        if (IsScreenInputBlocked() == 0) {
            TogglePartyCombatStance();
        }
        break;
    case W8_MGS_COMMAND_CYCLE_TARGET:
        if (CurrentGame().status.selected_character != -1) {
            CycleToNextTarget(CurrentGame().status.selected_character);
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
        AuditNpcScriptQuotes(CurrentGame().status);
        break;
    case W8_MGS_COMMAND_DEBUG_NUMERIC_HP:
        ToggleNumericHitPoints();
        break;
    case W8_MGS_COMMAND_DEBUG_INJECT_CLICK:
        if (IsCursorImageInsideViewport()) {
            const SDL_Point position = GetGameMousePosition();
            PostInputEvent({.type = InputEventType::LeftButtonDown, .position = position});
            PostInputEvent({.type = InputEventType::LeftButtonUp, .position = position});
        }
        break;
    case W8_MGS_COMMAND_TOGGLE_AUTO_ADVANCE:
        g_settings.auto_advance_character = !g_settings.auto_advance_character;
        ShowNotice(W8_FONT_PALETTE_BEIGE,
                   Data().strings[g_settings.auto_advance_character ? 0x1f68 / 4 : 0x1f6c / 4]);
        break;
    case W8_MGS_COMMAND_TEXTBOX_SCROLL_UP:
        ScrollTextBoxUp(1);
        break;
    case W8_MGS_COMMAND_TEXTBOX_SCROLL_DOWN:
        ScrollTextBoxDown(1);
        break;
    case W8_MGS_COMMAND_DEBUG_TOGGLE_FLAG_271:
        MainGameHud()->text_box_visible ^= 1;
        if (auto* hud = MainGameHud()) hud->repaint.text_box = true;
        break;
    case W8_MGS_COMMAND_DEBUG_MONSTER_SCRIPT:
        if (MainGameHud()->highlighted_item != -1) {
            FindMonster(MainGameHud()->highlighted_item);
        }
        break;
    default:
        break;
    }
}

constexpr glm::ivec2 keyboard_row_positions[13] = {
    {5, 55}, {23, 55}, {41, 55}, {59, 55}, {59, 37}, {5, 37},  {5, 19},
    {5, 1},  {23, 1},  {41, 1},  {59, 1},  {59, 19}, {32, 28},
};

bool MainGameScreen::keyboardMenuForSlot(int slot) const
{
    return keyboard_menu && keyboard_menu->slot == slot;
}

void MainGameScreen::openKeyboardMenu(int slot)
{
    keyboard_menu.reset();
    SelectPartyCharacter(slot);
    keyboard_menu = std::make_unique<KeyboardActionMenu>(*this, slot);
    dismissOverlays(false);
    DisableMainGamePortraitSlot(slot);
    edge_targets[slot]->setEnabled(false);
    combat_targets[slot]->setEnabled(false);
    hud->repaint.portrait(slot);
}

void MainGameScreen::closeKeyboardMenu()
{
    auto menu = std::move(keyboard_menu);
    auto& state = *hud;
    const int slot = menu ? menu->slot : state.combat_slot;
    state.combat_slot = -1;
    state.hover_combat_slot = slot;
    state.cursor_grace = 0;
    refreshPartySlotRegions();
    combat.setEnabled(game.combat.has_value());
    if (menu && !state.portrait_refresh_pending[slot]) {
        ClearSurfaceRect(menu->origin.x, menu->origin.y, menu->origin.x + 0x52,
                         menu->origin.y + 0x4a);
        InvalidateRegion(menu->origin.x, menu->origin.y, menu->origin.x + 0x52,
                         menu->origin.y + 0x4a);
    }
    menu.reset();
    if (slot >= 0) state.repaint.portrait(slot);
}

std::string KeyboardActionMenu::tooltip(const Row& row) const
{
    if (row.menu == -1) return Data().strings[0x11];
    if (row.menu == W8_SUBMENU_SPELLS && row.item == 1) {
        if (!row.enabled) return Data().strings[0x5a];
        const auto& spell = Data().spells[screen_.game.party.rows[slot].remembered.spell.detail];
        return fmt::format("{}: {} ({:d})", Data().strings[0x5a], spell.display_name,
                           GetAffordableSpellPowerLevel(slot));
    }
    if (row.menu == W8_SUBMENU_ITEMS && row.item == 2) {
        if (!row.enabled) return Data().strings[0x5d];
        const auto& party = screen_.game.party.rows[slot];
        const auto* item =
            screen_.game.inventory.itemAt(party.remembered.item_detail.item_use.item);
        return fmt::format("{}: {}", Data().strings[0x5d],
                           FormatItemDisplayName(Data(), item, false));
    }
    return Data().strings[g_submenu_entry_help_ids[row.menu * 5 + row.item]];
}

KeyboardActionMenu::KeyboardActionMenu(MainGameScreen& screen, int selected_slot)
    : slot(selected_slot), screen_(screen)
{
    int left, top, unused_4, unused_5, unused_6, unused_7;
    GetPartySlotMenuAnchor(slot, &left, &top, &unused_4, &unused_5, &unused_6, &unused_7, 0);
    origin = {left + 0x17, top};
    int index = 0;
    for (int menu = 0; menu < 4; ++menu)
        for (int item = 0; item < 5; ++item) {
            if (g_submenu_entry_message_ids[menu * 5 + item] == -1) continue;
            rows[index].menu = menu;
            rows[index++].item = item;
        }
    for (int i = 0; i < 13; ++i) {
        auto& row = rows[i];
        row.target = &input.add(
            SDL_Rect{left + 0x17 + keyboard_row_positions[i].x, top + keyboard_row_positions[i].y,
                     19, 19},
            [this, &row](const UiPointerEvent& event) {
                requestRedraw();
                const ButtonPolicy policy{.input_rule = ButtonInputRule::PrimaryPressedState};
                if (event.action == UiPointerAction::Leave) row.press.leave(policy);
                if (event.action == UiPointerAction::Enter &&
                    ((row.menu == W8_SUBMENU_ITEMS && row.item == 2) ||
                     (row.menu == W8_SUBMENU_SPELLS && row.item == 1)))
                    g_ui_help_forced = true;
                if ((row.enabled) &&
                    (event.action != UiPointerAction::Up || event.captured_buttons))
                    PlayButtonSound(event.action);
                if (!row.enabled) {

                    return UiResult::Handled;
                }
                const auto mask =
                    event.button == UiPointerButton::Right ? SDL_BUTTON_RMASK : SDL_BUTTON_LMASK;
                if (event.action == UiPointerAction::Down) row.press.press(mask, policy);
                if (event.action == UiPointerAction::Up && event.inside &&
                    (event.captured_buttons & mask) && row.press.release(mask, policy).activate) {
                    const int menu = event.button == UiPointerButton::Right ? -1 : row.menu;
                    AfterUiDispatch([owner = &screen_, slot = slot, menu, item = row.item] {
                        owner->chooseKeyboardMenuAction(slot, menu, item);
                    });
                }
                return UiResult::Handled;
            });
        row.target->setTooltip([this, &row] { return tooltip(row); });
    }
    rows[12].appearance.quick_image = ButtonGraphics::Load(
        CatalogArtwork::Load(0x89, 0).spriteSheet(), -1, 0xbd, 0xbf, 0xbd, 0xbf);
    input.add(SDL_Rect{left + 0x17, top, 0x53, 0x4b}, [this](const UiPointerEvent& event) {
        if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Right)
            AfterUiDispatch([owner = &screen_] { owner->closeKeyboardMenu(); });
        return UiResult::Handled;
    });
    refresh();
}

bool KeyboardActionMenu::containsCursor() const
{
    const auto [x, y] = origin;
    return IsCursorInRectangle(x, y, x + 0x52, y + 0x4a);
}

void KeyboardActionMenu::refresh()
{
    const auto art = CatalogArtwork::Load(0x89, 0);
    for (auto& row : rows) {
        if (row.menu == -1) continue;
        const int base = g_submenu_entry_message_ids[row.menu * 5 + row.item];
        int normal = base;
        int hover = base + 2;
        row.enabled = true;
        switch (GetSubMenuEntryState(row.menu, row.item, slot)) {
        case W8_SUBMENU_ENTRY_USABLE:
            break;
        case W8_SUBMENU_ENTRY_USABLE_SELECTED:
            normal += 1;
            break;
        case W8_SUBMENU_ENTRY_UNUSABLE:
            normal += 3;
            hover += 3;
            break;
        case W8_SUBMENU_ENTRY_UNUSABLE_SELECTED:
            normal += 4;
            hover += 3;
            break;
        case W8_SUBMENU_ENTRY_UNAVAILABLE:
            row.enabled = false;
            break;
        }
        int offset = 0;
        if (row.menu == W8_SUBMENU_SPELLS && row.item == 1)
            offset = Data().spells[screen_.game.party.rows[slot].remembered.spell.detail].realm * 7;
        else if (row.menu == W8_SUBMENU_ATTACK && row.item == 0)
            offset = GetAttackMenuWeaponOffset(
                         screen_.game.party.characters[slot].Hand[0].weapon_skill) *
                     7;
        row.appearance.quick_image =
            ButtonGraphics::Load(art.spriteSheet(), base + 6, normal + offset, hover + offset,
                                 normal + offset, hover + offset);
    }
    requestRedraw();
}

void KeyboardActionMenu::requestRedraw()
{
    screen_.hud->repaint.keyboard_menu = true;
}

void KeyboardActionMenu::paint() const
{
    const auto [x, y] = origin;
    auto& target = GetFrameBufferSurface();
    CatalogArtwork::Load(0xa6, 0).drawAndInvalidate(target, 0, x, y);
    for (int index = 0; index < 13; ++index) {
        const auto& row = rows[index];
        DrawButtonAppearance(
            target, row.appearance,
            {x + keyboard_row_positions[index].x, y + keyboard_row_positions[index].y, 18, 18},
            {.enabled = row.enabled,
             .pressed = row.press.pressed,
             .hovered = row.target->hovered(),
             .menu_artwork = true});
    }
}

void MainGameScreen::chooseKeyboardMenuAction(int slot, int menu, int item)
{
    W8ActionKind action = W8_ACTION_NONE;
    if (menu == W8_SUBMENU_ATTACK) {
        static constexpr std::array actions{W8_ACTION_ATTACK, W8_ACTION_BERSERK, W8_ACTION_BREATHE,
                                            W8_ACTION_TURN_UNDEAD, W8_ACTION_PRAY};
        action = actions[item];
    } else if (menu == W8_SUBMENU_DEFEND) {
        action = item == 0 ? W8_ACTION_DEFEND : W8_ACTION_PROTECT;
    } else if (menu == W8_SUBMENU_ITEMS && item == 0) {
        action = W8_ACTION_EQUIP;
    }
    if (action != W8_ACTION_NONE) {
        ChooseAction(slot, action, -1, 0, false, 1);
        hud->repaint.character_action = true;
        closeKeyboardMenu();
        return;
    }
    closeKeyboardMenu();
    if (menu == W8_SUBMENU_SPELLS) {
        if (item == 0) {
            if (!spell_picker) openSpellPicker(slot);
        } else if (CanPartySlotCastRecordedSpell(slot)) {
            StartCharacterSpellCast(slot, 0);
            hud->repaint.character_action = true;
        }
    } else if (menu == W8_SUBMENU_ITEMS) {
        if (item == 1) {
            if (!item_picker) openItemPicker(slot);
        } else {
            StartCharacterItemUse(slot);
            hud->repaint.character_action = true;
        }
    }
}

void HandleManualCameraHotkeys(void)
{
    if (g_modal_owner == 0 && !CurrentGame().activity.conversation) {
        if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_CAMERA_LOCK)) {
            BeginManualCameraControl();
        }
        ApplyCameraMotionHotkeys();
    }
}

void ApplyCameraMotionHotkeys(void)
{
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_TURN_LEFT) ||
        g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_TURN_LEFT_ALT)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_TURN_LEFT;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_TURN_RIGHT) ||
        g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_TURN_RIGHT_ALT)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_TURN_RIGHT;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_MOVE_FORWARD)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_MOVE_FORWARD_RUN)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_FORWARD | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_MOVE_BACKWARD)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_BACKWARD;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_MOVE_BACKWARD_RUN)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_BACKWARD | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_LOOK_UP)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_LOOK_UP;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_LOOK_DOWN)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_LOOK_DOWN;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_STRAFE_LEFT)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_LEFT;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_STRAFE_RIGHT)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_RIGHT;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_STRAFE_LEFT_RUN)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_LEFT | W8_CAMERA_MOTION_FAST;
    }
    if (g_mgs_keyboard.IsCommandPressed(W8_MGS_COMMAND_STRAFE_RIGHT_RUN)) {
        MainGameHud()->camera_motion_flags |= W8_CAMERA_MOTION_STRAFE_RIGHT | W8_CAMERA_MOTION_FAST;
    }
}
