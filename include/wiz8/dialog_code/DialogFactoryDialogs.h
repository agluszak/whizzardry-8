#include <deque>

#include "Font.h"
#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/text_input.h"

struct W8WorldItem;
struct W8ItemInstance;
class W8ButtonWidget;
class W8TextBuffer;

enum W8ItemSplitMode { W8_ITEM_SPLIT_INVENTORY = 0, W8_ITEM_SPLIT_SELL = 1, W8_ITEM_SPLIT_BUY = 2 };

enum W8SplitDialogResult {
    W8_SPLIT_RESULT_PENDING = 0,
    W8_SPLIT_RESULT_CONFIRMED = 1,
    W8_SPLIT_RESULT_CANCELLED = 2
};

/* The small numeric entry field embedded by the split dialogs. */
class W8DialogNumericInput {
public:
    W8DialogNumericInput(UiLayer& layer, const W8ControlsRect* bounds, int value, Font& font,
                         std::function<void(int)> on_change);
    void SetValue(int value);
    void StopEditing();
    void StartEditing(SDL_Point point);
    void ProcessInput(const std::function<void(const InputEvent&)>& unhandled);
    void Draw(SDL_Surface& target);
    bool HandleInput(const InputEvent* input);

private:
    UiLayer& m_editor_input;
    std::unique_ptr<TextFieldGroup> m_editor;

public:
    W8ControlsRect m_bounds;
    Font* m_font;
    int m_value;
    bool m_dirty;
    /* The stack total for the split dialogs; -1 is "no maximum". */
    unsigned int m_maximum;
    std::function<void(int)> m_on_change;
};

// VTABLE: WIZ8 0x005ef7c8
class W8ListBoxDialog : public W8DialogBase {
public:
    bool TakeDirty() override;
    W8ListBoxDialog();
    void Configure();
    virtual void Draw(SDL_Surface& target) override;

    virtual bool ProcessInput() override;

private:
    void UpdateLayout();
    /* Text-area button height divided by the dialog font height. */
    int GetVisibleLineCount();
    /* Select a text line and scroll it into the visible range. */
    void SetCurrentLine(int line);
    void SetFirstVisibleLine(int line);
    void ClearLines();
    /* Keyboard handling for the visible text list. */
    bool HandleInputEvent(const InputEvent* input);

    /* Owner-local activation callbacks for the text area, scroll arrows and
       the confirmation buttons. */
    void OnTextAreaClick();
    void OnUp();
    void OnDown();
    void OnOk();
    void OnCancel();
    void OnSliderTrackClick();

public:
    std::vector<std::string> m_lines;
    SDL_Color m_fill_colour; /* highlight fill colour */
    SDL_Rect m_heading_bounds{}, m_footer_bounds{};
    GenericButtonImage area_inlay;  /* DialogInlay inlay for m_area_button */
    W8ButtonWidget m_area_button{InputLayer()}; /* scrolling text area */
    W8ButtonWidget m_up_button{InputLayer()};
    W8ButtonWidget m_down_button{InputLayer()};
    W8ButtonWidget m_slider_button{InputLayer()};
    GenericButtonImage third_btn_inlay; /* DialogInlay inlay for m_third_text_button */
    W8ButtonWidget m_third_text_button{InputLayer()};
    W8ButtonWidget m_ok_button{InputLayer()};
    W8ButtonWidget m_cancel_button{InputLayer()};
    bool m_scrollable; /* scrolling area is scrollable */
    int m_first_visible_line;
    int m_selected_line;
    GenericButtonImage edge_inlay; /* DialogEdge inlay for the text buttons */
    short flags;
};

// VTABLE: WIZ8 0x005ef9f0
class W8SplitAmountDialog : public W8DialogBase {
public:
    bool TakeDirty() override;
    std::function<void()> TakeCompletion(bool completed) override;
    std::function<void(int)> on_complete;
    /* The split-size entry point; the pool total seeds both the
       remaining and total fields. */
    W8SplitAmountDialog(int total, SDL_Point origin);
    virtual ~W8SplitAmountDialog() override;
    virtual void Draw(SDL_Surface& target) override;
    virtual bool ProcessInput() override;

private:
    /* Create and place the six dialog buttons. */
    ButtonAppearance m_frame;
    SDL_Rect m_frame_bounds{};
    bool CreateButtons();
    /* Create the three text buffers above the numeric field. */
    void CreateTextBuffers();
    /* Create the numeric entry field and its backing button. */
    void CreateNumericInput();
    /* Enable the plus/minus buttons from the current split. */
    void UpdateButtonStates();
    /* Push the split into the text buffers and the numeric field. */
    void UpdateTextBuffers();
    /* Keyboard handling for the plus/minus buttons and the field. */
    void DecreaseAmount(int amount);
    void IncreaseAmount(int amount);
    bool HandleInputEvent(const InputEvent* input);

    /* Per-button callbacks stored through W8ButtonWidget::Configure. */
    void Finish(W8SplitDialogResult result);
    void ActivateField();

private:
    std::array<std::unique_ptr<W8ButtonWidget>, 5> m_buttons;
    /* The labels named by g_split_amount_string_ids; [2] shows the
       amount still remaining. */
    std::array<std::unique_ptr<W8TextBuffer>, 3> m_text_buffers;
    std::unique_ptr<W8DialogNumericInput> m_split_input;
    int Remaining() const { return m_total - m_taken; }
    int m_taken;     /* the field value */
    int m_total;
    W8SplitDialogResult m_result; /* 1 confirms, 2 cancels */
};

/* The trigger-owned item picker. The item group is what the trigger hands in
   and the completion callback receives. */
// VTABLE: WIZ8 0x005ef810
class W8TriggerItemPickerDialog : public W8DialogBase {
public:
    bool TakeDirty() override;
    std::function<void()> TakeCompletion(bool completed) override;
    std::function<void(W8WorldItem*)> on_complete;
    W8TriggerItemPickerDialog();
    virtual void Draw(SDL_Surface& target) override;

    virtual bool ProcessInput() override;

    int AddItem(std::unique_ptr<W8WorldItem> item);
    W8WorldItem* ReturnItemsToGroup();
    void SetItemGroup(W8WorldItem* group);

private:
    void UpdateLayout();
    bool CreateButtons();
    /* Clamp and apply the first visible item row. Fewer than five items force
       the first row. Out-of-range input is ignored, not clamped. */
    void SetFirstVisible(int index);
    /* Sync the four scroll buttons' pressed/visible state with the scroll
       offset and the per-item enable flags. */
    void RefreshScrollButtons();
    bool IsItemSelected(int index);
    void SetItemSelected(int index, bool selected);
    void ToggleItem(int index, W8ButtonWidget* button);
    void ToggleAllItems();
    void ShowVisibleItemInfo(int row);
    void ScrollItemsToMouse();
    /* Move every flagged item to the destination: -1 copies it into the shared
       party pool, any other value gives it to that party slot's character.
       Each successful transfer removes the entry; a failure plays
       the beep. An emptied picker closes itself. */
    void TransferSelectedItems(int destination);
    /* Handle one event the picker owns: keyboard list navigation and clicks. */
    bool HandleInputEvent(const InputEvent* input);

public:
    struct PickerEntry {
        std::unique_ptr<W8WorldItem> item;
        bool selected = false;
    };
    std::vector<PickerEntry> entries;
    std::array<std::unique_ptr<W8ButtonWidget>, 13> m_buttons;
    int m_first_item;
    W8WorldItem* m_item_group;
};

/* The item-split dialog returns the confirmed count to its owner. */
// VTABLE: WIZ8 0x005efb78
class W8SplitItemDialog : public W8DialogBase {
public:
    bool TakeDirty() override;
    std::function<void()> TakeCompletion(bool completed) override;
    std::function<void(int)> on_complete;
    W8SplitItemDialog(W8ItemSplitMode mode, W8ItemInstance* item, int count, SDL_Point origin);
    virtual ~W8SplitItemDialog() override;
    virtual void Draw(SDL_Surface& target) override;
    virtual bool ProcessInput() override;

private:
    /* Create and place the arrow, frame, accept and cancel
       buttons; eight for inventory splits, ten in trade modes. */
    bool CreateButtons();
    /* Create the label text buffers and fill the item-name rows. */
    void CreateTextBuffers();
    /* Create the count entry field over its backing button. */
    void CreateNumericInput();
    /* Refresh the two trade-price labels in trade modes. */
    void UpdateCostLabels();
    /* Enable the minus/plus arrows while each side has count. */
    void UpdateArrowStates();
    /* Enable accept when the split is nonzero and affordable. */
    void UpdateAcceptButton();
    /* Shared refresh for the numeric-field and split-count callbacks. */
    void UpdateTotals();
    /* Numeric-field and Enter/Escape handling for ProcessInput. */
    bool HandleInputEvent(const InputEvent* input);

    /* Per-button callbacks stored through W8ButtonWidget::Configure. */
    void StepSplit(int count);
    void Finish(W8SplitDialogResult result);
    void OnCountFieldClick();

public:
    std::deque<W8ButtonWidget> m_buttons; /* minus/plus, frames, accept/cancel */
    std::array<std::unique_ptr<W8TextBuffer>, 14> m_texts;
    std::unique_ptr<W8DialogNumericInput> m_count_input;
    int m_remaining; /* the count left in the source stack */
    int split_count;
    int m_stack_total; /* stack_count when the dialog opened */
    W8SplitDialogResult split_result;

private:
    W8ItemSplitMode m_mode; /* 0 inventory, 1 and 2 trade modes */
    W8ItemInstance* m_item;
};

extern const W8SplitDialogResult g_amount_split_confirm_result;
extern const W8SplitDialogResult g_item_split_confirm_result;
extern const W8ItemSplitMode g_item_split_inventory_mode;
extern const W8ItemSplitMode g_item_split_sell_mode;
extern const W8ItemSplitMode g_item_split_buy_mode;
