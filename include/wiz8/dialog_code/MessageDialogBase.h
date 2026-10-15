#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "compat/compiler.h"
#include "input.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"

// VTABLE: WIZ8 0x005ef8b0
class W8MessageDialogBase : public W8DialogBase {
public:
    std::function<void()> TakeCompletion(bool completed) override;
    std::function<void(bool)> on_complete;
    W8MessageDialogBase();
    virtual void Draw(SDL_Surface& target) override;

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

    /* Both are read and written on this object from outside the class by the
       Please Wait screen's frame handler, which is what puts them here. */
    bool accepted; /* cleared; a derived close passes it on */
    bool is_open;  /* set, and gates the close path */

private:
    void Configure();

protected:
    // FUNCTION: WIZ8 0x005d32c0 (confirm), 0x005d3370 (cancel)
    void Close(bool accept);
    GenericButtonImage m_edge_image; /* 0x56 generic-button-image resource */
    W8ButtonWidget m_message_button{InputLayer()};
    W8ButtonWidget m_confirm_button{InputLayer()};
    W8ButtonWidget m_cancel_button{InputLayer()};
    std::vector<std::string> m_lines;
    unsigned int m_line_count;
    bool m_show_confirm;
    bool allow_cancel; /* changes Escape handling */
};
