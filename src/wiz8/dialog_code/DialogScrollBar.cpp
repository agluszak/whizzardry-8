#include <SDL3/SDL_surface.h>
#include "wiz8/engine_code/Video2.h"
#include <SDL3/SDL_rect.h>
#include <algorithm>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/button_interaction.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/ui_input.h"
#include "Video2.h"
#include "local_code/ControlsRect.h"

/* Reconstructed logical owner; original translation-unit identity is unproven.
   Live query: 0x005E0C40 sits in the gap after StatInfoDialogs.cpp (upper
   0x005E0180) and before 3D Code\PList.cpp (lower 0x005E22C0). It is not
   stListBox.cpp. */

// FUNCTION: WIZ8 0x005e0ca0
bool W8DialogScrollBar::CreateControls(const Resources* resources)
{
    if (m_initialized) return true;
    const bool configured =
        // FUNCTION: WIZ8 0x005e1280
        m_up_button.Configure(resources->arrows_path, 3, 0, 1, 2, 2,
                              [this](W8ButtonWidget*) { ScrollUp(); }, nullptr, false,
                              UI_PRIORITY_HIGHEST - 1, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        // FUNCTION: WIZ8 0x005e1320
        m_down_button.Configure(resources->arrows_path, 11, 8, 9, 10, 10,
                                [this](W8ButtonWidget*) { ScrollDown(); }, nullptr, false,
                                UI_PRIORITY_HIGHEST - 1, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        m_thumb_button.Configure(resources->arrows_path, 7, 4, 5, 6, 6, nullptr, nullptr, false,
                                 UI_PRIORITY_HIGHEST - 2, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        // FUNCTION: WIZ8 0x005e13d0
        m_track_button.Configure(resources->track_path, BUTTON_NO_IMAGE, resources->track_frame,
                                 BUTTON_NO_IMAGE, resources->track_frame, BUTTON_NO_IMAGE,
                                 [this](W8ButtonWidget* button) { ScrollToMouse(button->GetMousePosition().y); },
                                 nullptr, false, UI_PRIORITY_HIGHEST - 2,
                                 W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr);
    if (!configured) {
        m_up_button.Reset();
        m_down_button.Reset();
        m_thumb_button.Reset();
        m_track_button.Reset();
        return false;
    }
    m_up_button.policy.activation = m_down_button.policy.activation = m_track_button.policy.activation =
        ButtonActivation::Press;
    m_up_button.policy.repeat = m_down_button.policy.repeat = m_track_button.policy.repeat = true;
    m_on_scroll = resources->on_scroll;
    m_initialized = true;
    m_visible = false;
    m_dirty = true;
    return true;
}

// FUNCTION: WIZ8 0x005e0eb0
void W8DialogScrollBar::SetLayout(int x, int y, int entry_count, int first_visible_entry,
                                  int entry_height, int view_height)
{
    if (m_initialized) {
        int track_width = m_track_button.Bounds().w;
        int arrow_width = m_up_button.Bounds().w;
        m_track_button.SetPosition( static_cast<short>(x), static_cast<short>(y));
        int arrow_x = x + (track_width - arrow_width) / 2;
        m_up_button.SetPosition( static_cast<short>(arrow_x), static_cast<short>(y));
        m_down_button.SetPosition( static_cast<short>(arrow_x),
                          static_cast<short>(y - m_down_button.Bounds().h +
                                             m_track_button.Bounds().h));
        m_track_bounds.left = x;
        m_track_bounds.top = y + m_up_button.Bounds().h;
        m_track_bounds.right = x + m_track_button.Bounds().w;
        m_track_bounds.bottom =
            y - m_down_button.Bounds().h + m_track_button.Bounds().h;
        m_entry_count = entry_count;
        m_first_visible_entry = first_visible_entry;
        m_entry_height = entry_height;
        m_view_height = view_height;
        UpdateThumb();
        const bool scrollable = m_entry_count > m_view_height / m_entry_height;
        m_up_button.SetEnabled(scrollable);
        m_down_button.SetEnabled(scrollable);
        m_thumb_button.SetVisible(scrollable);
        m_visible = true;
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005e1000
void W8DialogScrollBar::UpdateThumb()
{
    if (m_initialized && m_entry_count != -1 && m_first_visible_entry != -1) {
        int x = m_track_bounds.left +
                (m_track_bounds.right - m_track_bounds.left - m_thumb_button.Bounds().w) / 2;
        int offset = 0;
        if (m_entry_count > m_view_height / m_entry_height) {
            offset =
                ((m_track_bounds.bottom - m_track_bounds.top - m_thumb_button.Bounds().h) *
                 m_first_visible_entry) /
                (m_entry_count - m_view_height / m_entry_height);
        }
        m_thumb_button.SetPosition( static_cast<short>(x),
                          static_cast<short>(m_track_bounds.top + offset));
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005e10b0
void W8DialogScrollBar::Draw(SDL_Surface& target, bool force)
{
    if (m_initialized && m_visible && (force || m_dirty || m_up_button.m_dirty || m_down_button.m_dirty ||
        m_track_button.m_dirty || m_thumb_button.m_dirty)) {
        m_track_button.m_dirty = true;
        m_track_button.Draw(target);
        m_up_button.m_dirty = true;
        m_up_button.Draw(target);
        m_down_button.m_dirty = true;
        m_down_button.Draw(target);
        m_thumb_button.m_dirty = true;
        m_thumb_button.Draw(target);
        InvalidateRegion(m_track_button.Bounds().x, m_track_button.Bounds().y,
                         m_track_button.Bounds().x + m_track_button.Bounds().w,
                         m_track_button.Bounds().y + m_track_button.Bounds().h);
        m_dirty = false;
    }
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

// FUNCTION: WIZ8 0x005e1170
void W8DialogScrollBar::ScrollUp()
{
    if (m_first_visible_entry != 0) {
        --m_first_visible_entry;
        if (m_on_scroll) {
            m_on_scroll(this, m_first_visible_entry);
        }
        UpdateThumb();
    }
}

// FUNCTION: WIZ8 0x005e11a0
void W8DialogScrollBar::ScrollDown()
{
    int visible_entries = m_view_height / m_entry_height;
    if (visible_entries < m_entry_count &&
        m_first_visible_entry < m_entry_count - visible_entries) {
        ++m_first_visible_entry;
        if (m_on_scroll) {
            m_on_scroll(this, m_first_visible_entry);
        }
        UpdateThumb();
    }
}

// FUNCTION: WIZ8 0x005e11e0
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
            m_on_scroll(this, m_first_visible_entry);
        }
        UpdateThumb();
    }
}

// FUNCTION: WIZ8 0x005e0c40
W8DialogScrollBar::W8DialogScrollBar(UiLayer& layer)
    : m_up_button(layer), m_down_button(layer), m_thumb_button(layer), m_track_button(layer)
{
    m_initialized = false;
    m_visible = false;
    m_owner = 0;
    m_entry_count = 1;
    m_first_visible_entry = 0;
    m_entry_height = -1;
    m_view_height = -1;
    m_dirty = false;
    m_track_bounds.left = 0;
    m_track_bounds.top = 0;
    m_track_bounds.right = 0;
    m_track_bounds.bottom = 0;
    m_on_scroll = 0;
}
