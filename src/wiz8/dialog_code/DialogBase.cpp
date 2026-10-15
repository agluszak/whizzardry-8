#include "wiz8/dialog_code/DialogBase.h"

#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/ui_input.h"

void W8DialogBase::CreateCloseButton(W8ButtonWidget& button, int left, int top)
{
    button.Configure(
        ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2),
        {.on_click = [this](W8ButtonWidget*) { m_keep_open = false; }});
    button.SetPosition(m_x + left, m_y + top);
}

void W8DialogBase::ScrollTextArea(W8DialogTextArea& area, int first_visible_line)
{
    area.SetFirstVisibleLine(first_visible_line);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc7a0
W8DialogBase::W8DialogBase()
{
    m_dirty_flags = 0;
    m_error = 0;
    m_font = g_dialog_interface_font;
    m_x = -1;
    m_y = -1;
    m_width = -1;
    m_height = -1;
    m_background_flags = 0;
    m_initialized = false;
    m_keep_open = true;
    m_destroy_callback = {};
    m_right_button_down = false;
}

// FUNCTION: WIZ8 0x005dc860
W8DialogBase::~W8DialogBase()
{
    EndModalInput();
    NotifyDestroyed();
    DestroyControls();
}

void W8DialogBase::NotifyDestroyed()
{
    if (const auto callback = std::exchange(m_destroy_callback, {})) {
        callback(this);
        m_destroy_callback = {};
    }
}

// FUNCTION: WIZ8 0x005dc890
void W8DialogBase::Draw(SDL_Surface& target)
{
    if ((m_dirty_flags & W8_DIALOG_DIRTY_REDRAW) == 0) {
        return;
    }
    if (!m_initialized) {
        CreateControls();
    }
    if (m_error == 0 && m_body) {
        m_body->setShape(SDL_Rect{m_x, m_y, m_width + 1, m_height + 1});
        DrawButtonAppearance(target, {.generic_image = m_border}, {m_x, m_y, m_width, m_height},
                             {});
        InvalidateRegion(m_x, m_y, m_x + m_width, m_y + m_height);
    }
    m_dirty_flags &= ~W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc940
void W8DialogBase::SetText(std::string_view text)
{
    m_text.assign(text);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc9c0
void W8DialogBase::SetOrigin(int x, int y)
{
    m_x = x;
    m_y = y;
    if (m_body) m_body->setShape(SDL_Rect{x, y, m_width + 1, m_height + 1});
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dc9f0
void W8DialogBase::SetExtent(int width, int height)
{
    if (m_width != width || m_height != height) {
        if (m_initialized && m_width > 0 && m_height > 0) {
            EraseRegion(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
        }
        m_width = width;
        m_height = height;
        m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    }
}

// FUNCTION: WIZ8 0x005dca70
void W8DialogBase::SetBackground(const char* path, int flags)
{
    m_background_path = path ? path : "";
    m_background_flags = flags;
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005dcaf0
int W8DialogBase::CreateControls()
{
    if (m_x < 0 || m_y < 0) {
        return m_error = 1;
    }
    if (m_width < 0 || m_height < 0) {
        return m_error = 3;
    }
    if (!m_font) {
        return m_error = 2;
    }
    if (m_background_path.empty()) {
        return m_error = 4;
    }
    if (!m_border) {
        m_border = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogBorder.STI", {},
                                           "Data\\Dialogs\\DialogBorder.STI", {}, m_background_path,
                                           static_cast<short>(m_background_flags), 0, 0);
        if (!m_border) {
            return m_error = 4;
        }
    }
    if (m_body) m_input.remove(*m_body);
    m_body =
        &m_input.add(SDL_Rect{m_x, m_y, m_width + 1, m_height + 1}, {}, UI_PRIORITY_BACKGROUND);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    m_initialized = true;
    return 0;
}

// FUNCTION: WIZ8 0x005dcc30
void W8DialogBase::DestroyControls()
{
    EndModalInput();
    if (m_body) m_input.remove(*m_body);
    m_body = nullptr;
    m_border.reset();
    m_text.clear();
    m_background_path.clear();
    m_background_flags = 0;
    if (m_initialized && m_x >= 0 && m_y >= 0 && m_width >= 0 && m_height >= 0) {
        EraseRegion(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
    }
    m_initialized = false;
}

void W8DialogBase::BeginModalInput()
{
    m_input.setModal(true);
}

void W8DialogBase::EndModalInput()
{
    m_input.setModal(false);
}

// FUNCTION: WIZ8 0x005dcce0
bool W8DialogBase::ProcessInput()
{
    BeginModalInput();
    PumpUiInput({
        .consumed =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::RightButtonDown) {
                    OnRightButtonDown();
                } else if (input.type == InputEventType::RightButtonUp) {
                    OnRightButtonUp();
                }
                return InputRoute::Handled;
            },
        .unhandled =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::MouseWheel) {
                    OnMouseWheel(input.wheel);
                } else if (input.type == InputEventType::KeyDown && input.key == SDLK_ESCAPE) {
                    m_keep_open = false;
                }
                return InputRoute::Handled;
            },
    });
    if (!m_keep_open) EndModalInput();
    return m_keep_open;
}

// FUNCTION: WIZ8 0x005d6fa0

/* Retail ICF shares this one-argument no-op with the retained widget Redraw
   body at 0x005B1BE0; this override has no separate retail address marker. */
void W8DialogBase::OnNumericInputChanged(int) {}

// FUNCTION: WIZ8 0x005ad270
void W8DialogBase::OnRightButtonDown()
{
    m_right_button_down = true;
}

// FUNCTION: WIZ8 0x005b1bf0
void W8DialogBase::OnRightButtonUp() {}

/* Retail ICF shares this no-op with the retained body at 0x005B1BE0. */
void W8DialogBase::OnMouseWheel(int) {}
