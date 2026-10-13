#include <SDL3/SDL_rect.h>
#include <vector>

#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogFactoryDialogs.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "Font.h"
#include "dialog_code/DialogBase.h"

/* The list buttons capture their dialog. Scroll and selection state lives
   on that owner, rather than being recovered from a generic button pointer. */

// FUNCTION: WIZ8 0x005cce70
void W8ListBoxDialog::TextAreaButtonCallback(W8ButtonWidget* button)
{
    const SDL_Point cursor = button->GetMousePosition();
    const int height = g_dialog_interface_font->height();
    if (height == 0) {
        return;
    }
    const int line = (cursor.y - m_area_button.Bounds().y) / height + m_first_visible_line;
    if (line == m_selected_line) {
        m_keep_open = false;
        return;
    }
    SetCurrentLine(line);
}

// FUNCTION: WIZ8 0x005cd1e0
void W8ListBoxDialog::SliderTrackButtonCallback(W8ButtonWidget* button)
{
    SDL_Point cursor = button->GetMousePosition();
    const int top = button->Bounds().y + m_up_button.Bounds().h;
    const int bottom = button->Bounds().y + button->Bounds().h - m_down_button.Bounds().h;
    if (cursor.y < top) {
        cursor.y = top;
    }
    if (cursor.y > bottom) {
        cursor.y = bottom;
    }
    if (bottom <= top) {
        return;
    }
    SetCurrentLine((cursor.y - top) * static_cast<int>(m_lines.size()) / (bottom - top));
}
