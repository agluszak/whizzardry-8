#include <SDL3/SDL_rect.h>
#include <vector>

#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/ui_input.h"
#include "Font.h"
#include "dialog_code/DialogBase.h"

/* Retail Dialog Code\stListBox.cpp - the button callbacks that give
   W8ListBoxDialog its scrolling text-list behaviour. The list state lives on
   the dialog (first visible line 0x0f0, selected line 0x0f4). The gap helpers it calls -
   GetVisibleLineCount 0x005CC650, SetCurrentLine 0x005CCB80 and the keyboard
   path 0x005CD2B0 - are not proven to this unit and stay declared in
   DialogFactoryDialogs.h for these call sites. */

// FUNCTION: WIZ8 0x005cce70
void W8ListBoxDialog::OnTextAreaClick()
{
    const SDL_Point cursor = UiPointerPosition();
    const int height = g_dialog_interface_font->height();
    if (height == 0) return;
    const int line = (cursor.y - m_area_button.Bounds().y) / height + m_first_visible_line;
    if (line == m_selected_line) {
        m_keep_open = false;
        return;
    }
    SetCurrentLine(line);
}

// FUNCTION: WIZ8 0x005ccf30
void W8ListBoxDialog::OnUp()
{
    SetCurrentLine(m_selected_line - 1);
}

// FUNCTION: WIZ8 0x005ccfe0
void W8ListBoxDialog::OnDown()
{
    SetCurrentLine(m_selected_line + 1);
}

// FUNCTION: WIZ8 0x005cd090
void W8ListBoxDialog::OnOk()
{
    m_keep_open = false;
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005cd130
void W8ListBoxDialog::OnCancel()
{
    m_keep_open = false;
    SetCurrentLine(-1);
    m_dirty_flags |= W8_DIALOG_DIRTY_REDRAW;
}

// FUNCTION: WIZ8 0x005cd1e0
void W8ListBoxDialog::OnSliderTrackClick()
{
    SDL_Point cursor = UiPointerPosition();
    const int top = m_third_text_button.Bounds().y + m_up_button.Bounds().h;
    const int bottom =
        m_third_text_button.Bounds().y + m_third_text_button.Bounds().h - m_down_button.Bounds().h;
    if (cursor.y < top) cursor.y = top;
    if (cursor.y > bottom) cursor.y = bottom;
    if (bottom <= top) return;
    SetCurrentLine((cursor.y - top) * static_cast<int>(m_lines.size()) / (bottom - top));
}
