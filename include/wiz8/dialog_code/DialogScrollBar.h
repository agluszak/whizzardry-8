#pragma once

#include <cstdint>
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/ControlsRect.h"
#include "Types.h"
#include "compat/compiler.h"

class W8DialogBase;

class W8DialogScrollBar {
public:
    struct Resources {
        const char* arrows_path;
        const char* track_path;
        std::int32_t track_frame;
        void (*on_scroll)(W8DialogScrollBar*, std::int32_t first_visible_entry);
    };
    W8DialogScrollBar();
    W8DialogScrollBar(const W8DialogScrollBar&) = delete;
    W8DialogScrollBar& operator=(const W8DialogScrollBar&) = delete;
    bool CreateControls(const Resources* resources);
    void SetLayout(std::int32_t x, std::int32_t y, std::int32_t entry_count, std::int32_t first_visible_entry, std::int32_t entry_height,
                   std::int32_t view_height);
    void UpdateThumb();
    void Draw(bool force);
    void ScrollUp();
    void ScrollBy(std::int32_t delta);
    void ScrollDown();
    void ScrollToMouse(std::int32_t mouse_y);

private:
    bool m_initialized;
    bool m_visible;

public:
    bool m_dirty; /* owning dialogs set this before Draw */
private:
    std::int32_t m_entry_count;
    std::int32_t m_first_visible_entry;
    std::int32_t m_entry_height;
    std::int32_t m_view_height;
    W8ControlsRect m_track_bounds; /* left, top, right, bottom */
public:
    /* Owning dialog; MonsterInfoDialog stores this so the scroll callback can
       retarget the text area. */
    W8DialogBase* m_owner;

private:
    W8ButtonWidget m_up_button;
    W8ButtonWidget m_down_button;
    W8ButtonWidget m_thumb_button;
    W8ButtonWidget m_track_button;
    void (*m_on_scroll)(W8DialogScrollBar* scroll_bar, std::int32_t first_visible_entry);
};
