#pragma once

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
        int track_frame;
        void (*on_scroll)(W8DialogScrollBar*, int first_visible_entry);
    };
    explicit W8DialogScrollBar(UiLayer& layer);
    W8DialogScrollBar(const W8DialogScrollBar&) = delete;
    W8DialogScrollBar& operator=(const W8DialogScrollBar&) = delete;
    ~W8DialogScrollBar() = default;
    bool CreateControls(const Resources* resources);
    void SetLayout(int x, int y, int entry_count, int first_visible_entry, int entry_height,
                   int view_height);
    void UpdateThumb();
    void Draw(SDL_Surface& target, bool force);
    void ScrollUp();
    void ScrollBy(int delta);
    void ScrollDown();
    void ScrollToMouse(int mouse_y);

private:
    bool m_initialized;
    bool m_visible;

public:
    bool m_dirty; /* owning dialogs set this before Draw */
private:
    int m_entry_count;
    int m_first_visible_entry;
    int m_entry_height;
    int m_view_height;
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
    void (*m_on_scroll)(W8DialogScrollBar* scroll_bar, int first_visible_entry);
};
