#include "wiz8/dialog_code/StatInfoDialogs.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <fmt/printf.h>

#include "character_skills.h"
#include "dialog_code/DialogBase.h"
#include "dialog_code/DialogScrollBar.h"
#include "dialog_code/DialogTextArea.h"
#include "local_code/ControlsRect.h"
#include "surrender/srDebug.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/dialog_code/MonsterInfoDialog.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/character.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_screens/CharacterScreen.h"

/* Dialog Code\StatInfoDialogs.cpp. Two attribute-info dialogs whose
   constructors are the same body twice over: the canonical pair differs only
   in its vtable, its two lookup tables and its assertion line. Both are named
   by their own assertion, which also supplies the parameter name uiIndex and
   pins ATTR_COUNT to seven - the same seven attributes W8Character carries.
   The shared base holds the scroll bar, close button and text area every
   info dialog in this family reuses; its own vtable is emitted retail even
   though it is only ever a base subobject. */

enum { ATTR_COUNT = 7 };
enum { SKILL_COUNT = 0x29 };

static unsigned short g_attr_table0[8] = {
    0x6a0, 0x6a1, 0x6a2, 0x6a3, 0x6a4, 0x6a5, 0x6a6, 0,
};
unsigned short g_attr_table1[18] = {
    0x6a7, 0x6a8, 0x6a9, 0x6aa, 0x6ab, 0,     0x30b, 0x30c, 0x30d,
    0x30e, 0x30f, 0x310, 0x311, 0x312, 0x313, 0x314, 0x315, 0x316,
};

W8StatInfoDialogBase::W8StatInfoDialogBase()
{
    SetOrigin(0x9c, 0x69);
    SetExtent(0x14a, 0x10e);
    SetBackground(g_info_dialog_background, 0);
    UseInfoPopup({textarea, scrollbar, &button});
}

void W8StatInfoDialogBase::Configure()
{
    if (!PopulateText()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }

    scrollbar.SetLayout(m_x + 299, m_y + 0x26, textarea.GetTotalLineCount(), 0,
                        textarea.GetLineHeight(), 0xb9);

    CreateCloseButton(button, 0x11a, 0xe6);
    return;
}

void W8StatInfoDialogBase::Draw(SDL_Surface& target)
{

    W8DialogBase::Draw(target);
    DrawTitle(target);

    textarea.Paint(target);
    scrollbar.Draw(target);
    button.Draw(target);
}

void W8StatInfoDialogBase::DrawTitle(SDL_Surface& target)
{
    g_wiz_text_font_secondary.resetPalette();
    const std::string& title = g_localized_strings[m_title_id];
    std::int16_t width = g_wiz_text_font_secondary.textWidth(title);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0xe + (0x112 - width) / 2, m_y + 0x11},
             title);
}

bool W8StatInfoDialogBase::PopulateText()
{
    W8ControlsRect bounds;
    bounds.left = m_x + 0x11;
    bounds.top = m_y + 0x26;
    bounds.right = m_x + 0x11f;
    bounds.bottom = m_y + 0xdf;
    textarea.Configure(&bounds, g_wiz_text_font_secondary, 0);
    textarea.SetEntrySpacing(0);
    textarea.AddEntry(g_localized_strings[0x155], g_localized_strings[m_detail_id], 10, 0xf, 0);
    textarea.AddEntry({}, "", 10, 0xf, 0);
    return true;
}

W8AttributeInfoDialog::W8AttributeInfoDialog(W8Attribute uiIndex)
{
    if (static_cast<unsigned int>(uiIndex) >= ATTR_COUNT) {
        Fail("uiIndex < ATTR_COUNT");
    }
    m_uiIndex = uiIndex;
    m_title_id = g_character_description_first_ids[uiIndex];
    m_detail_id = g_attr_table0[uiIndex];
    Configure();
}

W8SkillInfoDialog::W8SkillInfoDialog(W8Skill skill, bool first, bool second, bool bonus)
{
    if (static_cast<unsigned int>(skill) >= SKILL_COUNT) {
        Fail("uiIndex < SKILL_COUNT");
    }
    m_skill = skill;
    m_title_id = g_character_skill_name_ids[skill];
    m_detail_id = g_character_skill_name_ids[skill + 0x2a];
    m_first = first;
    m_second = second;
    m_bonus = bonus;
    Configure();
}

bool W8SkillInfoDialog::PopulateText()
{
    W8StatInfoDialogBase::PopulateText();
    textarea.AddEntry(g_localized_strings[0x156], "", 10, 0xf, 0);
    W8SkillAttributes* skill = &g_skill_attributes[m_skill];
    textarea.AddEntry(
        {}, g_localized_strings[g_character_description_first_ids[skill->attribute_1]], 10, 0xf, 0);
    if (skill->attribute_1 != skill->attribute_2) {
        textarea.AddEntry(
            {}, g_localized_strings[g_character_description_first_ids[skill->attribute_2]], 10, 0xf,
            0);
    }
    if (m_first) {
        textarea.AddEntry({}, "", 10, 0xf, 0);
        textarea.AddEntry({}, g_localized_strings[0x157], 10, 5, 0);
    }
    if (m_second) {
        textarea.AddEntry({}, "", 10, 0xf, 0);
        textarea.AddEntry({}, g_localized_strings[0x158], 10, 0xb, 0);
    }
    if (m_bonus) {
        textarea.AddEntry({}, "", 10, 0xf, 0);
        textarea.AddEntry({}, fmt::sprintf(g_localized_strings[0x159], 0x19), 10, 3, 0);
    }
    return true;
}

W8SecondaryAttributeInfoDialog::W8SecondaryAttributeInfoDialog(unsigned int uiIndex)
{
    if (uiIndex >= ATTR_COUNT) {
        Fail("uiIndex < ATTR_COUNT");
    }
    m_uiIndex = uiIndex;
    m_title_id = g_character_description_first_ids[16 + uiIndex];
    m_detail_id = g_attr_table1[uiIndex];
    Configure();
}
