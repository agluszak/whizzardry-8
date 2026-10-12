#include <cstdint>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <SDL3/SDL_rect.h>
#include <charconv>
#include <string>
#include <algorithm>
#include <cstddef>
#include <string_view>

#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/engine_code/Video2.h"
#include "Font.h"
#include "image.h"
#include "input.h"
#include "dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "local_code/ControlsRect.h"


/* Dialog Code numeric entry field, reconstructed logical owner: the original
   translation-unit identity is unproven. The functions at 0x005E1460-0x005E1B22
   sit in the gap after DialogScrollBar.cpp (TrackButtonCallback ends at
   0x005E13F1) and before the VC6 runtime block at 0x005E1C10, and this scratch
   buffer is referenced only by them. */

// GLOBAL: WIZ8 0x0069ca4c
static std::string g_numeric_input_text;

// FUNCTION: WIZ8 0x005e1460
W8DialogNumericInput::W8DialogNumericInput(std::int32_t control_id, const W8ControlsRect* bounds, std::int32_t value,
                                           Font& font, W8DialogBase* dialog, W8ButtonWidget* button)
{
    m_font = &font;
    m_value = value;
    m_dialog = dialog;
    m_caret = -1;
    m_maximum = -1;
    m_control_id = control_id;
    m_button = button;
    m_dirty = false;
    m_bounds = *bounds;
}

// FUNCTION: WIZ8 0x005e14c0
void W8DialogNumericInput::SetValue(std::int32_t value)
{
    m_value = value;
    m_dirty = true;
}

// FUNCTION: WIZ8 0x005e14d0
void W8DialogNumericInput::SetActive(bool active)
{
    m_active = active;
    if (!active) {
        m_caret = -1;
        m_dialog->m_field_4c = 0;
        m_dirty = true;
        m_button->m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005e1500
void W8DialogNumericInput::SetActive(bool active, const SDL_Point* point)
{
    SetActive(active);
    if (!active) {
        return;
    }
    g_numeric_input_text = fmt::format("{:d}", m_value);
    size_t length = g_numeric_input_text.size();
    std::uint32_t count = 0;
    m_caret = -1;
    std::size_t suffix = length - 1;
    for (; count < length; ++count, --suffix) {
        std::int16_t suffix_width = m_font->textWidth(std::string_view(g_numeric_input_text).substr(suffix));
        if ((m_bounds.right - point->x) - m_bounds.left < suffix_width) {
            m_caret = count;
            break;
        }
    }
    if (m_caret == -1) {
        m_caret = length;
    }
    m_dirty = true;
    m_button->m_dirty = true;
}

// FUNCTION: WIZ8 0x005e15c0
void W8DialogNumericInput::Draw(bool force)
{
    if (!force && !m_dirty) {
        return;
    }
    g_numeric_input_text = fmt::format("{:d}", m_value);
    size_t length = g_numeric_input_text.size();
    g_numeric_input_text = fmt::format("{:d}", m_value);
    m_font->setShade(Font::kNormalShade);
    SDL_Rect text_clip{m_bounds.left, m_bounds.top, m_bounds.right - m_bounds.left,
                       m_bounds.bottom - m_bounds.top};
    ScopedClipRect clipping(GetFrameBufferSurface(), text_clip);
    std::uint16_t font_height = m_font->height();
    std::int32_t text_y = m_bounds.top + std::max(0, (m_bounds.bottom - m_bounds.top - font_height) / 2);
    std::int32_t text_x = m_bounds.right - m_font->textWidth(g_numeric_input_text);
    if (text_x <= m_bounds.left) {
        text_x = m_bounds.left;
    }
    DrawTextAndInvalidate(GetFrameBufferSurface(), *m_font, {text_x, text_y},
                          g_numeric_input_text);
    if (m_active && m_caret != -1) {
        std::int32_t caret_x =
            m_bounds.right - m_font->textWidth(std::string_view(g_numeric_input_text).substr(length - m_caret)) - 1;
        font_height = m_font->height();
        std::int32_t caret_top = m_bounds.top + std::max(0, (m_bounds.bottom - m_bounds.top - font_height) / 2);
        std::int32_t caret_bottom = m_bounds.bottom;
        if (caret_top + m_font->height() < caret_bottom) {
            caret_bottom = caret_top + m_font->height();
        }
        const SDL_Rect caret{caret_x, caret_top, 1, caret_bottom - caret_top + 1};
        SDL_FillSurfaceRect(&GetFrameBufferSurface(), &caret, 0xffff);
    }
    m_dirty = false;
}

void W8DialogNumericInput::NotifyValueChanged()
{
    m_dirty = true;
    m_button->m_dirty = true;
    m_dialog->OnNumericInputChanged(m_control_id);
}

// FUNCTION: WIZ8 0x005e17a0
void W8DialogNumericInput::TypeDigit(char digit)
{
    if (m_active) {
        g_numeric_input_text = fmt::format("{:d}", m_value);
        size_t length = g_numeric_input_text.size();
        if (length < 10) {
            std::uint32_t value;
            g_numeric_input_text.insert(length - m_caret, 1, digit);
            std::from_chars(g_numeric_input_text.data(), g_numeric_input_text.data() + g_numeric_input_text.size(), value);
            if (value <= m_maximum) {
                m_value = value;
                NotifyValueChanged();
                --m_caret;
                if (m_caret < 0) {
                    m_caret = 0;
                }
            }
        }
    }
}

static void RemoveNumericInputCharacter(std::uint32_t position, std::uint32_t length)
{
    if (position < length) g_numeric_input_text.erase(position, 1);
}

// FUNCTION: WIZ8 0x005e1840
void W8DialogNumericInput::DeleteForward()
{
    if (m_active && m_caret != 0) {
        g_numeric_input_text = fmt::format("{:d}", m_value);
        size_t length = g_numeric_input_text.size();
        std::uint32_t position = length - m_caret;
        RemoveNumericInputCharacter(position, length);
        --m_caret;
        if (length == 1) {
            m_value = 0;
        } else {
            std::from_chars(g_numeric_input_text.data(), g_numeric_input_text.data() + g_numeric_input_text.size(), m_value);
        }
        NotifyValueChanged();
    }
}

// FUNCTION: WIZ8 0x005e18f0
void W8DialogNumericInput::Backspace()
{
    if (m_active) {
        g_numeric_input_text = fmt::format("{:d}", m_value);
        size_t length = g_numeric_input_text.size();
        if (length != 0 && m_caret != static_cast<std::int32_t>(length)) {
            std::uint32_t position = (length - m_caret) - 1;
            RemoveNumericInputCharacter(position, length);
            if (m_caret == static_cast<std::int32_t>(length)) {
                --m_caret;
            }
            if (length == 1) {
                m_value = 0;
            } else {
                std::from_chars(g_numeric_input_text.data(), g_numeric_input_text.data() + g_numeric_input_text.size(), m_value);
            }
            NotifyValueChanged();
        }
    }
}

// FUNCTION: WIZ8 0x005e19a0
bool W8DialogNumericInput::HandleInput(const InputEvent* input)
{
    if (input->type != InputEventType::KeyDown && input->type != InputEventType::KeyRepeat) {
        return false;
    }
    switch (input->key) {
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
        if (m_active && m_caret != -1) {
            TypeDigit(static_cast<char>(input->key));
            return true;
        }
        break;
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:
        if (m_active && m_caret != -1) {
            TypeDigit(static_cast<char>(input->key - 0x30));
            return true;
        }
        break;
    case 0x2e:
        if (m_active && m_caret != -1) {
            DeleteForward();
            return true;
        }
        break;
    case 8:
        if (m_active && m_caret != -1) {
            Backspace();
            return true;
        }
        break;
    case 0x27:
        if (m_active && m_caret != -1) {
            std::int32_t next = m_caret - 1;
            m_dirty = true;
            m_caret = next < 0 ? 0 : next;
            m_button->m_dirty = true;
            return true;
        }
        break;
    case 0x25:
        if (m_active && m_caret != -1) {
            g_numeric_input_text = fmt::format("{:d}", m_value);
            size_t length = g_numeric_input_text.size();
            std::uint32_t next = m_caret + 1;
            if (next <= length) {
                length = next;
            }
            m_caret = length;
            m_dirty = true;
            m_button->m_dirty = true;
            return true;
        }
        break;
    case 0x1b:
        SetActive(false);
        break;
    default:
        return false;
    }
    return true;
}
