#pragma once

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "compat/compiler.h"

/* The monster information dialog for one monster location id. */
// VTABLE: WIZ8 0x005ef910
class W8MonsterInfoDialog : public W8DialogBase {
public:
    W8MonsterInfoDialog(int location_id);
    virtual int CreateControls() override;
    virtual void Draw() override;
    virtual void OnRightButtonUp() override;
    virtual void OnMouseWheel(int delta) override;

private:
    bool PopulateText();
    static void ScrollCallback(W8DialogScrollBar* scroll_bar, int first_visible_entry);

    int m_location_id;
    W8DialogScrollBar m_scroll_bar{InputLayer()};
    W8ButtonWidget m_button{InputLayer()};
    W8DialogTextArea m_text_area;
};

/* The monster and statistic info dialogs' background. */
extern const char* g_info_dialog_background;
