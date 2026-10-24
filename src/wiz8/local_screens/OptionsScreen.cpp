#include "wiz8/local_screens/OptionsScreen.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <wiz8/filesystem.h>

#include "asset_paths.h"
#include "dialog_code/MessageDialogBase.h"
#include "mgs_commands.h"
#include "wiz8/audio/AmbientSound.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/difficulty.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/save_screenshot.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/platform/input.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/render/quality.h"
#include "wiz8/rules/party.h"
#include "wiz8/screen.h"
#include "wiz8/surface_storage.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"
#include "wiz8/world_view.h"

/* Shared zero-initialized wide string: binary-wide DATA references read it as
   empty text and as a sprintf format argument. No recovered writer exists. */

// GLOBAL: WIZ8 0x0069c254
std::string g_options_last_save_name;

static void AddOptionsLabel(W8OptionsPanel& panel, int label);

void W8OptionsPanel::AddCheckbox(int label, bool* value)
{
    AddCheckbox(label, *value, [value](bool selected) { *value = selected; });
}

template<typename T>
void W8OptionsPanel::AddChoices(int label, int count, const int* choices, T* value)
{
    AddChoices(label, count, choices, static_cast<int>(*value),
               [value](int selected) { *value = static_cast<T>(selected); });
}

void W8OptionsPanel::AddChoices(int label, int count, const int* choices, int selected,
                                std::function<void(int)> changed)
{
    AddOptionsLabel(*this, label);
    const auto first = m_buttons.size();
    for (int index = 0; index < count; ++index) {
        auto* button = AddChoiceButton(choices[index], [this, first, count, index, changed](auto*) {
            changed(index);
            for (int i = 0; i < count; ++i) m_buttons[first + i]->SetPressed(i == index);
        });
        button->Policy().primary_toggle = ButtonToggle::StayLatched;
        button->SetPressed(index == selected);
    }
    m_content_top += 22;
}

// GLOBAL: WIZ8 0x0064d72c
static int g_last_options_panel = 5;

static W8OptionsMenuRow g_options_menu_rows[8] = {
    {1, {{20, 51, 69, 137}, {-1, -1, -1, -1}}, {16, 194, 0, 0}, {0, 2, 1, -1, -1}},
    {4, {{90, 72, 117, 117}, {217, 72, 244, 117}}, {16, 220, 0, 0}, {3, 5, 4, -1, -1}},
    {3, {{132, 29, 201, 102}, {-1, -1, -1, -1}}, {16, 246, 0, 0}, {6, 8, 7, -1, -1}},
    {2, {{124, 141, 194, 172}, {-1, -1, -1, -1}}, {16, 272, 0, 0}, {9, 11, 10, -1, -1}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 322, 0, 0}, {12, 14, 13, -1, 25}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 360, 0, 0}, {15, 17, 16, -1, 24}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 398, 0, 0}, {18, 20, 19, -1, -1}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 436, 0, 0}, {21, 23, 22, -1, -1}}};

static W8OptionsPanelRange g_options_panel_ranges[8] = {{0, 2},   {3, 3},   {4, 5}, {6, 10},
                                                        {11, 11}, {12, 12}, {0, 0}, {0, 0}};
static int g_options_difficulty_labels[3] = {0x7f8, 0x7f9, 0x7fa};
static int g_options_camera_rotation_labels[3] = {0x7fe, 0x7ff, 0x800};
static int g_options_combat_mode_labels[2] = {0x7f6, 0x7f5};
static int g_options_audio_labels[4] = {0x81e, 0x81f, 0x821, 0x820};

struct W8OptionsKeyName {
    SDL_Keycode key;
    unsigned short reserved;
    int label;
};

struct W8OptionsKeyRow {
    W8MGSCommand primary_binding;
    W8MGSCommand secondary_binding;
    int label;
};

struct W8OptionsKeyboardPage {
    int title;
    W8MGSCommand first_binding;
    W8MGSCommand last_binding;
};

static W8OptionsKeyName g_options_key_names[] = {{SDLK_F1, 0, 2158},
                                                 {SDLK_F2, 0, 2159},
                                                 {SDLK_F3, 0, 2160},
                                                 {SDLK_F4, 0, 2161},
                                                 {SDLK_F5, 0, 2162},
                                                 {SDLK_F6, 0, 2163},
                                                 {SDLK_F7, 0, 2164},
                                                 {SDLK_F8, 0, 2165},
                                                 {SDLK_F9, 0, 2166},
                                                 {SDLK_F10, 0, 2167},
                                                 {SDLK_F11, 0, 2168},
                                                 {SDLK_F12, 0, 2169},
                                                 {SDLK_TAB, 0, 2170},
                                                 {SDLK_NUMLOCKCLEAR, 0, 2171},
                                                 {SDLK_BACKSPACE, 0, 2172},
                                                 {SDLK_INSERT, 0, 2173},
                                                 {SDLK_DELETE, 0, 2174},
                                                 {SDLK_END, 0, 2175},
                                                 {SDLK_PAGEDOWN, 0, 2176},
                                                 {SDLK_PAGEUP, 0, 2177},
                                                 {SDLK_HOME, 0, 2178},
                                                 {SDLK_RETURN, 0, 2179},
                                                 {SDLK_SPACE, 0, 2180},
                                                 {SDLK_DOWN, 0, 2181},
                                                 {SDLK_LEFT, 0, 2182},
                                                 {SDLK_RIGHT, 0, 2183},
                                                 {SDLK_UP, 0, 2184},
                                                 {SDLK_PAUSE, 0, 2185},
                                                 {SDLK_SCROLLLOCK, 0, 2186},
                                                 {SDLK_CAPSLOCK, 0, 2187},
                                                 {SDLK_KP_0, 0, 2188},
                                                 {SDLK_KP_1, 0, 2189},
                                                 {SDLK_KP_2, 0, 2190},
                                                 {SDLK_KP_3, 0, 2191},
                                                 {SDLK_KP_4, 0, 2192},
                                                 {SDLK_KP_5, 0, 2193},
                                                 {SDLK_KP_6, 0, 2194},
                                                 {SDLK_KP_7, 0, 2195},
                                                 {SDLK_KP_8, 0, 2196},
                                                 {SDLK_KP_9, 0, 2197},
                                                 {SDLK_KP_MULTIPLY, 0, 2198},
                                                 {SDLK_KP_PLUS, 0, 2199},
                                                 {SDLK_KP_ENTER, 0, 2200},
                                                 {SDLK_KP_MINUS, 0, 2201},
                                                 {SDLK_KP_PERIOD, 0, 2202},
                                                 {SDLK_KP_DIVIDE, 0, 2203},
                                                 {SDLK_NUMLOCKCLEAR, 0, 2171},
                                                 {SDLK_GRAVE, 0, 2204},
                                                 {0, 0, 2157}};

static W8OptionsKeyRow g_options_key_rows[] = {
    {W8_MGS_COMMAND_MOVE_FORWARD, W8_MGS_COMMAND_MOVE_FORWARD_RUN, 2106},
    {W8_MGS_COMMAND_MOVE_BACKWARD, W8_MGS_COMMAND_MOVE_BACKWARD_RUN, 2107},
    {W8_MGS_COMMAND_TURN_LEFT, W8_MGS_COMMAND_TURN_LEFT_ALT, 2108},
    {W8_MGS_COMMAND_TURN_RIGHT, W8_MGS_COMMAND_TURN_RIGHT_ALT, 2109},
    {W8_MGS_COMMAND_STRAFE_LEFT, W8_MGS_COMMAND_STRAFE_LEFT_RUN, 2110},
    {W8_MGS_COMMAND_STRAFE_RIGHT, W8_MGS_COMMAND_STRAFE_RIGHT_RUN, 2111},
    {W8_MGS_COMMAND_LOOK_UP, W8_MGS_COMMAND_NONE, 2112},
    {W8_MGS_COMMAND_LOOK_DOWN, W8_MGS_COMMAND_NONE, 2113},
    {W8_MGS_COMMAND_LOOK_LEVEL, W8_MGS_COMMAND_NONE, 2114},
    {W8_MGS_COMMAND_PAUSE_GAME, W8_MGS_COMMAND_NONE, 2115},
    {W8_MGS_COMMAND_NEXT_LAYOUT, W8_MGS_COMMAND_PREV_LAYOUT, 2116},
    {W8_MGS_COMMAND_AUTOMAP, W8_MGS_COMMAND_NONE, 2117},
    {W8_MGS_COMMAND_OPTIONS, W8_MGS_COMMAND_NONE, 2118},
    {W8_MGS_COMMAND_USE_ITEM, W8_MGS_COMMAND_USE_LAST_ITEM, 2121},
    {W8_MGS_COMMAND_CAST_SPELL, W8_MGS_COMMAND_CAST_LAST_SPELL, 2122},
    {W8_MGS_COMMAND_INVENTORY, W8_MGS_COMMAND_NONE, 2120},
    {W8_MGS_COMMAND_CAMP, W8_MGS_COMMAND_NONE, 2123},
    {W8_MGS_COMMAND_TOGGLE_SEARCH, W8_MGS_COMMAND_NONE, 2124},
    {W8_MGS_COMMAND_TOGGLE_COMBAT, W8_MGS_COMMAND_NONE, 2125},
    {W8_MGS_COMMAND_RADAR_ZOOM, W8_MGS_COMMAND_NONE, 2127},
    {W8_MGS_COMMAND_QUICK_SAVE, W8_MGS_COMMAND_QUICK_LOAD, 2126},
    {W8_MGS_COMMAND_REPLAY_QUOTE, W8_MGS_COMMAND_NONE, 2128},
    {W8_MGS_COMMAND_SWAP_WEAPONS, W8_MGS_COMMAND_SWAP_ALL_WEAPONS, 2129},
    {W8_MGS_COMMAND_JOURNAL, W8_MGS_COMMAND_NONE, 2119},
    {W8_MGS_COMMAND_SELECT_PC_1, W8_MGS_COMMAND_NONE, 2130},
    {W8_MGS_COMMAND_SELECT_PC_2, W8_MGS_COMMAND_NONE, 2131},
    {W8_MGS_COMMAND_SELECT_PC_3, W8_MGS_COMMAND_NONE, 2132},
    {W8_MGS_COMMAND_SELECT_PC_4, W8_MGS_COMMAND_NONE, 2133},
    {W8_MGS_COMMAND_SELECT_PC_5, W8_MGS_COMMAND_NONE, 2134},
    {W8_MGS_COMMAND_SELECT_PC_6, W8_MGS_COMMAND_NONE, 2135},
    {W8_MGS_COMMAND_SELECT_RECRUITED_1, W8_MGS_COMMAND_NONE, 2136},
    {W8_MGS_COMMAND_SELECT_RECRUITED_2, W8_MGS_COMMAND_NONE, 2137},
    {W8_MGS_COMMAND_TEXTBOX_PAGE_UP, W8_MGS_COMMAND_NONE, 2138},
    {W8_MGS_COMMAND_TEXTBOX_PAGE_DOWN, W8_MGS_COMMAND_NONE, 2139},
    {W8_MGS_COMMAND_TEXTBOX_TOP, W8_MGS_COMMAND_NONE, 2140},
    {W8_MGS_COMMAND_TEXTBOX_BOTTOM, W8_MGS_COMMAND_NONE, 2141},
    {W8_MGS_COMMAND_TEXTBOX_CLEAR, W8_MGS_COMMAND_NONE, 2142},
    {W8_MGS_COMMAND_START_COMBAT_ROUND, W8_MGS_COMMAND_NONE, 2143},
    {W8_MGS_COMMAND_CONTINUOUS_COMBAT, W8_MGS_COMMAND_NONE, 2144},
    {W8_MGS_COMMAND_CAMERA_LOCK, W8_MGS_COMMAND_NONE, 2145},
    {W8_MGS_COMMAND_CYCLE_TARGET, W8_MGS_COMMAND_NONE, 2146},
    {W8_MGS_COMMAND_ATTACK, W8_MGS_COMMAND_NONE, 2147},
    {W8_MGS_COMMAND_BERSERK, W8_MGS_COMMAND_NONE, 2148},
    {W8_MGS_COMMAND_BREATHE, W8_MGS_COMMAND_NONE, 2149},
    {W8_MGS_COMMAND_TURN_UNDEAD, W8_MGS_COMMAND_NONE, 2150},
    {W8_MGS_COMMAND_PRAY, W8_MGS_COMMAND_NONE, 2151},
    {W8_MGS_COMMAND_DEFEND, W8_MGS_COMMAND_NONE, 2152},
    {W8_MGS_COMMAND_PROTECT, W8_MGS_COMMAND_NONE, 2153},
    {W8_MGS_COMMAND_EQUIP, W8_MGS_COMMAND_NONE, 2154},
    {W8_MGS_COMMAND_PARTY_WALK, W8_MGS_COMMAND_PARTY_RUN, 2155},
    {W8_MGS_COMMAND_REPEAT_ACTION, W8_MGS_COMMAND_NONE, 2156},
    {W8_MGS_COMMAND_NONE, W8_MGS_COMMAND_NONE, -1}};

static W8OptionsKeyboardPage g_options_keyboard_pages[5] = {
    {0x835, W8_MGS_COMMAND_MOVE_FORWARD, W8_MGS_COMMAND_LOOK_LEVEL},
    {0x836, W8_MGS_COMMAND_PAUSE_GAME, W8_MGS_COMMAND_JOURNAL},
    {0x837, W8_MGS_COMMAND_SELECT_PC_1, W8_MGS_COMMAND_SELECT_RECRUITED_2},
    {0x838, W8_MGS_COMMAND_TEXTBOX_PAGE_UP, W8_MGS_COMMAND_TEXTBOX_CLEAR},
    {0x839, W8_MGS_COMMAND_START_COMBAT_ROUND, W8_MGS_COMMAND_REPEAT_ACTION}};

void W8OptionsSaveRow::SetSave(W8SaveSlot& save)
{
    m_save = &save;
    button.SetEnabled(true);
    button.m_dirty = true;
}

void W8OptionsSaveRow::ClearSave()
{
    m_save = nullptr;
    button.SetEnabled(false);
    button.m_dirty = true;
}

W8OptionsSaveLoadPanel::W8OptionsSaveLoadPanel(OptionsScreen& owner, int panel)
    : screen(owner), m_panel(panel)
{
    m_background = CatalogArtwork::Load(0xf7);
    m_backgroundImage = 0;
    for (int index = 0; index < 5; ++index) {
        m_rows.emplace_back(
            std::make_unique<W8OptionsSaveRow>(m_input, *this, index, m_panel == 12));
    }

    if (m_panel == 11) {
        m_delete_button = AddButton(0xd, 0x1b0, 0xf8, 6, 8, 7, -1, -1, [this](auto* button) {
            AfterUiDispatch([this, button] { OnPrimary(button); });
        });
    } else {
        m_delete_button = 0;
    }

    int first_sprite = m_panel == 11 ? 3 : 0;
    m_action_button =
        AddButton(0xf5, 0x1b0, 0xf8, first_sprite, first_sprite + 2, first_sprite + 1, -1, -1,
                  [this](auto* button) { AfterUiDispatch([this, button] { OnPrimary(button); }); });

    if (m_panel == 11 && static_cast<int>(screen.m_save_slots.size()) == 1) {
        m_delete_button->SetEnabled(false);
        m_action_button->SetEnabled(false);
    }

    int page = 0;
    int selected = 0;
    if (m_panel == 11 && g_options_last_save_name[0] != 0) {
        for (int index = 1; index < static_cast<int>(screen.m_save_slots.size()); ++index) {
            if (strcmp(g_options_last_save_name.c_str(),
                       (screen.m_save_slots[index])->name.c_str()) == 0) {
                page = (index - 1) / 5;
                selected = (index - 1) % 5;
            }
        }
    }
    SetCurrent(page);
    SelectRow(selected);
}

void W8OptionsSaveLoadPanel::SetActive(bool active)
{
    W8OptionsPanel::SetActive(active);
    if (active && screen.m_text_editor == 0 && m_panel == 12 && m_selected == 0 && m_current == 0) {
        OnEditSaveName();
    }
}

/* Commit or cancel the active save-name edit before changing options pages. */
void OptionsScreen::FinishTextEdit(bool cancelled)
{
    W8OptionsTextEditor* editor = m_text_editor.get();
    if (!editor) return;
    editor->owner->OnTextEditComplete(editor, cancelled);
    m_text_editor.reset();
}

void W8OptionsSaveLoadPanel::SetCurrent(int current)
{
    screen.FinishTextEdit(true);
    m_current = current;
    InvalidateAll();

    int first = current * 5;
    if (m_panel == 11) {
        ++first;
    }
    int limit = static_cast<int>(screen.m_save_slots.size());
    if (first + 5 < limit) {
        limit = first + 5;
    }
    for (int slot = first; slot < limit; ++slot) {
        m_rows[slot - first]->SetSave(*screen.m_save_slots[slot]);
    }
    for (; limit < first + 5; ++limit) {
        m_rows[limit - first]->ClearSave();
    }
    SelectRow(0);
    if (screen.m_text_editor == 0 && m_panel == 12 && m_selected == 0 && m_current == 0) {
        OnEditSaveName();
    }
}

void W8OptionsSaveLoadPanel::OnPrimary(W8ButtonWidget* control)
{
    if (control == m_delete_button) {
        screen.ShowNotification(true, 0x828,
                                [this](bool accepted) { OnDialogClosed(accepted, 1); });
        return;
    }
    if (control != m_action_button) {
        return;
    }
    if (m_panel == 11) {
        LoadSelectedSave();
        return;
    }

    if (screen.m_text_editor) {
        // Completing the edit already saves or requests overwrite confirmation.
        screen.FinishTextEdit(false);
        return;
    }
    if (m_current == 0 && m_selected == 0) {
        SaveSelectedSave();
    } else {
        screen.ShowNotification(true, 0x829,
                                [this](bool accepted) { OnDialogClosed(accepted, 2); });
    }
}

void W8OptionsSaveLoadPanel::OnDialogClosed(bool accepted, int value)
{
    int selected_slot;
    if (!accepted) {
        if (value == 2) {
            if (screen.m_text_editor != 0 || m_panel != 12 || m_selected != 0) {
                return;
            }
            selected_slot = m_current;
        } else if (value == 3) {
            selected_slot = m_current * 5 + m_selected;
            (screen.m_save_slots[selected_slot])->name = m_previous_name;
            W8OptionsSaveRow* row = m_rows[m_selected].get();
            row->SetSave(*screen.m_save_slots[selected_slot]);
            if (screen.m_text_editor != 0 || m_panel != 12 || m_selected != 0) {
                return;
            }
            selected_slot = m_current;
        } else {
            return;
        }
    } else {
        switch (value) {
        case 1:
            DeleteSelectedSave();
            return;
        case 3:
            SaveSelectedSave(m_previous_name);
            return;
        case 2:
            SaveSelectedSave();
            return;
        case 4:
            if (screen.m_text_editor != 0 || m_panel != 12 || m_selected != 0) {
                return;
            }
            selected_slot = m_current;
            break;
        default:
            return;
        }
    }
    if (selected_slot == 0) {
        OnEditSaveName();
    }
}

void W8OptionsSaveLoadPanel::OnTextEditComplete(W8OptionsTextEditor* editor,
                                                unsigned char cancelled)
{
    int selected_slot = m_current * 5 + m_selected;
    m_rows[m_editing_row]->m_editing = false;
    if (cancelled != 0) {
        m_rows[m_editing_row]->button.m_dirty = true;
        return;
    }

    W8SaveSlot& slot = *screen.m_save_slots[selected_slot];
    m_previous_name = slot.name;
    slot.name = editor->m_fields.Text(0);
    if (slot.name.size() == 0) {
        slot.name = m_previous_name;
        screen.ShowNotification(false, 0x82a,
                                [this](bool accepted) { OnDialogClosed(accepted, 4); });
        return;
    }

    W8OptionsSaveRow* row = m_rows[m_selected].get();
    row->SetSave(slot);
    if (m_current == 0 && m_selected == 0) {
        if (SaveSlotFileExists(slot.name) == 0) {
            SaveSelectedSave();
        } else {
            screen.ShowNotification(true, 0x829,
                                    [this](bool accepted) { OnDialogClosed(accepted, 2); });
        }
    } else {
        screen.ShowNotification(true, 0x829,
                                [this](bool accepted) { OnDialogClosed(accepted, 3); });
    }
}

void W8OptionsSaveLoadPanel::OnEditSaveName()
{
    int selected_slot = m_current * 5 + m_selected;
    screen.BeginSaveNameEdit(this, m_selected, screen.m_save_slots[selected_slot]->name);
    m_editing_row = m_selected;
    m_rows[m_editing_row]->m_editing = true;
}

void W8OptionsSaveLoadPanel::SelectRow(int selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        m_rows[i]->button.SetPressed(i == selected);
    screen.FinishTextEdit(true);
    if (m_panel == 12 && m_current == 0 && selected == 0) OnEditSaveName();
}

void W8OptionsSaveLoadPanel::DeleteSelectedSave()
{
    int selected_slot = m_current * 5 + 1 + m_selected;
    W8SaveSlot& slot = *screen.m_save_slots[selected_slot];
    std::string path;
    path = fmt::format("{}\\{}.{}", "saves", slot.name, g_save_extension);
    bool removed = false;
    try {
        removed = wiz8::remove_file(wiz8::user_path(path));
    } catch (const std::exception& error) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not delete save %s: %s", slot.name.c_str(),
                    error.what());
    }
    if (!removed) {
        screen.ShowNotification(false, 0x82f,
                                [this](bool accepted) { OnDialogClosed(accepted, 0); });
        return;
    }

    screen.m_save_slots.erase(screen.m_save_slots.begin() + selected_slot);
    if (static_cast<int>(screen.m_save_slots.size()) == 1) {
        selected_slot = -1;
        if (m_panel == 11) {
            m_delete_button->SetEnabled(false);
            m_action_button->SetEnabled(false);
        }
    } else if (selected_slot >= static_cast<int>(screen.m_save_slots.size())) {
        --selected_slot;
        if (m_selected == 0) {
            --m_current;
        }
    }

    screen.UpdateNavigation();
    SetCurrent(m_current);
    SelectRow(selected_slot == -1 ? -1 : (selected_slot - 1) % 5);
}

void W8OptionsSaveLoadPanel::LoadSelectedSave()
{
    int selected_slot = m_current * 5 + 1 + m_selected;
    W8SaveSlot& slot = *screen.m_save_slots[selected_slot];
    if (slot.version_major + slot.version_minor * 0.1f + slot.version_patch * 0.01f <= 1.24f) {
        g_options_last_save_name = slot.name;
        if (GameStarted()) {
            ClearHeldItemDisplay();
        }
        RequestScreen({.id = W8_SCREEN_PLEASE_WAIT,
                       .mode = W8_PLEASE_WAIT_LOAD_GAME,
                       .parameter = slot.level_id,
                       .name = slot.name},
                      ScreenDeparture::Discard);
    }
}

void W8OptionsSaveLoadPanel::SaveSelectedSave(std::string_view previous_name)
{
    int selected_slot = m_current * 5 + m_selected;
    W8SaveSlot& slot = *screen.m_save_slots[selected_slot];
    if (slot.name.size() == 0) {
        screen.ShowNotification(false, 0x82a,
                                [this](bool accepted) { OnDialogClosed(accepted, 0); });
        return;
    }

    g_options_last_save_name = slot.name;
    const std::string old_name(previous_name);
    RequestScreen({.id = W8_SCREEN_PLEASE_WAIT,
                   .mode = W8_PLEASE_WAIT_SAVE_GAME,
                   .save_screenshot = screen.m_save_slots[0]->screenshot,
                   .name = slot.name,
                   .previous_save_name = SDL_strcasecmp(old_name.c_str(), slot.name.c_str()) == 0
                                             ? std::string()
                                             : old_name},
                  ScreenDeparture::Discard);
}

W8OptionsSaveRow::W8OptionsSaveRow(UiLayer& layer, W8OptionsSaveLoadPanel& owner, int index,
                                   bool save_mode)
    : button(layer)
{
    auto art = CatalogArtwork::Load(0xf9, 0);
    const int normal = save_mode ? 0 : 2;
    button.Configure(
        ButtonGraphics::Load(art.spriteSheet(), 4, normal, -1, normal + 1, -1),
        {.on_click =
             [&owner, this, index, save_mode](auto*) {
                 AfterUiDispatch([&owner, this, index, save_mode] {
                     const auto point = button.GetMousePosition();
                     if (!m_editing && owner.m_selected == index && save_mode &&
                         point.x - button.Bounds().x >= 0x5c && point.y - button.Bounds().y >= 0x33)
                         owner.OnEditSaveName();
                     owner.SelectRow(index);
                 });
             },
         .on_double_click =
             [&owner, this, index](auto*) {
                 if (!m_editing && owner.m_selected == index)
                     AfterUiDispatch([&owner] { owner.OnPrimary(owner.m_action_button); });
             },
         .policy = {.primary_toggle = ButtonToggle::StayLatched,
                    .input_rule = ButtonInputRule::PrimaryPressedState},
         .priority = UI_PRIORITY_LOWEST,
         .menu_artwork = true});

    button.SetPosition(0x104 + 10, 0x12 + index * 0x49);
    const auto size = art.size(normal);
    button.Resize(size.x, size.y);
}

void W8OptionsSaveRow::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!full_redraw && !button.m_dirty) return;
    button.Refresh(true);
    if (!button.IsEnabled() || m_save == 0) {
        return;
    }

    int x = button.Bounds().x;
    int y = button.Bounds().y;
    if (m_save->screenshot.capture_result == W8ScreenshotCaptureResult::failed) {
        CatalogArtwork::Load(0xf6, 0).draw(target, 0, x + 6, y + 6);
    } else {
        auto portrait =
            CreateCpuStorage(0x50, 0x3c, SDL_PIXELFORMAT_ARGB1555, m_save->screenshot.pixels, 0xa0);
        DrawColorSurface(portrait.get(), x + 6, y + 6);
    }

    std::string level_name = m_save->level_id == 0x38
                                 ? g_default_level
                                 : Data().strings[g_level_name_indices[m_save->level_id]];
    std::string timestamp;
    timestamp = fmt::format("{:d}-{:02d}-{:02d} {:2d}:{:02d}", m_save->timestamp.year,
                            m_save->timestamp.month, m_save->timestamp.day, m_save->timestamp.hour,
                            m_save->timestamp.minute);

    int text_x = x + 0x5e;
    if (m_save->version_major + m_save->version_minor * 0.1f + m_save->version_patch * 0.01f <=
        1.24f) {
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 9}, level_name);
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x16},
                 fmt::format("{} {:3d}, {:2d}:{:02d}", Data().strings[0x826],
                             m_save->game_time_days, m_save->game_time_ms / 3600000,
                             (m_save->game_time_ms / 60000) % 60));
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x23}, timestamp);
    } else {
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 9}, Data().strings[0x830]);
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x16}, Data().strings[0x831]);
    }
    DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x34}, m_save->name);
    if (m_save->iron_man) {
        DrawText(target, g_wiz_text_font_secondary, {x + 0x156, y + 9}, Data().strings[0x827]);
    }
}

void W8OptionsSaveLoadPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    const bool repaint = full_redraw || m_dirty;
    W8OptionsPanel::Redraw(target, full_redraw);
    for (auto& row : m_rows) row->Redraw(target, repaint);
}

void OptionsScreen::BeginSaveNameEdit(W8OptionsSaveLoadPanel* owner, int row, std::string_view text)
{
    auto editor = std::make_unique<W8OptionsTextEditor>(m_menu_input);
    editor->m_fields.Add(0x16b, row * 0x49 + 0x46, 0xfa, 0xc, 0x7f, text, 0x3b, 0xf, true);
    editor->m_fields.Focus(0);
    editor->owner = owner;
    m_text_editor = std::move(editor);
}

void W8OptionsKeyButton::SetKey(SDL_Keycode key)
{
    if (key == SDLK_ESCAPE) {
        return;
    }

    MGSKeyBinding* binding =
        g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(m_primary_binding));
    if (binding != 0) {
        binding->key = key;
        binding->unmapped_retail_key = 0;
    }
    if (m_secondary_binding != W8_MGS_COMMAND_NONE) {
        binding = g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(m_secondary_binding));
        if (binding != 0) {
            binding->key = key;
            binding->unmapped_retail_key = 0;
        }
    }
    SetKeyText(key);
}

void W8OptionsKeyButton::SetKeyText(SDL_Keycode key)
{
    std::string text;
    for (const auto* name = g_options_key_names; name->key != 0; ++name) {
        if (name->key == key) {
            text = Data().strings[name->label];
            break;
        }
    }
    if (text.empty()) {
        const char* name = SDL_GetKeyName(key);
        text = name && *name ? name : Data().strings[0x86d];
    }

    if (m_secondary_binding != W8_MGS_COMMAND_NONE && key != 0) {
        text = fmt::format("({}) {}", Data().strings[0x89d], text);
    }
    this->text.SetText(text, g_options_detail_font);
}

W8OptionsKeyboardPanel::W8OptionsKeyboardPanel(OptionsScreen& owner, int panel)
    : screen(owner), m_panel(panel)
{
    const auto add_key = [this](SDL_Rect bounds, W8MGSCommand primary, W8MGSCommand secondary,
                                std::string_view caption) {
        auto& entry = m_keys.emplace_back(std::make_unique<W8OptionsKeyButton>());
        auto* key = entry.get();
        key->m_primary_binding = primary;
        key->m_secondary_binding = secondary;
        key->text.SetLayoutBounds(bounds.x, bounds.y, bounds.x + bounds.w, bounds.y + bounds.h);
        key->text.SetLayoutMode(g_W8TextBufferNoWrap | g_W8TextBufferAlignTop |
                                (primary == W8_MGS_COMMAND_NONE ? 0 : g_W8TextBufferAlignRight));
        key->text.SetText(caption, g_options_detail_font);
        key->target = &m_input.add(
            SDL_Rect{bounds.x, bounds.y, bounds.w + 1, bounds.h + 1},
            [this, key](const UiPointerEvent& event) {
                if (event.action != UiPointerAction::Up || event.captured_buttons)
                    PlayButtonSound(event.action);
                const ButtonPolicy policy{.primary_toggle = ButtonToggle::StayLatched,
                                          .input_rule = ButtonInputRule::PrimaryPressedState};
                m_dirty = true;
                if (event.action == UiPointerAction::Leave)
                    key->interaction.leave(policy);
                else if (event.button == UiPointerButton::Left) {
                    if (event.action == UiPointerAction::Down)
                        key->interaction.press(SDL_BUTTON_LMASK, policy);
                    else if (event.action == UiPointerAction::Up && event.inside &&
                             key->interaction.release(SDL_BUTTON_LMASK, policy).activate) {
                        if (key->m_primary_binding == W8_MGS_COMMAND_NONE) {
                            key->interaction = {};
                            AfterUiDispatch([this] {
                                screen.ShowNotification(true, 0x834, [this](bool accepted) {
                                    OnDialogClosed(accepted, 0);
                                });
                            });
                        } else
                            SelectKey(key);
                    }
                }
                return UiResult::Handled;
            });
    };
    const auto& page = g_options_keyboard_pages[m_panel - 6];
    const W8ControlsRect title_bounds{m_bounds.x + 30, m_content_top, m_bounds.x + m_bounds.w - 30,
                                      m_content_top + 22};
    m_text_buffers.emplace_back(std::make_unique<W8TextBuffer>(
        &title_bounds, Data().strings[page.title], g_options_detail_font,
        g_W8TextBufferAlignTop | g_W8TextBufferAlignCenter, 4));
    m_content_top += 44;
    auto* row = g_options_key_rows;
    while (row->primary_binding != page.first_binding) ++row;
    for (;;) {
        const W8ControlsRect bounds{m_bounds.x + 20, m_content_top, m_bounds.x + m_bounds.w,
                                    m_content_top + 22};
        m_text_buffers.emplace_back(std::make_unique<W8TextBuffer>(
            &bounds, Data().strings[row->label], g_options_detail_font,
            g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));
        add_key({m_bounds.x + 100, m_content_top, 0x15e - 100, 22}, row->primary_binding,
                row->secondary_binding, "");
        m_content_top += 22;
        if ((row++)->primary_binding == page.last_binding) break;
    }
    const auto& reset = Data().strings[0x833];
    const int width = g_options_detail_font.textWidth(reset) + 20;
    add_key({m_bounds.x + (m_bounds.w - width) / 2, 0x18e, width, 22}, W8_MGS_COMMAND_NONE,
            W8_MGS_COMMAND_NONE, reset);
}

void W8OptionsKeyboardPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    const bool repaint = full_redraw || m_dirty;
    W8OptionsPanel::Redraw(target, full_redraw);
    if (repaint)
        for (auto& key : m_keys) {
            key->text.SetFontStateIndex(key->interaction.pressed ? 13
                                        : key->target->hovered() ? 14
                                                                 : -1);
            key->text.RenderToTarget(0, true, target);
        }
}

void W8OptionsKeyboardPanel::SelectKey(W8OptionsKeyButton* key)
{
    m_captured_button = key;
    screen.m_key_capture = key ? this : nullptr;
    for (auto& entry : m_keys) {
        entry->interaction.cancel(false);
        entry->interaction.selected = entry->interaction.pressed = entry.get() == key;
    }
    InvalidateAll();
}

void W8OptionsKeyboardPanel::RefreshBindingLabels()
{
    for (auto& key : m_keys) {
        if (key->m_primary_binding == W8_MGS_COMMAND_NONE) continue;
        auto* binding =
            g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(key->m_primary_binding));
        if (binding) key->SetKeyText(binding->key);
    }
    InvalidateAll();
}

void W8OptionsKeyboardPanel::SetActive(bool active)
{
    W8OptionsPanel::SetActive(active);
    if (active) {
        RefreshBindingLabels();
    }
    SelectKey(nullptr);
}

bool W8OptionsKeyboardPanel::OnKey(SDL_Keycode key, unsigned short modifiers)
{
    if (modifiers != 0) {
        return false;
    }
    if (key != SDLK_ESCAPE) {
        ClearDuplicateBinding(key);
    }
    m_captured_button->SetKey(key);
    SelectKey(nullptr);
    return true;
}

void W8OptionsKeyboardPanel::OnDialogClosed(bool accepted, int)
{
    SelectKey(nullptr);
    if (accepted) {
        ResetMGSKeyboardBindings();
        RefreshBindingLabels();
    }
}

void W8OptionsKeyboardPanel::ClearDuplicateBinding(SDL_Keycode key)
{
    int row_index = 0;
    if (g_options_key_rows[0].primary_binding == W8_MGS_COMMAND_NONE) {
        return;
    }

    W8OptionsKeyRow* row = g_options_key_rows;
    for (;;) {
        MGSKeyBinding* binding =
            g_mgs_keyboard.GetBinding(g_mgs_keyboard.FindBinding(row->primary_binding));
        if (binding != 0 && binding->key == key) {
            binding->key = SDLK_UNKNOWN;
            binding->unmapped_retail_key = 0;
            if (g_options_key_rows[row_index].secondary_binding != W8_MGS_COMMAND_NONE) {
                binding = g_mgs_keyboard.GetBinding(
                    g_mgs_keyboard.FindBinding(g_options_key_rows[row_index].secondary_binding));
                if (binding != 0) {
                    binding->key = SDLK_UNKNOWN;
                    binding->unmapped_retail_key = 0;
                }
            }

            RefreshBindingLabels();
            return;
        }

        ++row_index;
        ++row;
        if (row->primary_binding == W8_MGS_COMMAND_NONE) {
            return;
        }
    }
}

static void AddRenderCheckbox(W8OptionsPanel& panel, int label, W8RenderOption option)
{
    panel.AddCheckbox(label, GetRenderOptionState(option) != 0,
                      [option](bool enabled) { SetRenderOption(option, enabled); });
}

static std::unique_ptr<W8OptionsPanel> CreateGameOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 22;
    panel->AddSlider(0x805, (5000 - g_settings.combat_delay_ms) * 0.0002f, 0, 1, [](float speed) {
        g_settings.combat_delay_ms = 5000 - static_cast<int>(speed * 5000);
    });
    panel->m_content_top += 22;
    panel->AddSlider(
        0x80c, static_cast<float>(g_settings.text_display_delay_ms), 0, 5000,
        [](float delay) { g_settings.text_display_delay_ms = static_cast<unsigned int>(delay); });
    panel->m_content_top += 22;
    panel->AddSlider(
        0x802, g_settings.monster_movement_speed, 1, 5,
        [](float speed) { g_settings.monster_movement_speed = speed; }, true);
    panel->m_content_top += 22;
    panel->AddChoices(0x7f7, 3, g_options_difficulty_labels, g_settings.difficulty,
                      [](int selected) {
                          g_settings.difficulty = static_cast<W8Difficulty>(selected);
                          if (GameInProgress())
                              CurrentGame().status.difficulty = g_settings.difficulty;
                      });
    panel->AddChoices(0x7f4, 2, g_options_combat_mode_labels, g_settings.continuous_combat != 0,
                      [](int selected) {
                          if (g_settings.continuous_combat != (selected != 0))
                              TogglePartyCombatStance();
                      });
    const int rotation = g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_DISABLED
                             ? 2
                             : static_cast<int>(g_settings.camera_rotation_style);
    panel->AddChoices(0x7fd, 3, g_options_camera_rotation_labels, rotation, [](int selected) {
        g_settings.camera_rotation_mode =
            selected == 2 ? W8_CAMERA_ROTATION_DISABLED : W8_CAMERA_ROTATION_ALL_TARGETS;
        if (selected != 2)
            g_settings.camera_rotation_style = static_cast<W8CameraRotationStyle>(selected);
    });
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateMouseOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 22;
    panel->AddCheckbox(0x7f0, &g_settings.mouselook_toggle);
    panel->AddCheckbox(0x7f1, &g_settings.mouselook_smoothing);
    panel->AddCheckbox(0x7f2, &g_settings.invert_mouse_y);
    panel->AddCheckbox(0x801, false); // Unavailable: this legacy option has no gameplay setting.
    panel->AddCheckbox(0x809, &g_settings.autotarget_spells);
    panel->AddCheckbox(0x804, &g_settings.auto_advance_character);
    panel->AddCheckbox(0x808, &g_settings.autoswap_weapons);
    if (!GameStarted() || !CurrentGame().status.iron_man) {
        panel->AddCheckbox(0x80d, &g_settings.auto_save);
    }
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateGraphicsOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 22;
    AddRenderCheckbox(*panel, 0x816, W8_RENDER_OPTION_VIDEO_SYNC);
    panel->AddCheckbox(0x819, &g_settings.smooth_monster_animations);
    panel->AddCheckbox(0x81a, &g_settings.smooth_world_animations);
    AddRenderCheckbox(*panel, 0x81b, W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL);
    panel->AddCheckbox(0x818, &g_settings.monster_shadows);
    AddRenderCheckbox(*panel, 0x817, W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS);
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateAdvancedGraphicsOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 22;
    AddRenderCheckbox(*panel, 0x815, W8_RENDER_OPTION_MESH_SKY);
    AddRenderCheckbox(*panel, 0x812, W8_RENDER_OPTION_MIP_MAPPING);
    AddRenderCheckbox(*panel, 0x813, W8_RENDER_OPTION_MISSILE_LIGHTS);
    return panel;
}

static void ChangeVolume(int index, float value)
{
    switch (index) {
    case 0:
        SetMusicVolume(static_cast<unsigned char>(value));
        return;
    case 1:
        SetSoundEffectsVolume(static_cast<unsigned char>(value));
        return;
    case 2:
        g_settings.footstep_volume = static_cast<unsigned char>(value);
        wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::footsteps,
                                        g_settings.footstep_volume);
        return;
    case 3:
        g_settings.voice_volume = static_cast<unsigned char>(value);
        wiz8::sound::set_channel_volume(wiz8::sound::VolumeChannel::voice, g_settings.voice_volume);
        return;
    }
}

static void PreviewVolume(int index)
{
    switch (index) {
    case 1:
        wiz8::sound::play("Data\\Sound\\Misc\\Interface Swoosh 01.wav");
        break;
    case 2:
        PlayFootstep(W8_FOOTSTEP_SURFACE_MEDIUM_ROOM, W8_FOOTSTEP_MATERIAL_GRAVEL,
                     W8_FOOTSTEP_KIND_STEP);
        break;
    case 3: {
        wiz8::sound::play("Data\\Sound\\Misc\\Interface Vox 01.wav",
                          {.channel = wiz8::sound::VolumeChannel::voice});
        break;
    }
    }
}

static void SetAudioMuted(int index, bool muted)
{
    switch (index) {
    case 0:
        SetMusicMuted(muted);
        break;
    case 1:
        SetSoundEffectsMuted(muted);
        break;
    case 2:
        if (muted != 0) {
            DisableRenderOption(W8_RENDER_OPTION_FOOTSTEP_SOUND);
        } else {
            EnableRenderOption(W8_RENDER_OPTION_FOOTSTEP_SOUND);
        }
        break;
    case 3:
        SetVoiceMuted(muted);
        break;
    }
}

static std::unique_ptr<W8OptionsPanel> CreateInterfaceOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 22;
    auto* slider =
        panel->AddSlider(0x80e, static_cast<float>(g_settings.tooltip_delay_ms), 0, 1200);
    slider->on_release = [slider] {
        g_settings.tooltip_delay_ms = static_cast<int>(slider->m_position);
        SetUiHelpDelay(0);
    };
    auto* button = panel->AddChoiceButton(0x80f, [slider](auto* control) {
        g_settings.tooltips_enabled = !control->IsPressed();
        slider->SetEnabled(g_settings.tooltips_enabled);
    });
    button->SetTooltipIndex(0x810);
    if (!g_settings.tooltips_enabled) {
        button->SetPressed(true);
        slider->SetEnabled(false);
    }
    panel->m_content_top += 22;
    panel->AddCheckbox(0x80b, &g_settings.simplified_npc_interaction);
    panel->AddCheckbox(0x807, &g_settings.verbose_combat_messages);
    panel->AddCheckbox(0x7f3, &g_settings.ctrl_right_click_info);
    panel->AddCheckbox(0x7fc, &g_settings.skill_increase_messages);
    panel->AddCheckbox(0x806, &g_settings.numeric_hit_points);
    panel->AddCheckbox(0x80a, &g_settings.autoscroll_combat_messages);
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateAudioOptionsPanel()
{
    auto panel = std::make_unique<W8OptionsPanel>();
    for (int index = 0; index < 4; ++index) {
        int volume;
        bool muted = false;

        panel->m_content_top += 22;
        switch (index) {
        case 0:
            if (IsMusicMuted()) {
                volume = g_settings.muted_music_volume;
                muted = true;
            } else {
                volume = g_settings.music_volume;
            }
            break;
        case 1:
            if (IsSoundEffectsMuted()) {
                volume = g_settings.muted_sound_effects_volume;
                muted = true;
            } else {
                volume = g_settings.sound_effects_volume;
            }
            break;
        case 2:
            if (GetRenderOptionState(W8_RENDER_OPTION_FOOTSTEP_SOUND) == 0) {
                muted = true;
            }
            volume = g_settings.footstep_volume;
            break;
        case 3:
            if (IsVoiceMuted()) {
                volume = g_settings.muted_voice_volume;
                muted = true;
            } else {
                volume = g_settings.voice_volume;
            }
            break;
        }
        auto* slider =
            panel->AddSlider(g_options_audio_labels[index], static_cast<float>(volume), 0, 127,
                             [index](float value) { ChangeVolume(index, value); });
        slider->on_release = [index] { PreviewVolume(index); };

        auto* mute = panel->AddChoiceButton(0x822, [index, slider](auto* control) {
            SetAudioMuted(index, control->IsPressed());
            slider->SetEnabled(!control->IsPressed());
        });
        if (muted) {
            slider->SetEnabled(false);
            mute->SetPressed(true);
        }
    }
    panel->m_content_top += 22;
    panel->AddCheckbox(0x823, &g_settings.pc_confirmations);
    panel->AddCheckbox(0x824, &g_settings.pc_subtitles);
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateUnavailableOptionsPanel(int message)
{
    auto panel = std::make_unique<W8OptionsPanel>();
    panel->m_content_top += 44;
    W8ControlsRect bounds = {
        panel->ScreenBounds().x + 30, panel->ScreenBounds().y + panel->m_content_top,
        ToEdges(panel->ScreenBounds()).right - 30, ToEdges(panel->ScreenBounds()).bottom};
    panel->m_text_buffers.emplace_back(
        std::make_unique<W8TextBuffer>(&bounds, Data().strings[message], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignCenter, 4));
    panel->m_text_buffers[0]->SetLineHeight(22);
    return panel;
}

static std::unique_ptr<W8OptionsPanel> CreateOptionsPanel(OptionsScreen& screen, int panel)
{
    switch (panel) {
    case 0:
        return CreateGameOptionsPanel();
    case 1:
        return CreateMouseOptionsPanel();
    case 2:
        return CreateInterfaceOptionsPanel();
    case 3:
        return CreateAudioOptionsPanel();
    case 4:
        return CreateGraphicsOptionsPanel();
    case 5:
        return CreateAdvancedGraphicsOptionsPanel();
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
        return std::make_unique<W8OptionsKeyboardPanel>(screen, panel);
    case 11:
        if (GameStarted() && CurrentGame().status.iron_man) {
            return CreateUnavailableOptionsPanel(0x82c);
        }
        return std::make_unique<W8OptionsSaveLoadPanel>(screen, 11);
    case 12:
        if (GameStarted()) {
            if (CurrentGame().status.iron_man) {
                return CreateUnavailableOptionsPanel(0x82c);
            }
            if (CurrentGame().combat.has_value()) {
                return CreateUnavailableOptionsPanel(0x82b);
            }
        }
        return std::make_unique<W8OptionsSaveLoadPanel>(screen, 12);
    }
    return nullptr;
}

void OptionsScreen::CreateControls()
{
    const auto artwork = CatalogArtwork::Load(0xef, 0);
    for (int item = 0; item < 8; ++item) {
        const auto& row = g_options_menu_rows[item];
        const auto& frames = row.image_indices;
        const auto frame = [&](int index) {
            return index < 0 ? -1 : index + artwork.imageOffset();
        };
        auto& button = m_menu_buttons[item];
        button = std::make_unique<W8ButtonWidget>(m_menu_input);
        button->Configure(
            ButtonGraphics::Load(artwork.spriteSheet(), frame(frames[4]), frame(frames[0]),
                                 frame(frames[2]), frame(frames[1]), frame(frames[3])),
            {.on_click =
                 [this, item](W8ButtonWidget*) {
                     AfterUiDispatch([this, item] { ActivateMenu(item); });
                 },
             .policy = {.primary_toggle = item < 6 ? ButtonToggle::StayLatched : ButtonToggle::None,
                        .input_rule = ButtonInputRule::PrimaryPressedState},
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});
        const auto size = artwork.size(frames[0]);
        button->SetPosition(row.bounds.left, row.bounds.top);
        button->Resize(size.x, size.y);

        if (item == 5) button->SetEnabled(GameStarted());
        if (item == 4) button->SetEnabled(m_save_slots.size() > 1);
        for (const auto& bounds : row.child_bounds) {
            if (bounds.left == -1) continue;
            m_menu_input.add(
                SDL_Rect{bounds.left, bounds.top, bounds.right - bounds.left + 1,
                         bounds.bottom - bounds.top + 1},
                [this, item, press = ButtonInteraction{}](const UiPointerEvent& event) mutable {
                    if (event.action != UiPointerAction::Enter &&
                        (event.action != UiPointerAction::Up || event.captured_buttons))
                        PlayButtonSound(event.action);
                    if (event.action == UiPointerAction::Leave) press.leave({});
                    if (event.button == UiPointerButton::Left &&
                        event.action == UiPointerAction::Down)
                        press.press(SDL_BUTTON_LMASK, {});
                    if (event.action == UiPointerAction::Up && event.inside &&
                        event.button == UiPointerButton::Left &&
                        press.release(SDL_BUTTON_LMASK, {}).activate)
                        AfterUiDispatch([this, item] { ActivateMenu(item); });
                    return UiResult::Handled;
                });
        }
    }
    const auto arrows = CatalogArtwork::Load(0xf4, 0);
    for (int direction = 0; direction < 2; ++direction) {
        const int frame = arrows.imageOffset() + direction * 4;
        auto& button = m_navigation[direction];
        button = std::make_unique<W8ButtonWidget>(m_menu_input);
        button->Configure(
            ButtonGraphics::Load(arrows.spriteSheet(), frame + 3, frame, frame + 1, frame + 2, -1),
            {.on_click =
                 [this, direction](W8ButtonWidget*) {
                     AfterUiDispatch([this, direction] {
                         Scroll(direction == 0 ? -1 : 1);
                         UpdateNavigation();
                     });
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});
        const auto size = arrows.size(direction * 4);
        button->Resize(size.x, size.y);
    }
    const auto size = CatalogArtwork::Load(0xf3, 0).size(0);
    const W8ControlsRect bounds{0x11b, m_navigation_y, 0x11b + size.x, m_navigation_y + size.y};
    m_page_text =
        std::make_unique<W8TextBuffer>(&bounds, "", g_options_detail_font,
                                       g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle, 4);
}

void OptionsScreen::SelectPanel(int selected)
{
    if (selected == m_selected_panel) {
        return;
    }
    FinishTextEdit(true);
    if (!m_panels.empty()) m_panels[m_current_page]->SetActive(false);
    m_panels.clear();
    m_current_page = 0;
    m_selected_panel = selected;
    if (selected != -1) {
        for (int index = g_options_panel_ranges[selected].first;
             index <= g_options_panel_ranges[selected].last; ++index) {
            m_panels.push_back(CreateOptionsPanel(*this, index));
        }
        m_panels.front()->SetActive(true);
        UpdateNavigation();
    }
    for (int item = 0; item < 6; ++item) m_menu_buttons[item]->SetPressed(item == selected);
    if (m_selected_panel >= 0) {
        g_last_options_panel = m_selected_panel;
    }
}

void OptionsScreen::ShowNotification(bool allow_cancel, int message,
                                     std::function<void(bool)> closed)
{
    auto dialog = CreateMessageDialog();
    dialog->SetClientExtent(250, 200);
    dialog->SetMessage(Data().strings[message], 1, 50, true, allow_cancel, true, true, 0, 350);
    dialog->InputLayer().setModal(true);
    m_active_modal = std::move(dialog);
    m_notification_closed = std::move(closed);
}

void OptionsScreen::ActivateMenu(int item)
{
    if (!m_menu_buttons[item]->IsEnabled()) return;
    if (item < 6)
        SelectPanel(item);
    else if (item == 7)
        ReturnToPreviousScreen();
    else {
        FinishTextEdit(true);
        ShowNotification(true, 0x832, [this](bool accepted) { OnDialogClosed(accepted, 0); });
    }
}

unsigned char OptionsScreen::ProcessInput(const InputEvent* input)
{
    W8OptionsTextEditor* editor = m_text_editor.get();
    if (editor != 0) {
        if (IsTypingEvent(*input)) {
            if (input->key != SDLK_ESCAPE && input->key != SDLK_RETURN) {
                const char32_t character = input->type == InputEventType::Text ? input->text : 0;
                if (character != 0 && character < 0x80 &&
                    strchr("\\/:*?\"<>|", int(character)) != 0) {
                    return 0;
                }
                editor->m_fields.HandleInput(*input);
                return 0;
            }
            ScreenPointer().showSlot(W8_CURSOR_NONE);
            editor->owner->OnTextEditComplete(editor, input->key == SDLK_ESCAPE);
            m_text_editor.reset();
            return 1;
        }
    } else if (m_key_capture != 0 && input->type == InputEventType::KeyDown) {
        return m_key_capture->OnKey(input->key, input->modifiers);
    }
    return 0;
}

void OptionsScreen::Redraw()
{
    if (m_redraw_pending) {
        ClearPrimarySurface();
        ClearSurfaceRect(0, 0, 640, 480);
        CatalogArtwork::Load(0xee, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
        for (auto& button : m_menu_buttons) button->m_dirty = true;
        m_navigation_dirty = true;
        m_redraw_pending = false;
    }
    if (m_selected_panel != -1) {
        m_panels[m_current_page]->Redraw(GetFrameBufferSurface(), false);
    }
    for (int item = 0; item < 8; ++item) {
        auto& button = *m_menu_buttons[item];
        if (button.m_dirty && item == m_selected_panel)
            CatalogArtwork::Load(0xf0, 0).drawAndInvalidate(
                GetFrameBufferSurface(), g_options_menu_rows[item].item_id, 12, 15);
        button.Refresh();
    }
    if (m_navigation_visible &&
        (m_navigation_dirty || m_navigation[0]->m_dirty || m_navigation[1]->m_dirty)) {
        auto& target = GetFrameBufferSurface();
        CatalogArtwork::Load(0xf3, 0).drawAndInvalidate(target, 0, 0x11b, m_navigation_y);
        for (auto& button : m_navigation) {
            button->Refresh(true);
        }
        m_page_text->RenderToTarget(0, true, target);
        m_navigation_dirty = false;
    }
    if (m_active_modal != 0) {
        m_active_modal->RefreshModal();
    }
    if (m_text_editor != 0) {
        m_text_editor->m_fields.RenderAll(GetFrameBufferSurface());
    }
}

W8OptionsSlider::W8OptionsSlider(UiLayer& layer, SDL_Point origin, float position, float minimum,
                                 float maximum, bool alternate)
    : m_minimumPosition(minimum), m_maximumPosition(maximum), m_position(position),
      m_origin(origin), m_backgroundSprite(alternate ? 4 : 0)
{
    const auto size = m_artwork.size(m_backgroundSprite);
    m_thumbWidth = m_artwork.size(1).x;
    m_trackLength = size.x - m_thumbWidth;
    UpdatePixelPosition();
    m_target = &layer.add(SDL_Rect{origin.x, origin.y, size.x + 1, size.y + 1},
                          [this](const UiPointerEvent& event) { return HandlePointer(event); });
}

void W8OptionsSlider::UpdatePixelPosition()
{
    m_position = std::clamp(m_position, m_minimumPosition, m_maximumPosition);
    m_pixelPosition = static_cast<int>((m_position - m_minimumPosition) /
                                       (m_maximumPosition - m_minimumPosition) * m_trackLength);
    m_dirty = true;
}

void W8OptionsSlider::CancelDrag()
{
    m_target->releaseCapture();
    m_hovered = m_dragging = false;
    m_dirty = true;
}

void W8OptionsSlider::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) CancelDrag();
    m_dirty = true;
}

UiResult W8OptionsSlider::HandlePointer(const UiPointerEvent& event)
{

    if (!m_enabled) return UiResult::Handled;
    const int x = event.position.x - g_cursor_hotspot_x - m_origin.x;
    switch (event.action) {
    case UiPointerAction::Down:
        if (event.button != UiPointerButton::Left) break;
        m_dragCoordinate = x;
        m_dragging = true;
        if (m_hovered) break;
        m_hovered = true;
        m_position = (x - m_thumbWidth / 2) / static_cast<float>(m_trackLength) *
                         (m_maximumPosition - m_minimumPosition) +
                     m_minimumPosition;
        UpdatePixelPosition();
        if (on_change) on_change(m_position);
        break;
    case UiPointerAction::Move:
        if (m_dragging) {
            if (x < 0 || x > m_trackLength + m_thumbWidth / 2) break;
            m_position += (x - m_dragCoordinate) / static_cast<float>(m_trackLength) *
                          (m_maximumPosition - m_minimumPosition);
            m_dragCoordinate = x;
            UpdatePixelPosition();
            if (on_change) on_change(m_position);
        } else {
            const bool hovered = m_pixelPosition <= x && x <= m_pixelPosition + m_thumbWidth;
            m_dirty |= m_hovered != hovered;
            m_hovered = hovered;
        }
        break;
    case UiPointerAction::Up:
        if (event.button == UiPointerButton::Left && m_dragging) {
            CancelDrag();
            if (on_release) on_release();
        }
        break;
    case UiPointerAction::Leave:
        if (!m_dragging) {
            m_hovered = false;
            m_dirty = true;
        }
        break;
    default:
        break;
    }
    return UiResult::Handled;
}

void W8OptionsSlider::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!full_redraw && !m_dirty) return;
    m_dirty = false;
    const auto [left, top] = m_origin;
    m_artwork.drawAndInvalidate(target, m_backgroundSprite, left, top);
    m_artwork.draw(target, !m_enabled ? 3 : m_hovered ? 2 : 1, left + m_pixelPosition, top);
    if (m_enabled && m_pixelPosition > 13) {
        const auto color = Get16BPPColor(SDL_Color{0, 255, 0, SDL_ALPHA_OPAQUE});
        const SDL_Rect first{left + 13, top + 7, std::min(m_pixelPosition - 1, 73) - 13, 2};
        SDL_FillSurfaceRect(&target, &first, color);
        if (m_pixelPosition > 83) {
            const SDL_Rect second{left + 83, top + 7, m_pixelPosition - 84, 2};
            SDL_FillSurfaceRect(&target, &second, color);
        }
    }
}

W8ButtonWidget* W8OptionsPanel::AddButton(int x, int y, int artwork, int normal, int pressed,
                                          int hover, int pressed_hover, int disabled,
                                          W8ButtonCallback callback)
{
    const auto art = CatalogArtwork::Load(artwork, 0);
    auto& button = m_buttons.emplace_back(std::make_unique<W8ButtonWidget>(m_input));
    button->Configure(
        ButtonGraphics::Load(art.spriteSheet(), disabled, normal, hover, pressed, pressed_hover),
        {.on_click = std::move(callback),
         .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
         .priority = UI_PRIORITY_LOWEST,
         .menu_artwork = true});

    button->SetPosition(m_bounds.x + x, m_bounds.y + y);
    const auto size = art.size(normal);
    button->Resize(size.x, size.y);
    return button.get();
}

/* All labelled option rows use the same geometry and text formatting. */
static void AddOptionsLabel(W8OptionsPanel& panel, int label)
{
    W8ControlsRect bounds = {
        panel.ScreenBounds().x + 20, panel.ScreenBounds().y + panel.m_content_top,
        ToEdges(panel.ScreenBounds()).right, panel.ScreenBounds().y + panel.m_content_top + 22};
    panel.m_text_buffers.emplace_back(
        std::make_unique<W8TextBuffer>(&bounds, Data().strings[label], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));
}

void W8OptionsPanel::AddCheckbox(int label, bool selected, std::function<void(bool)> changed)
{
    const bool enabled = static_cast<bool>(changed);
    AddOptionsLabel(*this, label);
    auto* checkbox = AddButton(0x14d, m_content_top - 2, 0xf1, 2, 0, 3, 1, -1,
                               [changed = std::move(changed)](auto* button) {
                                   if (changed) changed(button->IsPressed());
                               });
    checkbox->Policy().primary_toggle = ButtonToggle::Release;
    checkbox->SetPressed(selected);
    checkbox->SetEnabled(enabled);
    m_content_top += 44;
}

W8ButtonWidget* W8OptionsPanel::AddChoiceButton(int label, W8ButtonCallback callback)
{
    const W8ControlsRect bounds{m_bounds.x, m_bounds.y + m_content_top, m_bounds.x + 0x147,
                                m_bounds.y + m_content_top + 22};
    m_text_buffers.emplace_back(
        std::make_unique<W8TextBuffer>(&bounds, Data().strings[label], g_options_detail_font,
                                       g_W8TextBufferAlignTop | g_W8TextBufferAlignRight, 4));
    auto* button = AddButton(0x151, m_content_top + 1, 0xf1, 4, 6, 5, 7, -1, std::move(callback));
    button->Policy().primary_toggle = ButtonToggle::Release;
    m_content_top += 22;
    return button;
}

W8OptionsSlider* W8OptionsPanel::AddSlider(int label, float value, float minimum, float maximum,
                                           std::function<void(float)> changed, bool alternate)
{
    AddOptionsLabel(*this, label);
    auto& slider = m_owned_sliders.emplace_back(std::make_unique<W8OptionsSlider>(
        m_input, SDL_Point{ScreenBounds().x + 0xc9, ScreenBounds().y + m_content_top - 2}, value,
        minimum, maximum, alternate));
    slider->on_change = std::move(changed);
    m_content_top += 22;
    return slider.get();
}

void W8OptionsPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    const bool repaint = full_redraw || m_dirty;
    if (repaint) {
        m_background.drawAndInvalidate(target, m_backgroundImage, m_bounds.x, m_bounds.y);
        for (auto& text : m_text_buffers) text->RenderToTarget(0, true, target);
    }
    for (auto& button : m_buttons) {
        button->m_dirty |= repaint;
        button->Refresh();
    }
    for (auto& slider : m_owned_sliders) slider->Redraw(target, repaint);
    m_dirty = false;
}

W8OptionsPanel::W8OptionsPanel()
{
    m_input.setEnabled(false);
    const auto size = CatalogArtwork::Load(0xf2, 0).size(0);
    m_bounds = {0x104, 0, size.x, size.y};
}

void W8OptionsPanel::SetActive(bool active)
{
    m_active = active;
    if (!active)
        for (auto& slider : m_owned_sliders) slider->CancelDrag();
    m_input.setEnabled(active);
    for (auto& button : m_buttons) button->SetVisible(active);
    InvalidateAll();
}

void W8OptionsPanel::SetCurrent(int current)
{
    m_current = current;
    InvalidateAll();
}

int OptionsScreen::CurrentPage() const
{
    return m_selected_panel == 4 || m_selected_panel == 5 ? m_panels[0]->m_current : m_current_page;
}

int OptionsScreen::PageCount() const
{
    if (m_selected_panel == 4) return (static_cast<int>(m_save_slots.size()) - 2) / 5 + 1;
    if (m_selected_panel == 5) return (static_cast<int>(m_save_slots.size()) - 1) / 5 + 1;
    return static_cast<int>(m_panels.size());
}

void OptionsScreen::Scroll(int delta)
{
    const int current = CurrentPage();
    const int next = std::clamp(current + delta, 0, PageCount() - 1);
    if (next == current) return;
    if (m_selected_panel == 4 || m_selected_panel == 5) {
        m_panels[0]->SetCurrent(next);
    } else {
        m_panels[m_current_page]->SetActive(false);
        m_current_page = next;
        m_panels[m_current_page]->SetActive(true);
    }
}

void OptionsScreen::UpdateNavigation()
{
    const int current = CurrentPage();
    const int count = PageCount();
    const bool save_menu = m_selected_panel == 4 || m_selected_panel == 5;
    const bool save_panel =
        dynamic_cast<W8OptionsSaveLoadPanel*>(m_panels.front().get()) != nullptr;
    m_navigation_y = save_panel ? 0x180 : 0x1a8;
    m_navigation_visible = save_menu ? save_panel : count > 1;
    for (int direction = 0; direction < 2; ++direction) {
        auto& button = *m_navigation[direction];
        button.SetPosition(0x11b + (direction == 0 ? 3 : 0x11f), m_navigation_y + 3);
        button.SetEnabled(direction == 0 ? current > 0 : current < count - 1);
        button.SetVisible(m_navigation_visible);
    }
    const auto size = CatalogArtwork::Load(0xf3, 0).size(0);
    const W8ControlsRect bounds{0x11b, m_navigation_y + 1, 0x11b + size.x, m_navigation_y + size.y};
    m_page_text->SetClipBounds(bounds);
    m_page_text->SetLayoutBounds(&bounds, true);
    m_page_text->SetText(fmt::format("{:d} / {:d}", current + 1, count), g_options_detail_font);
    m_navigation_dirty = true;
}

/* The dialog callback's teardown decision is part of the Options screen's
   state transition: only an accepted close outside combat saves an active
   party, then the common audio-state update runs for every accepted close. */
void OptionsScreen::OnDialogClosed(bool accepted, int)
{
    if (accepted) {
        if (GameStarted() && !CurrentGame().combat.has_value() &&
            (CurrentGame().party.participantCount() != 0)) {
            AutoSaveIfAllowed(true);
        }
        RequestExitScreen();
    }
}

bool OptionsScreen::leave(ScreenDeparture)
{
    SelectPanel(-1);
    return true;
}

OptionsScreen::OptionsScreen() = default;
OptionsScreen::~OptionsScreen() = default;

/* The selected panel is determined by the transition mode, with the in-game
   branch preserving the combat and save-state overrides. */
bool OptionsScreen::enter(const ScreenRequest& request)
{
    int selected;
    const W8OptionsMode mode = std::get<W8OptionsMode>(request.mode);

    SetWorldViewport(GameInProgress() ? &CurrentGame() : nullptr, {0, 0, 0x281, 0x1e1});
    SetUiCanvasAlphaTestEnabled(false);
    ScreenPointer().reset();
    g_wiz_text_font_secondary.resetPalette();
    auto& current = m_save_slots.emplace_back(std::make_unique<W8SaveSlot>());
    if (GameStarted()) {
        FillCurrentSaveSlot(*current);
    }
    EnumerateSaveSlots(m_save_slots);
    CreateControls();

    if (mode == W8_OPTIONS_LOAD) {
        selected = 4;
    } else if (mode == W8_OPTIONS_SAVE) {
        selected = 5;
    } else if (mode == W8_OPTIONS_IN_GAME) {
        if (g_last_options_panel == 4) {
            selected = 5;
        } else {
            selected = g_last_options_panel;
            if (g_last_options_panel == 5) {
                if (CurrentGame().combat.has_value()) {
                    selected = 4;
                }
                if (CurrentGame().status.iron_man) {
                    selected = 0;
                }
            }
        }
    } else {
        selected = 0;
    }
    SelectPanel(selected);
    return true;
}

/* The frame owns modal retirement, input refusal order and redraw sequencing.
   Options-specific input gets first refusal, followed by shared region
   dispatch; only an unhandled Escape requests the screen transition. */
void OptionsScreen::frame()
{
    RepositionAmbientSounds(GetWorld());
    UpdateAmbientSounds(GetWorld());
    ServiceMusicPlaylist();
    auto& active_modal = m_active_modal;
    if (active_modal) {
        if (!active_modal->ProcessInput()) {
            const bool accepted = active_modal->accepted;
            auto closed = std::exchange(m_notification_closed, {});
            active_modal.reset();
            if (closed) closed(accepted);
            m_redraw_pending = true;
            if (m_selected_panel != -1) {
                m_panels[m_current_page]->InvalidateAll();
            }
        }
    }

    if (!m_active_modal)
        PumpUiInput({
            .before =
                [this](const InputEvent& input) {
                    return ProcessInput(&input) ? InputRoute::Handled : InputRoute::Pass;
                },
            .unhandled =
                [](const InputEvent& input) {
                    if (input.type == InputEventType::KeyDown && input.key == SDLK_ESCAPE) {
                        ReturnToPreviousScreen();
                    }
                    return InputRoute::Handled;
                },
        });
    Redraw();
    PresentGameFrame(GameInProgress() ? &CurrentGame() : nullptr);
}
std::string_view GetLastSaveName(void)
{
    return g_options_last_save_name;
}
