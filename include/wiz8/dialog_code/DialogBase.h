#include "wiz8/canvas/Font.h"
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>

#include "Font.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/modal_content.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

class W8DialogBase;
class W8ButtonWidget;
class W8DialogTextArea;
class W8DialogScrollBar;

/* Modal controls own their regions and state; only visual resources and
   rendering uses the shared dialog button appearance module. */
class W8DialogBase : public ModalContent {
    /* First, so it outlives every control's region. */
    UiLayer m_input;

public:
    W8DialogBase();
    UiLayer& InputLayer() { return m_input; }
    ~W8DialogBase() override;
    void DrawModal(SDL_Surface& target) override { Draw(target); }
    void RequestModalRedraw() override { m_dirty = true; }
    virtual void Draw(SDL_Surface& target);
    void RefreshModal() override;

    virtual void SetText(std::string_view text);
    virtual void SetOrigin(int x, int y);
    virtual void SetExtent(int width, int height);
    virtual void SetBackground(const char* path, int flags);
    bool ProcessInput() override;

public:
    /* Main Game raises the redraw bit when promoting its pending dialog. */
    bool m_dirty;

protected:
    virtual bool TakeDirty();
    void BeginModalInput();
    void EndModalInput();
    void CreateCloseButton(W8ButtonWidget& button, int left, int top);
    void ScrollTextArea(W8DialogTextArea& area, int first_visible_line);
    /* Information popups close when a right click on them is released,
       scroll their text with wheel input no control consumed, and repaint
       when their text, scroll bar or close button changes. */
    struct InfoPopup {
        W8DialogTextArea& text;
        W8DialogScrollBar& scroll_bar;
        W8ButtonWidget* close = nullptr;
    };
    void UseInfoPopup(InfoPopup popup) { m_info_popup.emplace(popup); }

    HitTarget* m_body = nullptr;
    std::string m_text;
    Font* m_font;
    std::string m_background_path;
    int m_background_flags;
    GenericButtonImage m_border;
    int m_x;
    int m_y;
    int m_width;
    int m_height;

public:
    /* Cleared to close the dialog; ProcessInput keeps running while set. */
    bool m_keep_open;

public:
    std::function<void()> TakeCompletion(bool completed) override;
    std::function<void()> on_close;
    std::function<void()> cleanup; // Resource restoration, including cancellation.

private:
    std::optional<InfoPopup> m_info_popup;
    bool m_right_button_down = false;
};
