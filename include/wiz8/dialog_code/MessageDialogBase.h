#pragma once

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
    virtual int CreateControls() override;
    virtual void DestroyControls() override;
    virtual void Draw() override;
    virtual W8DialogKind GetDialogType() override;
    virtual bool ProcessInput() override;
    virtual bool HandleInput(const InputEvent* input);

    /* Called on this object from outside the class by the Please Wait screen,
       which is what puts it here rather than under protected. */
    void SetMessage(std::string_view message, int line_count, unsigned short characters_per_line,
                    bool confirmation, bool cancel, bool size_to_message, bool wrap_message,
                    int maximum_width, int maximum_height);
    /* The party-selection screen calls this centering helper on a freshly
       allocated base dialog, so it is part of the public surface rather than
       a derived-only helper. */
    void SetClientExtent(int width, int height);

    unsigned int WrapMessage(std::string_view message);

    friend void MessageDialogConfirmCallback(W8ButtonWidget* button);
    friend void MessageDialogCancelCallback(W8ButtonWidget* button);

    /* Both are read and written on this object from outside the class by the
       Please Wait screen's frame handler, which is what puts them here. */
    bool accepted; /* cleared; a derived close passes it on */
    bool is_open;  /* set, and gates the close path */

protected:
    GenericButtonImage m_edge_image; /* 0x56 generic-button-image resource */
    W8ButtonWidget m_message_button{InputLayer()};
    W8ButtonWidget m_confirm_button{InputLayer()};
    unsigned char unknown_064[0x10];
    W8ButtonWidget m_cancel_button{InputLayer()};
    unsigned char unknown_07c[0x10];
    std::vector<std::string> m_lines;
    unsigned int m_line_count;
    bool m_show_confirm;
    bool allow_cancel; /* changes Escape handling */
    unsigned char unknown_096[2];
};

void MessageDialogConfirmCallback(W8ButtonWidget* button);
void MessageDialogCancelCallback(W8ButtonWidget* button);
