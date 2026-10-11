#include <SDL3/SDL_rect.h>
#include <algorithm>

#include "Button System.h"
#include "Types.h"
#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "Video2.h"
#include "local_code/ControlsRect.h"
#include "mousesystem.h"

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
                              [this](W8DialogButton*) { ScrollUp(); }, nullptr, false,
                              MSYS_PRIORITY_HIGHEST - 1, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        // FUNCTION: WIZ8 0x005e1320
        m_down_button.Configure(resources->arrows_path, 11, 8, 9, 10, 10,
                                [this](W8DialogButton*) { ScrollDown(); }, nullptr, false,
                                MSYS_PRIORITY_HIGHEST - 1, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        m_thumb_button.Configure(resources->arrows_path, 7, 4, 5, 6, 6, nullptr, nullptr, false,
                                 MSYS_PRIORITY_HIGHEST - 2, W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr) &&
        // FUNCTION: WIZ8 0x005e13d0
        m_track_button.Configure(resources->track_path, BUTTON_NO_IMAGE, resources->track_frame,
                                 BUTTON_NO_IMAGE, resources->track_frame, BUTTON_NO_IMAGE,
                                 [this](W8DialogButton* button) { ScrollToMouse(button->GetMousePosition().y); },
                                 nullptr, false, MSYS_PRIORITY_HIGHEST - 2,
                                 W8_DIALOG_BUTTON_NO_TOOLTIP, nullptr, nullptr);
    if (!configured) {
        m_up_button.Reset();
        m_down_button.Reset();
        m_thumb_button.Reset();
        m_track_button.Reset();
        return false;
    }
    m_up_button.m_activation = m_down_button.m_activation = m_track_button.m_activation =
        DialogButtonActivation::Press;
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
        int track_width = m_track_button.GetWidth();
        int arrow_width = m_up_button.GetWidth();
        m_track_button.SetPosition( static_cast<short>(x), static_cast<short>(y));
        int arrow_x = x + (track_width - arrow_width) / 2;
        m_up_button.SetPosition( static_cast<short>(arrow_x), static_cast<short>(y));
        m_down_button.SetPosition( static_cast<short>(arrow_x),
                          static_cast<short>(y - m_down_button.GetHeight() +
                                             m_track_button.GetHeight()));
        m_track_bounds.left = x;
        m_track_bounds.top = y + m_up_button.GetHeight();
        m_track_bounds.right = x + m_track_button.GetWidth();
        m_track_bounds.bottom =
            y - m_down_button.GetHeight() + m_track_button.GetHeight();
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
                (m_track_bounds.right - m_track_bounds.left - m_thumb_button.GetWidth()) / 2;
        int offset = 0;
        if (m_entry_count > m_view_height / m_entry_height) {
            offset =
                ((m_track_bounds.bottom - m_track_bounds.top - m_thumb_button.GetHeight()) *
                 m_first_visible_entry) /
                (m_entry_count - m_view_height / m_entry_height);
        }
        m_thumb_button.SetPosition( static_cast<short>(x),
                          static_cast<short>(m_track_bounds.top + offset));
        m_dirty = true;
    }
}

// FUNCTION: WIZ8 0x005e10b0
void W8DialogScrollBar::Draw(bool force)
{
    if (m_initialized && m_visible && (force || m_dirty || m_up_button.m_dirty || m_down_button.m_dirty ||
        m_track_button.m_dirty || m_thumb_button.m_dirty)) {
        m_track_button.m_dirty = true;
        m_track_button.Draw();
        m_up_button.m_dirty = true;
        m_up_button.Draw();
        m_down_button.m_dirty = true;
        m_down_button.Draw();
        m_thumb_button.m_dirty = true;
        m_thumb_button.Draw();
        InvalidateRegion(m_track_button.GetX(), m_track_button.GetY(),
                         m_track_button.GetX() + m_track_button.GetWidth(),
                         m_track_button.GetY() + m_track_button.GetHeight(), 0);
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
        mouse_y = std::clamp(mouse_y, m_track_bounds.top, m_track_bounds.bottom);
        int top = m_track_bounds.top;
        int range = m_track_bounds.bottom - top - m_thumb_button.GetHeight();
        if (range <= 0) return;
        const int last_entry = m_entry_count - m_view_height / m_entry_height;
        m_first_visible_entry = std::clamp(last_entry * (mouse_y - top) / range, 0, last_entry);
        if (m_on_scroll) {
            m_on_scroll(this, m_first_visible_entry);
        }
        UpdateThumb();
    }
}

// FUNCTION: WIZ8 0x005e0c40
W8DialogScrollBar::W8DialogScrollBar()
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
