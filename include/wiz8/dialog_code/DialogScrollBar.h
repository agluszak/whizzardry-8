#pragma once

#include <functional>

#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"

class W8DialogScrollBar {
public:
    struct Resources {
        const char* arrows_path;
        const char* track_path;
        int track_frame;
        std::function<void(int first_visible_entry)> on_scroll;
    };
    W8DialogScrollBar(UiLayer& layer, Resources resources);
    W8DialogScrollBar(const W8DialogScrollBar&) = delete;
    W8DialogScrollBar& operator=(const W8DialogScrollBar&) = delete;
    void SetLayout(int x, int y, int entry_count, int first_visible_entry, int entry_height,
                   int view_height);
    void UpdateThumb();
    void Draw(SDL_Surface& target);
    void Refresh();
    bool TakeDirty();
    void ScrollUp();
    void ScrollBy(int delta);
    void ScrollDown();
    void ScrollToMouse(int mouse_y);

private:
    bool m_visible;

    int m_entry_count = 1;
    int m_first_visible_entry = 0;
    int m_entry_height = 1;
    int m_view_height = 1;
    W8ControlsRect m_track_bounds{}; /* left, top, right, bottom */
private:
    W8ButtonWidget m_up_button;
    W8ButtonWidget m_down_button;
    W8ButtonWidget m_thumb_button;
    W8ButtonWidget m_track_button;
    std::function<void(int first_visible_entry)> m_on_scroll;
};
