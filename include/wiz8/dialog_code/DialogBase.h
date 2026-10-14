#include "Font.h"
#pragma once

#include <SDL3/SDL_rect.h>
#include <string>
#include <string_view>
#include <functional>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/modal_content.h"

enum { W8_DIALOG_DIRTY_REDRAW = 1u };

class W8DialogBase;
class W8ButtonWidget;
class W8DialogTextArea;

using W8DialogDestroyCallback = std::function<void(W8DialogBase* dialog)>;

/* Modal controls own their regions and state; only visual resources and
   rendering uses the shared dialog button appearance module. */
// VTABLE: WIZ8 0x005efaf8
class W8DialogBase : public ModalContent {
    /* First, so it outlives every control's region. */
    UiLayer m_input;

public:
    W8DialogBase();
    UiLayer& InputLayer() { return m_input; }
    ~W8DialogBase() override;
    void DrawModal(SDL_Surface& target) override { Draw(target); }
    void RequestModalRedraw() override { m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW; }
    virtual int CreateControls();
    virtual void DestroyControls();
    virtual void Draw(SDL_Surface& target);

    virtual void SetText(std::string_view text);
    virtual void SetOrigin(int x, int y);
    virtual void SetExtent(int width, int height);
    virtual void SetBackground(const char* path, int flags);
    bool ProcessInput() override;
    virtual void OnNumericInputChanged(int control_id);
    virtual void OnRightButtonDown();
    virtual void OnRightButtonUp();
    virtual void OnMouseWheel(int delta);

    template <class Dialog, class Field>
    friend void ProcessNumericDialogInputEvents(Dialog* dialog, Field*& active_field,
                                                bool (Dialog::*handle_event)(const InputEvent*));

public:
    /* Main Game raises the redraw bit when promoting its pending dialog. */
    unsigned int m_dirty_flags;

protected:
    void NotifyDestroyed();
    void BeginModalInput();
    void EndModalInput();
    void CreateCloseButton(W8ButtonWidget& button, int left, int top);
    void ScrollTextArea(W8DialogTextArea& area, int first_visible_line);

    int m_error;
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
    bool m_initialized;

public:
    /* Cleared to close the dialog; ProcessInput keeps running while set. */
    bool m_keep_open;

public:
  // Completion can capture its actual actor. List and picker destructors
  // notify while their derived state is still live, before base teardown.
    W8DialogDestroyCallback m_destroy_callback;

protected:
    bool m_right_button_down;
};
