#include "wiz8/dialog_code/DialogFactoryDialogs.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "Font.h"
#include "dialog_code/DialogBase.h"
#include "himage.h"
#include "image.h"
#include "input.h"
#include "layouts/game_status.h"
#include "layouts/item_instance.h"
#include "local_code/ControlsRect.h"
#include "local_code/TextBuffer.h"
#include "soundman.h"
#include "sprite.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/design_rect.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/fonts.h"
#include "wiz8/item_spawning.h"
#include "wiz8/item_video_object_vector.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

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

// FUNCTION: WIZ8 0x005cbd70
// FUNCTION: WIZ8 0x005CC650
int W8ListBoxDialog::GetVisibleLineCount()
{
    if (!m_area_button.IsConfigured()) {
        return 0;
    }
    return m_area_button.Bounds().h /
           static_cast<int>(static_cast<unsigned int>(g_wiz_dialog_font.height()));
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
            m_dirty = true;
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
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005CD2B0
bool W8ListBoxDialog::HandleInputEvent(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown && input->type != InputEventType::KeyRepeat) {
        return m_keep_open;
    }

    if (IsKeyHeld(SDLK_UP)) {
        if (m_selected_line > 0) {
            SetCurrentLine(m_selected_line - 1);
        }
    } else if (IsKeyHeld(SDLK_DOWN) && m_selected_line < static_cast<int>(m_lines.size())) {
        SetCurrentLine(m_selected_line + 1);
    }

    switch (input->key) {
    case SDLK_ESCAPE:
        m_selected_line = -1;
        m_keep_open = false;
        return false;
    case SDLK_RETURN:
        if (m_selected_line != -1) {
            m_keep_open = false;
        }
        return m_keep_open;
    case SDLK_PAGEUP:
        SetCurrentLine(m_selected_line - GetVisibleLineCount());
        return m_keep_open;
    case SDLK_PAGEDOWN:
        SetCurrentLine(m_selected_line + GetVisibleLineCount());
        return m_keep_open;
    case SDLK_HOME:
        SetCurrentLine(0);
        return m_keep_open;
    case SDLK_END:
        SetCurrentLine(static_cast<int>(m_lines.size()) - 1);
        return m_keep_open;
    default:
        return m_keep_open;
    }
}

/* Build two text buttons, the scrolling text area, the up and down arrows, a
   slider and the confirmation pair. */
// FUNCTION: WIZ8 0x005cbdb0
void W8ListBoxDialog::Configure()
{
    edge_inlay = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogEdge.STI", {},
                                         "Data\\Dialogs\\DialogEdge.STI", {}, m_background_path,
                                         static_cast<short>(m_background_flags), 0, 0);
    if (!edge_inlay) throw std::runtime_error("Cannot load list inlay");
    m_heading_bounds = {m_x + 9, m_y + 9, m_width - 18, g_wiz_dialog_font.height() * 150 / 100};
    area_inlay = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogInlay.STI", {},
                                         "Data\\Dialogs\\DialogInlay.STI", {},
                                         "Data\\Dialogs\\DialogBackground_dark.STI", 0, 3, 3);
    if (!area_inlay) {
        throw std::runtime_error("Cannot load list inlay");
    }
    m_area_button.Configure(
        ButtonAppearance{.generic_image = area_inlay ? area_inlay : DefaultGenericButtonImage(),
                         .string = std::string({}),
                         .usFont = &g_wiz_dialog_font,
                         .sForeColor = 255,
                         .sShadowColor = 0},
        {m_heading_bounds.x, m_heading_bounds.y + m_heading_bounds.h + 4, m_heading_bounds.w, 0x14},
        {.on_click = [this](W8ButtonWidget*) { OnTextAreaClick(); },
         .policy = {.activation = ButtonActivation::Press},
         .priority = UI_PRIORITY_HIGHEST - 1});

    const bool configured =
        m_up_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogUpArrow.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { OnUp(); },
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_down_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogDownArrow.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { OnDown(); },
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_slider_button.Configure(ButtonGraphics::Load("Data\\Dialogs\\DialogSlideBar.STI",
                                                       BUTTON_NO_IMAGE, 0, BUTTON_NO_IMAGE, 0,
                                                       BUTTON_NO_IMAGE),
                                  {.priority = UI_PRIORITY_HIGHEST - 2}) &&
        m_ok_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { OnOk(); }}) &&
        m_cancel_button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 7, 4, 5, 6, 6),
            {.on_click = [this](W8ButtonWidget*) { OnCancel(); }});
    if (configured) {
        m_up_button.Policy().activation = m_down_button.Policy().activation =
            ButtonActivation::Press;
        m_up_button.Policy().repeat = m_down_button.Policy().repeat = true;
        third_btn_inlay = LoadGenericButtonImages(
            {}, "Data\\Dialogs\\DialogInlay.STI", {}, "Data\\Dialogs\\DialogInlay.STI", {},
            "Data\\Dialogs\\DialogBackground_dark.STI", 0, 3, 3);
        if (third_btn_inlay) {
            m_third_text_button.Configure(
                ButtonAppearance{.generic_image = third_btn_inlay ? third_btn_inlay
                                                                  : DefaultGenericButtonImage(),
                                 .string = std::string({}),
                                 .usFont = &g_wiz_dialog_font,
                                 .sForeColor = 255,
                                 .sShadowColor = 0},
                {0, 0, 1, 1},
                {.on_click = [this](W8ButtonWidget*) { OnSliderTrackClick(); },
                 .policy = {.activation = ButtonActivation::Press, .repeat = true},
                 .priority = UI_PRIORITY_HIGHEST - 2});
            if (m_third_text_button.IsConfigured()) {

                m_footer_bounds = {m_x + 9, m_y + m_height - m_ok_button.Bounds().h * 150 / 100 - 9,
                                   m_width - 18, m_ok_button.Bounds().h * 150 / 100};
                m_cancel_button.SetPosition(
                    m_footer_bounds.x - m_cancel_button.Bounds().w + m_footer_bounds.w,
                    m_footer_bounds.h / 2 - m_cancel_button.Bounds().h / 2 + m_footer_bounds.y);
                m_ok_button.SetPosition(m_cancel_button.Bounds().x -
                                            m_cancel_button.Bounds().w / 2 - m_ok_button.Bounds().w,
                                        m_cancel_button.Bounds().y);
                m_area_button.Resize(m_heading_bounds.w, -6 - m_heading_bounds.y -
                                                             m_heading_bounds.h +
                                                             m_footer_bounds.y);
                int selected = m_selected_line;
                m_selected_line = 0;
                SetCurrentLine(selected);
                UpdateLayout();
                return;
            }
        }
    }
    throw std::runtime_error("Cannot configure list controls");
}

// FUNCTION: WIZ8 0x005cc430
/* The scrolling text area. Rows shrink by the scroll arrow when the list does
   not fit; the slider position tracks the selected line. */
// FUNCTION: WIZ8 0x005cc690
void W8ListBoxDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    DrawButtonAppearance(target,
                         {.generic_image = edge_inlay,
                          .string = m_text,
                          .usFont = &g_wiz_dialog_font,
                          .fMultiColor = true,
                          .sForeColor = 255,
                          .sShadowColor = 0},
                         m_heading_bounds, {});
    DrawButtonAppearance(target, {.generic_image = edge_inlay}, m_footer_bounds, {});
    m_area_button.Draw(target);
    if (m_scrollable) {
        const auto& area = m_area_button.Bounds();
        const SDL_Rect fill{area.x + area.w, area.y + m_up_button.Bounds().h,
                            m_up_button.Bounds().w,
                            area.h - m_up_button.Bounds().h - m_down_button.Bounds().h};
        SDL_FillSurfaceRect(&target, &fill, Get16BPPColor(m_fill_colour));
    }
    for (auto* button : {&m_third_text_button, &m_up_button, &m_down_button, &m_slider_button})
        button->Draw(target);
    if (m_ok_button.IsEnabled()) m_ok_button.Draw(target);
    if (m_cancel_button.IsEnabled()) m_cancel_button.Draw(target);
    const auto& rect = m_area_button.Bounds();
    const SDL_Rect clip{rect.x + 3, rect.y + 3, rect.w - 6, rect.h - 6};
    ScopedClipRect clipping(target, clip);
    const int visible = std::min(GetVisibleLineCount(), int(m_lines.size()) - m_first_visible_line);
    for (int row = 0; row < visible; ++row) {
        const int line = m_first_visible_line + row;
        const int y = rect.y + 2 + g_wiz_dialog_font.height() * row;
        if (line == m_selected_line) {
            const SDL_Rect selection{rect.x + 3, y, rect.w - 6, g_wiz_dialog_font.height() - 2};
            SDL_FillSurfaceRect(&target, &selection, Get16BPPColor(m_fill_colour));
        }
        DrawText(target, g_wiz_dialog_font, {rect.x + 3, y}, m_lines[line]);
    }
}

void W8ListBoxDialog::UpdateLayout()
{
    int dx = m_heading_bounds.x - m_x;
    int dy = m_heading_bounds.y + m_heading_bounds.h + 4 - m_y;
    int width = m_heading_bounds.w;
    int height = -6 - m_heading_bounds.y - m_heading_bounds.h + m_footer_bounds.y;
    const int rows = height / g_wiz_dialog_font.height();
    m_scrollable = static_cast<int>(m_lines.size()) > rows;
    if (m_scrollable) width -= 7 + m_up_button.Bounds().w;
    m_area_button.Resize(width, height);

    m_up_button.SetVisible(m_scrollable);
    m_down_button.SetVisible(m_scrollable);
    m_slider_button.SetVisible(m_scrollable);
    m_third_text_button.SetVisible(m_scrollable);
    if (m_scrollable) {
        m_up_button.SetPosition(m_x + dx + 4 + width, m_y + 4 + dy);
        m_down_button.SetPosition(m_x + dx + 4 + width,
                                  m_y + dy + height - m_down_button.Bounds().h - 4);
        m_slider_button.SetPosition(m_x + dx + 4 + width, m_area_button.Bounds().h +
                                                              ((height - m_up_button.Bounds().h -
                                                                m_down_button.Bounds().h - 7) *
                                                               m_selected_line) /
                                                                  static_cast<int>(m_lines.size()) +
                                                              m_y + 3 + dy);
        m_third_text_button.SetPosition(m_x + dx + width, m_y + dy);
        m_third_text_button.Resize(m_up_button.Bounds().w + 7, height);
    }
}

bool W8ListBoxDialog::TakeDirty()
{
    bool dirty = W8DialogBase::TakeDirty();
    for (auto* button : {&m_area_button, &m_third_text_button, &m_up_button, &m_down_button,
                         &m_slider_button, &m_ok_button, &m_cancel_button})
        dirty |= button->TakeDirty();
    return dirty;
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
    if (m_dirty) UpdateLayout();
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
static DesignRect g_split_amount_field_bounds = {0xbe, 0x35, 0xf4, 0x41};

// GLOBAL: WIZ8 0x0064fc78
static int g_split_amount_string_ids[3] = {265, 266, 267};

// FUNCTION: WIZ8 0x005d97d0

// FUNCTION: WIZ8 0x005d9890
W8SplitAmountDialog::W8SplitAmountDialog(int total, SDL_Point origin)
    : m_active_field(nullptr), m_remaining(total), m_taken(0), m_total(total),
      m_result(W8_SPLIT_RESULT_PENDING)
{
    SetOrigin(origin.x, origin.y);
    SetExtent(322, 111);
    SetBackground("Data\\Dialogs\\popup_splititem.sti", 2);
    if (!CreateButtons()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }
    CreateTextBuffers();
    CreateNumericInput();
    UpdateButtonStates();
    UpdateTextBuffers();
}

// FUNCTION: WIZ8 0x005d9930
W8SplitAmountDialog::~W8SplitAmountDialog() = default;

/* The split-item dialog. Three text buffers show the running totals, the
   numeric field edits the taken amount, and the buttons step it. */
// FUNCTION: WIZ8 0x005d9b30
bool W8SplitAmountDialog::CreateButtons()
{
    int index;

    for (auto& button : m_buttons) button = std::make_unique<W8ButtonWidget>(InputLayer());
    if (!m_buttons[0]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 0xc, 9, 10, 0xd, 0xb),
            {.on_click = [this](W8ButtonWidget*) { DecreaseAmount(1); },
             .on_right_click = [this](W8ButtonWidget*) { DecreaseAmount(5); }})) {
        return false;
    }
    if (!m_buttons[1]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", 7, 4, 5, 8, 6),
            {.on_click = [this](W8ButtonWidget*) { IncreaseAmount(1); },
             .on_right_click = [this](W8ButtonWidget*) { IncreaseAmount(5); }})) {
        return false;
    }
    m_frame.quick_image =
        ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti", BUTTON_NO_IMAGE, 3,
                             BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE);
    if (!m_frame.quick_image) return false;
    m_frame_bounds = {m_x + g_split_amount_button_offsets[2].x,
                      m_y + g_split_amount_button_offsets[2].y, int(m_frame.quick_image->MaxWidth),
                      int(m_frame.quick_image->MaxHeight)};
    if (!m_buttons[2]->Configure(ButtonGraphics::Load("Data\\Dialogs\\popup_splititem.sti",
                                                      BUTTON_NO_IMAGE, 3, BUTTON_NO_IMAGE, 3,
                                                      BUTTON_NO_IMAGE),
                                 {.on_click = [this](W8ButtonWidget*) { ActivateField(); }})) {
        return false;
    }
    if (!m_buttons[3]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2),
            {.on_click = [this](W8ButtonWidget*) { Finish(W8_SPLIT_RESULT_CONFIRMED); }})) {
        return false;
    }
    if (!m_buttons[4]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 5, 6, 9, 7),
            {.on_click = [this](W8ButtonWidget*) { Finish(W8_SPLIT_RESULT_CANCELLED); }})) {
        return false;
    }
    m_buttons[0]->Policy().repeat = true;
    m_buttons[1]->Policy().repeat = true;
    for (index = 0; index < 5; ++index) {
        m_buttons[index]->SetPosition(
            g_split_amount_button_offsets[index < 2 ? index : index + 1].x + m_x,
            g_split_amount_button_offsets[index < 2 ? index : index + 1].y + m_y);
    }
    return true;
}

// FUNCTION: WIZ8 0x005d9d10
void W8SplitAmountDialog::CreateTextBuffers()
{
    int index;
    W8ControlsRect bounds;

    for (index = 0; index < 3; ++index) {
        bounds.left = g_split_amount_text_bounds[index].left + m_x;
        bounds.top = g_split_amount_text_bounds[index].top + m_y;
        bounds.right = g_split_amount_text_bounds[index].right + m_x;
        bounds.bottom = g_split_amount_text_bounds[index].bottom + m_y;
        m_text_buffers[index] = std::make_unique<W8TextBuffer>(
            &bounds, g_localized_strings[g_split_amount_string_ids[index]],
            g_wiz_text_font_secondary, g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight, 4);
    }
}

// FUNCTION: WIZ8 0x005d9e30
void W8SplitAmountDialog::CreateNumericInput()
{
    W8ControlsRect bounds;

    m_active_field = 0;
    bounds.left = g_split_amount_field_bounds.left + m_x;
    bounds.top = g_split_amount_field_bounds.top + m_y;
    bounds.right = g_split_amount_field_bounds.right + m_x;
    bounds.bottom = g_split_amount_field_bounds.bottom + m_y;
    m_split_input = std::make_unique<W8DialogNumericInput>(
        0, &bounds, m_taken, g_wiz_text_font_secondary, this, m_buttons[2].get());
    m_split_input->m_maximum = 1000000;
}

// FUNCTION: WIZ8 0x005d9f20
void W8SplitAmountDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    DrawButtonAppearance(target, m_frame, m_frame_bounds, {});
    CatalogArtwork::Load(0x1ac, 0).draw(target, 0, m_x + 0x18, m_y + 0x1a);
    for (auto& button : m_buttons) button->Draw(target);
    for (auto& text : m_text_buffers)
        if (text) text->Paint(target);
    m_split_input->Draw(target);
}

// FUNCTION: WIZ8 0x005da000
void W8SplitAmountDialog::UpdateTextBuffers()
{
    std::string text;

    text = fmt::format("{:d}", m_remaining);
    m_text_buffers[2]->SetText(text, g_wiz_text_font_secondary);
    m_dirty = true;
    m_text_buffers[2]->SetGeometryDirty();
    if (m_remaining < 0) {
        m_text_buffers[2]->SetFontStateIndex(0);
    } else {
        m_text_buffers[2]->SetFontStateIndex(-1);
    }
    m_split_input->SetValue(m_taken);
    m_buttons[2]->m_dirty = true;
    m_split_input->m_dirty = true;
    m_split_input->m_button->m_dirty = true;
}

// FUNCTION: WIZ8 0x005da090
void W8SplitAmountDialog::UpdateButtonStates()
{
    m_buttons[0]->UpdateEnabledState(m_taken != 0);
    m_buttons[1]->UpdateEnabledState(m_remaining != 0);
    if (m_remaining < 0) {
        m_buttons[3]->SetEnabled(false);
        m_buttons[3]->m_dirty = true;
        return;
    }
    m_buttons[3]->SetEnabled(true);
    m_buttons[3]->m_dirty = true;
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
    if (m_split_input && m_split_input->m_active && m_split_input->HandleInput(input)) return true;
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        const SDL_Keycode key = input->key;
        if (key == SDLK_ESCAPE) {
            m_keep_open = false;
        } else if (key == 0x2b) {
            IncreaseAmount(1);
            return m_keep_open;
        } else if (key == SDLK_INSERT) {
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

// FUNCTION: WIZ8 0x005da5a0
void W8SplitAmountDialog::Finish(W8SplitDialogResult result)
{
    m_result = result;
    m_keep_open = false;
}

// FUNCTION: WIZ8 0x005da5c0
void W8SplitAmountDialog::ActivateField()
{
    if (!m_split_input) {
        return;
    }
    const SDL_Point mouse = UiPointerPosition();
    SDL_Point point{mouse.x - m_x, mouse.y - m_y};
    if (!ScreenPointInRect(&g_split_amount_field_bounds, &point)) {
        return;
    }
    point.x -= g_split_amount_field_bounds.left;
    point.y -= g_split_amount_field_bounds.top;
    m_split_input->SetActive(true, &point);
    m_active_field = m_split_input.get();
}

/* The trigger-owned item picker configures its buttons during construction. */
// FUNCTION: WIZ8 0x005cd710
W8TriggerItemPickerDialog::W8TriggerItemPickerDialog()
{
    SetExtent(200, 100);
    SetOrigin(0x84, 0x50);
    SetBackground("Data\\Dialogs\\DialogBackground.STI", 0);
    m_dirty = true;
    m_first_item = 0;
    if (!CreateButtons()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }
    UpdateLayout();
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
    configured =
        m_buttons[5]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 1,
                                 BUTTON_NO_IMAGE, 2, BUTTON_NO_IMAGE),
            {.on_click = [this](W8ButtonWidget* button) { ToggleItem(m_first_item + 0, button); },
             .on_right_click = [this](W8ButtonWidget*) { ShowVisibleItemInfo(0); },
             .policy = {.primary_toggle = ButtonToggle::Press,
                        .secondary_toggle = ButtonToggle::Press},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        configured;
    configured =
        m_buttons[6]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 1,
                                 BUTTON_NO_IMAGE, 2, BUTTON_NO_IMAGE),
            {.on_click = [this](W8ButtonWidget* button) { ToggleItem(m_first_item + 1, button); },
             .on_right_click = [this](W8ButtonWidget*) { ShowVisibleItemInfo(1); },
             .policy = {.primary_toggle = ButtonToggle::Press,
                        .secondary_toggle = ButtonToggle::Press},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        configured;
    configured =
        m_buttons[7]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 1,
                                 BUTTON_NO_IMAGE, 2, BUTTON_NO_IMAGE),
            {.on_click = [this](W8ButtonWidget* button) { ToggleItem(m_first_item + 2, button); },
             .on_right_click = [this](W8ButtonWidget*) { ShowVisibleItemInfo(2); },
             .policy = {.primary_toggle = ButtonToggle::Press,
                        .secondary_toggle = ButtonToggle::Press},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        configured;
    configured =
        m_buttons[8]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 1,
                                 BUTTON_NO_IMAGE, 2, BUTTON_NO_IMAGE),
            {.on_click = [this](W8ButtonWidget* button) { ToggleItem(m_first_item + 3, button); },
             .on_right_click = [this](W8ButtonWidget*) { ShowVisibleItemInfo(3); },
             .policy = {.primary_toggle = ButtonToggle::Press,
                        .secondary_toggle = ButtonToggle::Press},
             .priority = UI_PRIORITY_HIGHEST - 1}) &&
        configured;
    configured =
        m_buttons[9]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 3, 0, 1, 2, 2),
            {.on_click = [this](W8ButtonWidget*) { SetFirstVisible(m_first_item - 1); }}) &&
        configured;
    configured =
        m_buttons[10]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 0xb, 8, 9, 10, 10),
            {.on_click = [this](W8ButtonWidget*) { SetFirstVisible(m_first_item + 1); }}) &&
        configured;
    configured = m_buttons[11]->Configure(
                     ButtonGraphics::Load("Data\\Dialogs\\maininterface_scroll.STI", 7, 4, 5, 6, 6),
                     {.priority = UI_PRIORITY_LOWEST}) &&
                 configured;
    configured =
        m_buttons[12]->Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_chest2.sti", BUTTON_NO_IMAGE, 3, 3, 3, 3),
            {.on_move = [this](W8ButtonWidget*) { ScrollItemsToMouse(); },
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
    if (static_cast<int>(entries.size()) <= 4) {
        m_first_item = 0;
    }
    for (int index = 0; index < 4; ++index) {
        W8ButtonWidget& button = *m_buttons[5 + index];
        int item = m_first_item + index;
        if (item < static_cast<int>(entries.size())) {
            button.SetPressed(IsItemSelected(item));
        } else if (button.IsPressed()) {
            button.SetPressed(false);
            button.SetVisible(false);
        }
    }
}

void W8TriggerItemPickerDialog::SetFirstVisible(int index)
{
    if (static_cast<int>(entries.size()) <= 4) {
        m_first_item = 0;
        return;
    }
    if (index < 0 || index > static_cast<int>(entries.size()) - 4) {
        return;
    }
    m_first_item = index;
    m_dirty = true;
}

bool W8TriggerItemPickerDialog::IsItemSelected(int index)
{
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return false;
    }
    return entries[index].selected;
}

void W8TriggerItemPickerDialog::SetItemSelected(int index, bool selected)
{
    if (static_cast<std::size_t>(index) < entries.size()) {
        entries[index].selected = selected;
        if (index >= m_first_item && index <= m_first_item + 3) {
            m_buttons[5 + index - m_first_item]->SetPressed(selected);
            m_buttons[5 + index - m_first_item]->m_dirty = true;
        }
    }
}

void W8TriggerItemPickerDialog::ToggleItem(int index, W8ButtonWidget* button)
{
    bool selected = !IsItemSelected(index);
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        button->SetPressed(false);
    } else {
        SetItemSelected(index, selected);
    }
}

/* Move every selected item to the destination: -1 hands a copy to the shared
   party pool, any other value gives it to that party slot's character. A
   successful transfer removes the entry and retries the same
   row; any failure raises the beep flag. Afterwards the scroll state refreshes
   and an emptied picker closes itself. */
// FUNCTION: WIZ8 0x005ce4c0
void W8TriggerItemPickerDialog::TransferSelectedItems(int destination)
{
    int index;
    bool failed = false;

    for (index = 0; index < static_cast<int>(entries.size()); ++index) {
        if (entries[index].selected) {
            W8ItemInstance& instance = entries[index].item->item;
            bool added;
            if (destination == -1) {
                added = AddItemToParty(&instance, true, false);
            } else {
                added = AddItemToCharacter(&g_status.buffers.Char[destination], &instance, false,
                                           true, false);
            }
            if (added) {
                FreeWorldItemGroup(entries[index].item);
                entries.erase(entries.begin() + index);

                --index;
                m_dirty = true;
            } else {
                // A full destination can accept part of a stack. Keep the
                // remainder in this owned world item instead of duplicating it.
                failed = true;
                m_dirty = true;
            }
        }
    }
    if (failed) {
        wiz8::sound::play("Data\\Sound\\Misc\\beep2.wav");
    }
    RefreshScrollButtons();
    if (entries.empty()) {
        m_keep_open = false;
    }
}

/* The select-all button: clear every flag when all are set, otherwise set them
   all. The four visible row buttons are synced with the flags. */
// FUNCTION: WIZ8 0x005ce5f0
void W8TriggerItemPickerDialog::ToggleAllItems()
{
    const bool all_selected = std::ranges::all_of(entries, &PickerEntry::selected);
    for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
        SetItemSelected(index, !all_selected);
    }
}

/* Right-clicking a visible row opens the assay dialog on a copy of its item. */
// FUNCTION: WIZ8 0x005cea30
void W8TriggerItemPickerDialog::ShowVisibleItemInfo(int row)
{
    const int index = m_first_item + row;
    if (index < 0 || index >= static_cast<int>(entries.size())) {
        return;
    }
    OpenAssayDialog(CopyWorldItemInstance(entries[index].item), -1);
}

/* Thumb drag callback: while the left button is held, map the live cursor
   position between the scroll buttons' top and bottom bounds onto the item
   list, then clamp through SetFirstVisible. */
// FUNCTION: WIZ8 0x005ceaf0
void W8TriggerItemPickerDialog::ScrollItemsToMouse()
{
    const W8ButtonWidget& thumb = *m_buttons[12];
    if (!thumb.IsLeftButtonHeld()) {
        return;
    }
    SDL_Point point = UiPointerPosition();
    const int bottom = thumb.Bounds().y + 2 * m_buttons[9]->Bounds().h;
    const int top = thumb.Bounds().y + m_buttons[9]->Bounds().h + thumb.Bounds().h -
                    2 * m_buttons[10]->Bounds().h - m_buttons[11]->Bounds().h;
    if (top <= bottom) {
        return;
    }
    point.y = std::clamp(point.y, bottom, top);
    SetFirstVisible((point.y - bottom) * static_cast<int>(entries.size()) / (top - bottom));
}

/* Keyboard list navigation. The up/down keys move the first visible row, the
   paging keys jump, the digit keys toggle one of the four visible flags and
   SDLK_ESCAPE closes the picker. */
// FUNCTION: WIZ8 0x005cec20
bool W8TriggerItemPickerDialog::HandleInputEvent(const InputEvent* input)
{
    if (input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) {
        if (!IsKeyHeld(SDLK_UP)) {
            if (IsKeyHeld(SDLK_DOWN)) {
                SetFirstVisible(m_first_item + 1);
            }
        } else {
            SetFirstVisible(m_first_item - 1);
        }
        switch (input->key) {
        case SDLK_ESCAPE:
            m_keep_open = false;
            return false;
        case SDLK_PAGEUP: {
            int target = m_first_item - 4;
            if (target < 0) {
                target = 0;
            }
            SetFirstVisible(target);
            return m_keep_open;
        }
        case SDLK_PAGEDOWN: {
            int target = m_first_item + 4;
            if (target > static_cast<int>(entries.size()) - 4) {
                target = static_cast<int>(entries.size()) - 4;
            }
            SetFirstVisible(target);
            return m_keep_open;
        }
        case SDLK_END:
            SetFirstVisible(static_cast<int>(entries.size()) - 4);
            return m_keep_open;
        case SDLK_HOME:
            SetFirstVisible(0);
            return m_keep_open;
        case '1':
        case '2':
        case '3':
        case '4': {
            int index = m_first_item + input->key - '1';
            if (index < static_cast<int>(entries.size())) {
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
    UpdateLayout();
}

/* Hand every picker item back to the trigger's group, keeping the order the
   picker displayed them in. */
// FUNCTION: WIZ8 0x005cf110
W8WorldItem* W8TriggerItemPickerDialog::ReturnItemsToGroup()
{
    for (const auto& entry : entries) ItemInfoAddToGroup(m_item_group, entry.item);
    entries.clear();
    return m_item_group;
}

/* Merge one item into the picker. Identification comes first, then the item is
   placed next to the existing entries with the same equipment class and, among
   those, the same unidentified name, keeping each name run ordered by item id.
   The existing world-item lifetime is unchanged. */
// FUNCTION: WIZ8 0x005ce210
int W8TriggerItemPickerDialog::AddItem(W8WorldItem* item)
{
    W8ItemInstance* instance = &item->item;

    if (instance != 0) {
        int index;

        PartyAttemptsToIdentifyItem(instance, 0);
        for (index = 0; index < static_cast<int>(entries.size()); ++index) {
            W8WorldItem* other = entries[index].item;
            if (ItemsShareEquipClass(instance, &other->item)) {
                break;
            }
        }
        if (index < static_cast<int>(entries.size())) {
            W8WorldItem* other = entries[index].item;
            while (index < static_cast<int>(entries.size()) &&
                   !ItemsShareUnidentifiedName(instance, &other->item)) {
                other = entries[index].item;
                ++index;
            }
            if (index < static_cast<int>(entries.size())) {
                while (index < static_cast<int>(entries.size()) &&
                       other->item.iItemNo != instance->iItemNo) {
                    other = entries[index].item;
                    ++index;
                }
                if (index < static_cast<int>(entries.size())) {
                    entries.insert(entries.begin() + index, PickerEntry{item});

                    return -1;
                }
            }
        }
        entries.push_back({item});
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
            [](const InputEvent& input) {
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
    if (m_dirty) UpdateLayout();
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
    W8DialogBase::Draw(target);
    for (auto& button : m_buttons) button->Draw(target);
    for (int row = 0; row < std::min(4, int(entries.size()) - m_first_item); ++row) {
        W8ButtonWidget* button = m_buttons[5 + row].get();

        if (entries.empty()) {
            continue;
        }
        int item_index = m_first_item + row;
        if (item_index >= static_cast<int>(entries.size())) {
            continue;
        }
        W8WorldItem* world_item = entries[item_index].item;
        W8ItemInstance* item = &world_item->item;
        DrawSprite(target, g_item_video_objects.Sprites(item->iItemNo), 0, button->Bounds().x + 2,
                   button->Bounds().y + 2);
        if (item->stack_count > 1) {
            auto name = GetItemDisplayName(item);
            DrawText(target, g_wiz_text_font, {button->Bounds().x + 0x3c, button->Bounds().y + 6},
                     fmt::format("{}({:d})", name, item->stack_count));
        } else {
            auto name = GetItemDisplayName(item);
            DrawText(target, g_wiz_text_font, {button->Bounds().x + 0x3c, button->Bounds().y + 6},
                     name);
        }
        unsigned short weight = g_item_records[item->iItemNo].weight;
        DrawText(target, g_wiz_text_font,
                 {button->Bounds().x + 0x3c, button->Bounds().y + g_wiz_text_font.height() + 6},
                 fmt::format("{:4.1f} lbs", weight * 0.1f));
    }
}
void W8TriggerItemPickerDialog::UpdateLayout()
{
    int count = static_cast<int>(entries.size());
    int visible_rows;
    if (count < 3) {
        visible_rows = 2;
    } else if (count > 3) {
        visible_rows = 4;
    } else {
        visible_rows = count;
    }

    if (gXStatus.fCombatMode) {
        m_buttons[2]->SetEnabled(false);
    }
    if (m_dirty) {
        SetExtent(m_buttons[4]->Bounds().w + 0xe,
                  m_buttons[5]->Bounds().h * visible_rows + m_buttons[4]->Bounds().h + 0xe);
    }

    if (count > 4) {
        m_buttons[12]->SetVisible(true);
        m_buttons[9]->SetVisible(true);
        m_buttons[10]->SetVisible(true);
        m_buttons[11]->SetVisible(true);

        m_buttons[12]->SetPosition(m_x + m_width - m_buttons[12]->Bounds().w - 9, m_y + 7);

        m_buttons[9]->SetPosition(m_buttons[12]->Bounds().x + 4, m_buttons[12]->Bounds().y + 3);

        m_buttons[10]->SetPosition(m_buttons[9]->Bounds().x, m_buttons[12]->Bounds().y +
                                                                 m_buttons[12]->Bounds().h - 4 -
                                                                 m_buttons[10]->Bounds().h);

        int progress = 0;
        if (m_first_item != 0) {
            progress = static_cast<int>(entries.size());
            if (m_first_item + 4 < static_cast<int>(entries.size())) {
                progress = m_first_item + 2;
            }
        }
        int travel = m_buttons[12]->Bounds().h - m_buttons[9]->Bounds().h -
                     m_buttons[10]->Bounds().h - m_buttons[11]->Bounds().h;
        m_buttons[11]->SetPosition(m_buttons[9]->Bounds().x,
                                   m_buttons[9]->Bounds().h + m_buttons[12]->Bounds().y +
                                       travel * progress / static_cast<int>(entries.size()) + 1);

    } else {
        m_buttons[12]->SetVisible(false);
        m_buttons[9]->SetVisible(false);
        m_buttons[10]->SetVisible(false);
        m_buttons[11]->SetVisible(false);
    }

    m_buttons[4]->SetPosition(m_x + 7, m_y + m_height - m_buttons[4]->Bounds().h - 5);

    m_buttons[3]->SetPosition(m_buttons[4]->Bounds().x - m_buttons[3]->Bounds().w +
                                  m_buttons[4]->Bounds().w - 1,
                              m_buttons[4]->Bounds().y);

    m_buttons[2]->SetPosition(m_buttons[3]->Bounds().x - m_buttons[2]->Bounds().w,
                              m_buttons[4]->Bounds().y);

    m_buttons[1]->SetPosition(m_buttons[2]->Bounds().x - m_buttons[1]->Bounds().w,
                              m_buttons[4]->Bounds().y);

    m_buttons[0]->SetPosition(m_buttons[1]->Bounds().x - m_buttons[0]->Bounds().w,
                              m_buttons[4]->Bounds().y);

    RefreshScrollButtons();

    for (int row = 0; row < 4; ++row) {
        auto& button = *m_buttons[5 + row];
        button.SetVisible(row < visible_rows);
        button.SetPosition(m_x + 7, m_y + button.Bounds().h * row + 7);
    }
}

/* Create the base controls first, then the picker buttons. */
// FUNCTION: WIZ8 0x005cdc10
/* Release base graphics and owned picker buttons. */
// FUNCTION: WIZ8 0x005cdc40
std::function<void()> W8SplitAmountDialog::TakeCompletion(bool completed)
{
    W8DialogBase::TakeCompletion(completed);
    if (!completed || m_result != W8_SPLIT_RESULT_CONFIRMED || !on_complete) return {};
    return [callback = std::exchange(on_complete, {}), result = m_taken] { callback(result); };
}

std::function<void()> W8TriggerItemPickerDialog::TakeCompletion(bool completed)
{
    W8DialogBase::TakeCompletion(completed);
    if (!on_complete) return {};
    return [callback = std::exchange(on_complete, {}), result = ReturnItemsToGroup()] {
        callback(result);
    };
}

bool W8SplitAmountDialog::TakeDirty()
{
    bool dirty = W8DialogBase::TakeDirty();
    for (auto& button : m_buttons) dirty |= button->TakeDirty();
    for (auto& text : m_text_buffers)
        if (text) dirty |= std::exchange(text->m_geometryDirty, false);
    dirty |= std::exchange(m_split_input->m_dirty, false);
    return dirty;
}

bool W8TriggerItemPickerDialog::TakeDirty()
{
    bool dirty = W8DialogBase::TakeDirty();
    for (auto& button : m_buttons) dirty |= button->TakeDirty();
    return dirty;
}
