#include <deque>
#include "Font.h"
#pragma once

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_pixels.h>
#include <vector>
#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "input.h"
#include "wiz8/text_input.h"
#include "Types.h"
#include "compat/compiler.h"

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
    W8DialogNumericInput(int control_id, const W8ControlsRect* bounds, int value, Font& font,
                         W8DialogBase* dialog, W8ButtonWidget* button);
    void SetValue(int value);
    void SetActive(bool active);
    void SetActive(bool active, const SDL_Point* point);
    void Draw(SDL_Surface& target, bool force);
    bool HandleInput(const InputEvent* input);

private:
    UiLayer& m_editor_input;
    std::unique_ptr<TextFieldGroup> m_editor;

public:
    W8ControlsRect m_bounds;
    Font* m_font;
    int m_value;
    bool m_dirty;
    bool m_active;
    /* The stack total for the split dialogs; -1 is "no maximum". */
    unsigned int m_maximum;
    W8DialogBase* m_dialog;
    W8ButtonWidget* m_button;
    /* Echoed back as the argument of m_dialog->OnNumericInputChanged. */
    int m_control_id;
};

// VTABLE: WIZ8 0x005ef7c8
class W8ListBoxDialog : public W8DialogBase {
public:
    W8ListBoxDialog();
    virtual ~W8ListBoxDialog() override;
    virtual int CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw(SDL_Surface& target) override;

    virtual void SetText(std::string_view text) override;
    virtual bool ProcessInput() override;

private:
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
    void TextAreaButtonCallback(W8ButtonWidget* button);
    void SliderTrackButtonCallback(W8ButtonWidget* button);

  public:
    std::vector<std::string> m_lines;
    SDL_Color m_fill_colour; /* highlight fill colour */
    W8ButtonWidget m_text_button{InputLayer()};
    W8ButtonWidget m_second_text_button{InputLayer()};
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
    /* NPCInteractionSubscreen's destroy callback reads m_taken and
       m_result back out of the closing dialog. */
    friend void OnNpcTradeSplitDialogDestroy(W8DialogBase* dialog);

public:
    W8SplitAmountDialog();
    /* The split-size entry point; the pool total seeds both the
       remaining and total fields. */
    W8SplitAmountDialog(int total);
    virtual ~W8SplitAmountDialog() override;
    virtual int CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw(SDL_Surface& target) override;
    virtual bool ProcessInput() override;
    virtual void OnNumericInputChanged(int value) override;

private:
    /* Create and place the six dialog buttons. */
    bool CreateButtons();
    /* Create the three text buffers above the numeric field. */
    bool CreateTextBuffers();
    /* Create the numeric entry field and its backing button. */
    bool CreateNumericInput();
    /* Enable the plus/minus buttons from the current split. */
    void UpdateButtonStates();
    /* Push the split into the text buffers and the numeric field. */
    void UpdateTextBuffers();
    /* Keyboard handling for the plus/minus buttons and the field. */
    void DecreaseAmount(int amount);
    void IncreaseAmount(int amount);
    bool HandleInputEvent(const InputEvent* input);

    /* Per-button callbacks stored through W8ButtonWidget::Configure. */
    void SplitAccept();
    void SplitCancel();
    void SplitActivateField(W8ButtonWidget* button);

  private:
    std::array<std::unique_ptr<W8ButtonWidget>, 6> m_buttons;
    /* The labels named by g_split_amount_string_ids; [2] shows the
       amount still remaining. */
    std::array<std::unique_ptr<W8TextBuffer>, 3> m_text_buffers;
    std::unique_ptr<W8DialogNumericInput> m_split_input;
    /* The numeric field while the cursor or keyboard owns it. */
    W8DialogNumericInput* m_active_field;
    int m_remaining; /* total minus the field value */
    int m_taken;     /* the field value */
    int m_total;
    W8SplitDialogResult m_result; /* 1 confirms, 2 cancels */
};

/* The trigger-owned item picker. The item group is what the trigger hands in
   and the destroy callback hands back. */
// VTABLE: WIZ8 0x005ef810
class W8TriggerItemPickerDialog : public W8DialogBase {
public:
    W8TriggerItemPickerDialog();
    virtual ~W8TriggerItemPickerDialog() override;
    virtual int CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw(SDL_Surface& target) override;

    virtual bool ProcessInput() override;

    int AddItem(W8WorldItem* item);
    W8WorldItem* ReturnItemsToGroup();
    void SetItemGroup(W8WorldItem* group);

private:
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
    void ShowVisibleItemInfo(int row);
    /* Move every flagged item to the destination: -1 copies it into the shared
       party pool, any other value gives it to that party slot's character.
       Each successful transfer unlinks the item and its flag; a failure plays
       the beep. An emptied picker closes itself. */
    void TransferSelectedItems(int destination);
    /* Handle one event the picker owns: keyboard list navigation and clicks. */
    bool HandleInputEvent(const InputEvent* input);

    /* Per-button callbacks stored through W8ButtonWidget::Configure.
       Their configuration captures the owning picker directly. */
    void ToggleAllItems();

  private:
    void ScrollItemsUp();
    void ScrollItemsDown();
    void ScrollItemsToMouse(W8ButtonWidget* button);

  public:
    std::vector<W8WorldItem*> items;
    std::vector<unsigned char> flags;
    std::array<std::unique_ptr<W8ButtonWidget>, 13> m_buttons;
    int m_first_item;
    W8WorldItem* m_item_group;
};

/* The item-split dialog opened for stackable item stacks. The destroy
   callback reads back split_count and tests split_result. */
// VTABLE: WIZ8 0x005efb78
class W8SplitItemDialog : public W8DialogBase {
public:
    W8SplitItemDialog(W8ItemSplitMode mode, W8ItemInstance* item, int count);
    virtual ~W8SplitItemDialog() override;
    virtual int CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw(SDL_Surface& target) override;
    virtual bool ProcessInput() override;
    virtual void OnNumericInputChanged(int value) override;

private:
    /* Create and place the arrow, frame, accept and cancel
       buttons; eight for inventory splits, ten in trade modes. */
    bool CreateButtons();
    void DestroyButtons();
    /* Create the label text buffers and fill the item-name rows. */
    bool CreateTextBuffers();
    void DestroyTextBuffers();
    /* Create the count entry field over its backing button. */
    bool CreateNumericInput();
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
    void OnSplitDecrement();
    void OnSplitIncrement();
    void OnSplitDecrementMany();
    void OnSplitIncrementMany();
    void OnAccept();
    void OnCancel();
    void OnCountFieldClick(W8ButtonWidget* button);

  public:
    std::deque<W8ButtonWidget> m_buttons; /* minus/plus, frames, accept/cancel */
    W8TextBuffer* m_texts[14];
    std::unique_ptr<W8DialogNumericInput> m_count_input;
    /* The numeric field while a click or keypress owns it. */
    W8DialogNumericInput* m_active_input;
    int m_remaining; /* the count left in the source stack */
    int split_count;
    int m_stack_total; /* stack_count when the dialog opened */
    W8SplitDialogResult split_result;

private:
    W8ItemSplitMode m_mode; /* 0 inventory, 1 and 2 trade modes */
    W8ItemInstance* m_item;
    bool m_first_draw; /* draw the item icon once */
};

extern const W8SplitDialogResult g_amount_split_confirm_result;
extern const W8SplitDialogResult g_item_split_confirm_result;
extern const W8ItemSplitMode g_item_split_inventory_mode;
extern const W8ItemSplitMode g_item_split_sell_mode;
extern const W8ItemSplitMode g_item_split_buy_mode;
