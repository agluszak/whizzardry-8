#include "wiz8/local_code/RangeControl.h"

#include <algorithm>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "wiz8/button_interaction.h"
#include "wiz8/cursor.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_layer.h"

W8RangeControl::W8RangeControl(UiLayer& input, int left, int top, int right, int bottom)
    : m_input(input)
{
    for (int part = 0; part < 3; ++part)
        m_targets[part] =
            &m_input.add(SDL_Rect{0, 0, 1, 1}, [this, part](const UiPointerEvent& event) {
                return HandlePointer(part, event);
            });
    SetBounds({left, top, right - left, bottom - top});
    SetActive(false);
}

W8RangeControl::~W8RangeControl()
{
    for (auto* target : m_targets) m_input.remove(*target);
}

void W8RangeControl::SetBounds(SDL_Rect bounds)
{
    m_bounds = bounds;
    const auto thumb = m_artwork.size(4);
    m_thumb_height = thumb.y;
    m_track = {bounds.x, bounds.y + 16, std::max(16, thumb.x), bounds.h - 32};
    m_travel = std::max(1, m_track.h - thumb.y);
    m_targets[0]->setShape(SDL_Rect{bounds.x, bounds.y, 17, 17});
    m_targets[1]->setShape(SDL_Rect{bounds.x, bounds.y + bounds.h - 16, 17, 17});
    m_targets[2]->setShape(SDL_Rect{m_track.x, m_track.y, m_track.w + 1, m_track.h + 1});
    m_dirty = true;
}

void W8RangeControl::SetRange(int first, int second)
{
    m_minimum = std::min(first, second);
    m_maximum = std::max(first, second);
    SetValue(m_value);
}

void W8RangeControl::SetValue(int value)
{
    m_value = std::clamp(value, m_minimum, m_maximum);
    m_position = m_value == m_minimum   ? 0.f
                 : m_value == m_maximum ? 1.f
                                        : (m_value - m_minimum + .5f) / (m_maximum - m_minimum + 1);
    m_dirty = true;
}

void W8RangeControl::AdjustValue(int steps)
{
    if (!m_enabled) return;
    // Notify each step: existing controllers may update the range in response.
    while (steps != 0) {
        const int direction = steps > 0 ? 1 : -1;
        SetValue(m_value - direction);
        if (on_change) on_change(this);
        steps -= direction;
    }
}

void W8RangeControl::SetEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_dragging = m_hovered = false;
        for (auto* target : m_targets) target->releaseCapture();
        for (auto& arrow : m_arrows) arrow.cancel(false);
    }
    m_dirty = true;
}

void W8RangeControl::SetActive(bool active)
{
    m_active = active;
    if (!active) {
        m_dragging = m_hovered = false;
        for (auto& arrow : m_arrows) arrow.cancel(false);
    }
    // Other controls can share this layer: only enable or disable our targets.
    for (auto* target : m_targets) target->setEnabled(active);
    m_dirty = true;
}

UiResult W8RangeControl::HandlePointer(int part, const UiPointerEvent& event)
{
    m_dirty = true;
    if ((m_enabled && part < 2) && (event.action != UiPointerAction::Up || event.captured_buttons))
        PlayButtonSound(event.action);
    if (!m_enabled) return UiResult::Handled;
    if (event.action == UiPointerAction::Wheel) {
        AdjustValue(event.wheel);
        return UiResult::Handled;
    }
    if (part < 2) {
        const ButtonPolicy policy{.activation = ButtonActivation::Press,
                                  .repeat = true,
                                  .input_rule = ButtonInputRule::PrimaryPressedState};
        auto& press = m_arrows[part];
        if (event.action == UiPointerAction::Leave) press.leave(policy);
        if (event.button != UiPointerButton::Left) return UiResult::Handled;
        bool activate = false;
        if (event.action == UiPointerAction::Down)
            activate = press.press(SDL_BUTTON_LMASK, policy).activate;
        else if (event.action == UiPointerAction::Repeat)
            activate = press.repeat(SDL_BUTTON_LMASK, policy).activate;
        else if (event.action == UiPointerAction::Up)
            press.release(SDL_BUTTON_LMASK, policy);
        if (activate) AdjustValue(part == 0 ? 1 : -1);
        return UiResult::Handled;
    }
    const int y = event.position.y - g_cursor_hotspot_y - m_track.y;
    bool changed = false;
    if (event.action == UiPointerAction::Down && event.button == UiPointerButton::Left) {
        if (!m_hovered) {
            m_position = (y - m_thumb_height / 2) / static_cast<float>(m_travel);
            changed = true;
        }
        m_hovered = m_dragging = true;
        m_drag_y = y;
    } else if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Left) {
        m_dragging = false;
        m_targets[2]->releaseCapture();
    } else if (event.action == UiPointerAction::Leave && !m_dragging)
        m_hovered = false;
    else if (event.action == UiPointerAction::Move) {
        if (m_dragging) {
            if (y <= m_thumb_height / 2)
                m_position = 0;
            else if (y >= m_travel + m_thumb_height / 2)
                m_position = 1;
            else {
                m_position += (y - m_drag_y) / static_cast<float>(m_travel);
                m_drag_y = y;
            }
            changed = true;
        } else {
            const int pixel = static_cast<int>(m_position * m_travel);
            m_hovered = pixel <= y && y <= pixel + m_thumb_height;
        }
    }
    if (changed) {
        m_position = std::clamp(m_position, 0.f, 1.f);
        const int value = std::min(
            m_maximum, m_minimum + static_cast<int>(m_position * (m_maximum - m_minimum + 1)));
        if (value != m_value) {
            m_value = value;
            if (on_change) on_change(this);
        }
    }
    return UiResult::Handled;
}

void W8RangeControl::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (!m_active || (!full_redraw && !m_dirty)) return;
    m_dirty = false;
    for (int part = 0; part < 2; ++part) {
        const int frame = part * 8 + (!m_enabled                   ? 3
                                      : m_arrows[part].pressed     ? 2
                                      : m_targets[part]->hovered() ? 1
                                                                   : 0);
        const auto size = m_artwork.size(frame);
        m_artwork.drawAndInvalidate(target, frame, m_bounds.x + 16 - size.x,
                                    m_bounds.y + (part ? m_bounds.h - 16 : 0) + (16 - size.y) / 2);
    }
    SDL_FillSurfaceRect(&target, &m_track, 0x8000);
    InvalidateRegion(m_track.x, m_track.y, m_track.x + m_track.w, m_track.y + m_track.h);
    if (m_enabled)
        m_artwork.draw(target, m_hovered ? 5 : 4, m_track.x + m_track.w - m_artwork.size(4).x,
                       m_track.y + static_cast<int>(m_position * m_travel));
}
