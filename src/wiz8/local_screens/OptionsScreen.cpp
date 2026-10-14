#include "wiz8/dialog_code/ButtonAppearance.h"
#include <functional>
#include <SDL3/SDL_mouse.h>
#include "wiz8/button_interaction.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/ui_layer.h"
#include <cstdint>
#include <algorithm>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <memory>
#include <utility>
#include <wiz8/filesystem.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_stdinc.h>
#include <string_view>
#include <string>
#include <cctype>
#include <cstring>

#include "soundman.h"
#include "Font.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/OptionsScreen.h"
#include "wiz8/local_screens/JournalScreen.h"
#include "wiz8/local_screens/MGSKeyboard.h"
#include "wiz8/xstatus.h"
#include "wiz8/cursor.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/engine_code/AmbientSound.h"
#include "wiz8/engine_code/Levels.h"
#include "wiz8/music_playlist.h"
#include "wiz8/ui_input.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/fonts.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/text_input.h"
#include "wiz8/local_code/Strings.h"
#include "himage.h"
#include "input.h"
#include "surrender/srColorSurface.h"
#include "dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "difficulty.h"
#include "file_time.h"
#include "layouts/world.h"
#include "mgs_commands.h"
#include "surrender/srPixelConvert.h"

/* Shared zero-initialized wide string: binary-wide DATA references read it as
   empty text and as a sprintf format argument. No recovered writer exists. */

/* Suppress hover sounds on the registered options-row children. */
// GLOBAL: WIZ8 0x005ed590

// GLOBAL: WIZ8 0x0069c1c8
static bool g_options_first_frame;
// GLOBAL: WIZ8 0x0069c138
W8OptionsValues g_options_values;
// GLOBAL: WIZ8 0x0069c254
std::unique_ptr<W8OptionsScreen> g_options_screen;
// GLOBAL: WIZ8 0x0069c1cc
std::string g_options_last_save_name;

// GLOBAL: WIZ8 0x0064d72c
static int g_last_options_panel = 5;

// GLOBAL: WIZ8 0x0064d0b8
static W8OptionsMenuRow g_options_menu_rows[8] = {
    {1, {{20, 51, 69, 137}, {-1, -1, -1, -1}}, {16, 194, 0, 0}, {0, 2, 1, -1, -1}},
    {4, {{90, 72, 117, 117}, {217, 72, 244, 117}}, {16, 220, 0, 0}, {3, 5, 4, -1, -1}},
    {3, {{132, 29, 201, 102}, {-1, -1, -1, -1}}, {16, 246, 0, 0}, {6, 8, 7, -1, -1}},
    {2, {{124, 141, 194, 172}, {-1, -1, -1, -1}}, {16, 272, 0, 0}, {9, 11, 10, -1, -1}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 322, 0, 0}, {12, 14, 13, -1, 25}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 360, 0, 0}, {15, 17, 16, -1, 24}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 398, 0, 0}, {18, 20, 19, -1, -1}},
    {0, {{-1, -1, -1, -1}, {-1, -1, -1, -1}}, {7, 436, 0, 0}, {21, 23, 22, -1, -1}}};

// GLOBAL: WIZ8 0x0064d078
static W8OptionsPanelRange g_options_panel_ranges[8] = {{0, 2},   {3, 3},   {4, 5}, {6, 10},
                                                        {11, 11}, {12, 12}, {0, 0}, {0, 0}};
// GLOBAL: WIZ8 0x0064d730
static int g_options_difficulty_labels[3] = {0x7f8, 0x7f9, 0x7fa};
// GLOBAL: WIZ8 0x0064d73c
static int g_options_camera_rotation_labels[3] = {0x7fe, 0x7ff, 0x800};
// GLOBAL: WIZ8 0x0064d748
static int g_options_combat_mode_labels[2] = {0x7f6, 0x7f5};
// GLOBAL: WIZ8 0x0064d750
static int g_options_audio_labels[4] = {0x81e, 0x81f, 0x821, 0x820};

struct W8OptionsKeyName {
    unsigned short key;
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

// GLOBAL: WIZ8 0x0064d2f8
static W8OptionsKeyName g_options_key_names[] = {
    {112, 0, 2158}, {113, 0, 2159}, {114, 0, 2160}, {115, 0, 2161}, {116, 0, 2162}, {117, 0, 2163},
    {118, 0, 2164}, {119, 0, 2165}, {120, 0, 2166}, {121, 0, 2167}, {122, 0, 2168}, {123, 0, 2169},
    {9, 0, 2170},   {144, 0, 2171}, {8, 0, 2172},   {45, 0, 2173},  {46, 0, 2174},  {35, 0, 2175},
    {34, 0, 2176},  {33, 0, 2177},  {36, 0, 2178},  {13, 0, 2179},  {32, 0, 2180},  {40, 0, 2181},
    {37, 0, 2182},  {39, 0, 2183},  {38, 0, 2184},  {19, 0, 2185},  {145, 0, 2186}, {20, 0, 2187},
    {96, 0, 2188},  {97, 0, 2189},  {98, 0, 2190},  {99, 0, 2191},  {100, 0, 2192}, {101, 0, 2193},
    {102, 0, 2194}, {103, 0, 2195}, {104, 0, 2196}, {105, 0, 2197}, {106, 0, 2198}, {107, 0, 2199},
    {108, 0, 2200}, {109, 0, 2201}, {110, 0, 2202}, {111, 0, 2203}, {144, 0, 2171}, {192, 0, 2204},
    {0, 0, 2157}};

// GLOBAL: WIZ8 0x0064d480
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

// GLOBAL: WIZ8 0x0064d6f0
static W8OptionsKeyboardPage g_options_keyboard_pages[5] = {
    {0x835, W8_MGS_COMMAND_MOVE_FORWARD, W8_MGS_COMMAND_LOOK_LEVEL},
    {0x836, W8_MGS_COMMAND_PAUSE_GAME, W8_MGS_COMMAND_JOURNAL},
    {0x837, W8_MGS_COMMAND_SELECT_PC_1, W8_MGS_COMMAND_SELECT_RECRUITED_2},
    {0x838, W8_MGS_COMMAND_TEXTBOX_PAGE_UP, W8_MGS_COMMAND_TEXTBOX_CLEAR},
    {0x839, W8_MGS_COMMAND_START_COMBAT_ROUND, W8_MGS_COMMAND_REPEAT_ACTION}};

W8OptionsPanelSet::W8OptionsPanelSet()
    : m_page_count(0), m_compact_layout(0), m_hide_navigation(0), m_active(false), m_current(0)
{
}

W8OptionsPanelSet::~W8OptionsPanelSet() = default;

W8OptionsKeyboardPanel::W8OptionsKeyboardPanel(int panel)
    : W8OptionsPanel(), m_panel(panel), m_captured_button(0)
{
}
W8OptionsSaveLoadPanel::W8OptionsSaveLoadPanel(int panel) : W8OptionsPanel(), m_panel(panel)
{
    m_background = CatalogArtwork::Load(0xf7);
    m_backgroundImage = 0;
}

W8OptionsUnavailablePanel::W8OptionsUnavailablePanel(int message)
    : W8OptionsPanel(), m_message(message)
{
}

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

void W8OptionsSaveLoadPanel::Populate()
{
    for (int index = 0; index < 5; ++index) {
        m_rows.emplace_back(std::make_unique<W8OptionsSaveRow>(m_input, *this, index, m_panel == 12));
    }

    if (m_panel == 11) {
        m_delete_button =
            AddButton(0xd, 0x1b0, 0xf8, 6, 8, 7, -1, -1, [this](auto* button) { AfterUiDispatch([this, button] { OnPrimary(button); }); });
    } else {
        m_delete_button = 0;
    }

    int first_sprite = m_panel == 11 ? 3 : 0;
    m_action_button = AddButton(0xf5, 0x1b0, 0xf8, first_sprite, first_sprite + 2, first_sprite + 1, -1, -1, [this](auto* button) { AfterUiDispatch([this, button] { OnPrimary(button); }); });

    if (m_panel == 11 && static_cast<int>(g_options_screen->m_save_slots.size()) == 1) {
        m_delete_button->SetEnabled(false);
        m_action_button->SetEnabled(false);
    }

    int page = 0;
    int selected = 0;
    if (m_panel == 11 && g_options_last_save_name[0] != 0) {
        for (int index = 1; index < static_cast<int>(g_options_screen->m_save_slots.size()); ++index) {
            if (strcmp(g_options_last_save_name.c_str(), (g_options_screen->m_save_slots[index])->name.c_str()) == 0) {
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
    if (active && g_options_screen->m_text_editor == 0 && m_panel == 12 &&
        m_selected == 0 && m_current == 0) {
        OnEditSaveName();
    }
}

/* Commit or cancel the active save-name edit before changing options pages. */
static void FinishOptionsTextEdit(W8OptionsScreen* screen, unsigned char cancelled)
{
    W8OptionsTextEditor* editor = screen->m_text_editor.get();
    if (!editor)
        return;
    editor->owner->OnTextEditComplete(editor, cancelled);
    screen->m_text_editor.reset();
}

void W8OptionsSaveLoadPanel::SetCurrent(int current)
{
    FinishOptionsTextEdit(g_options_screen.get(), 1);
    m_current = current;
    InvalidateAll();

    int first = current * 5;
    if (m_panel == 11) {
        ++first;
    }
    int limit = static_cast<int>(g_options_screen->m_save_slots.size());
    if (first + 5 < limit) {
        limit = first + 5;
    }
    for (int slot = first; slot < limit; ++slot) {
        m_rows[slot - first]->SetSave(*g_options_screen->m_save_slots[slot]);
    }
    for (; limit < first + 5; ++limit) {
        m_rows[limit - first]->ClearSave();
    }
    SelectRow(0);
    if (g_options_screen->m_text_editor == 0 && m_panel == 12 && m_selected == 0 &&
        m_current == 0) {
        OnEditSaveName();
    }
}

void W8OptionsSaveLoadPanel::OnPrimary(W8ButtonWidget* control)
{
    if (control == m_delete_button) {
        g_options_screen->ShowNotification(true, 0x828, [this](bool accepted) { OnDialogClosed(accepted, 1); });
        return;
    }
    if (control != m_action_button) {
        return;
    }
    if (m_panel == 11) {
        LoadSelectedSave();
        return;
    }

    W8OptionsTextEditor* editor = g_options_screen->m_text_editor.get();
    FinishOptionsTextEdit(g_options_screen.get(), 0);
    if (editor == 0 && m_current == 0 && m_selected == 0) {
        SaveSelectedSave();
    } else {
        g_options_screen->ShowNotification(true, 0x829, [this](bool accepted) { OnDialogClosed(accepted, 2); });
    }
}

void W8OptionsSaveLoadPanel::OnDialogClosed(bool accepted, int value)
{
    int selected_slot;
    if (!accepted) {
        if (value == 2) {
            if (g_options_screen->m_text_editor != 0 || m_panel != 12 ||
                m_selected != 0) {
                return;
            }
            selected_slot = m_current;
        } else if (value == 3) {
            selected_slot = m_current * 5 + m_selected;
            (g_options_screen->m_save_slots[selected_slot])->name = m_previous_name;
            W8OptionsSaveRow* row = m_rows[m_selected].get();
            row->SetSave(*g_options_screen->m_save_slots[selected_slot]);
            if (g_options_screen->m_text_editor != 0 || m_panel != 12 ||
                m_selected != 0) {
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
            if (g_options_screen->m_text_editor != 0 || m_panel != 12 ||
                m_selected != 0) {
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

void W8OptionsSaveLoadPanel::OnTextEditComplete(W8OptionsTextEditor* editor, unsigned char cancelled)
{
    int selected_slot = m_current * 5 + m_selected;
    m_rows[m_editing_row]->m_editing = false;
    if (cancelled != 0) {
        m_rows[m_editing_row]->button.m_dirty = true;
        return;
    }

    W8SaveSlot& slot = *g_options_screen->m_save_slots[selected_slot];
    m_previous_name = slot.name;
    slot.name = editor->m_fields.Text(0);
    if (slot.name.size() == 0) {
        slot.name = m_previous_name;
        g_options_screen->ShowNotification(false, 0x82a, [this](bool accepted) { OnDialogClosed(accepted, 4); });
        return;
    }

    W8OptionsSaveRow* row = m_rows[m_selected].get();
    row->SetSave(slot);
    if (m_current == 0 && m_selected == 0) {
        if (SaveSlotFileExists(slot.name) == 0) {
            SaveSelectedSave();
        } else {
            g_options_screen->ShowNotification(true, 0x829, [this](bool accepted) { OnDialogClosed(accepted, 2); });
        }
    } else {
        g_options_screen->ShowNotification(true, 0x829, [this](bool accepted) { OnDialogClosed(accepted, 3); });
    }
}

void W8OptionsSaveLoadPanel::OnEditSaveName()
{
    int selected_slot = m_current * 5 + m_selected;
    g_options_screen->BeginSaveNameEdit(this, m_selected,
                                        g_options_screen->m_save_slots[selected_slot]->name);
    m_editing_row = m_selected;
    m_rows[m_editing_row]->m_editing = true;
}

void W8OptionsSaveLoadPanel::SelectRow(int selected)
{
    if (m_selected == selected) return;
    m_selected = selected;
    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i)
        m_rows[i]->button.SetPressed(i == selected);
    FinishOptionsTextEdit(g_options_screen.get(), 1);
    if (m_panel == 12 && m_current == 0 && selected == 0) OnEditSaveName();
}

void W8OptionsSaveLoadPanel::DeleteSelectedSave()
{
    int selected_slot = m_current * 5 + 1 + m_selected;
    W8SaveSlot& slot = *g_options_screen->m_save_slots[selected_slot];
    std::string path;
    path = fmt::format("{}\\{}.{}", "Saves", slot.name, g_save_extension);
    if (wiz8::remove_file(path) == 0) {
        g_options_screen->ShowNotification(false, 0x82f, [this](bool accepted) { OnDialogClosed(accepted, 0); });
        return;
    }

    g_options_screen->m_save_slots.erase(g_options_screen->m_save_slots.begin() + selected_slot);
    if (static_cast<int>(g_options_screen->m_save_slots.size()) == 1) {
        selected_slot = -1;
        if (m_panel == 11) {
            m_delete_button->SetEnabled(false);
            m_action_button->SetEnabled(false);
        }
    } else if (selected_slot >= static_cast<int>(g_options_screen->m_save_slots.size())) {
        --selected_slot;
        if (m_selected == 0) {
            --m_current;
        }
    }

    if (g_options_screen->m_selected_panel == 4) {
        g_options_screen->m_panel[4]->m_page_count =
            (static_cast<int>(g_options_screen->m_save_slots.size()) - 2) / 5 + 1;
    } else if (g_options_screen->m_selected_panel == 5) {
        g_options_screen->m_panel[5]->m_page_count =
            (static_cast<int>(g_options_screen->m_save_slots.size()) - 1) / 5 + 1;
    }
    g_options_screen->UpdateNavigation();
    SetCurrent(m_current);
    SelectRow(selected_slot == -1 ? -1 : (selected_slot - 1) % 5);
}

void W8OptionsSaveLoadPanel::LoadSelectedSave()
{
    int selected_slot = m_current * 5 + 1 + m_selected;
    W8SaveSlot& slot = *g_options_screen->m_save_slots[selected_slot];
    if (slot.version_major + slot.version_minor * 0.1f + slot.version_patch * 0.01f <= 1.24f) {
        SetLastSaveName(slot.name);
        if (g_status.game_started) {
            ClearHeldItemDisplay();
        }
        RequestScreenTransition();
        g_pending_screen_state.mode = 1;
        g_pending_screen_state.parameter = slot.level_id;
        g_pending_screen_state.name = slot.name;
        SetPendingScreenState(W8_SCREEN_PLEASE_WAIT);
    }
}

void W8OptionsSaveLoadPanel::SaveSelectedSave(std::string_view previous_name)
{
    int selected_slot = m_current * 5 + m_selected;
    W8SaveSlot& slot = *g_options_screen->m_save_slots[selected_slot];
    if (slot.name.size() == 0) {
        g_options_screen->ShowNotification(false, 0x82a, [this](bool accepted) { OnDialogClosed(accepted, 0); });
        return;
    }

    SetLastSaveName(slot.name);
    RequestScreenTransition();
    g_pending_screen_state.mode = 2;
    g_pending_screen_state.name = slot.name;
    const std::string old_name(previous_name);
    // Game save paths resolve ASCII names case-insensitively. Never remove the
    // new file when a rename changes only capitalization.
    g_pending_screen_state.previous_save_name =
        SDL_strcasecmp(old_name.c_str(), slot.name.c_str()) == 0
            ? std::string()
            : old_name;
    g_pending_screen_state.parameter_3 =
        new W8SaveScreenshot(g_options_screen->m_save_slots[0]->screenshot);
    SetPendingScreenState(W8_SCREEN_PLEASE_WAIT);
}

W8OptionsSaveRow::W8OptionsSaveRow(UiLayer& layer, W8OptionsSaveLoadPanel& owner,
                                 int index, bool save_mode) : button(layer)
{
    auto art = CatalogArtwork::Load(0xf9, 0);
    const int normal = save_mode ? 0 : 2;
    button.on_double_click = [&owner, this, index](auto*) {
        if (!m_editing && owner.m_selected == index) {
            AfterUiDispatch([&owner] { owner.OnPrimary(owner.m_action_button); });
        }
    };
    button.Configure(
        ButtonGraphics::Load(art.spriteSheet(), 4, normal, -1, normal + 1, -1),
        [&owner, this, index, save_mode](auto*) {
            AfterUiDispatch([&owner, this, index, save_mode] {
                const auto point = button.GetMousePosition();
                if (!m_editing && owner.m_selected == index && save_mode &&
                    point.x - button.Bounds().x >= 0x5c && point.y - button.Bounds().y >= 0x33)
                    owner.OnEditSaveName();
                owner.SelectRow(index);
            });
        },
        0);
    button.policy.primary_toggle = ButtonToggle::StayLatched;
    button.policy.input_rule = ButtonInputRule::PrimaryPressedState;
    button.menu_artwork = true;
    button.SetPosition(0x104 + 10, 0x12 + index * 0x49);
    const auto size = art.size(normal);
    button.Resize(size.x, size.y);
}

void W8OptionsSaveRow::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!full_redraw && !button.m_dirty) {
        return;
    }
    button.DrawAndInvalidate(target, true);
    if (!button.IsEnabled() || m_save == 0) {
        return;
    }

    int x = button.Bounds().x;
    int y = button.Bounds().y;
    if (m_save->screenshot.capture_result == 0) {
        CatalogArtwork::Load(0xf6, 0).draw(target, 0, x + 6, y + 6);
    } else {
        auto portrait = std::make_unique<srColorSurface>(srPixelConvert::SURFACE_ARGB1555,
                                                      m_save->screenshot.pixels, 0x50, 0x3c, 0xa0);
        DrawColorSurface(portrait.get(), x + 6, y + 6);
    }

    std::string level_name = m_save->level_id == 0x38
                                    ? g_default_level
                                    : g_localized_strings[g_level_name_indices[m_save->level_id]];
    std::string timestamp;
    timestamp = fmt::format("{:d}-{:02d}-{:02d} {:2d}:{:02d}", m_save->timestamp.year,
                            m_save->timestamp.month, m_save->timestamp.day, m_save->timestamp.hour,
                            m_save->timestamp.minute);

    int text_x = x + 0x5e;
    if (m_save->version_major + m_save->version_minor * 0.1f + m_save->version_patch * 0.01f <=
        1.24f) {
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 9}, level_name);
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x16},
                 fmt::format("{} {:3d}, {:2d}:{:02d}", g_localized_strings[0x826],
                             m_save->game_time_days, m_save->game_time_ms / 3600000,
                             (m_save->game_time_ms / 60000) % 60));
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x23}, timestamp);
    } else {
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 9},
                 g_localized_strings[0x830]);
        DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x16},
                 g_localized_strings[0x831]);
    }
    DrawText(target, g_wiz_text_font_secondary, {text_x, y + 0x34}, m_save->name);
    if (m_save->iron_man) {
        DrawText(target, g_wiz_text_font_secondary, {x + 0x156, y + 9},
                 g_localized_strings[0x827]);
    }
}

void W8OptionsSaveLoadPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    const bool repaint = full_redraw || m_dirty;
    W8OptionsPanel::Redraw(target, full_redraw);
    for (auto& row : m_rows) row->Redraw(target, repaint);
}

void W8OptionsScreen::BeginSaveNameEdit(W8OptionsSaveLoadPanel* owner, int row,
                                        std::string_view text)
{
    auto editor = std::make_unique<W8OptionsTextEditor>();
    editor->m_fields.Add(0x16b, row * 0x49 + 0x46, 0xfa, 0xc, 0x7f, text, 0x3b, 0xf, true);
    editor->m_fields.Focus(0);
    editor->owner = owner;
    m_text_editor = std::move(editor);
}

void W8OptionsKeyButton::SetKey(unsigned short key)
{
    if (key == VK_ESCAPE) {
        return;
    }

    MGSKeyBinding* binding =
        g_mgs_keyboard->GetBinding(g_mgs_keyboard->FindBinding(m_primary_binding));
    if (binding != 0) {
        binding->key = key;
    }
    if (m_secondary_binding != W8_MGS_COMMAND_NONE) {
        binding = g_mgs_keyboard->GetBinding(g_mgs_keyboard->FindBinding(m_secondary_binding));
        if (binding != 0) {
            binding->key = key;
        }
    }
    SetKeyText(key);
}

void W8OptionsKeyButton::SetKeyText(unsigned short key)
{
    std::string text;
    for (const auto* name = g_options_key_names; name->key != 0; ++name) {
        if (name->key == key) {
            text = g_localized_strings[name->label];
            break;
        }
    }
    if (text.empty()) {
        const auto character = TranslateKeyToCharacter(key, 0);
        if (character == 0) {
            text = g_localized_strings[0x86d];
        } else {
            text.push_back(static_cast<char>(
                std::toupper(static_cast<unsigned char>(character))));
        }
    }

    if (m_secondary_binding != W8_MGS_COMMAND_NONE && key != 0) {
        text = fmt::format("({}) {}", g_localized_strings[0x89d], text);
    }
    this->text.SetText(text, g_options_detail_font);
}

void W8OptionsKeyboardPanel::Populate()
{
    const auto add_key = [this](SDL_Rect bounds, W8MGSCommand primary,
                               W8MGSCommand secondary, std::string_view caption) {
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
                const ButtonPolicy policy{.primary_toggle = ButtonToggle::StayLatched,
                                          .input_rule = ButtonInputRule::PrimaryPressedState};
                m_dirty = true;
                if (event.action == UiPointerAction::Leave) {
                    key->interaction.leave(policy);
                } else if (event.button == UiPointerButton::Left) {
                    if (event.action == UiPointerAction::Down)
                        key->interaction.press(SDL_BUTTON_LMASK, policy);
                    else if (event.action == UiPointerAction::Up && event.inside &&
                             key->interaction.release(SDL_BUTTON_LMASK, policy).activate) {
                        if (key->m_primary_binding == W8_MGS_COMMAND_NONE) {
                            key->interaction = {};
                            AfterUiDispatch([this] { g_options_screen->ShowNotification(true, 0x834, [this](bool accepted) { OnDialogClosed(accepted, 0); }); });
                        } else {
                            SelectKey(key);
                        }
                    }
                }
                return UiResult::Handled;
            });
    };
    const auto& page = g_options_keyboard_pages[m_panel - 6];
    const W8ControlsRect title_bounds{m_bounds.x + 30, m_content_top,
                                     m_bounds.x + m_bounds.w - 30, m_content_top + 22};
    m_text_buffers.emplace_back(std::make_unique<W8TextBuffer>(&title_bounds,
        g_localized_strings[page.title], g_options_detail_font,
        g_W8TextBufferAlignTop | g_W8TextBufferAlignCenter, 4));
    m_content_top += 44;
    auto* row = g_options_key_rows;
    while (row->primary_binding != page.first_binding) ++row;
    for (;;) {
        const W8ControlsRect bounds{m_bounds.x + 20, m_content_top,
                                    m_bounds.x + m_bounds.w, m_content_top + 22};
        m_text_buffers.emplace_back(std::make_unique<W8TextBuffer>(&bounds,
            g_localized_strings[row->label], g_options_detail_font,
            g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));
        add_key({m_bounds.x + 100, m_content_top, 0x15e - 100, 22},
                row->primary_binding, row->secondary_binding, "");
        m_content_top += 22;
        if ((row++)->primary_binding == page.last_binding) break;
    }
    const auto& reset = g_localized_strings[0x833];
    const int width = g_options_detail_font.textWidth(reset) + 20;
    add_key({m_bounds.x + (m_bounds.w - width) / 2, 0x18e, width, 22},
            W8_MGS_COMMAND_NONE, W8_MGS_COMMAND_NONE, reset);
}

void W8OptionsKeyboardPanel::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active) return;
    const bool repaint = full_redraw || m_dirty;
    W8OptionsPanel::Redraw(target, full_redraw);
    if (repaint) for (auto& key : m_keys) {
        key->text.SetFontStateIndex(key->interaction.pressed ? 13 : key->target->hovered() ? 14 : -1);
        key->text.RenderToTarget(0, true, target);
    }
}

void W8OptionsKeyboardPanel::SelectKey(W8OptionsKeyButton* key)
{
    m_captured_button = key;
    if (g_options_screen) g_options_screen->m_key_capture = key ? this : nullptr;
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
        auto* binding = g_mgs_keyboard->GetBinding(g_mgs_keyboard->FindBinding(key->m_primary_binding));
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

unsigned char W8OptionsKeyboardPanel::OnKey(unsigned short key, unsigned short modifiers)
{
    if (modifiers != 0) {
        return 0;
    }
    if (key != VK_ESCAPE) {
        ClearDuplicateBinding(key);
    }
    m_captured_button->SetKey(key);
    SelectKey(nullptr);
    return 1;
}

void W8OptionsKeyboardPanel::OnDialogClosed(bool accepted, int)
{
    SelectKey(nullptr);
    if (accepted) {
        ResetMGSKeyboardBindings();
        RefreshBindingLabels();
    }
}

void W8OptionsKeyboardPanel::ClearDuplicateBinding(unsigned short key)
{
    int row_index = 0;
    if (g_options_key_rows[0].primary_binding == W8_MGS_COMMAND_NONE) {
        return;
    }

    W8OptionsKeyRow* row = g_options_key_rows;
    for (;;) {
        MGSKeyBinding* binding =
            g_mgs_keyboard->GetBinding(g_mgs_keyboard->FindBinding(row->primary_binding));
        if (binding != 0 && binding->key == key) {
            binding->key = 0;
            if (g_options_key_rows[row_index].secondary_binding != W8_MGS_COMMAND_NONE) {
                binding = g_mgs_keyboard->GetBinding(
                    g_mgs_keyboard->FindBinding(g_options_key_rows[row_index].secondary_binding));
                if (binding != 0) {
                    binding->key = 0;
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

W8OptionsPanel* CreateOptionsPanel(int panel, unsigned char* compact,
                                   unsigned char* hide_navigation)
{
    switch (panel) {
    case 0:
        return new W8OptionsGamePanel();
    case 1:
        return new W8OptionsMousePanel();
    case 2:
        return new W8OptionsInterfacePanel();
    case 3:
        return new W8OptionsAudioPanel();
    case 4:
        return new W8OptionsGraphicsPanel();
    case 5:
        return new W8OptionsAdvancedGraphicsPanel();
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
        return new W8OptionsKeyboardPanel(panel);
    case 11:
        if (g_status.game_started && g_status.iron_man) {
            *hide_navigation = 1;
            return new W8OptionsUnavailablePanel(0x82c);
        }
        *compact = 1;
        return new W8OptionsSaveLoadPanel(11);
    case 12:
        if (g_status.game_started) {
            if (g_status.iron_man) {
                *hide_navigation = 1;
                return new W8OptionsUnavailablePanel(0x82c);
            }
            if (gXStatus.fCombatMode) {
                *hide_navigation = 1;
                return new W8OptionsUnavailablePanel(0x82b);
            }
        }
        *compact = 1;
        return new W8OptionsSaveLoadPanel(12);
    }
    return 0;
}

void W8OptionsGamePanel::Populate()
{
    m_content_top += 22;
    W8OptionsSlider* slider = AddSlider(0x805, &g_options_values.combat_speed, false);
    slider->m_minimumPosition = 0.0f;
    slider->m_maximumPosition = 1.0f;
    slider->UpdatePixelPosition();
    m_content_top += 22;
    slider = AddSlider(0x80c, &g_options_values.text_display_delay_ms, false);
    slider->m_minimumPosition = 0.0f;
    slider->m_maximumPosition = 5000.0f;
    slider->UpdatePixelPosition();
    m_content_top += 22;
    slider = AddSlider(0x802, &g_options_values.monster_movement_speed, true);
    slider->m_minimumPosition = 1.0f;
    slider->m_maximumPosition = 5.0f;
    slider->UpdatePixelPosition();
    m_content_top += 22;
    AddChoices(0x7f7, 3, g_options_difficulty_labels, &g_options_values.difficulty);
    AddChoices(0x7f4, 2, g_options_combat_mode_labels, &g_options_values.continuous_combat);
    AddChoices(0x7fd, 3, g_options_camera_rotation_labels, &g_options_values.camera_auto_rotation);
}

void W8OptionsMousePanel::Populate()
{
    m_content_top += 22;
    AddCheckbox(0x7f0, &g_options_values.mouselook_toggle);
    AddCheckbox(0x7f1, &g_options_values.mouselook_smoothing);
    AddCheckbox(0x7f2, &g_options_values.invert_mouse_y);
    AddCheckbox(0x801, &g_options_values.stop_movement_for_events);
    AddCheckbox(0x809, &g_options_values.autotarget_spells);
    AddCheckbox(0x804, &g_options_values.auto_advance_character);
    AddCheckbox(0x808, &g_options_values.autoswap_weapons);
    if (!g_status.game_started || !g_status.iron_man) {
        AddCheckbox(0x80d, &g_options_values.auto_save);
    }
}

void W8OptionsInterfacePanel::Populate()
{
    m_content_top += 22;
    m_tooltip_delay = AddSlider(0x80e, 0, false);
    m_tooltip_delay->m_minimumPosition = 0.0f;
    m_tooltip_delay->m_maximumPosition = 1200.0f;
    m_tooltip_delay->UpdatePixelPosition();
    m_tooltip_delay->m_position = static_cast<float>(g_settings.tooltip_delay_ms);
    m_tooltip_delay->UpdatePixelPosition();
    m_tooltip_delay->on_release = [this] {
        g_settings.tooltip_delay_ms = static_cast<int>(m_tooltip_delay->m_position);
        SetUiHelpDelay(0);
    };
    auto* button = AddChoiceButton(0x80f, [this](auto* control) { OnPrimary(control); });
    button->SetTooltipIndex(0x810);
    if (!g_settings.tooltips_enabled) {
        button->SetPressed(true);
        m_tooltip_delay->SetEnabled(false);
    }
    m_content_top += 22;
    AddCheckbox(0x80b, &g_options_values.simplified_npc_interaction);
    AddCheckbox(0x807, &g_options_values.verbose_combat_messages);
    AddCheckbox(0x7f3, &g_options_values.ctrl_right_click_info);
    AddCheckbox(0x7fc, &g_options_values.skill_increase_messages);
    AddCheckbox(0x806, &g_options_values.numeric_hit_points);
    AddCheckbox(0x80a, &g_options_values.autoscroll_combat_messages);
}

void W8OptionsInterfacePanel::OnPrimary(W8ButtonWidget* control)
{
    g_settings.tooltips_enabled = !control->IsPressed();
    m_tooltip_delay->SetEnabled(g_settings.tooltips_enabled);
}

void W8OptionsAudioPanel::Populate()
{
    for (int index = 0; index < 4; ++index) {
        int volume;
        bool muted = false;

        m_content_top += 22;
        W8OptionsSlider* slider = AddSlider(g_options_audio_labels[index], 0, false);
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
        slider->m_minimumPosition = 0.0f;
        slider->m_maximumPosition = 127.0f;
        slider->UpdatePixelPosition();
        slider->m_position = static_cast<float>(volume);
        slider->UpdatePixelPosition();
        slider->on_change = [this, index](float value) { ChangeVolume(index, value); };
        slider->on_release = [this, index] { PreviewVolume(index); };
        m_sliders[index] = slider;

        m_mute_buttons[index] = AddChoiceButton(0x822, [this](auto* control) { OnPrimary(control); });
        if (muted) {
            m_sliders[index]->SetEnabled(false);
            m_mute_buttons[index]->SetPressed(true);
        }
    }
    m_content_top += 22;
    AddCheckbox(0x823, &g_options_values.pc_confirmations);
    AddCheckbox(0x824, &g_options_values.pc_subtitles);
}

void W8OptionsAudioPanel::ChangeVolume(int index, float value)
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
        return;
    case 3:
        g_settings.voice_volume = static_cast<unsigned char>(value);
        return;
    }
}

void W8OptionsAudioPanel::PreviewVolume(int index)
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
                          {.volume = std::uint32_t(g_settings.voice_volume)});
        break;
    }
    }
}

void W8OptionsAudioPanel::OnPrimary(W8ButtonWidget* control)
{
    int index;
    for (index = 0; index < 4; ++index) {
        if (control == m_mute_buttons[index]) {
            break;
        }
    }
    if (index == 4) {
        index = -1;
    }
    const bool muted = control->IsPressed();
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
    m_sliders[index]->SetEnabled(muted == 0);
}

void W8OptionsGraphicsPanel::Populate()
{
    m_content_top += 22;
    W8OptionsSlider* slider = AddSlider(0x811, &g_options_values.gamma, false);
    slider->m_minimumPosition = 0.1f;
    slider->m_maximumPosition = 1.9f;
    slider->UpdatePixelPosition();
    slider->on_change = [](float value) { SetDisplayGamma(value); };
    slider->SetEnabled(GetRendererModeByte());
    m_content_top += 22;
    AddCheckbox(0x816, &g_options_values.render_options[4]);
    AddCheckbox(0x819, &g_options_values.smooth_monster_animations);
    AddCheckbox(0x81a, &g_options_values.smooth_world_animations);
    AddCheckbox(0x81b, &g_options_values.render_options[6]);
    AddCheckbox(0x818, &g_options_values.monster_shadows);
    AddCheckbox(0x817, &g_options_values.render_options[5]);
    AddCheckbox(0x81d, &g_options_values.render_options[8]);
}

void W8OptionsAdvancedGraphicsPanel::Populate()
{
    m_content_top += 22;
    AddCheckbox(0x815, &g_options_values.render_options[3]);
    AddCheckbox(0x814, &g_options_values.render_options[2]);
    AddCheckbox(0x812, &g_options_values.render_options[0]);
    AddCheckbox(0x81c, &g_options_values.render_options[7]);
    AddCheckbox(0x813, &g_options_values.render_options[1]);
}

void W8OptionsUnavailablePanel::Populate()
{
    m_content_top += 44;
    W8ControlsRect bounds = {ScreenBounds().x + 30, ScreenBounds().y + m_content_top, ToEdges(ScreenBounds()).right - 30,
                             ToEdges(ScreenBounds()).bottom};
    W8TextBuffer* text = new W8TextBuffer(&bounds, g_localized_strings[m_message], g_options_detail_font,
                                          g_W8TextBufferAlignTop | g_W8TextBufferAlignCenter, 4);
    m_text_buffers.emplace_back(text);
    m_text_buffers[0]->SetLineHeight(22);
}

W8OptionsScreen::W8OptionsScreen()
    : m_redraw_pending(1), m_selected_panel(-1), m_key_capture(0)
{
    auto& current = m_save_slots.emplace_back(std::make_unique<W8SaveSlot>());
    if (g_status.game_started) {
        FillCurrentSaveSlot(*current);
    }
    EnumerateSaveSlots(m_save_slots);
}

void W8OptionsScreen::CreateControls()
{
    const auto artwork = CatalogArtwork::Load(0xef, 0);
    for (int item = 0; item < 8; ++item) {
        const auto& row = g_options_menu_rows[item];
        const auto& frames = row.image_indices;
        const auto frame = [&](int index) { return index < 0 ? -1 : index + artwork.imageOffset(); };
        auto& button = m_menu_buttons[item];
        button = std::make_unique<W8ButtonWidget>(m_menu_input);
        button->Configure(
            ButtonGraphics::Load(artwork.spriteSheet(), frame(frames[4]), frame(frames[0]),
                                 frame(frames[2]), frame(frames[1]), frame(frames[3])),
            [this, item](W8ButtonWidget*) {
                AfterUiDispatch([this, item] { ActivateMenu(item); });
            },
            0);
        const auto size = artwork.size(frames[0]);
        button->SetPosition(row.bounds.left, row.bounds.top);
        button->Resize(size.x, size.y);
        button->policy = {.primary_toggle =
                              item < 6 ? ButtonToggle::StayLatched : ButtonToggle::None,
                          .input_rule = ButtonInputRule::PrimaryPressedState};
        button->menu_artwork = true;
        if (item == 5) button->SetEnabled(g_status.game_started);
        if (item == 4) button->SetEnabled(m_save_slots.size() > 1);
        for (const auto& bounds : row.child_bounds) {
            if (bounds.left == -1) continue;
            m_menu_input.add(SDL_Rect{bounds.left, bounds.top, bounds.right - bounds.left + 1,
                                     bounds.bottom - bounds.top + 1},
                [this, item, press = ButtonInteraction{}](const UiPointerEvent& event) mutable {
                    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Leave)
                        PushButtonSoundScheme(0, true);
                    if (event.action == UiPointerAction::Leave) press.leave({});
                    if (event.button == UiPointerButton::Left && event.action == UiPointerAction::Down)
                        press.press(SDL_BUTTON_LMASK, {});
                    if (event.action == UiPointerAction::Up && event.inside &&
                        event.button == UiPointerButton::Left && press.release(SDL_BUTTON_LMASK, {}).activate)
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
            [this, direction](W8ButtonWidget*) {
                AfterUiDispatch([this, direction] {
                    m_panel[m_selected_panel]->Scroll(direction == 0 ? -1 : 1);
                    UpdateNavigation();
                });
            },
            0);
        const auto size = arrows.size(direction * 4);
        button->Resize(size.x, size.y);
        button->policy.input_rule = ButtonInputRule::PrimaryPressedState;
        button->menu_artwork = true;
    }
    const auto size = CatalogArtwork::Load(0xf3, 0).size(0);
    const W8ControlsRect bounds{0x11b, m_navigation_y, 0x11b + size.x, m_navigation_y + size.y};
    m_page_text = std::make_unique<W8TextBuffer>(&bounds, "", g_options_detail_font,
        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle, 4);
}

W8OptionsScreen::~W8OptionsScreen()
{
    SelectPanel(-1);
}

void W8OptionsScreen::SelectPanel(int selected)
{
    if (selected == m_selected_panel) {
        return;
    }
    FinishOptionsTextEdit(this, 1);
    if (m_selected_panel != -1) {
        W8OptionsPanelSet* previous = m_panel[m_selected_panel].get();
        previous->m_panels[previous->m_current]->SetActive(false);
        previous->m_active = false;
    }
    m_selected_panel = selected;
    if (selected != -1) {
        if (!m_panel[selected]) {
            m_panel[selected] = std::make_unique<W8OptionsPanelSet>();
            W8OptionsPanelSet* created = m_panel[m_selected_panel].get();
            for (int index = g_options_panel_ranges[m_selected_panel].first;
                 index <= g_options_panel_ranges[m_selected_panel].last; ++index) {
                W8OptionsPanel* panel = CreateOptionsPanel(index, &created->m_compact_layout,
                                                           &created->m_hide_navigation);
                panel->Populate();
                created->m_panels.emplace_back(panel);
            }
        }
        W8OptionsPanelSet* current = m_panel[m_selected_panel].get();
        if (!current->m_active) {
            current->m_current = 0;
        }
        current->m_panels[current->m_current]->SetActive(true);
        current->m_active = true;
        if (m_selected_panel == 4) {
            m_panel[4]->m_page_count = (static_cast<int>(m_save_slots.size()) - 2) / 5 + 1;
        } else if (m_selected_panel == 5) {
            m_panel[5]->m_page_count = (static_cast<int>(m_save_slots.size()) - 1) / 5 + 1;
        }
        UpdateNavigation();
    }
    for (int item = 0; item < 6; ++item)
        m_menu_buttons[item]->SetPressed(item == selected);
    if (m_selected_panel >= 0) {
        g_last_options_panel = m_selected_panel;
    }
}

void W8OptionsScreen::ShowNotification(bool allow_cancel, int message, std::function<void(bool)> closed)
{
    auto dialog = std::unique_ptr<W8MessageDialogBase>(CreateMessageDialog());
    dialog->SetClientExtent(250, 200);
    dialog->SetMessage(g_localized_strings[message], 1, 50, true, allow_cancel, true, true, 0, 350);
    dialog->InputLayer().setModal(true);
    m_active_modal = std::move(dialog);
    m_notification_closed = std::move(closed);
}

void W8OptionsScreen::ActivateMenu(int item)
{
    if (!m_menu_buttons[item]->IsEnabled()) return;
    if (item < 6) SelectPanel(item);
    else if (item == 7) RequestScreenTransition();
    else {
        FinishOptionsTextEdit(this, 1);
        ShowNotification(true, 0x832, [this](bool accepted) { OnDialogClosed(accepted, 0); });
    }
}

unsigned char W8OptionsScreen::ProcessInput(const InputEvent* input)
{
    W8OptionsTextEditor* editor = m_text_editor.get();
    if (editor != 0) {
        if (IsTypingEvent(*input)) {
            if (input->key != VK_ESCAPE && input->key != VK_RETURN) {
                const char32_t character = TypedCharacter(*input);
                if (character != 0 && character < 0x80 && strchr("\\/:*?\"<>|", int(character)) != 0) {
                    return 0;
                }
                editor->m_fields.HandleInput(*input);
                return 0;
            }
            SetTargetCursor(W8_CURSOR_NONE);
            editor->owner->OnTextEditComplete(editor, input->key == VK_ESCAPE);
            m_text_editor.reset();
            return 1;
        }
    } else if (m_key_capture != 0 && input->type == InputEventType::KeyDown) {
        return m_key_capture->OnKey(static_cast<unsigned short>(input->key), input->modifiers);
    }
    return 0;
}

void W8OptionsScreen::Redraw()
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
        W8OptionsPanelSet* panel_set = m_panel[m_selected_panel].get();
        if (panel_set->m_active) {
            panel_set->m_panels[panel_set->m_current]->Redraw(GetFrameBufferSurface(), false);
        }
    }
    for (int item = 0; item < 8; ++item) {
        auto& button = *m_menu_buttons[item];
        if (button.m_dirty && item == m_selected_panel)
            CatalogArtwork::Load(0xf0, 0).drawAndInvalidate(GetFrameBufferSurface(),
                g_options_menu_rows[item].item_id, 12, 15);
        button.DrawAndInvalidate(GetFrameBufferSurface());
    }
    if (m_navigation_visible && (m_navigation_dirty || m_navigation[0]->m_dirty || m_navigation[1]->m_dirty)) {
        auto& target = GetFrameBufferSurface();
        CatalogArtwork::Load(0xf3, 0).drawAndInvalidate(target, 0, 0x11b, m_navigation_y);
        for (auto& button : m_navigation) {
            button->DrawAndInvalidate(target, true);
        }
        m_page_text->RenderToTarget(0, true, target);
        m_navigation_dirty = false;
    }
    if (m_active_modal != 0) {
        m_active_modal->Draw(GetFrameBufferSurface());
    }
    if (m_text_editor != 0) {
        m_text_editor->m_fields.RenderAll(GetFrameBufferSurface());
    }
}

void W8OptionsValues::TransferByte(int* value, unsigned char* setting)
{
    if (applying == 0) {
        *value = *setting;
    } else {
        *setting = static_cast<unsigned char>(*value);
    }
}

void W8OptionsValues::TransferRenderOption(int* value, W8RenderOption option)
{
    if (applying == 0) {
        *value = GetRenderOptionState(option);
    } else if (*value != 0) {
        EnableRenderOption(option);
    } else {
        DisableRenderOption(option);
    }
}

void W8OptionsValues::TransferSettings()
{
    TransferByte(&mouselook_toggle, &g_settings.mouselook_toggle);
    TransferByte(&mouselook_smoothing, &g_settings.mouselook_smoothing);
    TransferByte(&invert_mouse_y, &g_settings.invert_mouse_y);
    TransferByte(&ctrl_right_click_info, &g_settings.ctrl_right_click_info);
    TransferByte(&numeric_hit_points, &g_settings.numeric_hit_points);
    TransferByte(&autoscroll_combat_messages, &g_settings.autoscroll_combat_messages);
    TransferByte(&simplified_npc_interaction, &g_settings.simplified_npc_interaction);
    TransferByte(&verbose_combat_messages, &g_settings.verbose_combat_messages);
    TransferByte(&autoswap_weapons, &g_settings.autoswap_weapons);
    TransferByte(&skill_increase_messages, &g_settings.skill_increase_messages);
    if (applying == 0) {
        monster_movement_speed = g_settings.monster_movement_speed;
    } else {
        g_settings.monster_movement_speed = monster_movement_speed;
    }
    TransferByte(&auto_advance_character, &g_settings.auto_advance_character);
    TransferByte(&autotarget_spells, &g_settings.autotarget_spells);
    if (applying == 0) {
        gamma = g_settings.gamma;
    } else {
        g_settings.gamma = gamma;
    }
    TransferByte(&pc_confirmations, &g_settings.pc_confirmations);
    TransferByte(&pc_subtitles, &g_settings.pc_subtitles);
    if (applying == 0) {
        text_display_delay_ms = static_cast<float>(g_settings.text_display_delay_ms);
    } else {
        g_settings.text_display_delay_ms = static_cast<unsigned int>(text_display_delay_ms);
    }
    TransferByte(&auto_save, &g_settings.auto_save);
    if (applying == 0) {
        difficulty = g_status.difficulty;
    } else {
        g_status.difficulty = static_cast<W8Difficulty>(difficulty);
    }
    if (applying == 0) {
        difficulty = g_settings.difficulty;
    } else {
        g_settings.difficulty = static_cast<W8Difficulty>(difficulty);
    }
    TransferRenderOption(&render_options[0], W8_RENDER_OPTION_MIP_MAPPING);
    TransferRenderOption(&render_options[1], W8_RENDER_OPTION_MISSILE_LIGHTS);
    TransferRenderOption(&render_options[2], W8_RENDER_OPTION_DITHER);
    TransferRenderOption(&render_options[3], W8_RENDER_OPTION_MESH_SKY);
    TransferRenderOption(&render_options[4], W8_RENDER_OPTION_VIDEO_SYNC);
    TransferRenderOption(&render_options[5], W8_RENDER_OPTION_ADDITIONAL_ANIMATIONS);
    TransferRenderOption(&render_options[6], W8_RENDER_OPTION_HIGH_TEXTURE_DETAIL);
    TransferRenderOption(&render_options[7], W8_RENDER_OPTION_HIGH_TEXTURE_CACHE);
    TransferRenderOption(&render_options[8], W8_RENDER_OPTION_CORRECT_BLURRED_TEXT);
    TransferByte(&monster_shadows, &g_settings.monster_shadows);
    TransferByte(&smooth_monster_animations, &g_settings.smooth_monster_animations);
    TransferByte(&smooth_world_animations, &g_settings.smooth_world_animations);
    if (applying == 0) {
        combat_speed = (g_settings.combat_delay_ms - 5000) * -0.0002f;
        camera_auto_rotation = g_settings.camera_rotation_mode == W8_CAMERA_ROTATION_DISABLED
                                   ? 2
                                   : g_settings.camera_rotation_style;
        TransferByte(&continuous_combat, &g_settings.continuous_combat);
    } else {
        g_settings.combat_delay_ms = 5000 - static_cast<int>(combat_speed * 5000.0f);
        if (camera_auto_rotation == 2) {
            g_settings.camera_rotation_mode = W8_CAMERA_ROTATION_DISABLED;
        } else {
            g_settings.camera_rotation_mode = W8_CAMERA_ROTATION_ALL_TARGETS;
            g_settings.camera_rotation_style =
                static_cast<W8CameraRotationStyle>(camera_auto_rotation);
        }
        if (continuous_combat != g_settings.continuous_combat) {
            TogglePartyCombatStance();
        }
    }
}

W8OptionsSlider::W8OptionsSlider(UiLayer& layer, SDL_Point origin, float* value, bool alternate)
    : m_origin(origin), m_value(value), m_backgroundSprite(alternate ? 4 : 0)
{
    if (value) m_position = *value;
    const auto size = m_artwork.size(m_backgroundSprite);
    m_thumbWidth = m_artwork.size(1).x;
    m_trackLength = size.x - m_thumbWidth;
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
    PushButtonSoundScheme(0, true);
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
                     (m_maximumPosition - m_minimumPosition) + m_minimumPosition;
        UpdatePixelPosition();
        if (m_value) *m_value = m_position;
        if (on_change) on_change(m_position);
        break;
    case UiPointerAction::Move:
        if (m_dragging) {
            if (x < 0 || x > m_trackLength + m_thumbWidth / 2) break;
            m_position += (x - m_dragCoordinate) / static_cast<float>(m_trackLength) *
                          (m_maximumPosition - m_minimumPosition);
            m_dragCoordinate = x;
            UpdatePixelPosition();
            if (m_value) *m_value = m_position;
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
    default: break;
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
        std::move(callback), 0);
    button->policy.input_rule = ButtonInputRule::PrimaryPressedState;
    button->menu_artwork = true;
    button->SetPosition(m_bounds.x + x, m_bounds.y + y);
    const auto size = art.size(normal);
    button->Resize(size.x, size.y);
    return button.get();
}

/* All labelled option rows use the same geometry and text formatting. */
static void AddOptionsLabel(W8OptionsPanel& panel, int label)
{
    W8ControlsRect bounds = {panel.ScreenBounds().x + 20,
                             panel.ScreenBounds().y + panel.m_content_top,
                             ToEdges(panel.ScreenBounds()).right,
                             panel.ScreenBounds().y + panel.m_content_top + 22};
    panel.m_text_buffers.emplace_back(
        new W8TextBuffer(&bounds, g_localized_strings[label], g_options_detail_font,
                         g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft, 4));
}

void W8OptionsPanel::AddCheckbox(int label, int* value)
{
    AddOptionsLabel(*this, label);
    auto* checkbox = AddButton(0x14d, m_content_top - 2, 0xf1, 2, 0, 3, 1, -1,
        [value](auto* button) { *value = button->IsPressed() ? 2 : 0; });
    checkbox->policy.primary_toggle = ButtonToggle::Release;
    checkbox->SetPressed(*value != 0);
    m_content_top += 44;
}

W8ButtonWidget* W8OptionsPanel::AddChoiceButton(int label, W8ButtonCallback callback)
{
    const W8ControlsRect bounds{m_bounds.x, m_bounds.y + m_content_top, m_bounds.x + 0x147,
                               m_bounds.y + m_content_top + 22};
    m_text_buffers.emplace_back(std::make_unique<W8TextBuffer>(&bounds, g_localized_strings[label],
        g_options_detail_font, g_W8TextBufferAlignTop | g_W8TextBufferAlignRight, 4));
    auto* button = AddButton(0x151, m_content_top + 1, 0xf1, 4, 6, 5, 7, -1, std::move(callback));
    button->policy.primary_toggle = ButtonToggle::Release;
    m_content_top += 22;
    return button;
}

W8OptionsSlider* W8OptionsPanel::AddSlider(int label, float* value, bool alternate)
{
    AddOptionsLabel(*this, label);
    auto& slider = m_owned_sliders.emplace_back(std::make_unique<W8OptionsSlider>(
        m_input, SDL_Point{ScreenBounds().x + 0xc9, ScreenBounds().y + m_content_top - 2},
        value, alternate));
    m_content_top += 22;
    return slider.get();
}

void W8OptionsPanel::AddChoices(int label, int count, const int* choices, int* value)
{
    AddOptionsLabel(*this, label);
    const auto first = m_buttons.size();
    for (int index = 0; index < count; ++index) {
        auto* button = AddChoiceButton(choices[index], [this, first, count, index, value](auto*) {
            *value = index;
            for (int i = 0; i < count; ++i) m_buttons[first + i]->SetPressed(i == index);
        });
        button->policy.primary_toggle = ButtonToggle::StayLatched;
        button->SetPressed(index == *value);
    }
    m_content_top += 22;
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
        button->DrawAndInvalidate(target);
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
    if (!active) for (auto& slider : m_owned_sliders) slider->CancelDrag();
    m_input.setEnabled(active);
    for (auto& button : m_buttons) button->SetVisible(active);
    InvalidateAll();
}

void W8OptionsPanel::SetCurrent(int current)
{
    m_current = current;
    InvalidateAll();
}

void W8OptionsPanelSet::Scroll(int delta)
{
    const int current = m_page_count ? m_panels[0]->m_current : m_current;
    const int count = m_page_count ? m_page_count : static_cast<int>(m_panels.size());
    const int next = std::clamp(current + delta, 0, count - 1);
    if (next == current) return;
    if (m_page_count) m_panels[0]->SetCurrent(next);
    else {
        m_panels[m_current]->SetActive(false);
        m_current = next;
        m_panels[m_current]->SetActive(true);
    }
}

void W8OptionsScreen::UpdateNavigation()
{
    const auto& panels = *m_panel[m_selected_panel];
    const int current = panels.m_page_count ? panels.m_panels[0]->m_current : panels.m_current;
    const int count = panels.m_page_count ? panels.m_page_count : static_cast<int>(panels.m_panels.size());
    m_navigation_y = panels.m_compact_layout ? 0x180 : 0x1a8;
    m_navigation_visible = !panels.m_hide_navigation && (panels.m_compact_layout || count > 1);
    for (int direction = 0; direction < 2; ++direction) {
        auto& button = *m_navigation[direction];
        button.SetPosition(0x11b + (direction == 0 ? 3 : 0x11f), m_navigation_y + 3);
        button.SetEnabled(direction == 0 ? current > 0 : current < count - 1);
        button.SetVisible(m_navigation_visible);
    }
    const auto size = CatalogArtwork::Load(0xf3, 0).size(0);
    const W8ControlsRect bounds{0x11b, m_navigation_y + 1, 0x11b + size.x, m_navigation_y + size.y};
    m_page_text->SetLayoutBounds(&bounds, true, true);
    m_page_text->SetText(fmt::format("{:d} / {:d}", current + 1, count), g_options_detail_font);
    m_navigation_dirty = true;
}

/* The dialog callback's teardown decision is part of the Options screen's
   state transition: only an accepted close outside combat saves an active
   party, then the common audio-state update runs for every accepted close. */
void W8OptionsScreen::OnDialogClosed(bool accepted, int)
{
    if (accepted) {
        if (g_status.game_started && !gXStatus.fCombatMode && AnyCharacterActive()) {
            AutoSaveIfAllowed(true);
        }
        RequestExitScreen();
    }
}

/* State 10 first transfers its option values back to the shared settings
   block, then releases every controller-owned control before returning the
   display and region systems to their common screen boundary. */
unsigned char OptionsScreenLeave(int)
{
    g_options_values.applying = 1;
    g_options_values.TransferSettings();
    g_options_screen.reset();
    NoOp();
    ResetUiInput();
    return 1;
}

/* State 10 creates the controller after resetting the shared 2D screen
   systems.  The selected panel is determined by the transition mode, with the
   in-game branch preserving the combat and save-state overrides. */
unsigned char OptionsScreenEnter()
{
    int selected;

    SetViewport(0, 0, 0x280, 0x1e0);
    SetPrimarySurfaceTextureHint2Enabled(false);
    ResetUiInput();
    UpdateHeldItemCursor();
    g_wiz_text_font_secondary.resetPalette();
    g_options_values.applying = 0;
    g_options_values.TransferSettings();
    g_options_screen = std::make_unique<W8OptionsScreen>();
    g_options_screen->CreateControls();

    if (g_current_screen_state.mode == 1) {
        selected = 4;
    } else if (g_current_screen_state.mode == 2) {
        selected = 5;
    } else if (g_current_screen_state.mode == 3) {
        if (g_last_options_panel == 4) {
            selected = 5;
        } else {
            selected = g_last_options_panel;
            if (g_last_options_panel == 5) {
                if (gXStatus.fCombatMode) {
                    selected = 4;
                }
                if (g_status.iron_man) {
                    selected = 0;
                }
            }
        }
    } else {
        selected = 0;
    }
    g_options_screen->SelectPanel(selected);
    g_options_first_frame = true;
    return 1;
}

/* The state-10 frame owns modal retirement, input refusal order and redraw
   sequencing.  Options-specific input gets first refusal, followed by shared
   region dispatch; only an unhandled Escape requests the screen transition. */
void OptionsScreenFrame()
{
    SDL_Point point;

    if (g_dev_mode) {
        RequestExitScreen();
    }
    RepositionAmbientSounds(g_world);
    UpdateAmbientSounds(g_world);
    ServiceMusicPlaylist();
    SGPMouseGetPos(&point);

    W8OptionsScreen* screen = g_options_screen.get();
    if (screen->m_text_editor != 0) {
        UpdateUiPointer(point.x, point.y);
        screen = g_options_screen.get();
    }

    auto& active_modal = screen->m_active_modal;
    if (active_modal) {
        if (!active_modal->ProcessInput()) {
            const bool accepted = active_modal->accepted;
            auto closed = std::exchange(screen->m_notification_closed, {});
            active_modal.reset();
            if (closed) closed(accepted);
            screen->m_redraw_pending = true;
            if (screen->m_selected_panel != -1) {
                W8OptionsPanelSet* panel_set = screen->m_panel[screen->m_selected_panel].get();
                if (panel_set->m_active) {
                    panel_set->m_panels[panel_set->m_current]->InvalidateAll();
                }
            }
        }
    }

    if (!screen->m_active_modal) PumpUiInput({
        .before = [screen](const InputEvent& input) {
            return screen->ProcessInput(&input) ? InputRoute::Handled : InputRoute::Pass;
        },
        .unhandled = [](const InputEvent& input) {
            if (input.type == InputEventType::KeyDown && input.key == VK_ESCAPE) {
                RequestScreenTransition();
            }
            return InputRoute::Handled;
        },
    });
    screen->Redraw();
    if (g_options_first_frame) {
        SetRendererAutoFlipEnabled(false);
        RenderFrame();
        SetRendererAutoFlipEnabled(true);
        g_options_first_frame = false;
    }
    RenderFrame();
}

void SetLastSaveName(std::string_view target)
{
    g_options_last_save_name = target;
}

std::string_view GetLastSaveName(void)
{
    return g_options_last_save_name;
}
