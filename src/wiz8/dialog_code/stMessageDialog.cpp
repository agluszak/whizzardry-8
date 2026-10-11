#include <fmt/format.h>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <string_view>
#include <vector>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include "wiz8/unicode.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/ButtonUserData.h"
#include "wiz8/sr_api.h"
#include "english.h"
#include "mousesystem.h"
#include "Button System.h"
#include "Font.h"
#include "Video2.h"
#include "dialog_code/DialogBase.h"
#include "input.h"

/* Dialog Code\stMessageDialog.cpp. SetMessage and WrapMessage assert this
   unit (lines 131 and 213); the two button callbacks are proven by their
   call-site strings (lines 615 and 652). The adjacent constructor,
   destructor, draw and input bodies are the surrounding attribution gaps
   placed with them provisionally. */

/* Dialog Code. The shared dialog base at vtable 0x005EF8B0. Its lifetime
   bodies are what every derived dialog runs before and after its own. */

// FUNCTION: WIZ8 0x005d25b0
W8MessageDialogBase::W8MessageDialogBase()
    : accepted(false), is_open(true), m_edge_image(), m_message_button(), m_confirm_button(),
      m_confirm_image(), m_cancel_button(), m_cancel_image(), m_line_count(0)
{
}

/* A virtual called from a destructor has a fixed dynamic type, so the
   compiler dispatches it directly; that direct call is slot 2. */

// FUNCTION: WIZ8 0x005d2610
W8MessageDialogBase::~W8MessageDialogBase()
{
    DestroyControls();
}

// FUNCTION: WIZ8 0x005d2660
void W8MessageDialogBase::Draw()
{
    int y;
    unsigned int index;

    W8DialogBase::Draw();
    DrawButton(m_message_button);
    if (!m_show_confirm && !allow_cancel) {
        y = m_y + m_height / 2 - (g_dialog_interface_font->height() * m_line_count >> 1);
    } else {
        y = m_y + (m_height - 0x21) / 2 -
            (g_dialog_interface_font->height() * m_line_count >> 1);
    }
    if (!m_lines.empty()) {
        {
            FontRenderScope font_scope;
            SetFont(*g_dialog_interface_font);
            SetFontForeground(g_dialog_font_foreground);
            SetFontBackground(g_dialog_font_background);
            for (index = 0; index < m_line_count; ++index) {
                const auto& line = m_lines[index];
                short width = g_dialog_interface_font->textWidth(line);
                DrawFontText(m_x + (m_width - width) / 2, y, line);
                y += g_dialog_interface_font->height();
            }
        }
    }
    if (m_show_confirm) {
        if (m_confirm_button) {
            DrawButton(m_confirm_button);
        }
        if (allow_cancel && m_cancel_button) {
            DrawButton(m_cancel_button);
        }
    }
}

// FUNCTION: WIZ8 0x005d2800
void W8MessageDialogBase::SetMessage(std::string_view message, int line_count,
                                     unsigned short characters_per_line, bool confirmation,
                                     bool cancel, bool size_to_message, bool wrap_message,
                                     int maximum_width, int maximum_height)
{
    /* 0x005D2950 compares the line counter unsigned; the signed compare in
       this function is the width clamp at 0x005D295E, and `width` carries it. */
    unsigned int index;

    m_lines.clear();
    if (line_count == 1 && wrap_message) {
        line_count = WrapMessage(message);
    } else {
        for (index = 0; index < static_cast<unsigned int>(line_count); ++index) {
            auto bytes = wiz8::text::codepoint_prefix(message, characters_per_line);
            m_lines.emplace_back(message.substr(0, bytes));
            message.remove_prefix(bytes);
        }
    }
    m_line_count = line_count;
    m_show_confirm = confirmation;
    allow_cancel = cancel;
    if (size_to_message) {
        /* 0x005D295E clamps the width with a signed compare. */
        int width = 0;
        int height;

        for (index = 0; index < m_line_count; ++index) {
            short line_width = g_dialog_interface_font->textWidth(m_lines[index]);
            if (width < line_width) {
                width = line_width;
            }
        }
        width = width * 5 / 4;
        if (width < 0xaa) {
            width = 0xaa;
        }
        g_dialog_interface_font->height();
        height = g_dialog_interface_font->height() * m_line_count + 0x1e;
        if (m_show_confirm || allow_cancel) {
            height = g_dialog_interface_font->height() * m_line_count + 0x3f;
        }
        if (height < g_dialog_interface_font->height() * 7) {
            height = g_dialog_interface_font->height() * 7;
        }
        if (maximum_width && maximum_width < width) {
            width = maximum_width;
        }
        if (maximum_height && maximum_height < height) {
            height = maximum_height;
        }
        SetClientExtent(width, height);
    }
}

// FUNCTION: WIZ8 0x005d2a50
unsigned int W8MessageDialogBase::WrapMessage(std::string_view message)
{
    const auto maximum_width = std::max(m_width + 15, 1);
    std::string line;
    for (std::size_t start = 0; start < message.size();) {
        if (message[start] == '\n') {
            m_lines.push_back(std::move(line));
            line.clear();
            ++start;
            continue;
        }
        const auto end = message.find_first_of(" \n", start);
        const auto stop = end == std::string_view::npos ? message.size() : end;
        const auto word = message.substr(start, stop - start);
        auto candidate = line.empty() ? std::string(word) : fmt::format("{} {}", line, word);
        if (!line.empty() && g_dialog_interface_font->textWidth(candidate) > maximum_width) {
            m_lines.push_back(std::move(line));
            line.assign(word);
        } else line = std::move(candidate);
        start = stop;
        if (start < message.size() && message[start] == ' ') ++start;
    }
    m_lines.push_back(std::move(line));
    return m_lines.size();
}

// FUNCTION: WIZ8 0x005d2cb0
void W8MessageDialogBase::SetClientExtent(int width, int height)
{
    int old_width = m_width;
    int old_height = m_height;

    SetExtent(width, height);
    SetOrigin(m_x + (old_width - width) / 2, m_y + (old_height - height) / 2);
}

// FUNCTION: WIZ8 0x005d2d00
int W8MessageDialogBase::CreateControls()
{
    W8DialogBase::CreateControls();
    if (!m_edge_image) {
        m_edge_image = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogEdge.STI", {}, "Data\\Dialogs\\DialogEdge.STI", {}, m_background_path, static_cast<short>(m_background_flags), 0, 0);
        if (!m_edge_image) {
            return m_error = 3;
        }
    }
    m_message_button = CreateTextButton({}, *g_dialog_interface_font, g_dialog_font_foreground, g_dialog_font_background, m_edge_image, static_cast<short>(m_x + 9), static_cast<short>(m_y + 9), static_cast<short>(m_width - 0x12), static_cast<short>(m_height - 0x12), BUTTON_NO_TOGGLE | BUTTON_IGNORE_CLICKS, MSYS_PRIORITY_HIGHEST - 1, BUTTON_NO_CALLBACK, BUTTON_NO_CALLBACK);

    m_confirm_image =
        ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 3, 0, 1, 2, 2);
    if (m_confirm_image) {
        m_confirm_button =
            QuickCreateButton(m_confirm_image, 0, 0, BUTTON_NO_TOGGLE, MSYS_PRIORITY_HIGHEST,
                              MessageDialogConfirmCallback, MessageDialogConfirmCallback);
    }
    m_cancel_image =
        ButtonGraphics::Load("Data\\Dialogs\\DialogConfirmation.STI", 7, 4, 5, 6, 6);
    if (m_cancel_image) {
        m_cancel_button =
            QuickCreateButton(m_cancel_image, 0, 0, BUTTON_NO_TOGGLE, MSYS_PRIORITY_HIGHEST,
                              MessageDialogCancelCallback, MessageDialogCancelCallback);
    }
    if (m_confirm_button && m_cancel_button) {
        int button_width;
        int button_y;
        int button_x;

        SetButtonUserDataPointer(m_confirm_button, this);
        SetButtonUserDataPointer(m_cancel_button, this);
        button_width = GetButtonWidth(m_confirm_button);
        button_y = m_y + m_height - GetButtonHeight(m_confirm_button) - 0xf;
        if (allow_cancel) {
            button_x = m_x + (m_width - button_width * 3) / 2;
        } else {
            button_x = m_x + (m_width - button_width) / 2;
        }
        SetButtonPosition(m_confirm_button, button_x, button_y);
        SetButtonPosition(m_cancel_button, m_x + m_width / 2 + GetButtonWidth(m_confirm_button) / 2,
                          GetButtonY(m_confirm_button));
        return 0;
    }
    DestroyControls();
    return m_error = 6;
}

// FUNCTION: WIZ8 0x005d2f40
void W8MessageDialogBase::DestroyControls()
{
    unsigned int index;

    W8DialogBase::DestroyControls();
    m_edge_image.reset();
    m_message_button.reset();
    m_confirm_button.reset();
    m_cancel_button.reset();
    m_confirm_image.reset();
    m_cancel_image.reset();
    m_lines.clear();
}

// FUNCTION: WIZ8 0x005d3020
bool W8MessageDialogBase::HandleInput(const InputAtom* input)
{
    if (input->usEvent != KEY_DOWN) {
        return is_open;
    }

    if (allow_cancel) {
        int key = toupper(input->usParam);
        if (key == ESC) {
            accepted = false;
            is_open = false;
            return false;
        }
        if (key != '\r') {
            return is_open;
        }
    }

    accepted = true;
    is_open = false;
    return false;
}

// FUNCTION: WIZ8 0x005d3080
bool W8MessageDialogBase::ProcessInput()
{
    SDL_Point mouse;
    InputAtom input;

    SGPMouseGetPos(&mouse);
    DispatchMouseEvent(MOUSE_POS, mouse.x, mouse.y, gfLeftButtonState, gfRightButtonState);

    while (DequeueEvent(&input)) {
        if (!DispatchDialogMouseInput(input.usEvent, mouse.x, mouse.y)) {
            return HandleInput(&input);
        }
    }

    return is_open;
}

// FUNCTION: WIZ8 0x005d32c0
void MessageDialogConfirmCallback(GUI_BUTTON* button, int reason)
{
    W8MessageDialogBase* dialog = GetButtonUserDataPointer<W8MessageDialogBase>(button);
    if (!dialog) {
        srAssertFail("pDialog", "C:\\Projects\\Wizardry 8\\Dialog Code\\stMessageDialog.cpp", 0x267,
                     0);
    }
    if (!(reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) &&
        (reason & MSYS_CALLBACK_REASON_LBUTTON_UP)) {
        if (button->uiFlags & BUTTON_CLICKED_ON) {
            dialog->accepted = true;
            dialog->is_open = false;
            button->uiFlags &= ~BUTTON_CLICKED_ON;
            dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
        }
    } else if (UpdateDialogArrowState(button, reason)) {
        dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    }
}

// FUNCTION: WIZ8 0x005d3370
void MessageDialogCancelCallback(GUI_BUTTON* button, int reason)
{
    W8MessageDialogBase* dialog = GetButtonUserDataPointer<W8MessageDialogBase>(button);
    if (!dialog) {
        srAssertFail("pDialog", "C:\\Projects\\Wizardry 8\\Dialog Code\\stMessageDialog.cpp", 0x28c,
                     0);
    }
    if (!(reason & MSYS_CALLBACK_REASON_LBUTTON_DWN) &&
        (reason & MSYS_CALLBACK_REASON_LBUTTON_UP)) {
        if (button->uiFlags & BUTTON_CLICKED_ON) {
            dialog->accepted = false;
            dialog->is_open = false;
            button->uiFlags &= ~BUTTON_CLICKED_ON;
            dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
        }
    } else if (UpdateDialogArrowState(button, reason)) {
        dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
    }
}
