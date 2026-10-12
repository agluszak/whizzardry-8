#include "wiz8/engine_code/Video2.h"
#include <fmt/format.h>
#include <algorithm>
#include <string_view>
#include <vector>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

#include "wiz8/unicode.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/ui_input.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "Font.h"
#include "dialog_code/DialogBase.h"
#include "Video2.h"
#include "input.h"

/* Dialog Code\stMessageDialog.cpp. SetMessage and WrapMessage assert this
   unit (lines 131 and 213); the two button callbacks are proven by their
   call-site strings (lines 615 and 652). The adjacent constructor,
   destructor, draw and input bodies are the surrounding attribution gaps
   placed with them provisionally. */

/* Dialog Code. The base dialog releases its graphics in its destructor;
   the message dialog's shared button and sprite resources are RAII members. */

// FUNCTION: WIZ8 0x005d25b0
W8MessageDialogBase::W8MessageDialogBase()
    : accepted(false), is_open(true), m_line_count(0), m_show_confirm(false), allow_cancel(false)
{
}

// FUNCTION: WIZ8 0x005d2660
void W8MessageDialogBase::Draw()
{
    int y;
    unsigned int index;

    W8DialogBase::Draw();
    m_message_button.m_dirty = true;
    m_message_button.Draw(GetFrameBufferSurface());
    if (!m_show_confirm && !allow_cancel) {
        y = m_y + m_height / 2 - (g_dialog_interface_font->height() * m_line_count >> 1);
    } else {
        y = m_y + (m_height - 0x21) / 2 -
            (g_dialog_interface_font->height() * m_line_count >> 1);
    }
    if (!m_lines.empty()) {
        {
            TextOptions options;
            options.colors.foreground =
                g_dialog_interface_font->color(g_dialog_font_foreground);
            options.colors.background =
                g_dialog_interface_font->color(g_dialog_font_background);
            for (index = 0; index < m_line_count; ++index) {
                const auto& line = m_lines[index];
                short width = g_dialog_interface_font->textWidth(line);
                DrawText(GetFrameBufferSurface(), *g_dialog_interface_font,
                         {m_x + (m_width - width) / 2, y}, line, options);
                y += g_dialog_interface_font->height();
            }
        }
    }
    if (m_show_confirm) {
        m_confirm_button.m_dirty = true;
        m_confirm_button.Draw(GetFrameBufferSurface());
        if (allow_cancel) {
            m_cancel_button.m_dirty = true;
            m_cancel_button.Draw(GetFrameBufferSurface());
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
    if (int error = W8DialogBase::CreateControls(); error != 0) return error;
    if (!m_edge_image) {
        m_edge_image = LoadGenericButtonImages({}, "Data\\Dialogs\\DialogEdge.STI", {}, "Data\\Dialogs\\DialogEdge.STI", {}, m_background_path, static_cast<short>(m_background_flags), 0, 0);
        if (!m_edge_image) {
            return m_error = 3;
        }
    }
    m_message_button.ConfigureTextButton({}, *g_dialog_interface_font, g_dialog_font_foreground,
                                        g_dialog_font_background, m_x + 9, m_y + 9,
                                        m_width - 0x12, m_height - 0x12, nullptr, 0,
                                        m_edge_image, REGION_PRIORITY_HIGHEST - 1);

    if (m_confirm_button.Configure("Data\\Dialogs\\DialogConfirmation.STI", 3, 0, 1, 2, 2,
                                   MessageDialogConfirmCallback, nullptr, false, REGION_PRIORITY_HIGHEST,
                                   W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        m_cancel_button.Configure("Data\\Dialogs\\DialogConfirmation.STI", 7, 4, 5, 6, 6,
                                  MessageDialogCancelCallback, nullptr, false, REGION_PRIORITY_HIGHEST,
                                  W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr)) {
        m_confirm_button.m_owner = this;
        m_cancel_button.m_owner = this;
        const int button_width = m_confirm_button.Bounds().w;
        const int button_y = m_y + m_height - m_confirm_button.Bounds().h - 0xf;
        const int button_x = m_x + (m_width - button_width * (allow_cancel ? 3 : 1)) / 2;
        m_confirm_button.SetPosition(button_x, button_y);
        m_cancel_button.SetPosition(m_x + m_width / 2 + button_width / 2, button_y);
        m_confirm_button.SetVisible(m_show_confirm);
        m_cancel_button.SetVisible(m_show_confirm && allow_cancel);
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
    m_message_button.Reset();
    m_confirm_button.Reset();
    m_cancel_button.Reset();
    m_lines.clear();
}

// FUNCTION: WIZ8 0x005d3020
bool W8MessageDialogBase::HandleInput(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown) {
        return is_open;
    }

    if (allow_cancel) {
        int key = toupper(input->key);
        if (key == VK_ESCAPE) {
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
    BeginModalInput();
    bool keep_open = is_open;
    PumpUiInput({
        .consumed =
            [this, &keep_open](const InputEvent&) {
                keep_open = is_open;
                return InputRoute::Handled;
            },
        .unhandled =
            [this, &keep_open](const InputEvent& input) {
                keep_open = HandleInput(&input);
                if (!keep_open) EndModalInput();
                return InputRoute::Stop;
            },
    });
    if (!keep_open) EndModalInput();
    return keep_open;
}

// FUNCTION: WIZ8 0x005d32c0
void MessageDialogConfirmCallback(W8ButtonWidget* button)
{
    auto* dialog = static_cast<W8MessageDialogBase*>(button->m_owner);
    dialog->accepted = true;
    dialog->is_open = false;
    dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005d3370
void MessageDialogCancelCallback(W8ButtonWidget* button)
{
    auto* dialog = static_cast<W8MessageDialogBase*>(button->m_owner);
    dialog->accepted = false;
    dialog->is_open = false;
    dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}
