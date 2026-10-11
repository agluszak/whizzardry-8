#include <SDL3/SDL_rect.h>
#include <vector>

#include "wiz8/dialog_code/DialogButton.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "Font.h"
#include "dialog_code/DialogBase.h"

/* Retail Dialog Code\stListBox.cpp - the button callbacks that give
   W8ListBoxDialog its scrolling text-list behaviour. The list state lives on
   the dialog (first visible line 0x0f0, selected line 0x0f4) and each button's
   owner link carries the dialog pointer. The gap helpers it calls -
   GetVisibleLineCount 0x005CC650, SetCurrentLine 0x005CCB80 and the keyboard
   path 0x005CD2B0 - are not proven to this unit and stay declared in
   DialogFactoryDialogs.h for these call sites. */

// FUNCTION: WIZ8 0x005cce70
void W8ListBoxDialog::TextAreaButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    const SDL_Point cursor = button->GetMousePosition();
    const int height = g_dialog_interface_font->height();
    if (height == 0) return;
    const int line = (cursor.y - dialog->m_area_button.GetY()) / height +
                     dialog->m_first_visible_line;
    if (line == dialog->m_selected_line) {
        dialog->m_keep_open = false;
        return;
    }
    dialog->SetCurrentLine(line);
}

// FUNCTION: WIZ8 0x005ccf30
void W8ListBoxDialog::UpButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    dialog->SetCurrentLine(dialog->m_selected_line - 1);
}

// FUNCTION: WIZ8 0x005ccfe0
void W8ListBoxDialog::DownButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    dialog->SetCurrentLine(dialog->m_selected_line + 1);
}

// FUNCTION: WIZ8 0x005cd090
void W8ListBoxDialog::OkButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    dialog->m_keep_open = false;
    dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005cd130
void W8ListBoxDialog::CancelButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    dialog->m_keep_open = false;
    dialog->SetCurrentLine(-1);
    dialog->m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005cd1e0
void W8ListBoxDialog::SliderTrackButtonCallback(W8DialogButton* button)
{
    auto* dialog = static_cast<W8ListBoxDialog*>(button->m_owner);
    SDL_Point cursor = button->GetMousePosition();
    const int top = button->GetY() + dialog->m_up_button.GetHeight();
    const int bottom = button->GetY() + button->GetHeight() - dialog->m_down_button.GetHeight();
    if (cursor.y < top) cursor.y = top;
    if (cursor.y > bottom) cursor.y = bottom;
    if (bottom <= top) return;
    dialog->SetCurrentLine((cursor.y - top) * static_cast<int>(dialog->m_lines.size()) /
                           (bottom - top));
}
