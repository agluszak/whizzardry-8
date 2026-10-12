#include <cstdint>
#include "Font.h"
#pragma once

#include <SDL3/SDL_rect.h>
#include <optional>
#include <string>
#include <string_view>

#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/regions.h"

enum W8DialogKind {
    W8_DIALOG_BASIC = 0,
    W8_DIALOG_MESSAGE = 1,
    W8_DIALOG_LIST_BOX = 3,
    W8_DIALOG_TRIGGER_ITEM_PICKER = 4,
    W8_DIALOG_SPLIT_AMOUNT = 5,
    W8_DIALOG_CHARACTER_SUMMARY = 6,
    W8_DIALOG_NPC = 7
};

enum { W8_DIALOG_DIRTY_REDRAW = 1u };

class W8DialogBase;
class W8ButtonWidget;
class W8DialogTextArea;

typedef void (*W8DialogDestroyCallback)(W8DialogBase* dialog);
void SetDialogDestroyCallback(W8DialogBase* dialog, W8DialogDestroyCallback callback);
/* Shared close-button callback used by the monster and spell info dialogs. */
void DialogCloseButtonCallback(W8ButtonWidget* button);

/* Modal controls own their regions and state; only visual resources and
   rendering uses the shared dialog button appearance module. */
// VTABLE: WIZ8 0x005efaf8
class W8DialogBase {
public:
    W8DialogBase();
    virtual ~W8DialogBase();
    virtual std::int32_t CreateControls();
    virtual void DestroyControls();
    virtual void Draw();
    virtual W8DialogKind GetDialogType(); /* Base=0, modal=1, list=3 */
    virtual void SetText(std::string_view text);
    virtual void SetOrigin(std::int32_t x, std::int32_t y);
    virtual void SetExtent(std::int32_t width, std::int32_t height);
    virtual void SetBackground(const char* path, std::int32_t flags);
    virtual bool ProcessInput();
    virtual void OnNumericInputChanged(std::int32_t control_id);
    virtual void OnRightButtonDown();
    virtual void OnRightButtonUp();
    virtual void OnMouseWheel(std::int32_t delta);

    template <class HandleInput>
    static void DrainModalInput(HandleInput&& handle_input)
    {
        InputEvent input;

        const auto mouse = GetRegionMousePosition();
        UpdateRegionMousePosition(mouse.x, mouse.y);
        while (DequeueEvent(&input) == 1) {
            if (!handle_input(input)) break;
        }
    }

    friend void SetDialogDestroyCallback(W8DialogBase* dialog, W8DialogDestroyCallback callback);
    friend class W8DialogNumericInput;
    template <class Dialog, class Field>
    friend void ProcessNumericDialogInputEvents(Dialog* dialog, Field*& active_field,
                                                bool (Dialog::*handle_event)(const InputEvent*));

public:
    /* Main Game raises the redraw bit when promoting its pending dialog. */
    std::uint32_t m_dirty_flags;

protected:
    void BeginModalInput();
    void EndModalInput();
    void CreateCloseButton(W8ButtonWidget& button, std::int32_t left, std::int32_t top);
    void ScrollTextArea(W8DialogTextArea& area, std::int32_t first_visible_line, std::int32_t left, std::int32_t top,
                        std::int32_t width, std::int32_t height);

    std::int32_t m_error;
    W8ButtonWidget m_resource;
    std::string m_text;
    Font* m_font;
    std::uint8_t m_foreground;
    std::uint8_t m_background;
    std::uint8_t padding_01a[2];
    std::string m_background_path;
    std::int32_t m_background_flags;
    GenericButtonImage m_border;
    std::uint8_t padding_026[2];
    std::int32_t m_x;
    std::int32_t m_y;
    std::int32_t m_width;
    std::int32_t m_height;
    std::uint8_t unknown_038[8];
    bool m_initialized;

public:
    /* Cleared to close the dialog; ProcessInput keeps running while set. */
    bool m_keep_open;

public:
    /* The trigger update installs its callback with a plain store, so this
       slot is public rather than reachable only through the setter. */
    std::uint8_t padding_042[2];
    W8DialogDestroyCallback m_destroy_callback;
    /* Base constructor clears this slot; the destruction callback consumes
       it. The item-picker callback's payload is a Trigger*. */
    void* m_destroy_callback_context;

protected:
    /* Cleared by W8DialogNumericInput when its field deactivates. */
    std::int32_t m_field_4c;
    bool m_right_button_down;
    std::uint8_t padding_051[3];
    std::optional<ExclusiveRegionSet> m_input_scope;
};


extern std::int32_t g_live_dialog_count;
