#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

#include "wiz8/dialog_code/DialogBase.h"
#include "input.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "compat/compiler.h"

// VTABLE: WIZ8 0x005ef8b0
class W8MessageDialogBase : public W8DialogBase {
public:
    W8MessageDialogBase();
    virtual std::int32_t CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw() override;
    virtual W8DialogKind GetDialogType() override;
    virtual bool ProcessInput() override;
    virtual bool HandleInput(const InputEvent* input);

    /* Called on this object from outside the class by the Please Wait screen,
       which is what puts it here rather than under protected. */
    void SetMessage(std::string_view message, std::int32_t line_count, std::uint16_t characters_per_line,
                    bool confirmation, bool cancel, bool size_to_message, bool wrap_message,
                    std::int32_t maximum_width, std::int32_t maximum_height);
    /* The party-selection screen calls this centering helper on a freshly
       allocated base dialog, so it is part of the public surface rather than
       a derived-only helper. */
    void SetClientExtent(std::int32_t width, std::int32_t height);

    std::uint32_t WrapMessage(std::string_view message);

    friend void MessageDialogConfirmCallback(W8ButtonWidget* button);
    friend void MessageDialogCancelCallback(W8ButtonWidget* button);

    /* Both are read and written on this object from outside the class by the
       Please Wait screen's frame handler, which is what puts them here. */
    bool accepted; /* cleared; a derived close passes it on */
    bool is_open;  /* set, and gates the close path */

protected:
    GenericButtonImage m_edge_image; /* 0x56 generic-button-image resource */
    W8ButtonWidget m_message_button;
    W8ButtonWidget m_confirm_button;
    std::uint8_t unknown_064[0x10];
    W8ButtonWidget m_cancel_button;
    std::uint8_t unknown_07c[0x10];
    std::vector<std::string> m_lines;
    std::uint32_t m_line_count;
    bool m_show_confirm;
    bool allow_cancel; /* changes Escape handling */
    std::uint8_t unknown_096[2];
};

void MessageDialogConfirmCallback(W8ButtonWidget* button);
void MessageDialogCancelCallback(W8ButtonWidget* button);
