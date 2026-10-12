#pragma once

#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/engine_code/game_timer.h"
#include "compat/compiler.h"

/* Dialog Code\SpellInfoDialog.cpp. The constructor stores the spell id; Draw
   and DrawLabels index g_spell_records with it. CreateControls, the close
   callback and OnMouseWheel sit with the other popup-info dialogs. */
// VTABLE: WIZ8 0x005efab0
class W8SpellInfoDialog : public W8DialogBase {
public:
    W8SpellInfoDialog(unsigned int spell);
    virtual int CreateControls() override;
    virtual void Draw(SDL_Surface& target) override;
    virtual void OnRightButtonUp() override;
    virtual void OnMouseWheel(int delta) override;

private:
    bool PopulateText();
    void DrawLabels(SDL_Surface& target);
    static void ScrollCallback(W8DialogScrollBar* scroll_bar, int first_visible_entry);

    unsigned int m_spell;
    W8DialogScrollBar m_scroll_bar{InputLayer()};
    W8ButtonWidget m_button{InputLayer()};
    W8DialogTextArea m_text_area;
    W8GameTimer m_timer;
    unsigned int m_animation_frame;
};

/* The "(on ...)" parenthetical per spell target type. */
extern const char* g_spell_target_parentheticals[11];

/* The ", " separator join lists of notices are built with. */
extern char g_comma_space[];

/* The "%d %s" count-and-name notice format. */

/* GppStringList indices naming each W8RangeCategory band. */
extern unsigned short g_spell_range_name_ids[4];
