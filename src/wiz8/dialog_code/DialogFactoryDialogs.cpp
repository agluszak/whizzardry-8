#include "wiz8/engine_code/Video2.h"
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <string>
#include <memory>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <utility>

#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "image.h"
#include "sprite.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/fonts.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/xstatus.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "Font.h"
#include "himage.h"
#include "input.h"
#include "soundman.h"
#include "dialog_code/DialogBase.h"
#include "layouts/game_status.h"
#include "layouts/item_instance.h"
#include "local_code/ControlsRect.h"
#include "local_code/TextBuffer.h"

void W8ListBoxDialog::ClearLines()
{
    m_lines.clear();
    m_selected_line = -1;
    m_scrollable = false;
    m_first_visible_line = 0;
}

// FUNCTION: WIZ8 0x005cbb40
W8ListBoxDialog::W8ListBoxDialog()
{
    m_fill_colour = {0, 0x60, 0, SDL_ALPHA_OPAQUE};
    ClearLines();
}

// FUNCTION: WIZ8 0x005cbce0
W8ListBoxDialog::~W8ListBoxDialog()
{
    NotifyDestroyed();
    DestroyControls();
}

// FUNCTION: WIZ8 0x005cbd70
void W8ListBoxDialog::SetText(std::string_view text)
{
    if (m_text_button.IsConfigured()) {
        m_text_button.SetText(text);
        W8DialogBase::SetText({});
        return;
    }
    W8DialogBase::SetText(text);
}

// FUNCTION: WIZ8 0x005CC650
int W8ListBoxDialog::GetVisibleLineCount()
{
    if (!m_area_button.IsConfigured()) {
        return 0;
    }
    return m_area_button.Bounds().h /
           static_cast<int>(static_cast<unsigned int>(g_dialog_interface_font->height()));
}

/* Both selection directions clamp the requested top row independently of
   the selected row. */
void W8ListBoxDialog::SetFirstVisibleLine(int requested)
{
    if (m_area_button.IsConfigured()) {
        int first = requested;
        if (static_cast<int>(m_lines.size()) - GetVisibleLineCount() < requested) {
            first = static_cast<int>(m_lines.size()) - GetVisibleLineCount();
        }
        if (first < 0) {
            first = 0;
        } else if (static_cast<int>(m_lines.size()) - GetVisibleLineCount() < requested) {
            first = static_cast<int>(m_lines.size()) - GetVisibleLineCount();
        }
        if (first != m_first_visible_line) {
            m_first_visible_line = first;
            m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
        }
    }
}

// FUNCTION: WIZ8 0x005CCB80
void W8ListBoxDialog::SetCurrentLine(int line)
{
    if (m_selected_line == line) {
        return;
    }
    if (line == -1) {
        m_selected_line = -1;
        return;
    }
    int target = line < 0 ? 0 : line;
    if (static_cast<int>(m_lines.size()) - 1 < target) {
        target = static_cast<int>(m_lines.size()) - 1;
    }
    int visible = GetVisibleLineCount();
    if (target < m_first_visible_line) {
        SetFirstVisibleLine(target);
    } else if (target < static_cast<int>(m_lines.size()) &&
               m_first_visible_line - 1 + visible < target) {
        SetFirstVisibleLine(target - visible + 1);
    }
    m_selected_line = target;
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005CD2B0
bool W8ListBoxDialog::HandleInputEvent(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown && input->type != InputEventType::KeyRepeat) {
        return m_keep_open;
    }

    if (IsKeyHeld(VK_UP)) {
        if (m_selected_line > 0) {
            SetCurrentLine(m_selected_line - 1);
        }
    } else if (IsKeyHeld(VK_DOWN) && m_selected_line < static_cast<int>(m_lines.size())) {
        SetCurrentLine(m_selected_line + 1);
    }

    switch (toupper(input->key)) {
    case VK_ESCAPE:
        m_selected_line = -1;
        m_keep_open = false;
        return false;
    case VK_RETURN:
        if (m_selected_line != -1) {
            m_keep_open = false;
        }
        return m_keep_open;
    case VK_PRIOR:
        SetCurrentLine(m_selected_line - GetVisibleLineCount());
        return m_keep_open;
    case VK_NEXT:
        SetCurrentLine(m_selected_line + GetVisibleLineCount());
        return m_keep_open;
    case VK_HOME:
        SetCurrentLine(0);
        return m_keep_open;
    case VK_END:
        SetCurrentLine(static_cast<int>(m_lines.size()) - 1);
        return m_keep_open;
    default:
        return m_keep_open;
    }
}

/* Build two text buttons, the scrolling text area, the up and down arrows, a
   slider and the confirmation pair. */
// FUNCTION: WIZ8 0x005cbdb0
int W8ListBoxDialog::CreateControls()
{
    if (W8DialogBase::CreateControls() != 0) {
        return m_error;
    }
    edge_inlay = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogEdge.STI", {}, "Data\\Dialogs\\DialogEdge.STI", {}, m_background_path, static_cast<short>(m_background_flags), 0, 0);
    if (!edge_inlay) return m_error = 4;
    m_text_button.Configure(
        ButtonAppearance{.generic_image = edge_inlay,
                         .string = std::string(m_text),
                         .usFont = &(*g_dialog_interface_font),
                         .sForeColor = g_dialog_font_foreground,
                         .sShadowColor = g_dialog_font_background},
        {m_x + 9, m_y + 9, m_width - 0x12, g_dialog_interface_font->height() * 0x96 / 100},
        {.priority = UI_PRIORITY_HIGHEST - 1});
    if (!m_text.empty()) {
        SetText(m_text);
    }
    m_text_button.SetMultiColor(g_dialog_font_enabled);
    area_inlay =
        LoadGenericButtonImages({}, "Data\\Dialogs\\DialogInlay.STI", {}, "Data\\Dialogs\\DialogInlay.STI", {}, "Data\\Dialogs\\DialogBackground_dark.STI", 0, 3, 3);
    if (!area_inlay) {
        m_error = 4;
        return 4;
    }
    m_area_button.Configure(
        ButtonAppearance{.generic_image = area_inlay,
                         .string = {},
                         .usFont = &(*g_dialog_interface_font),
                         .sForeColor = g_dialog_font_foreground,
                         .sShadowColor = g_dialog_font_background},
        {m_text_button.Bounds().x, m_text_button.Bounds().y + m_text_button.Bounds().h + 4,
         m_text_button.Bounds().w, 0x14},
        {.on_click = [this](W8ButtonWidget* button) { TextAreaButtonCallback(button); },
         .policy = {.activation = ButtonActivation::Press},
         .priority = UI_PRIORITY_HIGHEST - 1});
    const bool configured =
        m_up_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogUpArrow.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { SetCurrentLine(m_selected_line - 1); },
             .policy = {.activation = ButtonActivation::Press, .repeat = true},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_down_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogDownArrow.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { SetCurrentLine(m_selected_line + 1); },
             .policy = {.activation = ButtonActivation::Press, .repeat = true},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_slider_button.Configure(ButtonGraphics::Load("Data\\Dialogs\\DialogSlideBar.STI",
                                                       BUTTON_NO_IMAGE, 0, BUTTON_NO_IMAGE, 0,
                                                       BUTTON_NO_IMAGE),
                                  {.priority = UI_PRIORITY_HIGHEST - 2}) &&
        m_ok_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 3, 0, 1, 2, 2),
            {.on_click =
                 [this](W8ButtonWidget*) {
                     m_keep_open = false;
                     m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
                 }}) &&
        m_cancel_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 7, 4, 5, 6, 6),
            {.on_click = [this](W8ButtonWidget*) {
                m_keep_open = false;
                SetCurrentLine(-1);
                m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
            }});
    if (!configured) {
        DestroyControls();
    } else {
        third_btn_inlay = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogInlay.STI", {}, "Data\\Dialogs\\DialogInlay.STI", {}, "Data\\Dialogs\\DialogBackground_dark.STI", 0, 3, 3);
        if (third_btn_inlay) {
            m_third_text_button.Configure(
                ButtonAppearance{.generic_image = third_btn_inlay,
                                 .string = {},
                                 .usFont = &(*g_dialog_interface_font),
                                 .sForeColor = g_dialog_font_foreground,
                                 .sShadowColor = g_dialog_font_background},
                {0, 0, 1, 1},
                {.on_click = [this](W8ButtonWidget* button) { SliderTrackButtonCallback(button); },
                 .policy = {.activation = ButtonActivation::Press, .repeat = true},
                 .priority = UI_PRIORITY_HIGHEST - 2});
            if (m_third_text_button.IsConfigured()) {
                m_second_text_button.Configure(
                    ButtonAppearance{.generic_image = edge_inlay,
                                     .string = {},
                                     .usFont = &(*g_dialog_interface_font),
                                     .sForeColor = g_dialog_font_foreground,
                                     .sShadowColor = g_dialog_font_background},
                    {m_x + 9, m_y + m_height - m_ok_button.Bounds().h * 0x96 / 100 - 9,
                     m_width - 0x12, m_ok_button.Bounds().h * 0x96 / 100},
                    {.priority = UI_PRIORITY_HIGHEST - 1});
                m_cancel_button.SetPosition(
                    m_second_text_button.Bounds().x - m_cancel_button.Bounds().w +
                        m_second_text_button.Bounds().w,
                    m_second_text_button.Bounds().h / 2 -
                        m_cancel_button.Bounds().h / 2 + m_second_text_button.Bounds().y);
                m_ok_button.SetPosition(
                                  m_cancel_button.Bounds().x -
                                      m_cancel_button.Bounds().w / 2 -
                                      m_ok_button.Bounds().w,
                                  m_cancel_button.Bounds().y);
                m_area_button.Resize(m_text_button.Bounds().w,
                    -6 - m_text_button.Bounds().y - m_text_button.Bounds().h +
                    m_second_text_button.Bounds().y);
                int selected = m_selected_line;
                m_selected_line = 0;
                SetCurrentLine(selected);
                return 0;
            }
        }
    }
    m_error = 6;
    return 6;
}

// FUNCTION: WIZ8 0x005cc430
void W8ListBoxDialog::DestroyControls()
{
    W8DialogBase::DestroyControls();
    m_text_button.Reset();
    m_second_text_button.Reset();
    m_area_button.Reset();
    m_up_button.Reset();
    m_down_button.Reset();
    m_slider_button.Reset();
    m_third_text_button.Reset();
    m_ok_button.Reset();
    m_cancel_button.Reset();
    area_inlay.reset();
    third_btn_inlay.reset();
    edge_inlay.reset();
    ClearLines();
}

/* The scrolling text area. Rows shrink by the scroll arrow when the list does
   not fit; the slider position tracks the selected line. */
// FUNCTION: WIZ8 0x005cc690
void W8ListBoxDialog::Draw(SDL_Surface& target)
{
    SDL_Rect rect;
    int index;
    int line;

    if (m_area_button.m_dirty || m_third_text_button.m_dirty ||
        m_up_button.m_dirty || m_down_button.m_dirty || m_slider_button.m_dirty ||
        m_ok_button.m_dirty || m_cancel_button.m_dirty)
        m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    if ((m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) == 0) return;
    W8DialogBase::Draw(target);
    m_text_button.m_dirty = m_second_text_button.m_dirty = true;
    m_text_button.DrawAndInvalidate(target);
    m_second_text_button.DrawAndInvalidate(target);
    int dx = m_text_button.Bounds().x - m_x;
    int dy = m_text_button.Bounds().y + m_text_button.Bounds().h + 4 - m_y;
    int width = m_text_button.Bounds().w;
    int height = -6 - m_text_button.Bounds().y - m_text_button.Bounds().h +
                 m_second_text_button.Bounds().y;
    unsigned int visible_lines;
    if (static_cast<int>(m_lines.size()) < height / static_cast<int>(static_cast<unsigned int>(
                                                        g_dialog_interface_font->height()))) {
        m_scrollable = false;
        visible_lines = static_cast<int>(m_lines.size());
    } else {
        int rows = height / static_cast<int>(
                                static_cast<unsigned int>(g_dialog_interface_font->height()));
        if (static_cast<int>(m_lines.size()) > rows) {
            width = width + (-7 - m_up_button.Bounds().w);
            m_scrollable = true;
            visible_lines = rows;
        } else {
            m_scrollable = false;
            visible_lines = static_cast<int>(m_lines.size());
        }
    }
    m_area_button.Resize(width, height);
    m_area_button.DrawAndInvalidate(target);
    m_up_button.SetVisible(m_scrollable);
    m_down_button.SetVisible(m_scrollable);
    m_slider_button.SetVisible(m_scrollable);
    m_third_text_button.SetVisible(m_scrollable);
    if (m_scrollable) {
        m_up_button.SetPosition( m_x + dx + 4 + width, m_y + 4 + dy);
        m_down_button.SetPosition( m_x + dx + 4 + width,
                          m_y + dy + height - m_down_button.Bounds().h - 4);
        m_slider_button.SetPosition( m_x + dx + 4 + width,
            m_area_button.Bounds().h +
                ((height - m_up_button.Bounds().h - m_down_button.Bounds().h - 7) *
                 m_selected_line) /
                    static_cast<int>(m_lines.size()) +
                m_y + 3 + dy);
        const int fill_left = m_x + width + dx;
        const int fill_top = m_y + dy + m_up_button.Bounds().h;
        const SDL_Rect fill{fill_left, fill_top, m_up_button.Bounds().w,
                            height - m_up_button.Bounds().h - m_down_button.Bounds().h};
        SDL_FillSurfaceRect(&target, &fill, Get16BPPColor(m_fill_colour));
        m_third_text_button.SetPosition(m_x + dx + width, m_y + dy);
        m_third_text_button.Resize(m_up_button.Bounds().w + 7, height);
        m_third_text_button.DrawAndInvalidate(target);
        m_up_button.DrawAndInvalidate(target, true);
        m_down_button.DrawAndInvalidate(target, true);
        m_slider_button.DrawAndInvalidate(target, true);
    }
    if (m_ok_button.IsEnabled()) {
        m_ok_button.DrawAndInvalidate(target, true);
    }
    if (m_cancel_button.IsEnabled()) {
        m_cancel_button.DrawAndInvalidate(target, true);
    }
    if (!m_scrollable)
        m_up_button.m_dirty = m_down_button.m_dirty = m_slider_button.m_dirty =
            m_third_text_button.m_dirty = false;
    rect = m_area_button.Bounds();
    {
        SDL_Rect text_clip{rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6};
        ScopedClipRect clipping(target, text_clip);
        if (static_cast<int>(m_lines.size()) <=
            static_cast<int>(visible_lines + m_first_visible_line)) {
            visible_lines = static_cast<int>(m_lines.size()) - m_first_visible_line;
        }
        for (index = 0; index < static_cast<int>(visible_lines); ++index) {
            line = m_first_visible_line + index;
            const std::string& text = m_lines[line];
            if (line == m_selected_line) {
                const int top = m_y + g_dialog_interface_font->height() * index + 2 + dy;
                const SDL_Rect selection{m_x + 3 + dx, top, width - 6,
                                         g_dialog_interface_font->height() - 2};
                SDL_FillSurfaceRect(&target, &selection,
                                    Get16BPPColor(m_fill_colour));
            }
            DrawText(target, *g_dialog_interface_font,
                     {m_x + 3 + dx,
                      static_cast<int>(g_dialog_interface_font->height()) * index + m_y + 2 + dy},
                     text);
        }
    }
}

// FUNCTION: WIZ8 0x005cd470
bool W8ListBoxDialog::ProcessInput()
{
    BeginModalInput();
    const auto route = [this] { return m_keep_open ? InputRoute::Handled : InputRoute::Stop; };
    PumpUiInput({
        .consumed = [&](const InputEvent&) { return route(); },
        .unhandled =
            [&](const InputEvent& input) {
                if (input.type == InputEventType::MouseWheel) {
                    SetCurrentLine(m_selected_line - input.wheel);
                } else {
                    HandleInputEvent(&input);
                }
                return route();
            },
    });
    if (!m_keep_open) EndModalInput();
    return m_keep_open;
}

/* The split-size dialog ("popup_splititem.sti" with a numeric entry field):
   two stepper buttons, two passive frames around the field, accept/cancel. */
struct W8SplitAmountButtonOffset {
    int x;
    int y;
};

// GLOBAL: WIZ8 0x0064fc08
static W8SplitAmountButtonOffset g_split_amount_button_offsets[6] = {
    {0xdb, 0x20}, {0xc0, 0x20}, {0xbe, 0xf}, {0xbe, 0x35}, {0xed, 0x47}, {0x111, 0x47},
};

// GLOBAL: WIZ8 0x0064fc38
static W8ControlsRect g_split_amount_text_bounds[3] = {
    {0x4a, 0xf, 0xb8, 0x1b},
    {0x4a, 0x35, 0xb8, 0x41},
    {0xbe, 0xf, 0xf4, 0x1b},
};

// GLOBAL: WIZ8 0x0064fc68
static W8ScreenRect g_split_amount_field_bounds = {0xbe, 0x35, 0xf4, 0x41};

// GLOBAL: WIZ8 0x0064fc78
static int g_split_amount_string_ids[3] = {265, 266, 267};

// FUNCTION: WIZ8 0x005d97d0
W8SplitAmountDialog::W8SplitAmountDialog()
    : W8SplitAmountDialog(0)
{
}

// FUNCTION: WIZ8 0x005d9890
W8SplitAmountDialog::W8SplitAmountDialog(int total)
    : m_active_field(nullptr), m_remaining(total), m_taken(0), m_total(total),
      m_result(W8_SPLIT_RESULT_PENDING)
{
    SetExtent(322, 111);
    SetBackground("Data\\Dialogs\\popup_splititem.sti", 2);
}

// FUNCTION: WIZ8 0x005d9ac0
void W8SplitAmountDialog::DestroyControls()
{
    m_active_field = nullptr;
    m_split_input.reset();
    for (auto& text : m_text_buffers) text.reset();
    for (auto& button : m_buttons) button.reset();
    W8DialogBase::DestroyControls();
}

// FUNCTION: WIZ8 0x005d9930
W8SplitAmountDialog::~W8SplitAmountDialog() = default;

/* The split-item dialog. Three text buffers show the running totals, the
   numeric field edits the taken amount, and the buttons step it. */
// FUNCTION: WIZ8 0x005d99f0
int W8SplitAmountDialog::CreateControls()
{
    if (int error = W8DialogBase::CreateControls(); error != 0) return error;
    m_result = W8_SPLIT_RESULT_PENDING;
    if (!CreateButtons()) {
        DestroyControls();
        m_error = 7;
        return 7;
    }
    if (!CreateTextBuffers()) {
        DestroyControls();
        m_error = 7;
        return 7;
    }
    if (!CreateNumericInput()) {
        DestroyControls();
        m_error = 7;
        return 7;
    }
    UpdateButtonStates();
    UpdateTextBuffers();
    return 0;
}

// FUNCTION: WIZ8 0x005d9b30
bool W8SplitAmountDialog::CreateButtons()
{
    int index;

    for (auto& button : m_buttons) button = std::make_unique<W8ButtonWidget>(InputLayer());

    if (!m_buttons[0]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 0xc, 9, 10, 0xd, 0xb),
            {.on_click = [this](W8ButtonWidget*) { DecreaseAmount(1); },
             .on_right_click = [this](W8ButtonWidget*) { DecreaseAmount(5); },
             .policy = {.repeat = true}})) {
        return false;
    }

    if (!m_buttons[1]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 7, 4, 5, 8, 6),
            {.on_click = [this](W8ButtonWidget*) { IncreaseAmount(1); },
             .on_right_click = [this](W8ButtonWidget*) { IncreaseAmount(5); },
             .policy = {.repeat = true}})) {
        return false;
    }
    if (!m_buttons[2]->Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                      BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                      BUTTON_NO_IMAGE),
                                 {.priority = UI_PRIORITY_LOWEST})) {
        return false;
    }
    if (!m_buttons[3]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", BUTTON_NO_IMAGE, 3,
                                 BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE),
            {.on_click = [this](W8ButtonWidget* button) { SplitActivateField(button); }})) {
        return false;
    }
    if (!m_buttons[4]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2),
            {.on_click = [this](W8ButtonWidget*) { SplitAccept(); }})) {
        return false;
    }
    if (!m_buttons[5]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 5, 6, 9, 7),
            {.on_click = [this](W8ButtonWidget*) { SplitCancel(); }})) {
        return false;
    }
    for (index = 0; index < 6; ++index) {
        m_buttons[index]->SetPosition(g_split_amount_button_offsets[index].x + m_x,
                                      g_split_amount_button_offsets[index].y + m_y);
    }
    return true;
}

// FUNCTION: WIZ8 0x005d9d10
bool W8SplitAmountDialog::CreateTextBuffers()
{
    int index;
    W8ControlsRect bounds;

    for (index = 0; index < 3; ++index) {
        bounds.left = g_split_amount_text_bounds[index].left + m_x;
        bounds.top = g_split_amount_text_bounds[index].top + m_y;
        bounds.right = g_split_amount_text_bounds[index].right + m_x;
        bounds.bottom = g_split_amount_text_bounds[index].bottom + m_y;
        m_text_buffers[index] = std::make_unique<W8TextBuffer>(
            &bounds, g_localized_strings[g_split_amount_string_ids[index]], g_wiz_text_font_secondary,
            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight, 4);
    }
    return true;
}

// FUNCTION: WIZ8 0x005d9e30
bool W8SplitAmountDialog::CreateNumericInput()
{
    W8ControlsRect bounds;

    m_active_field = 0;
    bounds.left = g_split_amount_field_bounds.left + m_x;
    bounds.top = g_split_amount_field_bounds.top + m_y;
    bounds.right = g_split_amount_field_bounds.right + m_x;
    bounds.bottom = g_split_amount_field_bounds.bottom + m_y;
    m_split_input = std::make_unique<W8DialogNumericInput>(
        0, &bounds, m_taken, g_wiz_text_font_secondary, this, m_buttons[3].get());
    m_split_input->m_maximum = 1000000;
    return true;
}

// FUNCTION: WIZ8 0x005d9f20
void W8SplitAmountDialog::Draw(SDL_Surface& target)
{
    int index;

    if ((m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) != 0) {
        if (!m_initialized && CreateControls() != 0) return;
        for (index = 0; index < 6; ++index) {
            m_buttons[index]->m_dirty = true;
        }
        for (index = 0; index < 3; ++index) {
            m_text_buffers[index]->SetGeometryDirty();
        }
        W8DialogNumericInput* numeric = m_split_input.get();
        numeric->m_dirty = true;
        numeric->m_button->m_dirty = true;
        W8DialogBase::Draw(target);
        CatalogArtwork::Load(0x1ac, 0).draw(target, 0, m_x + 0x18, m_y + 0x1a);
    }
    if (m_buttons[3]->m_dirty) {
        W8DialogNumericInput* numeric = m_split_input.get();
        numeric->m_dirty = true;
        numeric->m_button->m_dirty = true;
    }
    for (index = 0; index < 6; ++index) {
        if (m_buttons[index] != 0) {
            m_buttons[index]->DrawAndInvalidate(target);
        }
    }
    for (index = 0; index < 3; ++index) {
        if (m_text_buffers[index] != 0) {
            m_text_buffers[index]->RenderToTarget(0, false, target);
        }
    }
    if (m_split_input != 0) {
        m_split_input->Draw(target, false);
    }
}

// FUNCTION: WIZ8 0x005da000
void W8SplitAmountDialog::UpdateTextBuffers()
{
    std::string text;

    text = fmt::format("{:d}", m_remaining);
    m_text_buffers[2]->SetText(text, g_wiz_text_font_secondary);
    m_buttons[2]->m_dirty = true;
    m_text_buffers[2]->SetGeometryDirty();
    if (m_remaining < 0) {
        m_text_buffers[2]->SetFontStateIndex(0);
    } else {
        m_text_buffers[2]->SetFontStateIndex(-1);
    }
    m_split_input->SetValue(m_taken);
    m_buttons[3]->m_dirty = true;
    m_split_input->m_dirty = true;
    m_split_input->m_button->m_dirty = true;
}

// FUNCTION: WIZ8 0x005da090
void W8SplitAmountDialog::UpdateButtonStates()
{
    m_buttons[0]->UpdateEnabledState(m_taken != 0);
    m_buttons[1]->UpdateEnabledState(m_remaining != 0);
    if (m_remaining < 0) {
        m_buttons[4]->SetEnabled(false);
        m_buttons[4]->m_dirty = true;
        return;
    }
    m_buttons[4]->SetEnabled(true);
    m_buttons[4]->m_dirty = true;
}

// FUNCTION: WIZ8 0x005da140
void W8SplitAmountDialog::OnNumericInputChanged(int value)
{
    if (value == 0) {
        int field_value = m_split_input->m_value;
        m_remaining = m_total - field_value;
        m_taken = field_value;
        UpdateButtonStates();
        UpdateTextBuffers();
    }
}

void W8SplitAmountDialog::DecreaseAmount(int amount)
{
    m_taken -= amount;
    if (m_taken < 0) {
        m_taken = 0;
    }
    m_remaining += amount;
    if (m_remaining > m_total) {
        m_remaining = m_total;
    }
    UpdateButtonStates();
    UpdateTextBuffers();
}

void W8SplitAmountDialog::IncreaseAmount(int amount)
{
    m_remaining -= amount;
    if (m_remaining < 0) {
        m_remaining = 0;
    }
    m_taken += amount;
    if (m_taken > m_total) {
        m_taken = m_total;
    }
    UpdateButtonStates();
    UpdateTextBuffers();
}

// FUNCTION: WIZ8 0x005da180
bool W8SplitAmountDialog::HandleInputEvent(const InputEvent* input)
{
    if (m_split_input && m_split_input->m_active && m_split_input->HandleInput(input))
        return true;
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        int key = toupper(input->key);
        if (key == 0x1b) {
            m_keep_open = false;
        } else if (key == 0x2b) {
            IncreaseAmount(1);
            return m_keep_open;
        } else if (key == 0x2d) {
            DecreaseAmount(1);
            return m_keep_open;
        }
    }
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005da2a0
bool W8SplitAmountDialog::ProcessInput()
{
    ProcessNumericDialogInputEvents(this, m_active_field, &W8SplitAmountDialog::HandleInputEvent);
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005da580
void W8SplitAmountDialog::SplitAccept()
{
    m_result = W8_SPLIT_RESULT_CONFIRMED;
    m_keep_open = false;
}

// FUNCTION: WIZ8 0x005da5a0
void W8SplitAmountDialog::SplitCancel()
{
    m_result = W8_SPLIT_RESULT_CANCELLED;
    m_keep_open = false;
}

// FUNCTION: WIZ8 0x005da5c0
void W8SplitAmountDialog::SplitActivateField(W8ButtonWidget* button)
{
    SDL_Point point;

    if (button != 0) {

        const SDL_Point mouse = button->GetMousePosition();
        if (m_split_input != 0) {
            point.x = mouse.x - m_x;
            point.y = mouse.y - m_y;
            if (!ScreenPointInRect(&g_split_amount_field_bounds, &point)) {
                return;
            }
            point.x -= g_split_amount_field_bounds.left;
            point.y -= g_split_amount_field_bounds.top;
            m_split_input->SetActive(true, &point);
            m_active_field = m_split_input.get();
        }
    }
}

/* The trigger-owned item picker allocates its buttons in CreateControls. */
// FUNCTION: WIZ8 0x005cd710
W8TriggerItemPickerDialog::W8TriggerItemPickerDialog()
{
    SetExtent(200, 100);
    SetOrigin(0x84, 0x50);
    SetBackground("Data\\Dialogs\\DialogBackground.STI", 0);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    m_first_item = 0;
}

// FUNCTION: WIZ8 0x005cd820
W8TriggerItemPickerDialog::~W8TriggerItemPickerDialog()
{
    NotifyDestroyed();
}

/* Configure the thirteen owned buttons. Buttons 5..8 are item rows and
   9..12 are the scroll bar. */
// FUNCTION: WIZ8 0x005cd8d0
bool W8TriggerItemPickerDialog::CreateButtons()
{
    for (auto& button : m_buttons) {
        button = std::make_unique<W8ButtonWidget>(InputLayer());
    }
    bool configured = true;

    configured =
        m_buttons[0]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest_selectionbuttons.sti", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { ToggleAllItems(); }, .tooltip = 0x15}) &&
        configured;

    configured =
        m_buttons[1]->Configure(ButtonGraphics::Load("Data\\Dialogs\\chest_confirmationbuttons.sti",
                                                     0xd, 10, 0xb, 0xc, 0xc),
                                {.on_click = [this](W8ButtonWidget*) { TransferSelectedItems(-1); },
                                 .tooltip = 0x13}) &&
        configured;

    configured =
        m_buttons[2]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\chest_confirmationbuttons.sti", 3, 0, 1, 2, 2),
            {.on_click =
                 [this](W8ButtonWidget*) { TransferSelectedItems(g_status.selected_character); },
             .tooltip = 0x14}) &&
        configured;

    configured =
        m_buttons[3]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\chest_confirmationbuttons.sti", 8, 5, 6, 7, 7),
            {.on_click = [this](W8ButtonWidget*) { m_keep_open = false; }, .tooltip = 0x16}) &&
        configured;
    configured = m_buttons[4]->Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti",
                                                              BUTTON_NO_IMAGE, 0, BUTTON_NO_IMAGE,
                                                              BUTTON_NO_IMAGE, BUTTON_NO_IMAGE),
                                         {.priority = UI_PRIORITY_LOWEST}) &&
                 configured;
    for (int row = 0; row < 4; ++row) {
        auto& button = *m_buttons[5 + row];

        configured =
            button.Configure(
                ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 1,
                                     BUTTON_NO_IMAGE, 2, BUTTON_NO_IMAGE),
                {.on_click =
                     [this, row](W8ButtonWidget* control) {
                         ToggleItem(m_first_item + row, control);
                     },
                 .on_right_click = [this, row](W8ButtonWidget*) { ShowVisibleItemInfo(row); },
                 .policy = {.primary_toggle = ButtonToggle::Press,
                            .secondary_toggle = ButtonToggle::Press},
                 .priority = UI_PRIORITY_HIGHEST - 1}) &&
            configured;
    }
    configured = m_buttons[9]->Configure(
                     ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 3, 0, 1, 2, 2),
                     {.on_click = [this](W8ButtonWidget*) { ScrollItemsUp(); }}) &&
                 configured;
    configured =
        m_buttons[10]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 0xb, 8, 9, 10, 10),
            {.on_click = [this](W8ButtonWidget*) { ScrollItemsDown(); }}) &&
        configured;
    configured = m_buttons[11]->Configure(
                     ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 7, 4, 5, 6, 6),
                     {.priority = UI_PRIORITY_LOWEST}) &&
                 configured;

    configured =
        m_buttons[12]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 3, 3, 3, 3),
            {.on_move = [this](W8ButtonWidget* button) { ScrollItemsToMouse(button); },
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        configured;
    if (!configured) {
        for (auto& button : m_buttons) {
            button.reset();
        }
    }
    return configured;
}

/* Sync the four visible item buttons with the scroll offset and the per-item
   enable flags. A short list pins the first row; a row scrolled past the end
   clears its pressed state and hides the button. */
// FUNCTION: WIZ8 0x005ce420
void W8TriggerItemPickerDialog::RefreshScrollButtons()
{
    if (static_cast<int>(items.size()) <= 4) {
        m_first_item = 0;
    }
    for (int index = 0; index < 4; ++index) {
        W8ButtonWidget& button = *m_buttons[5 + index];
        int item = m_first_item + index;
        if (item < static_cast<int>(items.size())) {
            button.SetPressed(IsItemSelected(item));
        } else if (button.IsPressed()) {
            button.SetPressed(false);
            button.SetVisible(false);
        }
    }
}

void W8TriggerItemPickerDialog::SetFirstVisible(int index)
{
    if (static_cast<int>(items.size()) <= 4) {
        m_first_item = 0;
        return;
    }
    if (index < 0 || index > static_cast<int>(items.size()) - 4) {
        return;
    }
    m_first_item = index;
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

bool W8TriggerItemPickerDialog::IsItemSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return false;
    }
    return flags[index] != 0;
}

void W8TriggerItemPickerDialog::SetItemSelected(int index, bool selected)
{
    if (static_cast<std::size_t>(index) < items.size()) {
        flags[index] = selected;
        if (index >= m_first_item && index <= m_first_item + 3) {
            m_buttons[5 + index - m_first_item]->SetPressed(selected);
            m_buttons[5 + index - m_first_item]->m_dirty = true;
        }
    }
}

void W8TriggerItemPickerDialog::ToggleItem(int index, W8ButtonWidget* button)
{
    bool selected = !IsItemSelected(index);
    if (index < 0 || index >= static_cast<int>(items.size())) {
        button->SetPressed(false);
    } else {
        SetItemSelected(index, selected);
    }
}

/* Move every selected item to the destination: -1 hands a copy to the shared
   party pool, any other value gives it to that party slot's character. A
   successful transfer unlinks the item entry and its flag and retries the same
   row; any failure raises the beep flag. Afterwards the scroll state refreshes
   and an emptied picker closes itself. */
// FUNCTION: WIZ8 0x005ce4c0
void W8TriggerItemPickerDialog::TransferSelectedItems(int destination)
{
    int index;
    bool failed = false;

    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        if (flags[index] != 0) {
            W8ItemInstance& instance = items[index]->item;
            bool added;
            if (destination == -1) {
                added = AddItemToParty(&instance, true, false);
            } else {
                added = AddItemToCharacter(&g_status.buffers.Char[destination], &instance, false,
                                           true, false);
            }
            if (added) {
                FreeWorldItemGroup(items[index]);
                items.erase(items.begin() + index);
                flags.erase(flags.begin() + index);
                --index;
                m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
            } else {
                // A full destination can accept part of a stack. Keep the
                // remainder in this owned world item instead of duplicating it.
                failed = true;
                m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
            }
        }
    }
    if (failed) {
        wiz8::sound::play("Data\\Sound\\Misc\\beep2.wav");
    }
    RefreshScrollButtons();
    if (items.empty()) {
        m_keep_open = false;
    }
}

/* The select-all button: clear every flag when all are set, otherwise set them
   all. The four visible row buttons are synced with the flags. */
// FUNCTION: WIZ8 0x005ce5f0
void W8TriggerItemPickerDialog::ToggleAllItems()
{
    int index;
    bool all_selected = true;

    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        if (!all_selected) {
            break;
        }
        if (flags[index] == 0) {
            all_selected = false;
        }
    }
    bool selected = !all_selected;
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        SetItemSelected(index, selected);
    }
}

// Item rows can disappear after a transfer while their input targets still
// exist. Ignore a stale click before indexing the remaining items.
void W8TriggerItemPickerDialog::ShowVisibleItemInfo(int row)
{
    const int index = m_first_item + row;
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return;
    }
    OpenAssayDialog(CopyWorldItemInstance(items[index]), -1);
}

/* The two scroll arrows step the first visible row through SetFirstVisible. */
// FUNCTION: WIZ8 0x005cea70
void W8TriggerItemPickerDialog::ScrollItemsUp()
{
    SetFirstVisible(m_first_item - 1);
}

// FUNCTION: WIZ8 0x005ceab0
void W8TriggerItemPickerDialog::ScrollItemsDown()
{
    SetFirstVisible(m_first_item + 1);
}

/* Thumb drag callback: while the left button is held, map the live cursor
   position between the scroll buttons' top and bottom bounds onto the item
   list, then clamp through SetFirstVisible. */
// FUNCTION: WIZ8 0x005ceaf0
void W8TriggerItemPickerDialog::ScrollItemsToMouse(W8ButtonWidget* button)
{
    int top;
    int bottom;
    int index;

    if (button != 0 && button->IsLeftButtonHeld()) {

        SDL_Point point = button->GetMousePosition();
        bottom = button->Bounds().y + 2 * m_buttons[9]->Bounds().h;
        top = button->Bounds().y + m_buttons[9]->Bounds().h + button->Bounds().h -
              2 * m_buttons[10]->Bounds().h - m_buttons[11]->Bounds().h;
        if (top <= bottom) {
            return;
        }
        if (point.y < bottom) {
            point.y = bottom;
        }
        if (point.y > top) {
            point.y = top;
        }
        index = (point.y - bottom) * static_cast<int>(items.size()) / (top - bottom);
        SetFirstVisible(index);
    }
}

/* Keyboard list navigation. The up/down keys move the first visible row, the
   paging keys jump, the digit keys toggle one of the four visible flags and
   VK_ESCAPE closes the picker. */
// FUNCTION: WIZ8 0x005cec20
bool W8TriggerItemPickerDialog::HandleInputEvent(const InputEvent* input)
{
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        if (!IsKeyHeld(VK_UP)) {
            if (IsKeyHeld(VK_DOWN)) {
                SetFirstVisible(m_first_item + 1);
            }
        } else {
            SetFirstVisible(m_first_item - 1);
        }
        switch (toupper(input->key)) {
        case VK_ESCAPE:
            m_keep_open = false;
            return false;
        case VK_PRIOR: {
            int target = m_first_item - 4;
            if (target < 0) {
                target = 0;
            }
            SetFirstVisible(target);
            return m_keep_open;
        }
        case VK_NEXT: {
            int target = m_first_item + 4;
            if (target > static_cast<int>(items.size()) - 4) {
                target = static_cast<int>(items.size()) - 4;
            }
            SetFirstVisible(target);
            return m_keep_open;
        }
        case VK_END:
            SetFirstVisible(static_cast<int>(items.size()) - 4);
            return m_keep_open;
        case VK_HOME:
            SetFirstVisible(0);
            return m_keep_open;
        case '1':
        case '2':
        case '3':
        case '4': {
            int index = m_first_item + input->key - '1';
            if (index < static_cast<int>(items.size())) {
                SetItemSelected(index, !IsItemSelected(index));
            }
            break;
        }
        default:
            break;
        }
    }
    return m_keep_open;
}

/* Move the trigger's whole item group into this picker, merging each item
   through the add path. */
// FUNCTION: WIZ8 0x005cf0c0
void W8TriggerItemPickerDialog::SetItemGroup(W8WorldItem* group)
{
    W8WorldItem* item;

    m_item_group = group;
    item = ItemInfoGroupGetNext(group);
    while (item != 0) {
        ItemInfoRemoveFromGroup(group, item);
        AddItem(item);
        item = ItemInfoGroupGetNext(group);
    }
}

/* Hand every picker item back to the trigger's group, keeping the order the
   picker displayed them in. */
// FUNCTION: WIZ8 0x005cf110
W8WorldItem* W8TriggerItemPickerDialog::ReturnItemsToGroup()
{
    if (items.empty()) {
        return m_item_group;
    }
    do {
        W8WorldItem* item = items[0];
        items.erase(items.begin());
        flags.erase(flags.begin());
        ItemInfoAddToGroup(m_item_group, item);
    } while (!items.empty());
    return m_item_group;
}

/* Merge one item into the picker. Identification comes first, then the item is
   placed next to the existing entries with the same equipment class and, among
   those, the same unidentified name, keeping each name run ordered by item id.
   Both vectors grow five at a time on this insertion path. A nil result means
   the caller's group handed in nothing. */
// FUNCTION: WIZ8 0x005ce210
int W8TriggerItemPickerDialog::AddItem(W8WorldItem* item)
{
    W8ItemInstance* instance = &item->item;

    if (instance != 0) {
        int index;

        PartyAttemptsToIdentifyItem(instance, 0);
        for (index = 0; index < static_cast<int>(items.size()); ++index) {
            W8WorldItem* other = items[index];
            if (ItemsShareEquipClass(instance, &other->item)) {
                break;
            }
        }
        if (index < static_cast<int>(items.size())) {
            W8WorldItem* other = items[index];
            while (index < static_cast<int>(items.size()) &&
                   !ItemsShareUnidentifiedName(instance, &other->item)) {
                other = items[index];
                ++index;
            }
            if (index < static_cast<int>(items.size())) {
                while (index < static_cast<int>(items.size()) &&
                       other->item.iItemNo != instance->iItemNo) {
                    other = items[index];
                    ++index;
                }
                if (index < static_cast<int>(items.size())) {
                    items.insert(items.begin() + index, item);
                    flags.insert(flags.begin() + index, 0);
                    return -1;
                }
            }
        }
        items.push_back(item);
        flags.push_back(0);
    }
    return -1;
}

/* Poll the input queue for this picker. Button presses over a party portrait
   belong to the party, and the two button-down kinds first ask the screen
   helper whether it wants them. The input layer routes dialog controls; the
   wheel scrolls the picker's first visible row in four-row steps. */
// FUNCTION: WIZ8 0x005cef00
bool W8TriggerItemPickerDialog::ProcessInput()
{
    BeginModalInput();
    const auto route = [this] { return m_keep_open ? InputRoute::Handled : InputRoute::Stop; };
    PumpUiInput({
        .before =
            [this](const InputEvent& input) {
                const bool button_down = input.type == InputEventType::LeftButtonDown ||
                                         input.type == InputEventType::RightButtonDown;
                return button_down && ProcessPendingEvent() ? InputRoute::Handled
                                                            : InputRoute::Pass;
            },
        .consumed = [&](const InputEvent&) { return route(); },
        .unhandled =
            [&](const InputEvent& input) {
                if (input.type == InputEventType::MouseWheel) {
                    SetFirstVisible(m_first_item - input.wheel);
                } else {
                    HandleInputEvent(&input);
                }
                return route();
            },
    });
    if (!m_keep_open) EndModalInput();
    return m_keep_open;
}

/* Draw the picker. Up to four item rows share the five buttons at the top of
   the slot array; the first dirty row draws the item's catalogue image, its
   display name and its weight. A short list hides the four scroll buttons,
   otherwise they are placed around the row area and the scroll bar tracks the
   first visible item. */
// FUNCTION: WIZ8 0x005cdc70
void W8TriggerItemPickerDialog::Draw(SDL_Surface& target)
{
    int count = static_cast<int>(items.size());
    int visible_rows;
    if (count < 3) {
        visible_rows = 2;
    } else if (count > 3) {
        visible_rows = 4;
    } else {
        visible_rows = count;
    }

    if (!m_initialized) {
        CreateControls();
    }
    if (gXStatus.fCombatMode) {
        m_buttons[2]->SetEnabled(false);
    }
    if ((m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) != 0) {
        for (int index = 0; index < 13; ++index) {
            m_buttons[index]->m_dirty = true;
        }
        SetExtent(m_buttons[4]->Bounds().w + 0xe,
                  m_buttons[5]->Bounds().h * visible_rows + m_buttons[4]->Bounds().h + 0xe);
        W8DialogBase::Draw(target);
    }

    if (count > 4) {
        m_buttons[12]->SetVisible(true);
        m_buttons[9]->SetVisible(true);
        m_buttons[10]->SetVisible(true);
        m_buttons[11]->SetVisible(true);
        if (m_buttons[12]->m_dirty) {
            m_buttons[9]->m_dirty = true;
            m_buttons[10]->m_dirty = true;
            m_buttons[11]->m_dirty = true;
        }

        m_buttons[12]->SetPosition(m_x + m_width - m_buttons[12]->Bounds().w - 9, m_y + 7);
        m_buttons[12]->DrawAndInvalidate(target);
        m_buttons[9]->SetPosition(m_buttons[12]->Bounds().x + 4, m_buttons[12]->Bounds().y + 3);
        m_buttons[9]->DrawAndInvalidate(target);
        m_buttons[10]->SetPosition(m_buttons[9]->Bounds().x, m_buttons[12]->Bounds().y +
                                                             m_buttons[12]->Bounds().h - 4 -
                                                             m_buttons[10]->Bounds().h);
        m_buttons[10]->DrawAndInvalidate(target);

        int progress = 0;
        if (m_first_item != 0) {
            progress = static_cast<int>(items.size());
            if (m_first_item + 4 < static_cast<int>(items.size())) {
                progress = m_first_item + 2;
            }
        }
        int travel = m_buttons[12]->Bounds().h - m_buttons[9]->Bounds().h -
                     m_buttons[10]->Bounds().h - m_buttons[11]->Bounds().h;
        m_buttons[11]->SetPosition(m_buttons[9]->Bounds().x,
                                   m_buttons[9]->Bounds().h + m_buttons[12]->Bounds().y +
                                       travel * progress / static_cast<int>(items.size()) + 1);
        m_buttons[11]->DrawAndInvalidate(target);
    } else {
        m_buttons[12]->SetVisible(false);
        m_buttons[9]->SetVisible(false);
        m_buttons[10]->SetVisible(false);
        m_buttons[11]->SetVisible(false);
    }

    m_buttons[4]->SetPosition(m_x + 7, m_y + m_height - m_buttons[4]->Bounds().h - 5);
    m_buttons[4]->DrawAndInvalidate(target);
    m_buttons[3]->SetPosition(m_buttons[4]->Bounds().x - m_buttons[3]->Bounds().w +
                                  m_buttons[4]->Bounds().w - 1,
                              m_buttons[4]->Bounds().y);
    m_buttons[3]->DrawAndInvalidate(target);
    m_buttons[2]->SetPosition(m_buttons[3]->Bounds().x - m_buttons[2]->Bounds().w,
                              m_buttons[4]->Bounds().y);
    m_buttons[2]->DrawAndInvalidate(target);
    m_buttons[1]->SetPosition(m_buttons[2]->Bounds().x - m_buttons[1]->Bounds().w,
                              m_buttons[4]->Bounds().y);
    m_buttons[1]->DrawAndInvalidate(target);
    m_buttons[0]->SetPosition(m_buttons[1]->Bounds().x - m_buttons[0]->Bounds().w,
                              m_buttons[4]->Bounds().y);
    m_buttons[0]->DrawAndInvalidate(target);
    RefreshScrollButtons();

    for (int row = 0; row < visible_rows; ++row) {
        W8ButtonWidget* button = m_buttons[5 + row].get();

        button->SetVisible(true);
        button->SetPosition(m_x + 7, m_y + button->Bounds().h * row + 7);
        if (!button->m_dirty) {
            continue;
        }
        button->DrawAndInvalidate(target);
        if (items.empty()) {
            continue;
        }
        int item_index = m_first_item + row;
        if (item_index >= static_cast<int>(items.size())) {
            continue;
        }
        W8WorldItem* world_item = items[item_index];
        W8ItemInstance* item = &world_item->item;
        DrawSprite(target, g_item_video_objects.Sprites(item->iItemNo), 0,
                   button->Bounds().x + 2, button->Bounds().y + 2);
        if (item->stack_count > 1) {
            auto name = GetItemDisplayName(item);
            DrawText(target, g_wiz_text_font,
                     {button->Bounds().x + 0x3c, button->Bounds().y + 6},
                     fmt::format("{}({:d})", name, item->stack_count));
        } else {
            auto name = GetItemDisplayName(item);
            DrawText(target, g_wiz_text_font,
                     {button->Bounds().x + 0x3c, button->Bounds().y + 6}, name);
        }
        unsigned short weight = g_item_records[item->iItemNo].weight;
        DrawText(target, g_wiz_text_font,
                 {button->Bounds().x + 0x3c, button->Bounds().y + g_wiz_text_font.height() + 6},
                 fmt::format("{:4.1f} lbs", weight * 0.1f));
    }
}

/* Create the base controls first, then the picker buttons. */
// FUNCTION: WIZ8 0x005cdc10
int W8TriggerItemPickerDialog::CreateControls()
{
    if (W8DialogBase::CreateControls() != 0) {
        return m_error;
    }
    if (!CreateButtons()) {
        m_error = 7;
        return 7;
    }
    return 0;
}

/* Release base graphics and owned picker buttons. */
// FUNCTION: WIZ8 0x005cdc40
void W8TriggerItemPickerDialog::DestroyControls()
{
    W8DialogBase::DestroyControls();
    for (auto& button : m_buttons) {
        button.reset();
    }
}

// FUNCTION: WIZ8 0x005ce6e0
