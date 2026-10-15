#include "wiz8/dialog_code/DialogInterface.h"

#include <string_view>

#include "dialog_code/DialogBase.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/dialog_code/MessageDialogBase.h"

// FUNCTION: WIZ8 0x005cf280
W8DialogBase* CreateCharacterSummaryDialog(W8Character* character)
{
    W8DialogBase* dialog = new W8CharacterSummaryDialog(character);
    dialog->SetText("");
    dialog->SetOrigin(0x87, 0xc0);
    return dialog;
}

// FUNCTION: WIZ8 0x005cf300
W8MessageDialogBase* CreateMessageDialog()
{
    auto* dialog = new W8MessageDialogBase;
    dialog->SetOrigin(240, 190);
    dialog->SetExtent(160, 100);
    dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    return dialog;
}

// FUNCTION: WIZ8 0x005cf250
// FUNCTION: WIZ8 0x005CF4F0
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text)
{
    dialog->SetMessage(text, 1, 0x32, true, true, true, false, 0, 0);
}
