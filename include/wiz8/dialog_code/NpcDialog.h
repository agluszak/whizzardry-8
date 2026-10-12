#pragma once

class TextFieldGroup;

#include <array>
#include <memory>
#include <string>

#include "wiz8/dialog_code/DialogBase.h"
#include "compat/compiler.h"

class W8TextBuffer;
class W8ButtonWidget;
struct W8NpcQuoteEntry;

/* The dialog receives the script entry itself. Its sub-entry array is the
   option list for kind 5; operand0 is the price for kinds 0x12/0x1e. */

/* NPC replies: options, a price confirmation, or keyword entry. The dialog
   owns its editor; closing restores the previous editor before reporting the
   reply to the NPC controller. */
// VTABLE: WIZ8 0x005efa4c
class W8NpcDialog : public W8DialogBase {
    friend void OnNpcDialogClosed(W8DialogBase* dialog);

public:
    W8NpcDialog(W8NpcQuoteEntry* message, int aux_data);
    virtual ~W8NpcDialog() override;
    virtual int CreateControls() override;
    virtual void Draw() override;
    virtual W8DialogKind GetDialogType() override; /* 7 */
    virtual bool ProcessInput() override;

private:
    std::array<std::unique_ptr<W8TextBuffer>, 2> m_text_buffers;
    std::array<std::unique_ptr<W8ButtonWidget>, 3> m_buttons;
    signed char m_selected_option = -1;
    bool m_cancelled = false;
    W8NpcQuoteEntry* m_message;
    int m_aux_data;
    bool m_compact_options = false; /* option list may size under 200 wide */
    unsigned char unknown_075;
    unsigned short m_text_width; /* widest option text + 6 */
    int m_input_field = -1;
    std::unique_ptr<TextFieldGroup> m_input_group;
    unsigned char unknown_079;
    std::string m_input_text; /* fills the 0x270-byte allocation */
};
