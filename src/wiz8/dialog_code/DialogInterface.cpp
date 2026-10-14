#include "wiz8/dialog_code/DialogInterface.h"

#include <string_view>

#include "Font.h"
#include "dialog_code/DialogBase.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/dialog_code/MessageDialogBase.h"

// GLOBAL: WIZ8 0x0064fde8
Font* g_dialog_interface_font = nullptr;
// GLOBAL: WIZ8 0x0069ca32
bool g_dialog_font_enabled;
// GLOBAL: WIZ8 0x0064fdec
unsigned char g_dialog_font_foreground = 255;
// GLOBAL: WIZ8 0x0064fded
unsigned char g_dialog_font_background = 255;

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
void ConfigureDialogFont(Font& font, bool enabled, unsigned char foreground,
                         unsigned char background)
{
    g_dialog_interface_font = &font;
    g_dialog_font_enabled = enabled;
    g_dialog_font_foreground = foreground;
    g_dialog_font_background = background;
}

// FUNCTION: WIZ8 0x005CF4F0
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text)
{
    dialog->SetMessage(text, 1, 0x32, true, true, true, false, 0, 0);
}

// FUNCTION: WIZ8 0x005cf510
bool GetDialogResult(W8DialogBase* dialog)
{
    return static_cast<W8MessageDialogBase*>(dialog)->accepted;
}
