#include "wiz8/dialog_code/DialogScrollBar.h"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>

#include "local_code/ControlsRect.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/render/frame.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

/* Reconstructed logical owner; original translation-unit identity is unproven.
   Live query: 0x005E0C40 sits in the gap after StatInfoDialogs.cpp (upper
   0x005E0180) and before 3D Code\PList.cpp (lower 0x005E22C0). */

W8DialogScrollBar::W8DialogScrollBar(UiLayer& layer, Resources resources)
    : m_up_button(layer), m_down_button(layer), m_thumb_button(layer), m_track_button(layer)
{
    const bool configured =
        m_up_button.Configure(ButtonGraphics::Load(resources.arrows_path, 3, 0, 1, 2, 2),
                              {.on_click = [this](W8ButtonWidget*) { ScrollUp(); },
                               .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_down_button.Configure(ButtonGraphics::Load(resources.arrows_path, 11, 8, 9, 10, 10),
                                {.on_click = [this](W8ButtonWidget*) { ScrollDown(); },
                                 .priority = UI_PRIORITY_HIGHEST - 1}) &&
        m_thumb_button.Configure(ButtonGraphics::Load(resources.arrows_path, 7, 4, 5, 6, 6),
                                 {.priority = UI_PRIORITY_HIGHEST - 2}) &&
        m_track_button.Configure(
            ButtonGraphics::Load(resources.track_path, std::nullopt, resources.track_frame,
                                 std::nullopt, resources.track_frame, std::nullopt),
            {.on_click =
                 [this](W8ButtonWidget* button) { ScrollToMouse(button->GetMousePosition().y); },
             .priority = UI_PRIORITY_HIGHEST - 2});
    if (!configured) throw std::runtime_error("Cannot load dialog scrollbar artwork");
    m_up_button.Policy().activation = m_down_button.Policy().activation =
        m_track_button.Policy().activation = ButtonActivation::Press;
    m_up_button.Policy().repeat = m_down_button.Policy().repeat = m_track_button.Policy().repeat =
        true;
    m_on_scroll = std::move(resources.on_scroll);
    m_visible = false;
}

void W8DialogScrollBar::SetLayout(int x, int y, int entry_count, int first_visible_entry,
                                  int entry_height, int view_height)
{
    {
        int track_width = m_track_button.Bounds().w;
        int arrow_width = m_up_button.Bounds().w;
        m_track_button.SetPosition(static_cast<short>(x), static_cast<short>(y));
        int arrow_x = x + (track_width - arrow_width) / 2;
        m_up_button.SetPosition(static_cast<short>(arrow_x), static_cast<short>(y));
        m_down_button.SetPosition(
            static_cast<short>(arrow_x),
            static_cast<short>(y - m_down_button.Bounds().h + m_track_button.Bounds().h));
        m_track_bounds.left = x;
        m_track_bounds.top = y + m_up_button.Bounds().h;
        m_track_bounds.right = x + m_track_button.Bounds().w;
        m_track_bounds.bottom = y - m_down_button.Bounds().h + m_track_button.Bounds().h;
        m_entry_count = entry_count;
        m_first_visible_entry = first_visible_entry;
        m_entry_height = entry_height;
        m_view_height = view_height;
        UpdateThumb();
        const bool scrollable = m_entry_count > m_view_height / m_entry_height;
        m_up_button.SetEnabled(scrollable);
        m_down_button.SetEnabled(scrollable);
        m_thumb_button.SetVisible(scrollable);
        m_track_button.m_dirty |= !m_visible;
        m_visible = true;
    }
}

void W8DialogScrollBar::UpdateThumb()
{
    {
        int x = m_track_bounds.left +
                (m_track_bounds.right - m_track_bounds.left - m_thumb_button.Bounds().w) / 2;
        int offset = 0;
        if (m_entry_count > m_view_height / m_entry_height) {
            offset = ((m_track_bounds.bottom - m_track_bounds.top - m_thumb_button.Bounds().h) *
                      m_first_visible_entry) /
                     (m_entry_count - m_view_height / m_entry_height);
        }
        m_thumb_button.SetPosition(static_cast<short>(x),
                                   static_cast<short>(m_track_bounds.top + offset));
    }
}

void W8DialogScrollBar::Draw(SDL_Surface& target)
{
    if (!m_visible) return;
    m_track_button.Draw(target);
    m_up_button.Draw(target);
    m_down_button.Draw(target);
    m_thumb_button.Draw(target);
}

bool W8DialogScrollBar::TakeDirty()
{
    bool dirty = false;
    for (auto* button : {&m_track_button, &m_up_button, &m_down_button, &m_thumb_button})
        dirty |= button->TakeDirty();
    return dirty;
}

void W8DialogScrollBar::Refresh()
{
    if (!TakeDirty() || !m_visible) return;
    Draw(GetFrameBufferSurface());
    InvalidateRegion(m_track_button.Bounds());
}

void W8DialogScrollBar::ScrollBy(int delta)
{
    if (delta > 0) {
        for (int step = 0; step < delta; ++step) {
            ScrollUp();
        }
    } else if (delta < 0) {
        for (int step = 0; step < -delta; ++step) {
            ScrollDown();
        }
    }
}

void W8DialogScrollBar::ScrollUp()
{
    if (m_first_visible_entry != 0) {
        --m_first_visible_entry;
        if (m_on_scroll) {
            m_on_scroll(m_first_visible_entry);
        }
        UpdateThumb();
    }
}

void W8DialogScrollBar::ScrollDown()
{
    int visible_entries = m_view_height / m_entry_height;
    if (visible_entries < m_entry_count &&
        m_first_visible_entry < m_entry_count - visible_entries) {
        ++m_first_visible_entry;
        if (m_on_scroll) {
            m_on_scroll(m_first_visible_entry);
        }
        UpdateThumb();
    }
}

void W8DialogScrollBar::ScrollToMouse(int mouse_y)
{
    if (m_view_height / m_entry_height < m_entry_count && m_entry_count != -1 &&
        m_first_visible_entry != -1) {
        int top = m_track_bounds.top;
        int range = m_track_bounds.bottom - top - m_thumb_button.Bounds().h;
        if (range <= 0) return;
        const int last_entry = m_entry_count - m_view_height / m_entry_height;
        m_first_visible_entry = last_entry * std::clamp(mouse_y - top, 0, range) / range;
        if (m_on_scroll) {
            m_on_scroll(m_first_visible_entry);
        }
        UpdateThumb();
    }
}
