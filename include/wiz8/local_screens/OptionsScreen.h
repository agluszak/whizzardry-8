#pragma once

#include <cstdint>
#include <string_view>
#include <string>

#include <memory>
#include <vector>

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
        virtual void OnTextEditComplete(W8OptionsTextEditor* editor, bool cancelled) = 0;
    };
    virtual ~W8OptionsTextEditor()
    {
        RemoveTextInputField(0);
        KillTextInputMode();
    }
    Listener* m_listener;
};

class W8OptionsKeyCapture {
public:
    virtual bool OnKey(std::uint16_t key, std::uint16_t modifiers) = 0;
};

class W8OptionsSaveRowListener {
public:
    virtual void OnEditSaveName(W8OptionsSaveRow* row) = 0;
    virtual void OnActivateSave(W8OptionsSaveRow* row) = 0;
};

// VTABLE: WIZ8 0x005eee2c
class W8OptionsSaveRow : public W8TextControl {
public:
    W8OptionsSaveRow(Controls* owner, std::int32_t top, std::uint8_t save_mode);
    void SetSave(W8SaveSlot& save);
    void ClearSave();
    virtual void Redraw(bool full_redraw) override;
    virtual void OnLeftButtonUp(std::int32_t event) override;
    virtual void OnLeftButtonDoubleClick(std::int32_t event) override;

    std::uint8_t m_save_mode;
    bool m_editing;
    std::uint8_t pad_0ba[2];
    W8SaveSlot* m_save;
    W8OptionsSaveRowListener* m_save_listener;
};

/* The settings transfer routines at 005A6E20/005A72F0/005A7320 share this
   receiver. The trailing direction flag is part of the record, not a global.
   Float loads/stores distinguish the slider values from the integer options. */
struct W8OptionsValues {
    void TransferSettings();
    void TransferByte(std::int32_t* value, std::uint8_t* setting);
    void TransferRenderOption(std::int32_t* value, W8RenderOption option);

    std::int32_t mouselook_toggle;
    std::int32_t mouselook_smoothing;
    std::int32_t invert_mouse_y;
    std::int32_t ctrl_right_click_info;
    std::int32_t difficulty;
    std::int32_t camera_auto_rotation;
    std::int32_t continuous_combat;
    std::int32_t numeric_hit_points;
    std::int32_t autoscroll_combat_messages;
    std::int32_t simplified_npc_interaction;
    std::int32_t verbose_combat_messages;
    std::int32_t autoswap_weapons;
    std::int32_t auto_advance_character;
    std::int32_t autotarget_spells;
    std::int32_t stop_movement_for_events; /* UI checkbox 0x801; TransferSettings does not persist it */
    std::int32_t skill_increase_messages;
    float combat_speed;
    float monster_movement_speed;
    float text_display_delay_ms;
    std::int32_t auto_save;
    float gamma;
    std::int32_t render_options[9];
    std::int32_t monster_shadows;
    std::int32_t smooth_monster_animations;
    std::int32_t smooth_world_animations;
    std::int32_t pc_confirmations;
    std::int32_t pc_subtitles;
    std::int32_t applying;
};

extern W8OptionsValues g_options_values;

/* A set of option panels; it owns the panels in its vector. */
class W8OptionsPanelSet {
public:
    W8OptionsPanelSet();
    ~W8OptionsPanelSet();
    void Advance();
    void Retreat();

    std::int32_t m_page_count;
    std::int32_t unknown_004;
    std::uint8_t m_compact_layout;
    std::uint8_t m_hide_navigation;
    bool m_active; /* set once the set's panels are built */
    std::int32_t m_current;
    std::vector<std::unique_ptr<W8OptionsPanel>> m_panels;
};

/* Widgets bound to an option value; their input handlers store through the
   value pointer. */
// VTABLE: WIZ8 0x005eed8c
class W8OptionsCheckbox : public W8TextControl {
public:
    W8OptionsCheckbox(Controls* owner, std::int32_t top, std::int32_t* value);
    virtual void OnLeftButtonUp(std::int32_t event) override;
    std::int32_t* m_value;
};

// VTABLE: WIZ8 0x005eef20
class W8OptionsSlider : public W8HorizontalRangeThumb {
public:
    W8OptionsSlider(Controls* owner, std::int32_t top, float* value, bool alternate);
    virtual void OnMouseMove(std::int32_t event) override;
    virtual void Redraw(bool full_redraw) override;
    float* m_value;
};

// VTABLE: WIZ8 0x005eefd4
class W8OptionsSelection : public W8ControlSelection {
public:
    explicit W8OptionsSelection(std::int32_t* value);
    virtual ~W8OptionsSelection();
    virtual void OnPrimary(W8TextControl* control) override;
    std::int32_t* m_value;
};

/* Common base of the concrete option panels. */
// VTABLE: WIZ8 0x005eefa8
class W8OptionsPanel : public Controls {
public:
    W8OptionsPanel();
    virtual ~W8OptionsPanel();
    virtual void Redraw() override;
    virtual void Populate() = 0;
    virtual void SetActive(bool active);
    virtual void SetCurrent(std::int32_t current);
    W8OptionsCheckbox* AddCheckbox(std::int32_t label, std::int32_t* value);
    W8TextControl* AddChoiceButton(std::int32_t label);
    W8OptionsSlider* AddSlider(std::int32_t label, float* value, bool alternate);
    void AddChoices(std::int32_t label, std::int32_t count, const std::int32_t* choices, std::int32_t* value);

    std::int32_t m_current;
    std::int32_t m_content_top;
    std::int32_t unknown_054;
    std::vector<std::unique_ptr<W8TextBuffer>> m_text_buffers;
    std::vector<std::unique_ptr<W8OptionsSelection>> m_option_selections;
};

// VTABLE: WIZ8 0x005ef174
class W8OptionsGamePanel : public W8OptionsPanel {
public:
    W8OptionsGamePanel();
    virtual void Populate() override;
};

// VTABLE: WIZ8 0x005ef158
class W8OptionsMousePanel : public W8OptionsPanel {
public:
    W8OptionsMousePanel();
    virtual void Populate() override;
};

// VTABLE: WIZ8 0x005ef134 W8OptionsPanel
// VTABLE: WIZ8 0x005ef12c W8HorizontalRangeThumbListener
// VTABLE: WIZ8 0x005ef124 W8TextControl::Listener
class W8OptionsInterfacePanel : public W8OptionsPanel,
                                public W8HorizontalRangeThumbListener,
                                public W8TextControl::Listener {
public:
    W8OptionsInterfacePanel();
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
    W8OptionsAudioPanel();
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
    W8OptionsGraphicsPanel();
    virtual void Populate() override;
    virtual void OnDrag(W8HorizontalRangeThumb* thumb) override;
    virtual void OnDragEnd(W8HorizontalRangeThumb*) override;
};

// VTABLE: WIZ8 0x005ef0b8
class W8OptionsAdvancedGraphicsPanel : public W8OptionsPanel {
public:
    W8OptionsAdvancedGraphicsPanel();
    virtual void Populate() override;
};

/* The plain options button (Reset Defaults); key rows derive from it. */
// VTABLE: WIZ8 0x005eee80
class W8OptionsButton : public W8TextControl {
public:
    W8OptionsButton(Controls* owner, std::int32_t left, std::int32_t top, std::int32_t right, std::int32_t bottom, std::string_view text);
    virtual void Redraw(bool full_redraw) override;
    virtual void OnMouseEnter(std::int32_t event) override;
    virtual void OnMouseLeave(std::int32_t event) override;
};

// VTABLE: WIZ8 0x005eeed0
class W8OptionsKeyButton : public W8OptionsButton {
public:
    W8OptionsKeyButton(Controls* owner, std::int32_t top, W8MGSCommand primary_binding,
                       W8MGSCommand secondary_binding);

    void SetKey(std::uint16_t key);
    void SetKeyText(std::uint16_t key);

    W8MGSCommand m_primary_binding;
    W8MGSCommand m_secondary_binding;
};

// VTABLE: WIZ8 0x005ef034 W8OptionsPanel
// VTABLE: WIZ8 0x005ef02c W8TextControl::Listener
// VTABLE: WIZ8 0x005ef028 W8ControlSelectionListener
// VTABLE: WIZ8 0x005ef024 W8OptionsKeyCapture
// VTABLE: WIZ8 0x005ef020 W8DialogCloseListener
class W8OptionsKeyboardPanel : public W8OptionsPanel,
                               public W8TextControl::Listener,
                               public W8ControlSelectionListener,
                               public W8OptionsKeyCapture,
                               public W8DialogCloseListener {
public:
    explicit W8OptionsKeyboardPanel(std::int32_t panel);
    virtual void Populate() override;
    virtual void Invalidate(const W8ControlsRect* bounds) override;
    virtual void SetActive(bool active) override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSelectionChanged(W8ControlSelection* selection, std::int32_t selected) override;
    virtual bool OnKey(std::uint16_t key, std::uint16_t modifiers) override;
    virtual void OnDialogClosed(bool accepted, std::int32_t value) override;
    void ClearDuplicateBinding(std::uint16_t key);
    void RefreshBindingLabels();
    std::int32_t m_panel;
    W8ControlSelection m_selection;
    W8OptionsKeyButton* m_captured_button;
};

// VTABLE: WIZ8 0x005ef09c
class W8OptionsUnavailablePanel : public W8OptionsPanel {
public:
    explicit W8OptionsUnavailablePanel(std::int32_t message);
    virtual void Populate() override;
    std::int32_t m_message;
};

// VTABLE: WIZ8 0x005ef070 W8OptionsPanel
// VTABLE: WIZ8 0x005ef068 W8TextControl::Listener
// VTABLE: WIZ8 0x005ef064 W8DialogCloseListener
// VTABLE: WIZ8 0x005ef060 W8OptionsTextEditor::Listener
// VTABLE: WIZ8 0x005ef058 W8OptionsSaveRowListener
// VTABLE: WIZ8 0x005ef054 W8ControlSelectionListener
class W8OptionsSaveLoadPanel : public W8OptionsPanel,
                               public W8TextControl::Listener,
                               public W8DialogCloseListener,
                               public W8OptionsTextEditor::Listener,
                               public W8OptionsSaveRowListener,
                               public W8ControlSelectionListener {
public:
    explicit W8OptionsSaveLoadPanel(std::int32_t panel);
    virtual void Populate() override;
    virtual void SetActive(bool active) override;
    virtual void SetCurrent(std::int32_t current) override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnDialogClosed(bool accepted, std::int32_t value) override;
    virtual void OnTextEditComplete(W8OptionsTextEditor* editor, bool cancelled) override;
    virtual void OnEditSaveName(W8OptionsSaveRow* row) override;
    virtual void OnActivateSave(W8OptionsSaveRow* row) override;
    virtual void OnSelectionChanged(W8ControlSelection* selection, std::int32_t selected) override;
    void DeleteSelectedSave();
    void LoadSelectedSave();
    void SaveSelectedSave(std::string_view previous_name = {});
    std::int32_t m_panel;
    std::vector<W8OptionsSaveRow*> m_rows;
    W8ControlSelection m_selection;
    W8TextControl* m_delete_button;
    W8TextControl* m_action_button;
    std::string m_previous_name;
    std::int32_t m_editing_row;
};

/* One row of the main options menu table: the menu item id, two optional
   child text controls (left == -1 when absent), the button bounds, and the
   button's W8TextControl image indices. */
struct W8OptionsMenuRow {
    std::int32_t item_id;
    W8ControlsRect child_bounds[2];
    W8ControlsRect bounds;
    std::int32_t image_indices[5];
};

/* Inclusive range of CreateOptionsPanel indices shown by one menu item. */
struct W8OptionsPanelRange {
    std::int32_t first;
    std::int32_t last;
};

/* One options menu row. */
// VTABLE: WIZ8 0x005eed3c W8TextControl
// VTABLE: WIZ8 0x005eed34 W8TextControl::Listener
class W8OptionsMenuButton : public W8TextControl, public W8TextControl::Listener {
public:
    W8OptionsMenuButton(Controls* owner, const W8OptionsMenuRow* row);
    virtual void Redraw(bool full_redraw) override;
    virtual void EnableSecondaryState(bool immediate) override;
    virtual void OnPrimary(W8TextControl* control) override;

    std::int32_t m_item_id;
};

/* The options menu: its rows and the panel set they select. */
// VTABLE: WIZ8 0x005eefec Controls
// VTABLE: WIZ8 0x005eefe4 W8TextControl::Listener
class W8OptionsMenuSet : public Controls, public W8TextControl::Listener {
public:
    W8OptionsMenuSet();
    virtual ~W8OptionsMenuSet();
    virtual void Redraw() override;
    virtual void OnPrimary(W8TextControl* control) override;

    W8OptionsPanelSet* m_pMenuSet; /* OptionsScreen.cpp:1481 */
    W8TextControl* m_next;
    W8TextControl* m_previous;
    std::unique_ptr<W8TextBuffer> m_page_text;

    void UpdateMenuSet();
};

// VTABLE: WIZ8 0x005ef008 W8ControlSelectionListener
// VTABLE: WIZ8 0x005ef000 W8TextControl::Listener
// VTABLE: WIZ8 0x005eeffc W8DialogCloseListener
class W8OptionsScreen final : public W8ControlSelectionListener,
                        public W8TextControl::Listener,
                        public W8DialogCloseListener {
public:
    W8OptionsScreen();
    ~W8OptionsScreen();
    void SelectPanel(std::int32_t selected, bool notify);
    void CreateControls();
    std::uint8_t ProcessInput(const InputEvent* input);
    void Redraw();
    void ShowNotification(W8DialogCloseListener* listener, bool allow_cancel, std::int32_t message,
                          std::int32_t value);
    void BeginSaveNameEdit(W8OptionsTextEditor::Listener* listener, std::int32_t row, std::string_view text);
    virtual void OnSelectionChanged(W8ControlSelection* control, std::int32_t selected) override;
    virtual void OnPrimary(W8TextControl* control) override;
    virtual void OnSecondary(W8TextControl*) override {}
    virtual void OnDialogClosed(bool accepted, std::int32_t value) override;

    std::vector<std::unique_ptr<W8SaveSlot>> m_save_slots;
    bool m_redraw_pending;
    bool m_modal_closing;
    std::uint8_t padding_01e[2];
    std::int32_t m_selected_panel;
    std::unique_ptr<Controls> m_controls;
    std::unique_ptr<W8OptionsMenuSet> m_menu_set;
    std::unique_ptr<W8ControlSelection> m_menu_selection;
    std::uint8_t unknown_030[8];
    std::unique_ptr<W8OptionsPanelSet> m_panel[8];
    std::unique_ptr<W8MessageDialogBase> m_active_modal;
    std::unique_ptr<W8OptionsTextEditor> m_text_editor;
    W8OptionsKeyCapture* m_key_capture;
};

extern std::unique_ptr<W8OptionsScreen> g_options_screen;
extern std::string g_options_last_save_name;

void SetLastSaveName(std::string_view target);
std::string_view GetLastSaveName(void);

bool OptionsScreenEnter(void);
void OptionsScreenFrame(void);
bool OptionsScreenLeave(std::int32_t leaving);
