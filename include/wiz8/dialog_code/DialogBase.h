#include "Font.h"
#pragma once

#include <SDL3/SDL_rect.h>
#include <string>
#include <string_view>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/regions.h"
#include "wiz8/ui_layer.h"
#include "wiz8/modal_content.h"

enum W8DialogKind {
    W8_DIALOG_BASIC = 0,
    W8_DIALOG_MESSAGE = 1,
    W8_DIALOG_LIST_BOX = 3,
    W8_DIALOG_TRIGGER_ITEM_PICKER = 4,
    W8_DIALOG_SPLIT_AMOUNT = 5,
    W8_DIALOG_CHARACTER_SUMMARY = 6,
};

enum { W8_DIALOG_DIRTY_REDRAW = 1u };

class W8DialogBase;
class W8ButtonWidget;
class W8DialogTextArea;

typedef void (*W8DialogDestroyCallback)(W8DialogBase* dialog);
void SetDialogDestroyCallback(W8DialogBase* dialog, W8DialogDestroyCallback callback);
/* Shared close-button callback used by the monster and spell info dialogs. */
void DialogCloseButtonCallback(W8ButtonWidget* button);

/* A dialog's body: it paints the bordered background and takes the clicks
   that miss the dialog's controls, so they count as handled. */
class W8DialogFrame final : public W8Widget {
public:
    void Configure(ButtonAppearance appearance, const SDL_Rect& bounds, unsigned int set);
    void Reset();
    bool IsConfigured() const { return OwnsRegion(); }
    void Draw(SDL_Surface& target) const;
    void Redraw(SDL_Surface& target, bool) override
    {
        if (m_active) Draw(target);
    }

private:
    ButtonAppearance m_appearance;
};

/* Modal controls own their regions and state; only visual resources and
   rendering uses the shared dialog button appearance module. */
// VTABLE: WIZ8 0x005efaf8
class W8DialogBase : public ModalContent {
    /* First, so it outlives every control's region. */
    UiLayer m_input;
    friend RegionSetRef CurrentInputLayer();

public:
    W8DialogBase();
    UiLayer& InputLayer() { return m_input; }
    ~W8DialogBase() override;
    void DrawModal(SDL_Surface&) override { Draw(); }
    void RequestModalRedraw() override { m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW; }
    virtual int CreateControls();
    virtual void DestroyControls();
    virtual void Draw();
    virtual W8DialogKind GetDialogType(); /* Base=0, modal=1, list=3 */
    virtual void SetText(std::string_view text);
    virtual void SetOrigin(int x, int y);
    virtual void SetExtent(int width, int height);
    virtual void SetBackground(const char* path, int flags);
    bool ProcessInput() override;
    virtual void OnNumericInputChanged(int control_id);
    virtual void OnRightButtonDown();
    virtual void OnRightButtonUp();
    virtual void OnMouseWheel(int delta);

    friend void SetDialogDestroyCallback(W8DialogBase* dialog, W8DialogDestroyCallback callback);
    friend class W8DialogNumericInput;
    template <class Dialog, class Field>
    friend void ProcessNumericDialogInputEvents(Dialog* dialog, Field*& active_field,
                                                bool (Dialog::*handle_event)(const InputEvent*));

public:
    /* Main Game raises the redraw bit when promoting its pending dialog. */
    unsigned int m_dirty_flags;

protected:
    void BeginModalInput();
    void EndModalInput();
    void CreateCloseButton(W8ButtonWidget& button, int left, int top);
    void ScrollTextArea(W8DialogTextArea& area, int first_visible_line, int left, int top,
                        int width, int height);

    int m_error;
    W8DialogFrame m_frame;
    std::string m_text;
    Font* m_font;
    unsigned char m_foreground;
    unsigned char m_background;
    unsigned char padding_01a[2];
    std::string m_background_path;
    int m_background_flags;
    GenericButtonImage m_border;
    unsigned char padding_026[2];
    int m_x;
    int m_y;
    int m_width;
    int m_height;
    unsigned char unknown_038[8];
    bool m_initialized;

public:
    /* Cleared to close the dialog; ProcessInput keeps running while set. */
    bool m_keep_open;

public:
    /* The trigger update installs its callback with a plain store, so this
       slot is public rather than reachable only through the setter. */
    unsigned char padding_042[2];
    W8DialogDestroyCallback m_destroy_callback;
    /* Base constructor clears this slot; the destruction callback consumes
       it. The item-picker callback's payload is a Trigger*. */
    void* m_destroy_callback_context;

protected:
    /* Cleared by W8DialogNumericInput when its field deactivates. */
    int m_field_4c;
    bool m_right_button_down;
    unsigned char padding_051[3];
};


/* Each live dialog is an input layer with its own region set; buttons and
   text fields made while it is innermost take input there, so its modal
   scope reaches exactly its own controls. Outside dialogs the layer is the
   shared control set. InputLayerSet resolves a captured layer, falling back
   to the shared set once that layer's dialog is gone. */
RegionSetRef CurrentInputLayer();
unsigned int InputLayerSet(const RegionSetRef& layer);
