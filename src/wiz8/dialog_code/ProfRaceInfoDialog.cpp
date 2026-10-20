#include "wiz8/dialog_code/ProfRaceInfoDialog.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "dialog_code/DialogBase.h"
#include "dialog_code/DialogScrollBar.h"
#include "dialog_code/DialogTextArea.h"
#include "local_code/ControlsRect.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/fonts.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/platform/assertion.h"

W8AttributeMinimums g_race_attribute_minimums[W8_RACE_PLAYABLE_COUNT] = {
    {{45, 45, 45, 45, 45, 45, 45}}, {{35, 50, 50, 35, 50, 45, 40}}, {{55, 30, 50, 60, 35, 35, 35}},
    {{35, 50, 40, 50, 50, 35, 45}}, {{40, 40, 30, 45, 55, 50, 50}}, {{25, 55, 35, 30, 50, 60, 45}},
    {{60, 25, 25, 70, 40, 50, 30}}, {{55, 35, 30, 60, 50, 40, 30}}, {{40, 40, 30, 35, 50, 60, 50}},
    {{40, 30, 55, 50, 40, 40, 50}}, {{50, 50, 25, 50, 35, 35, 55}},
};

W8AttributeMinimums g_profession_attribute_minimums[W8_PROFESSION_COUNT] = {
    {{55, 0, 0, 50, 50, 0, 0}},   {{55, 0, 55, 55, 50, 50, 0}}, {{50, 0, 55, 55, 50, 50, 0}},
    {{50, 50, 0, 50, 55, 0, 55}}, {{50, 55, 0, 50, 55, 55, 0}}, {{50, 50, 0, 50, 55, 55, 50}},
    {{0, 50, 50, 0, 50, 55, 55}}, {{0, 0, 0, 0, 55, 50, 50}},   {{45, 55, 0, 0, 60, 0, 55}},
    {{45, 50, 0, 0, 55, 0, 55}},  {{0, 0, 60, 55, 0, 0, 0}},    {{0, 55, 0, 0, 60, 0, 0}},
    {{0, 55, 55, 0, 55, 0, 55}},  {{0, 55, 0, 0, 0, 0, 60}},    {{0, 60, 0, 0, 55, 0, 0}},
};

/* Dialog Code\ProfRaceInfoDialog.cpp. The profession and race information
   dialogs, whose constructors are the same body twice over: they differ in
   their vtable, their row table, their two string ids, their assertion line and
   how they fill the attribute minimums. Both are named by their own assertion,
   which also supplies the parameter name uiIndex and pins PROF_COUNT to fifteen
   and RACE_COUNT to sixteen. */

static const char* g_popup_race_profession_path = "Data\\Dialogs\\popup_race_profession.sti";

enum { ATTR_COUNT = 7 };

/* Only the first eleven races have their own minimums; the rest are shown as
   -1, which is the dialog's "no requirement" marker. */

W8ProfRaceInfoRow g_profession_info_rows[W8_PROFESSION_COUNT] = {
    {676, 691, 10}, {677, 692, 24}, {678, 693, 8},  {679, 694, 2},  {680, 695, 16},
    {681, 696, 20}, {682, 697, 12}, {683, 698, 28}, {684, 699, 6},  {685, 700, 0},
    {686, 701, 18}, {687, 702, 22}, {688, 703, 4},  {689, 704, 14}, {690, 705, 26},
};

W8ProfRaceInfoRow g_race_info_rows[W8_RACE_COUNT] = {
    {644, 660, 8},  {645, 661, 4},  {646, 662, 12}, {647, 663, 16}, {648, 664, 14}, {649, 665, 20},
    {650, 666, 0},  {651, 667, 10}, {652, 668, 2},  {653, 669, 18}, {654, 670, 6},  {655, 671, 30},
    {656, 672, 24}, {657, 673, 22}, {658, 674, 26}, {659, 675, 28},
};

W8ProfRaceInfoDialogBase::W8ProfRaceInfoDialogBase(CatalogArtwork artwork)
    : m_uiArtwork(std::move(artwork))
{
    SetOrigin(0x85, 0x69);
    SetExtent(0x177, 0x10e);
    SetBackground(g_popup_race_profession_path, 0);
    UseInfoPopup({m_text_area, m_scroll_bar, &m_button});
}

void W8ProfRaceInfoDialogBase::Configure()
{
    if (!PopulateText()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }

    int x = m_x;
    m_scroll_bar.SetLayout(x + 0x159, m_y + 0x29, m_text_area.GetTotalLineCount(), 0,
                           m_text_area.GetLineHeight(), 0xb8);

    CreateCloseButton(m_button, 0x14f, 0xe8);
    return;
}

void W8ProfRaceInfoDialogBase::Draw(SDL_Surface& target)
{

    W8DialogBase::Draw(target);
    m_uiArtwork.draw(target, m_uiSummaryImage, m_x + 0xd, m_y + 0xd);
    DrawTextLine(target, g_W8TextBufferAlignLeft, 0x25, 0xb, 0x147, 0x18,
                 Data().strings[m_uiNameId], g_options_detail_font);
    DrawTextLine(target, g_W8TextBufferAlignCenter, 0xd, 0x29, 0x8e, 0xc,
                 Data().strings[m_uiHeadingId], g_wiz_text_font_secondary);
    int top = 0x37;
    for (int attribute = 0; attribute < ATTR_COUNT; ++attribute) {
        DrawTextLine(target, g_W8TextBufferAlignLeft, 0x10, top, 0x74, 0xc,
                     Data().strings[g_character_description_first_ids[attribute]],
                     g_wiz_text_font_secondary);
        std::string minimum;
        if (m_minimums[attribute] == -1) {
            minimum = "?";
        } else {
            minimum = fmt::format("{:d}", m_minimums[attribute]);
        }
        DrawTextLine(target, g_W8TextBufferAlignCenter, 0x85, top, 0x16, 0xc, minimum,
                     g_wiz_text_font_secondary);
        top += 0xe;
    }

    m_text_area.Paint(target);
    m_scroll_bar.Draw(target);
    m_button.Draw(target);
}

void W8ProfRaceInfoDialogBase::DrawTextLine(SDL_Surface& target, unsigned int layout_mode, int left,
                                            int top, int width, int height, std::string_view text,
                                            Font& font)
{
    W8TextBuffer buffer;
    buffer.SetLayoutMode(g_W8TextBufferAlignMiddle | layout_mode);
    W8ControlsRect bounds;
    bounds.left = m_x + left;
    bounds.right = bounds.left + width;
    bounds.top = m_y + top;
    bounds.bottom = bounds.top + height;
    buffer.RenderString(&bounds, text, font, false, target);
}

bool W8ProfRaceInfoDialogBase::PopulateText()
{
    W8ControlsRect bounds;
    bounds.left = m_x + 0xa2;
    bounds.top = m_y + 0x29;
    bounds.right = m_x + 0x151;
    bounds.bottom = m_y + 0xe1;
    m_text_area.Configure(&bounds, g_wiz_text_font_secondary, 0);
    m_text_area.SetEntrySpacing(0);
    return true;
}

/* Copies the row straight out of the table: every profession has minimums, so
   there is no per-entry test the way the race dialog needs one. */
W8ProfessionInfoDialog::W8ProfessionInfoDialog(W8Profession uiIndex)
    : W8ProfRaceInfoDialogBase(CatalogArtwork::Load(0x10b))
{
    if (uiIndex >= W8_PROFESSION_COUNT) {
        Fail("uiIndex < PROF_COUNT");
    }
    m_uiIndex = uiIndex;
    m_uiHeadingId = 0x14c;
    m_uiSummaryImage = g_profession_info_rows[uiIndex].image_id;
    m_uiNameId = g_profession_info_rows[uiIndex].name_id;
    m_uiDetailId = g_profession_info_rows[uiIndex].detail_id;
    for (int attribute = 0; attribute < ATTR_COUNT; ++attribute) {
        m_minimums[attribute] = g_profession_attribute_minimums[uiIndex].values[attribute];
    }
    Configure();
}

bool W8ProfessionInfoDialog::PopulateText()
{
    W8ProfRaceInfoDialogBase::PopulateText();
    m_text_area.AddEntry(Data().strings[0x153], Data().strings[m_uiDetailId], 10, 0xf, 0);
    m_text_area.AddEntry({}, "", 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x14d], "", 10, 0xf, 0);
    unsigned int index;
    for (index = 0; index < 3; ++index) {
        W8Trait ability = g_profession_abilities[m_uiIndex].ability_ids[index];
        if (ability == W8_TRAIT_NONE) {
            break;
        }
        m_text_area.AddEntry({}, Data().strings[g_character_trait_name_ids[ability]], 10, 0xf, 0);
    }
    m_text_area.AddEntry({}, "", 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x14e], "", 10, 0xf, 0);
    m_text_area.AddEntry(
        {}, Data().strings[g_character_skill_name_ids[g_profession_bonus_skills[m_uiIndex]]], 10,
        0xf, 0);
    m_text_area.AddEntry({}, "", 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x14f], "", 10, 0xf, 0);
    for (index = 0; index < 4; ++index) {
        W8Skill skill = g_profession_skills[m_uiIndex][index];
        if (skill == W8_SKILL_NONE) {
            break;
        }
        m_text_area.AddEntry({}, Data().strings[g_character_skill_name_ids[skill]], 10, 0xf, 0);
    }
    m_text_area.AddEntry({}, "", 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x150], "", 10, 0xf, 0);
    for (index = 0; index < 6; ++index) {
        int item = g_starting_equipment[m_uiIndex][index];
        if (item != -1) {
            m_text_area.AddEntry({}, Data().items[item].display_name, 10, 0xf, 0);
        }
    }
    std::string first;
    std::string second;
    switch (m_uiIndex) {
    case 10:
        second = Data().items[82].display_name;
        first = Data().items[22].display_name;
        break;
    case 0:
        second = Data().items[7].display_name;
        first = Data().items[18].display_name;
        break;
    case 8:
        m_text_area.AddEntry({}, Data().items[599].display_name, 10, 0xf, 0);
        return true;
    case 9:
        m_text_area.AddEntry({}, Data().items[324].display_name, 10, 0xf, 0);
        return true;
    default:
        return true;
    }
    m_text_area.AddEntry({}, fmt::sprintf(Data().strings[0x151], first, second), 10, 0xf, 0);
    return true;
}

/* The same shape, except that only the first eleven races have minimums and the
   rest show -1, so the copy is a counted loop with the test inside it rather
   than the profession dialog's straight block move. */
W8RaceInfoDialog::W8RaceInfoDialog(W8Race uiIndex)
    : W8ProfRaceInfoDialogBase(CatalogArtwork::Load(0x10c))
{
    if (uiIndex >= W8_RACE_COUNT) {
        Fail("uiIndex < RACE_COUNT");
    }
    m_uiIndex = uiIndex;
    m_uiHeadingId = 0x152;
    m_uiSummaryImage = g_race_info_rows[uiIndex].image_id;
    m_uiNameId = g_race_info_rows[uiIndex].name_id;
    m_uiDetailId = g_race_info_rows[uiIndex].detail_id;
    for (unsigned int attribute = 0; attribute < ATTR_COUNT; ++attribute) {
        if (uiIndex < W8_RACE_PLAYABLE_COUNT) {
            m_minimums[attribute] = g_race_attribute_minimums[uiIndex].values[attribute];
        } else {
            m_minimums[attribute] = -1;
        }
    }
    Configure();
}

bool W8RaceInfoDialog::PopulateText()
{
    W8ProfRaceInfoDialogBase::PopulateText();
    m_text_area.AddEntry(Data().strings[0x153], Data().strings[m_uiDetailId], 10, 0xf, 0);
    m_text_area.AddEntry({}, "", 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x14d], "", 10, 0xf, 0);
    bool listed = false;
    for (auto ability : g_race_abilities[m_uiIndex].ability_ids) {
        if (ability == W8_TRAIT_NONE) {
            if (!listed) {
                m_text_area.AddEntry({}, Data().strings[0x154], 10, 0xf, 0);
            }
            return true;
        }
        m_text_area.AddEntry({}, Data().strings[g_character_trait_name_ids[ability]], 10, 0xf, 0);
        listed = true;
    }
    return true;
}
