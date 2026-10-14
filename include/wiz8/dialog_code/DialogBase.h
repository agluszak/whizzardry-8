#include "Font.h"
#pragma once

#include <string>
#include <string_view>

#include <SDL3/SDL_rect.h>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/modal_content.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

enum { W8_DIALOG_DIRTY_REDRAW = 1u };

class W8DialogBase;
class W8ButtonWidget;
class W8DialogTextArea;

typedef void (*W8DialogDestroyCallback)(W8DialogBase* dialog);
/* Shared close-button callback used by the monster and spell info dialogs. */

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

    template<class Dialog, class Field>
    friend void ProcessNumericDialogInputEvents(Dialog* dialog, Field*& active_field,
                                                bool (Dialog::*handle_event)(const InputEvent*));

public:
    /* Main Game raises the redraw bit when promoting its pending dialog. */
    unsigned int m_dirty_flags;

protected:
    void BeginModalInput();
    void EndModalInput();
    void CreateCloseButton(W8ButtonWidget& button, int left, int top);
    void ScrollTextArea(W8DialogTextArea& area, int first_visible_line);

    int m_error;
    HitTarget* m_body = nullptr;
    std::string m_text;
    Font* m_font;
    unsigned char m_foreground;
    unsigned char m_background;
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
    /* The trigger update installs its callback with a plain store, so this
       slot is public rather than reachable only through the setter. */
    W8DialogDestroyCallback m_destroy_callback;
    /* Base constructor clears this slot; the destruction callback consumes
       it. The item-picker callback's payload is a Trigger*. */
    void* m_destroy_callback_context;

protected:
    bool m_right_button_down;
};
