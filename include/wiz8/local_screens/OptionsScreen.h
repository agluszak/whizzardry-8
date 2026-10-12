#pragma once

#include <string_view>
#include <string>

#include <memory>
#include <vector>
#include <array>
#include "wiz8/ui_layer.h"
#include "wiz8/local_code/ButtonWidget.h"

#include "wiz8/engine_code/Quality.h"
#include "wiz8/mgs_commands.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/dialog_code/NotificationDialog.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/text_input.h"
#include "compat/compiler.h"
#include "input.h"

class W8OptionsPanel;
class W8OptionsSaveRow;
class W8MessageDialogBase;
class W8TextBuffer;
struct W8SaveSlot;

// VTABLE: WIZ8 0x005eef68
class W8OptionsTextEditor {
public:
    class Listener {
    public:
        virtual void OnTextEditComplete(W8OptionsTextEditor* editor, unsigned char cancelled) = 0;
    };
    virtual ~W8OptionsTextEditor() = default;
    Listener* m_listener;
    TextFieldGroup m_fields{1};
};

class W8OptionsKeyCapture {
public:
    virtual unsigned char OnKey(unsigned short key, unsigned short modifiers) = 0;
};

class W8OptionsSaveRowListener {
public:
    virtual void OnEditSaveName(W8OptionsSaveRow* row) = 0;
    virtual void OnActivateSave(W8OptionsSaveRow* row) = 0;
};

// VTABLE: WIZ8 0x005eee2c
class W8OptionsSaveRow : public W8TextControl {
public:
    W8OptionsSaveRow(int top, unsigned char save_mode);
    void SetSave(W8SaveSlot& save);
    void ClearSave();
    virtual void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void OnLeftButtonUp(int event) override;
    virtual void OnLeftButtonDoubleClick(int event) override;

    unsigned char m_save_mode;
    bool m_editing;
    unsigned char pad_0ba[2];
    W8SaveSlot* m_save;
    W8OptionsSaveRowListener* m_save_listener;
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
    float gamma;
    int render_options[9];
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

// VTABLE: WIZ8 0x005eef20
class W8OptionsSlider : public W8HorizontalRangeThumb {
public:
    W8OptionsSlider(int top, float* value, bool alternate);
    virtual void OnMouseMove(int event) override;
    virtual void Redraw(SDL_Surface& target, bool full_redraw) override;
    float* m_value;
};

/* Common base of the concrete option panels. */
// VTABLE: WIZ8 0x005eefa8
class W8OptionsPanel : public Controls {
public:
    W8OptionsPanel();
    virtual ~W8OptionsPanel();
    void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void Populate() = 0;
    virtual void SetActive(bool active);
    virtual void SetCurrent(int current);
    void AddCheckbox(int label, int* value);
    W8TextControl* AddChoiceButton(int label);
    W8OptionsSlider* AddSlider(int label, float* value, bool alternate);
    void AddChoices(int label, int count, const int* choices, int* value);

    int m_current;
    int m_content_top;
    int unknown_054;
    std::vector<std::unique_ptr<W8TextBuffer>> m_text_buffers;
    std::vector<std::unique_ptr<W8ControlSelection>> m_option_selections;
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
// VTABLE: WIZ8 0x005ef12c W8HorizontalRangeThumbListener
// VTABLE: WIZ8 0x005ef124 W8TextControl::Listener
class W8OptionsInterfacePanel : public W8OptionsPanel,
                                public W8HorizontalRangeThumbListener,
                                public W8TextControl::Listener {
public:
    virtual void Populate() override;
    virtual void OnDrag(W8HorizontalRangeThumb*) override {}
    virtual void OnDragEnd(W8HorizontalRangeThumb* thumb) override;
    virtual void OnPrimary(W8TextControl* control) override;
    W8OptionsSlider* m_tooltip_delay;
};

// VTABLE: WIZ8 0x005ef108 W8OptionsPanel
// VTABLE: WIZ8 0x005ef100 W8HorizontalRangeThumbListener
// VTABLE: WIZ8 0x005ef0f8 W8TextControl::Listener
class W8OptionsAudioPanel : public W8OptionsPanel,
                            public W8HorizontalRangeThumbListener,
                            public W8TextControl::Listener {
public:
    virtual void Populate() override;
    virtual void OnDrag(W8HorizontalRangeThumb* thumb) override;
    virtual void OnDragEnd(W8HorizontalRangeThumb* thumb) override;
    virtual void OnPrimary(W8TextControl* control) override;
    W8OptionsSlider* m_sliders[4];
    W8TextControl* m_mute_buttons[4];
};

// VTABLE: WIZ8 0x005ef0dc W8OptionsPanel
// VTABLE: WIZ8 0x005ef0d4 W8HorizontalRangeThumbListener
class W8OptionsGraphicsPanel : public W8OptionsPanel, public W8HorizontalRangeThumbListener {
public:
    virtual void Populate() override;
    virtual void OnDrag(W8HorizontalRangeThumb* thumb) override;
    virtual void OnDragEnd(W8HorizontalRangeThumb*) override;
};

// VTABLE: WIZ8 0x005ef0b8
class W8OptionsAdvancedGraphicsPanel : public W8OptionsPanel {
public:
    virtual void Populate() override;
};

/* The plain options button (Reset Defaults); key rows derive from it. */
// VTABLE: WIZ8 0x005eee80
class W8OptionsButton : public W8TextControl {
public:
    W8OptionsButton(int left, int top, int right, int bottom, std::string_view text);
    virtual void Redraw(SDL_Surface& target, bool full_redraw) override;
    virtual void OnMouseEnter(int event) override;
    virtual void OnMouseLeave(int event) override;
};

// VTABLE: WIZ8 0x005eeed0
class W8OptionsKeyButton : public W8OptionsButton {
public:
    W8OptionsKeyButton(int top, W8MGSCommand primary_binding,
                       W8MGSCommand secondary_binding);

    void SetKey(unsigned short key);
    void SetKeyText(unsigned short key);

    W8MGSCommand m_primary_binding;
    W8MGSCommand m_secondary_binding;
};

// VTABLE: WIZ8 0x005ef034 W8OptionsPanel
// VTABLE: WIZ8 0x005ef02c W8TextControl::Listener
// VTABLE: WIZ8 0x005ef024 W8OptionsKeyCapture
// VTABLE: WIZ8 0x005ef020 W8DialogCloseListener
class W8OptionsKeyboardPanel : public W8OptionsPanel,
                               public W8TextControl::Listener,
                               public W8OptionsKeyCapture,
                               public W8DialogCloseListener {
public:
    explicit W8OptionsKeyboardPanel(int panel);
    virtual void Populate() override;
    void InvalidateAll() override;
    virtual void SetActive(bool active) override;
    virtual void OnPrimary(W8TextControl* control) override;
    void OnSelectionChanged(W8ControlSelection* selection, int selected);
    virtual unsigned char OnKey(unsigned short key, unsigned short modifiers) override;
    virtual void OnDialogClosed(bool accepted, int value) override;
    void ClearDuplicateBinding(unsigned short key);
    void RefreshBindingLabels();
    int m_panel;
    W8ControlSelection m_selection;
    W8OptionsKeyButton* m_captured_button;
};

// VTABLE: WIZ8 0x005ef09c
class W8OptionsUnavailablePanel : public W8OptionsPanel {
public:
    explicit W8OptionsUnavailablePanel(int message);
    virtual void Populate() override;
    int m_message;
};

// VTABLE: WIZ8 0x005ef070 W8OptionsPanel
// VTABLE: WIZ8 0x005ef068 W8TextControl::Listener
// VTABLE: WIZ8 0x005ef064 W8DialogCloseListener
// VTABLE: WIZ8 0x005ef060 W8OptionsTextEditor::Listener
// VTABLE: WIZ8 0x005ef058 W8OptionsSaveRowListener
class W8OptionsSaveLoadPanel : public W8OptionsPanel,
                               public W8TextControl::Listener,
                               public W8DialogCloseListener,
                               public W8OptionsTextEditor::Listener,
                               public W8OptionsSaveRowListener {
public:
    explicit W8OptionsSaveLoadPanel(int panel);
    virtual void Populate() override;
    virtual void SetActive(bool active) override;
    virtual void SetCurrent(int current) override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnDialogClosed(bool accepted, int value) override;
    virtual void OnTextEditComplete(W8OptionsTextEditor* editor, unsigned char cancelled) override;
    virtual void OnEditSaveName(W8OptionsSaveRow* row) override;
    virtual void OnActivateSave(W8OptionsSaveRow* row) override;
    void OnSelectionChanged(W8ControlSelection* selection, int selected);
    void DeleteSelectedSave();
    void LoadSelectedSave();
    void SaveSelectedSave(std::string_view previous_name = {});
    int m_panel;
    std::vector<W8OptionsSaveRow*> m_rows;
    W8ControlSelection m_selection;
    W8TextControl* m_delete_button;
    W8TextControl* m_action_button;
    std::string m_previous_name;
    int m_editing_row;
};

/* One row of the main options menu table: the menu item id, two optional
   child text controls (left == -1 when absent), the button bounds, and the
   button's W8TextControl image indices. */
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

class W8OptionsScreen final : public W8DialogCloseListener {
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
    void ShowNotification(W8DialogCloseListener* listener, bool allow_cancel, int message,
                          int value);
    void BeginSaveNameEdit(W8OptionsTextEditor::Listener* listener, int row, std::string_view text);
    virtual void OnDialogClosed(bool accepted, int value) override;

    std::vector<std::unique_ptr<W8SaveSlot>> m_save_slots;
    bool m_redraw_pending;
    bool m_modal_closing;
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
    W8OptionsKeyCapture* m_key_capture;
};

extern std::unique_ptr<W8OptionsScreen> g_options_screen;
extern std::string g_options_last_save_name;

void SetLastSaveName(std::string_view target);
std::string_view GetLastSaveName(void);

unsigned char OptionsScreenEnter(void);
void OptionsScreenFrame(void);
unsigned char OptionsScreenLeave(int leaving);
