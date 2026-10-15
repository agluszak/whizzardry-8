#pragma once

#include "compat/compiler.h"
#include "wiz8/character_skills.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/dialog_code/DialogScrollBar.h"
#include "wiz8/dialog_code/DialogTextArea.h"
#include "wiz8/local_code/ButtonWidget.h"

/* Shared storage for the attribute-information dialogs and the skill info
   dialog. */
// VTABLE: WIZ8 0x005efc88
class W8StatInfoDialogBase : public W8DialogBase {
public:
    W8StatInfoDialogBase();
    virtual void Draw(SDL_Surface& target) override;
    bool TakeDirty() override;
    virtual void OnRightButtonUp() override;
    virtual void OnMouseWheel(int delta) override;
    virtual void DrawTitle(SDL_Surface& target);
    virtual bool PopulateText();

protected:
    void Configure();
    W8DialogScrollBar scrollbar{InputLayer(),
                                {"Data\\Main Interface\\main_scroll.sti",
                                 "Data\\Dialogs\\popup_monsterinfo.sti", 1,
                                 [this](int first) { ScrollTextArea(textarea, first); }}};
    W8ButtonWidget button{InputLayer()};
    W8DialogTextArea textarea;
    unsigned int m_title_id;  /* g_localized_strings index, DrawTitle */
    unsigned int m_detail_id; /* g_localized_strings index, PopulateText */
};

// VTABLE: WIZ8 0x005efcc8
class W8AttributeInfoDialog : public W8StatInfoDialogBase {
public:
    W8AttributeInfoDialog(W8Attribute uiIndex);

private:
    W8Attribute m_uiIndex;
};

// VTABLE: WIZ8 0x005efd48
class W8SecondaryAttributeInfoDialog : public W8StatInfoDialogBase {
public:
    W8SecondaryAttributeInfoDialog(unsigned int uiIndex);

private:
    unsigned int m_uiIndex;
};

// VTABLE: WIZ8 0x005efd08
class W8SkillInfoDialog : public W8StatInfoDialogBase {
public:
    W8SkillInfoDialog(W8Skill skill, bool first, bool second, bool bonus);

protected:
    virtual bool PopulateText() override;

private:
    W8Skill m_skill;
    bool m_first;
    bool m_second;
    bool m_bonus;
};

/* GppStringList indices naming each attribute/resistance row. */
extern unsigned short g_attr_table1[18];
