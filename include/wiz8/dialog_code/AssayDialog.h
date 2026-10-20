#pragma once

#include <array>
#include <deque>
#include <memory>

#include "compat/compiler.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/rules/item_instance.h"

struct W8Character;
class W8ButtonWidget;
class W8TextBuffer;

enum { W8_ASSAY_BUTTON_COUNT = 0x25, W8_ASSAY_TEXT_BUFFER_COUNT = 5 };

/* Show the profession side first again, as a fresh game does. */
void ResetAssayTab(void);

/* The item information dialog. It keeps the inspected item and the character
   whose stats the requirement lines compare against. */
class W8AssayDialog : public W8DialogBase {
public:
    bool TakeDirty() override;
    W8AssayDialog(const W8ItemInstance& item, W8Character* character, SDL_Point origin);
    virtual ~W8AssayDialog() override;
    virtual void Draw(SDL_Surface& target) override;

private:
    void PopulateText();
    bool PopulateRequirements();
    void CreateTextBuffers();
    void SetProfessionIconsVisible(bool show);
    void SetRaceIconsVisible(bool show);
    void ShowTab(bool professions);
    void ShowPrimaryTab();
    void ShowSecondaryTab();

    std::deque<W8ButtonWidget> m_buttons;
    std::array<std::unique_ptr<W8TextBuffer>, W8_ASSAY_TEXT_BUFFER_COUNT> m_text_buffers;
    W8DialogScrollBar m_scroll_bar{InputLayer(),
                                   {"Data\\Main Interface\\main_scroll.sti",
                                    "Data\\Dialogs\\popup_iteminfo.sti", 4,
                                    [this](int first) { ScrollTextArea(m_text_area, first); }}};
    W8ItemInstance m_item;
    bool m_item_worn;
    W8DialogTextArea m_text_area;
    W8Character* m_character;
};
extern unsigned short g_equip_class_name_ids[32];
