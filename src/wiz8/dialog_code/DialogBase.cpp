#include "wiz8/dialog_code/DialogBase.h"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/modal_content.h"
#include "wiz8/ui_input.h"

void W8DialogBase::CreateCloseButton(W8ButtonWidget& button, int left, int top)
{
    if (!button.Configure(
            ButtonGraphics::Load("Data\\Dialogs\\popup_confirmationbuttons.sti", 3, 0, 1, 4, 2),
            {.on_click = [this](W8ButtonWidget*) { m_keep_open = false; }}))
        throw std::runtime_error("Cannot load dialog close button");
    button.SetPosition(m_x + left, m_y + top);
}

void W8DialogBase::ScrollTextArea(W8DialogTextArea& area, int first_visible_line)
{
    area.SetFirstVisibleLine(first_visible_line);
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005dc7a0
W8DialogBase::W8DialogBase()
{
    m_dirty = false;
    m_font = &g_wiz_dialog_font;
    m_x = 240;
    m_y = 190;
    m_width = 160;
    m_height = 100;
    m_background_flags = 0;
    m_keep_open = true;
    m_body =
        &m_input.add(SDL_Rect{m_x, m_y, m_width + 1, m_height + 1}, {}, UI_PRIORITY_BACKGROUND);
}

// FUNCTION: WIZ8 0x005dc860
W8DialogBase::~W8DialogBase()
{
    EndModalInput();
    if (cleanup) cleanup();
}

std::function<void()> W8DialogBase::TakeCompletion(bool)
{
    EndModalInput();
    m_input.setEnabled(false);
    if (m_border) EraseRegion(m_x, m_y, m_x + m_width + 1, m_y + m_height + 1);
    return std::exchange(on_close, {});
}

// FUNCTION: WIZ8 0x005dc890
void ModalContent::RefreshModal()
{
    DrawModal(GetFrameBufferSurface());
}

bool W8DialogBase::TakeDirty()
{
    return std::exchange(m_dirty, false);
}

void W8DialogBase::RefreshModal()
{
    if (!TakeDirty()) return;
    Draw(GetFrameBufferSurface());
    InvalidateRegion(m_x, m_y, m_x + m_width, m_y + m_height);
}

void W8DialogBase::Draw(SDL_Surface& target)
{
    DrawButtonAppearance(target, {.generic_image = m_border}, {m_x, m_y, m_width, m_height}, {});
}

// FUNCTION: WIZ8 0x005dc940
void W8DialogBase::SetText(std::string_view text)
{
    m_text.assign(text);
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005dc9c0
void W8DialogBase::SetOrigin(int x, int y)
{
    m_x = x;
    m_y = y;
    if (m_body) m_body->setShape(SDL_Rect{x, y, m_width + 1, m_height + 1});
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005dc9f0
void W8DialogBase::SetExtent(int width, int height)
{
    m_width = width;
    m_height = height;
    m_body->setShape(SDL_Rect{m_x, m_y, width + 1, height + 1});
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005dca70
void W8DialogBase::SetBackground(const char* path, int flags)
{
    m_background_path = path ? path : "";
    m_background_flags = flags;
    m_dirty = true;
    m_border = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogBorder.STI", {},
                                       "Data\\Dialogs\\DialogBorder.STI", {}, m_background_path,
                                       static_cast<short>(flags), 0, 0);
    if (!m_font->glyphs || !m_border) throw std::runtime_error("Cannot configure dialog backdrop");
}

// FUNCTION: WIZ8 0x005dcc30
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
                    m_right_button_down = true;
                } else if (input.type == InputEventType::RightButtonUp && m_info_scroll_bar &&
                           m_right_button_down) {
                    m_keep_open = false;
                }
                return InputRoute::Handled;
            },
        .unhandled =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::MouseWheel) {
                    if (m_info_scroll_bar) m_info_scroll_bar->ScrollBy(input.wheel);
                } else if (input.type == InputEventType::KeyDown && input.key == SDLK_ESCAPE) {
                    m_keep_open = false;
                }
                return InputRoute::Handled;
            },
    });
    if (!m_keep_open) EndModalInput();
    return m_keep_open;
}
