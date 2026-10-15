#pragma once

#include "compat/compiler.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/local_code/ButtonWidget.h"

/* The monster information dialog for one monster location id. */
// VTABLE: WIZ8 0x005ef910
class W8MonsterInfoDialog : public W8DialogBase {
public:
    W8MonsterInfoDialog(int location_id);
    virtual void Draw(SDL_Surface& target) override;
    virtual void OnRightButtonUp() override;
    virtual void OnMouseWheel(int delta) override;

private:
    bool PopulateText();

    int m_location_id;
    W8DialogScrollBar m_scroll_bar{InputLayer(),
                                   {"Data\\Main Interface\\main_scroll.sti",
                                    "Data\\Dialogs\\popup_monsterinfo.sti", 1,
                                    [this](int first) { ScrollTextArea(m_text_area, first); }}};
    W8ButtonWidget m_button{InputLayer()};
    W8DialogTextArea m_text_area;
};

/* The monster and statistic info dialogs' background. */
extern const char* g_info_dialog_background;
