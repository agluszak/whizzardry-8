#include "wiz8/dialog_code/DialogInterface.h"

#include <string_view>

#include "dialog_code/DialogBase.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/dialog_code/MessageDialogBase.h"

std::unique_ptr<W8DialogBase> CreateCharacterSummaryDialog(W8Character* character)
{
    auto dialog = std::make_unique<W8CharacterSummaryDialog>(character);
    dialog->SetText("");
    dialog->SetOrigin(0x87, 0xc0);
    return dialog;
}

std::unique_ptr<W8MessageDialogBase> CreateMessageDialog()
{
    auto dialog = std::make_unique<W8MessageDialogBase>();
    dialog->SetOrigin(240, 190);
    dialog->SetExtent(160, 100);
    dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    return dialog;
}
