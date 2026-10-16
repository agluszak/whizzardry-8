#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_keycode.h>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/engine_code/Quality.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/mgs_commands.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_layer.h"

class W8OptionsPanel;
struct W8OptionsSaveRow;
class W8MessageDialogBase;
class W8TextBuffer;
struct W8SaveSlot;

class W8OptionsSaveLoadPanel;
class W8OptionsKeyboardPanel;

struct W8OptionsTextEditor {
    W8OptionsSaveLoadPanel* owner;
    TextFieldGroup m_fields;
    explicit W8OptionsTextEditor(UiLayer& layer) : m_fields(layer, g_wiz_text_mono_font)
    {
        m_fields.UseGameStyle();
    }
};

struct W8OptionsSaveRow {
    W8OptionsSaveRow(UiLayer& layer, W8OptionsSaveLoadPanel& owner, int index, bool save_mode);
    void SetSave(W8SaveSlot& save);
    void ClearSave();
    void Redraw(SDL_Surface& target, bool full_redraw);
    W8ButtonWidget button;
    bool m_editing = false;
    W8SaveSlot* m_save = nullptr;
};

/* The settings transfer routines at 005A6E20/005A72F0/005A7320 share this
   receiver. The trailing direction flag is part of the record, not a global.
   Float loads/stores distinguish the slider values from the integer options. */
struct W8OptionsValues {
    void TransferSettings();
    void TransferByte(int* value, unsigned char* setting);
    void TransferRenderOption(int* value, W8RenderOption option);

    int mouselook_toggle;
    int mouselook_smoothing;
    int invert_mouse_y;
    int ctrl_right_click_info;
    int difficulty;
    int camera_auto_rotation;
    int continuous_combat;
    int numeric_hit_points;
    int autoscroll_combat_messages;
    int simplified_npc_interaction;
    int verbose_combat_messages;
    int autoswap_weapons;
    int auto_advance_character;
    int autotarget_spells;
    int stop_movement_for_events; /* UI checkbox 0x801; TransferSettings does not persist it */
    int skill_increase_messages;
    float combat_speed;
    float monster_movement_speed;
    float text_display_delay_ms;
    int auto_save;
    int render_options[8];
    int monster_shadows;
    int smooth_monster_animations;
    int smooth_world_animations;
    int pc_confirmations;
    int pc_subtitles;
    int applying;
};

extern W8OptionsValues g_options_values;

/* A set of option panels; it owns the panels in its vector. */
class W8OptionsPanelSet {
public:
    W8OptionsPanelSet();
    ~W8OptionsPanelSet();
    void Scroll(int delta);

    int m_page_count;
    unsigned char m_compact_layout;
    unsigned char m_hide_navigation;
    bool m_active; /* set once the set's panels are built */
    int m_current;
    std::vector<std::unique_ptr<W8OptionsPanel>> m_panels;
};

// Options own slider input and painting; no visual parent or listener hierarchy.
class W8OptionsSlider {
public:
    W8OptionsSlider(UiLayer& layer, SDL_Point origin, float* value, bool alternate);
    void Redraw(SDL_Surface& target, bool full_redraw);
    void UpdatePixelPosition();
    void SetEnabled(bool enabled);
    void CancelDrag();
    float m_minimumPosition = 0;
    float m_maximumPosition = 1;
    float m_position = 0;
    std::function<void(float)> on_change;
    std::function<void()> on_release;

private:
    UiResult HandlePointer(const UiPointerEvent& event);
    CatalogArtwork m_artwork = CatalogArtwork::Load(0xf5);
    SDL_Point m_origin;
    HitTarget* m_target;
    float* m_value;
    int m_backgroundSprite;
    int m_trackLength;
    int m_thumbWidth;
    int m_pixelPosition = 0;
    int m_dragCoordinate = 0;
    bool m_enabled = true;
    bool m_hovered = false;
    bool m_dragging = false;
    bool m_dirty = true;
};

// A settings page owns its artwork, input and draw order.
class W8OptionsPanel {
protected:
    UiLayer m_input;
    std::vector<std::unique_ptr<W8OptionsSlider>> m_owned_sliders;
    std::vector<std::unique_ptr<W8ButtonWidget>> m_buttons;
    CatalogArtwork m_background = CatalogArtwork::Load(0xee, 0);
    int m_backgroundImage = 1;
    bool m_active = false;
    bool m_dirty = true;
    SDL_Rect m_bounds;
    W8ButtonWidget* AddButton(int x, int y, int artwork, int normal, int pressed, int hover = -1,
                              int pressed_hover = -1, int disabled = -1,
                              W8ButtonCallback callback = {});

public:
    W8OptionsPanel();
    virtual ~W8OptionsPanel() = default;
    virtual void Redraw(SDL_Surface& target, bool full_redraw);
    virtual void Populate() = 0;
    virtual void SetActive(bool active);
    virtual void SetCurrent(int current);
    virtual void InvalidateAll() { m_dirty = true; }
    const SDL_Rect& ScreenBounds() const { return m_bounds; }
    void AddCheckbox(int label, int* value);
    W8ButtonWidget* AddChoiceButton(int label, W8ButtonCallback callback);
    W8OptionsSlider* AddSlider(int label, float* value, bool alternate);
    void AddChoices(int label, int count, const int* choices, int* value);
    int m_current = 0;
    int m_content_top = 0x18;
    std::vector<std::unique_ptr<W8TextBuffer>> m_text_buffers;
};

// VTABLE: WIZ8 0x005ef174
class W8OptionsGamePanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
};

// VTABLE: WIZ8 0x005ef158
class W8OptionsMousePanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
};

// VTABLE: WIZ8 0x005ef134 W8OptionsPanel
class W8OptionsInterfacePanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
    void OnPrimary(W8ButtonWidget* control);
    W8OptionsSlider* m_tooltip_delay;
};

// VTABLE: WIZ8 0x005ef108 W8OptionsPanel
class W8OptionsAudioPanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
    void OnPrimary(W8ButtonWidget* control);
    void ChangeVolume(int index, float value);
    void PreviewVolume(int index);
    W8OptionsSlider* m_sliders[4];
    W8ButtonWidget* m_mute_buttons[4];
};

// VTABLE: WIZ8 0x005ef0dc W8OptionsPanel
class W8OptionsGraphicsPanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
};

// VTABLE: WIZ8 0x005ef0b8
class W8OptionsAdvancedGraphicsPanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
};

struct W8OptionsKeyButton {
    void SetKey(SDL_Keycode key);
    void SetKeyText(SDL_Keycode key);
    W8MGSCommand m_primary_binding = W8_MGS_COMMAND_NONE;
    W8MGSCommand m_secondary_binding = W8_MGS_COMMAND_NONE;
    W8TextBuffer text;
    ButtonInteraction interaction;
    HitTarget* target = nullptr;
};

class W8OptionsKeyboardPanel : public W8OptionsPanel {
public:
    explicit W8OptionsKeyboardPanel(int panel);
    void Populate() override;
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    void SetActive(bool active) override;
    void SelectKey(W8OptionsKeyButton* key);
    unsigned char OnKey(SDL_Keycode key, unsigned short modifiers);
    void OnDialogClosed(bool accepted, int value);
    void ClearDuplicateBinding(SDL_Keycode key);
    void RefreshBindingLabels();
    int m_panel;
    std::vector<std::unique_ptr<W8OptionsKeyButton>> m_keys;
    W8OptionsKeyButton* m_captured_button = nullptr;
};

// VTABLE: WIZ8 0x005ef09c
class W8OptionsUnavailablePanel : public W8OptionsPanel {
public:
    explicit W8OptionsUnavailablePanel(int message);
    virtual void Populate() override;
    int m_message;
};

class W8OptionsSaveLoadPanel : public W8OptionsPanel {
public:
    explicit W8OptionsSaveLoadPanel(int panel);
    void Populate() override;
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    void SetActive(bool active) override;
    void SetCurrent(int current) override;
    void OnPrimary(W8ButtonWidget* control);
    void OnDialogClosed(bool accepted, int value);
    void OnTextEditComplete(W8OptionsTextEditor* editor, unsigned char cancelled);
    void OnEditSaveName();
    void SelectRow(int selected);
    void DeleteSelectedSave();
    void LoadSelectedSave();
    void SaveSelectedSave(std::string_view previous_name = {});
    int m_panel;
    std::vector<std::unique_ptr<W8OptionsSaveRow>> m_rows;
    int m_selected = -1;
    W8ButtonWidget* m_delete_button = nullptr;
    W8ButtonWidget* m_action_button = nullptr;
    std::string m_previous_name;
    int m_editing_row = 0;
};

/* One row of the main options menu table: the menu item id, two optional
   child text controls (left == -1 when absent), the button bounds, and the
   button's artwork frame indices. */
struct W8OptionsMenuRow {
    int item_id;
    W8ControlsRect child_bounds[2];
    W8ControlsRect bounds;
    int image_indices[5];
};

/* Inclusive range of CreateOptionsPanel indices shown by one menu item. */
struct W8OptionsPanelRange {
    int first;
    int last;
};

class W8OptionsScreen final {
    UiLayer m_menu_input;

public:
    W8OptionsScreen();
    ~W8OptionsScreen();
    void SelectPanel(int selected);
    void ActivateMenu(int item);
    void UpdateNavigation();
    void CreateControls();
    unsigned char ProcessInput(const InputEvent* input);
    void Redraw();
    void ShowNotification(bool allow_cancel, int message, std::function<void(bool)> closed);
    void BeginSaveNameEdit(W8OptionsSaveLoadPanel* owner, int row, std::string_view text);
    void OnDialogClosed(bool accepted, int value);

    std::vector<std::unique_ptr<W8SaveSlot>> m_save_slots;
    bool m_redraw_pending;
    std::function<void(bool)> m_notification_closed;
    int m_selected_panel;
    std::array<std::unique_ptr<W8ButtonWidget>, 8> m_menu_buttons;
    std::array<std::unique_ptr<W8ButtonWidget>, 2> m_navigation;
    std::unique_ptr<W8TextBuffer> m_page_text;
    int m_navigation_y = 0x1a8;
    bool m_navigation_visible = false;
    bool m_navigation_dirty = true;
    std::unique_ptr<W8OptionsPanelSet> m_panel[8];
    std::unique_ptr<W8MessageDialogBase> m_active_modal;
    std::unique_ptr<W8OptionsTextEditor> m_text_editor;
    W8OptionsKeyboardPanel* m_key_capture;
};

extern std::unique_ptr<W8OptionsScreen> g_options_screen;
extern std::string g_options_last_save_name;

void SetLastSaveName(std::string_view target);
std::string_view GetLastSaveName(void);

unsigned char OptionsScreenEnter(void);
void OptionsScreenFrame(void);
unsigned char OptionsScreenLeave(int leaving);
