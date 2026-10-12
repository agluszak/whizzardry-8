#include <cstdint>
#include "Font.h"
#include <string_view>

#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/CharacterSummaryDialog.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/sr_api.h"
#include "dialog_code/DialogBase.h"

/*
 * Dialog Code\DialogInterface.cpp, named by the assertion this body embeds at
 * line 152.
 *
 * The forwarders below operate on the shared dialog base recovered elsewhere:
 * slot three draws, slot nine processes input, and the setter establishes
 * the base field at 0x44. The assertion does not return, which is why each
 * operation remains unconditional after its null check.
 */

// GLOBAL: WIZ8 0x0064fde8
Font* g_dialog_interface_font = nullptr;
// GLOBAL: WIZ8 0x0069ca32
bool g_dialog_font_enabled;
// GLOBAL: WIZ8 0x0064fdec
std::uint8_t g_dialog_font_foreground = 255;
// GLOBAL: WIZ8 0x0064fded
std::uint8_t g_dialog_font_background = 255;

// FUNCTION: WIZ8 0x005cf280
W8DialogBase* CreateCharacterSummaryDialog(W8Character* character)
{
    W8DialogBase* dialog = new W8CharacterSummaryDialog(character);
    dialog->SetText("");
    dialog->SetOrigin(0x87, 0xc0);
    return dialog;
}

// FUNCTION: WIZ8 0x005cf300
W8DialogBase* CreateDialogByKind(W8DialogKind kind)
{
    W8DialogBase* dialog;

    switch (kind) {
    case W8_DIALOG_BASIC:
        dialog = new W8DialogBase;
        dialog->SetText("Test Dialog");
        dialog->SetOrigin(160, 120);
        dialog->SetExtent(320, 240);
        break;
    case W8_DIALOG_MESSAGE:
        dialog = new W8MessageDialogBase;
        dialog->SetOrigin(240, 190);
        dialog->SetExtent(160, 100);
        dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
        return dialog;
    case W8_DIALOG_LIST_BOX:
        dialog = new W8ListBoxDialog;
        dialog->SetText("ListBox Dialog");
        dialog->SetOrigin(200, 100);
        dialog->SetExtent(240, 280);
        break;
    case W8_DIALOG_SPLIT_AMOUNT:
        dialog = new W8SplitAmountDialog;
        dialog->SetText("");
        dialog->SetOrigin(159, 184);
        return dialog;
    default:
        return 0;
    }
    dialog->SetBackground("Data\\Dialogs\\DialogBackground.sti", 0);
    return dialog;
}

// FUNCTION: WIZ8 0x005cf250
void ConfigureDialogFont(Font& font, bool enabled, std::uint8_t foreground,
                         std::uint8_t background)
{
    g_dialog_interface_font = &font;
    g_dialog_font_enabled = enabled;
    g_dialog_font_foreground = foreground;
    g_dialog_font_background = background;
}

#define DIALOG_INTERFACE_CPP "C:\\Projects\\Wizardry 8\\Dialog Code\\DialogInterface.cpp"

// FUNCTION: WIZ8 0x005CF4F0
void SetDialogPrompt(W8MessageDialogBase* dialog, std::string_view text, std::int32_t, std::int32_t)
{
    dialog->SetMessage(text, 1, 0x32, true, true, true, false, 0, 0);
}

// FUNCTION: WIZ8 0x005cf510
bool GetDialogResult(W8DialogBase* dialog)
{
    return static_cast<W8MessageDialogBase*>(dialog)->accepted;
}

// FUNCTION: WIZ8 0x005cf520
void DrawDialog(W8DialogBase* dialog)
{
    if (dialog == 0) {
        srAssertFail("pDialog", DIALOG_INTERFACE_CPP, 0x66, 0);
    }
    dialog->Draw();
}

// FUNCTION: WIZ8 0x005cf550
bool ProcessDialogInput(W8DialogBase* dialog)
{
    if (dialog == 0) {
        srAssertFail("pDialog", DIALOG_INTERFACE_CPP, 0x74, 0);
    }
    return dialog->ProcessInput();
}

// FUNCTION: WIZ8 0x005cf580
void SetDialogDestroyCallback(W8DialogBase* dialog, W8DialogDestroyCallback callback)
{
    if (dialog == 0) {
        srAssertFail("pCDialog", DIALOG_INTERFACE_CPP, 0x98, 0);
    }
    dialog->m_destroy_callback = callback;
}
