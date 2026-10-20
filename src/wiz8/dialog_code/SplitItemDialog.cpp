#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "dialog_code/DialogBase.h"
#include "npc_trade_price.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/AssayDialog.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/platform/input.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"

/* The split-stack dialog ("popup_splititem.sti"). Inventory splits use eight
   buttons and ten labels; the two trade modes add a passive frame and a price
   label for each side of the transaction. The six retail expansions of the running-totals refresh share UpdateTotals. */

struct W8SplitButtonOffset {
    int x;
    int y;
};

static W8SplitButtonOffset g_split_button_offsets[10] = {
    {0xd9, 0x5a}, {0xbe, 0x5a}, {0xbc, 0x49},  {0xbc, 0x6f}, {0x81, 0x35},
    {0x81, 0x83}, {0xed, 0x95}, {0x111, 0x95}, {0xf4, 0x35}, {0xf4, 0x83},
};

static W8ControlsRect g_split_text_bounds[14] = {
    {0x48, 0x49, 0xb6, 0x55},  {0x48, 0x6f, 0xb6, 0x7b},  {0xbc, 0x49, 0xef, 0x55},
    {0x48, 0x0f, 0x12a, 0x1f}, {0x48, 0x23, 0x7d, 0x2f},  {0x81, 0x23, 0x12a, 0x2f},
    {0x48, 0x35, 0x7d, 0x41},  {0x81, 0x35, 0xb7, 0x41},  {0x48, 0x83, 0x7d, 0x8f},
    {0x81, 0x83, 0xb7, 0x8f},  {0xbb, 0x35, 0xf0, 0x41},  {0xf4, 0x35, 0x12a, 0x41},
    {0xbb, 0x83, 0xf0, 0x8f},  {0x90, 0x83, 0x12a, 0x8f},
};

static int g_split_text_string_ids[14] = {
    268, 269, 270, 270, 271, 270, 272, 270, 272, 270, 273, 270, 273, 270,
};

static DesignRect g_split_count_field_bounds = {0xbc, 0x6f, 0xef, 0x7b};

W8SplitItemDialog::W8SplitItemDialog(W8ItemSplitMode mode, W8ItemInstance* item, int count,
                                     SDL_Point origin)
{
    SetOrigin(origin.x, origin.y);
    for (int index = 0; index < 10; ++index) m_buttons.emplace_back(InputLayer());

    SetExtent(0x142, 0xbd);
    if (mode == W8_ITEM_SPLIT_SELL || mode == W8_ITEM_SPLIT_BUY) {
        SetBackground("Data\\Dialogs\\popup_splititem.sti", 0);
    } else if (mode == W8_ITEM_SPLIT_INVENTORY) {
        SetBackground("Data\\Dialogs\\popup_splititem.sti", 1);
    }
    m_mode = mode;
    m_remaining = item->stack_count;
    m_stack_total = item->stack_count;
    if (count == -1) {
        if (g_item_records[item->iItemNo].maximum_quantity <= 0xa) {
            split_count = 1;
        } else {
            split_count = item->stack_count >> 1;
        }
        m_remaining = item->stack_count - split_count;
    } else {
        split_count = count;
        m_remaining = item->stack_count - count;
    }
    m_item = item;
    split_result = W8_SPLIT_RESULT_PENDING;

    split_result = W8_SPLIT_RESULT_PENDING;
    if (!CreateButtons()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }
    CreateTextBuffers();
    CreateNumericInput();
    UpdateTotals();
}

W8SplitItemDialog::~W8SplitItemDialog() = default;

bool W8SplitItemDialog::CreateButtons()
{
    int count;
    int index;

    count = 0;
    if (m_mode == W8_ITEM_SPLIT_INVENTORY) {
        count = 8;
    } else if (static_cast<unsigned int>(m_mode) <= W8_ITEM_SPLIT_BUY) {
        count = 10;
    }
    m_buttons[0].Configure(
        ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 0xc, 9, 10, 0xd, 0xb),
        {.on_click = [this](W8ButtonWidget*) { StepSplit(-1); },
         .on_right_click = [this](W8ButtonWidget*) { StepSplit(-5); }});
    m_buttons[1].Configure(
        ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 7, 4, 5, 8, 6),
        {.on_click = [this](W8ButtonWidget*) { StepSplit(1); },
         .on_right_click = [this](W8ButtonWidget*) { StepSplit(5); }});
    m_buttons[2].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                BUTTON_NO_IMAGE),
                           {.priority = UI_PRIORITY_LOWEST});
    m_buttons[3].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                BUTTON_NO_IMAGE),
                           {.on_click = [this](W8ButtonWidget*) { OnCountFieldClick(); }});
    m_buttons[4].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                BUTTON_NO_IMAGE),
                           {.priority = UI_PRIORITY_LOWEST});
    m_buttons[5].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                BUTTON_NO_IMAGE),
                           {.priority = UI_PRIORITY_LOWEST});
    m_buttons[6].Configure(
        ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2),
        {.on_click = [this](W8ButtonWidget*) { Finish(W8_SPLIT_RESULT_CONFIRMED); }});
    m_buttons[7].Configure(
        ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 5, 6, 9, 7),
        {.on_click = [this](W8ButtonWidget*) { Finish(W8_SPLIT_RESULT_CANCELLED); }});
    if (m_mode == W8_ITEM_SPLIT_SELL || m_mode == W8_ITEM_SPLIT_BUY) {
        m_buttons[8].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                    BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                    BUTTON_NO_IMAGE),
                               {.priority = UI_PRIORITY_LOWEST});
        m_buttons[9].Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                    BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                    BUTTON_NO_IMAGE),
                               {.priority = UI_PRIORITY_LOWEST});
    }
    m_buttons[0].Policy().repeat = true;
    m_buttons[1].Policy().repeat = true;
    for (index = 0; index < count; ++index) {
        m_buttons[index].SetPosition(g_split_button_offsets[index].x + m_x,
                                     g_split_button_offsets[index].y + m_y);
    }
    return true;
}

void W8SplitItemDialog::CreateTextBuffers()
{
    int count;
    int index;
    std::string header;
    W8ControlsRect bounds;

    count = 0;
    if (m_mode == W8_ITEM_SPLIT_INVENTORY) {
        count = 10;
    } else if (static_cast<unsigned int>(m_mode) <= W8_ITEM_SPLIT_BUY) {
        count = 14;
    }
    for (index = 0; index < count; ++index) {
        bounds.left = g_split_text_bounds[index].left + m_x;
        bounds.top = g_split_text_bounds[index].top + m_y;
        bounds.right = g_split_text_bounds[index].right + m_x;
        bounds.bottom = g_split_text_bounds[index].bottom + m_y;
        m_texts[index] = std::make_unique<W8TextBuffer>(
            &bounds, g_localized_strings[g_split_text_string_ids[index]], g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter, 4);
    }
    m_texts[2]->SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    m_texts[7]->SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    m_texts[9]->SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
    if (m_mode == W8_ITEM_SPLIT_SELL || m_mode == W8_ITEM_SPLIT_BUY) {
        m_texts[11]->SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
        m_texts[13]->SetLayoutMode(g_W8TextBufferAlignRight | g_W8TextBufferAlignMiddle);
        if (m_mode == W8_ITEM_SPLIT_SELL) {
            m_texts[1]->SetText(g_localized_strings[274], g_wiz_text_font_secondary);
            header = g_localized_strings[268];
        } else {
            m_texts[1]->SetText(g_localized_strings[275], g_wiz_text_font_secondary);
            header = g_localized_strings[276];
        }
        m_texts[0]->SetText(header, g_wiz_text_font_secondary);
    }
    m_texts[3]->SetText(FormatItemDisplayName(m_item, false), g_wiz_text_font_secondary);
    m_texts[5]->SetText(
        fmt::format(
            "{} ({})", g_localized_strings[g_equip_class_name_ids[GetItemEquipClass(m_item)]],
            g_localized_strings[g_generic_item_name_notice[GetItemUnidentifiedNameIndex(m_item)]]),
        g_wiz_text_font_secondary);
}

void W8SplitItemDialog::CreateNumericInput()
{
    W8ControlsRect bounds;

    bounds.left = g_split_count_field_bounds.left + m_x;
    bounds.top = g_split_count_field_bounds.top + m_y;
    bounds.right = g_split_count_field_bounds.right + m_x;
    bounds.bottom = g_split_count_field_bounds.bottom + m_y;
    m_count_input = std::make_unique<W8DialogNumericInput>(
        InputLayer(), &bounds, split_count, g_wiz_text_font_secondary, [this](int value) {
            split_count = value;
            m_remaining = m_stack_total - value;
            UpdateTotals();
        });
    m_count_input->m_maximum = m_stack_total;
}

void W8SplitItemDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    DrawSprite(target, *ItemSprites(m_item->iItemNo), 0, m_x + 0x18, m_y + 0xe);
    for (auto& button : m_buttons) button.Draw(target);
    for (auto& text : m_texts)
        if (text) text->Paint(target);
    m_count_input->Draw(target);
}

void W8SplitItemDialog::UpdateCostLabels()
{
    W8ItemInstance stack;
    std::string text;
    int remaining_price;
    int split_price;

    stack = *m_item;
    if (m_mode == W8_ITEM_SPLIT_INVENTORY) {
        return;
    }
    if (m_mode == W8_ITEM_SPLIT_SELL) {
        stack.stack_count = static_cast<unsigned char>(split_count);
        if (split_count == 0) {
            split_price = 0;
        } else {
            split_price = CalculateTradeStackPrice(g_npc_interaction_state->dialogue_npc, &stack,
                                                   W8_TRADE_PRICE_PARTY_SELLS);
        }
        stack.stack_count = static_cast<unsigned char>(m_remaining);
        if (m_remaining == 0) {
            remaining_price = 0;
        } else {
            remaining_price = CalculateTradeStackPrice(g_npc_interaction_state->dialogue_npc,
                                                       &stack, W8_TRADE_PRICE_PARTY_SELLS);
        }
    } else if (m_mode == W8_ITEM_SPLIT_BUY) {
        stack.stack_count = static_cast<unsigned char>(split_count);
        if (split_count == 0) {
            split_price = 0;
        } else {
            split_price = CalculateTradeStackPrice(g_npc_interaction_state->dialogue_npc, &stack,
                                                   W8_TRADE_PRICE_PARTY_BUYS);
        }
        stack.stack_count = static_cast<unsigned char>(m_remaining);
        if (m_remaining == 0) {
            remaining_price = 0;
        } else {
            remaining_price = CalculateTradeStackPrice(g_npc_interaction_state->dialogue_npc,
                                                       &stack, W8_TRADE_PRICE_PARTY_BUYS);
        }
    } else {
        remaining_price = GetItemStackValue(m_item);
        split_price = remaining_price;
    }
    text = fmt::format("{:d}", remaining_price);
    m_texts[11]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[8].m_dirty = true;
    m_texts[11]->SetGeometryDirty();
    text = fmt::format("{:d}", split_price);
    m_texts[13]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[9].m_dirty = true;
    m_texts[13]->SetGeometryDirty();
}

void W8SplitItemDialog::UpdateArrowStates()
{
    m_buttons[0].UpdateEnabledState(split_count != 0);
    m_buttons[1].UpdateEnabledState(m_remaining != 0);
}

void W8SplitItemDialog::UpdateAcceptButton()
{
    W8ItemInstance stack;
    bool can_accept;

    stack = *m_item;
    stack.stack_count = static_cast<unsigned char>(split_count);
    can_accept = split_count != 0;
    switch (m_mode) {
    case W8_ITEM_SPLIT_SELL:
        if (g_status.party.gold <
            static_cast<unsigned int>(CalculateTradeStackPrice(
                g_npc_interaction_state->dialogue_npc, &stack, W8_TRADE_PRICE_PARTY_SELLS))) {
            can_accept = false;
        }
        break;
    case W8_ITEM_SPLIT_BUY:
        if (g_status.party.gold <
            static_cast<unsigned int>(CalculateTradeStackPrice(
                g_npc_interaction_state->dialogue_npc, &stack, W8_TRADE_PRICE_PARTY_BUYS))) {
            can_accept = false;
        }
        break;
    default:
        break;
    }
    if (can_accept) {
        m_buttons[6].SetEnabled(true);
    } else {
        m_buttons[6].SetEnabled(false);
    }
}

/* The same totals refresh appears in the numeric-field callback and all
   split-count button callbacks. */
void W8SplitItemDialog::UpdateTotals()
{
    std::string text;

    UpdateArrowStates();
    UpdateAcceptButton();
    text = fmt::format("{:d}", m_remaining);
    m_texts[2]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[2].m_dirty = true;
    m_texts[2]->SetGeometryDirty();
    m_count_input->SetValue(split_count);
    m_buttons[3].m_dirty = true;
    m_count_input->m_dirty = true;
    text = fmt::format("{:.1f}", static_cast<double>(GetItemUnitWeight(m_item) * m_remaining) *
                                     kItemWeightDisplayScale);
    m_texts[7]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[4].m_dirty = true;
    m_texts[7]->SetGeometryDirty();
    text = fmt::format("{:.1f}", static_cast<double>(GetItemUnitWeight(m_item) * split_count) *
                                     kItemWeightDisplayScale);
    m_texts[9]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[5].m_dirty = true;
    m_texts[9]->SetGeometryDirty();
    UpdateCostLabels();
}

bool W8SplitItemDialog::HandleInputEvent(const InputEvent* input)
{
    if (m_count_input->HandleInput(input)) return true;
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        const SDL_Keycode key = input->key;
        if (key == '\r') {
            split_result = W8_SPLIT_RESULT_CONFIRMED;
            m_keep_open = false;
        } else if (key == SDLK_ESCAPE) {
            m_keep_open = false;
            return m_keep_open;
        }
    }
    return m_keep_open;
}

bool W8SplitItemDialog::ProcessInput()
{
    BeginModalInput();
    m_count_input->ProcessInput([this](const InputEvent& input) { HandleInputEvent(&input); });
    if (!m_keep_open) {
        EndModalInput();
    }
    return m_keep_open;
}

/* Move `count` items between the split and the remaining stack; a negative
   count moves them back. Both sides stay within the stack. */
void W8SplitItemDialog::StepSplit(int count)
{
    split_count = std::clamp(split_count + count, 0, m_stack_total);
    m_remaining = std::clamp(m_remaining - count, 0, m_stack_total);
    UpdateTotals();
}

void W8SplitItemDialog::Finish(W8SplitDialogResult result)
{
    split_result = result;
    m_keep_open = false;
}

void W8SplitItemDialog::OnCountFieldClick()
{
    if (!m_count_input) {
        return;
    }
    const SDL_Point mouse = g_ui.pointer;
    SDL_Point point{mouse.x - m_x, mouse.y - m_y};
    if (!ScreenPointInRect(&g_split_count_field_bounds, &point)) {
        return;
    }
    point.x -= g_split_count_field_bounds.left;
    point.y -= g_split_count_field_bounds.top;
    m_count_input->StartEditing(point);
}

std::function<void()> W8SplitItemDialog::TakeCompletion(bool completed)
{
    W8DialogBase::TakeCompletion(completed);
    if (!completed || split_result != W8_SPLIT_RESULT_CONFIRMED || !on_complete) return {};
    return [callback = std::exchange(on_complete, {}), result = split_count] { callback(result); };
}

bool W8SplitItemDialog::TakeDirty()
{
    bool dirty = W8DialogBase::TakeDirty();
    for (auto& button : m_buttons) dirty |= button.TakeDirty();
    for (auto& text : m_texts)
        if (text) dirty |= std::exchange(text->m_geometryDirty, false);
    dirty |= std::exchange(m_count_input->m_dirty, false);
    return dirty;
}
