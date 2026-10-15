#include <algorithm>
#include <charconv>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "Font.h"
#include "image.h"
#include "input.h"
#include "local_code/ControlsRect.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"

W8DialogNumericInput::W8DialogNumericInput(UiLayer& layer, const W8ControlsRect* bounds, int value,
                                           Font& font, std::function<void(int)> on_change)
    : m_editor_input(layer), m_bounds(*bounds), m_font(&font), m_value(value), m_dirty(false),
      m_maximum(-1), m_on_change(std::move(on_change))
{
}

void W8DialogNumericInput::SetValue(int value)
{
    m_value = value;
    if (m_editor) {
        const auto cursor = m_editor->Cursor();
        m_editor->SetText(0, std::to_string(value));
        m_editor->SetCursor(cursor);
    }
    m_dirty = true;
}

void W8DialogNumericInput::StopEditing()
{
    if (!m_editor) return;
    m_editor.reset();
    m_dirty = true;
}

void W8DialogNumericInput::StartEditing(SDL_Point point)
{
    StopEditing();
    const auto text = std::to_string(m_value);
    m_editor = std::make_unique<TextFieldGroup>(m_editor_input, *m_font);
    m_editor->Add(m_bounds.left, m_bounds.top, m_bounds.right - m_bounds.left,
                  m_bounds.bottom - m_bounds.top, 0, text, 10, 1, false);
    m_editor->Focus(0);
    m_editor->SetText(0, text); // Clear the editor's initial select-all.
    std::size_t cursor = text.size();
    while (cursor > 0 && (m_bounds.right - point.x) - m_bounds.left >=
                             m_font->textWidth(std::string_view(text).substr(cursor - 1)))
        --cursor;
    m_editor->SetCursor(cursor);
    m_dirty = true;
}

void W8DialogNumericInput::Draw(SDL_Surface& target)
{
    const auto text = std::to_string(m_value);
    m_font->setShade(Font::kNormalShade);
    const SDL_Rect clip{m_bounds.left, m_bounds.top, m_bounds.right - m_bounds.left,
                        m_bounds.bottom - m_bounds.top};
    ScopedClipRect clipping(target, clip);
    const int top = m_bounds.top + std::max(0, (clip.h - m_font->height()) / 2);
    const int left = std::max(m_bounds.left, m_bounds.right - m_font->textWidth(text));
    DrawText(target, *m_font, {left, top}, text);
    if (m_editor) {
        const int x = m_bounds.right -
                      m_font->textWidth(std::string_view(text).substr(m_editor->Cursor())) - 1;
        const SDL_Rect caret{x, top, 1, std::min(m_bounds.bottom - top, int(m_font->height())) + 1};
        SDL_FillSurfaceRect(&target, &caret, 0xffff);
    }
}

bool W8DialogNumericInput::HandleInput(const InputEvent* input)
{
    if (!m_editor) return false;
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) &&
        input->key == SDLK_ESCAPE) {
        StopEditing();
        return true;
    }
    if ((input->type == InputEventType::KeyDown || input->type == InputEventType::KeyRepeat) &&
        (input->key == '+' || input->key == '-'))
        return false;
    const auto previous = m_editor->Text(0);
    const auto previous_cursor = m_editor->Cursor();
    if (!m_editor->HandleInput(*input)) return false;
    const auto text = m_editor->Text(0);
    unsigned int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if ((!text.empty() && (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())) ||
        value > m_maximum) {
        m_editor->SetText(0, previous);
        m_editor->SetCursor(previous_cursor);
    } else if (text != previous) {
        m_value = value;
        const auto cursor = m_editor->Cursor();
        m_editor->SetText(0, std::to_string(value));
        m_editor->SetCursor(cursor);
        if (m_on_change) m_on_change(m_value);
    }
    m_dirty = true;
    return true;
}

// Release editing before button-up dispatch; repeats belong to the buttons.
void W8DialogNumericInput::ProcessInput(const std::function<void(const InputEvent&)>& unhandled)
{
    PumpUiInput({
        .before =
            [this](const InputEvent& input) {
                if (input.type == InputEventType::LeftButtonUp) StopEditing();
                return InputRoute::Pass;
            },
        .unhandled =
            [&](const InputEvent& input) {
                if (input.type != InputEventType::LeftButtonRepeat &&
                    input.type != InputEventType::RightButtonRepeat)
                    unhandled(input);
                return InputRoute::Handled;
            },
    });
}
