#pragma once

#include <cstdint>
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

/* Dialog Code\NpcDialog.cpp. The NPC dialogue popup: the dispatch code
   allocates 0x270 bytes and hands the constructor a request buffer whose
   opcode byte selects the layout - 0x05 option list, 0x12/0x1e yes/no price
   check, 0x13 keyword text entry. The constructor publishes the instance in
   the file-scope singleton in NpcDialog.cpp; the option callback and the
   Enter-key handling in ProcessInput reach back through it. */
// VTABLE: WIZ8 0x005efa4c
class W8NpcDialog : public W8DialogBase {
    friend void OnNpcDialogClosed(W8DialogBase* dialog);

public:
    W8NpcDialog(W8NpcQuoteEntry* message, std::int32_t aux_data);
    virtual ~W8NpcDialog() override;
    virtual std::int32_t CreateControls() override;
    virtual void Draw() override;
    virtual W8DialogKind GetDialogType() override; /* 7 */
    virtual bool ProcessInput() override;

private:
    /* Copies the pressed option's user-data index into m_selected_option and
       closes the dialog. */
    static void OptionSelected(W8ButtonWidget* button);

    std::array<std::unique_ptr<W8TextBuffer>, 2> m_text_buffers;
    std::array<std::unique_ptr<W8ButtonWidget>, 3> m_buttons;
    std::int8_t m_selected_option;
    W8NpcQuoteEntry* m_message;
    std::int32_t m_aux_data;
    bool m_compact_options; /* option list may size under 200 wide */
    std::uint8_t unknown_075;
    std::uint16_t m_text_width; /* widest option text + 6 */
    std::uint8_t m_input_field; /* AddTextInputField id for opcode 0x13 */
    std::uint8_t unknown_079;
    std::string m_input_text; /* fills the 0x270-byte allocation */
};
