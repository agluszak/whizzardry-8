#include "wiz8/dialog_code/SpellInfoDialog.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "dialog_code/DialogScrollBar.h"
#include "dialog_code/DialogTextArea.h"
#include "local_code/ControlsRect.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/records.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/fonts.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/string_database.h"

const char* g_spell_info_background_path = "Data\\Dialogs\\popup_spellinfo.sti";
const char* g_spell_effect_database_path = "Data\\Databases\\SpellEffect.dbs";
const char* g_spell_desc_database_path = "Data\\Databases\\SpellDesc.dbs";
char g_comma_space[] = ", ";
static unsigned short g_spellbook_name_ids[4] = {791, 792, 793, 794};
static unsigned short g_spell_usage_name_ids[5] = {795, 797, 796, 796, 796};
static unsigned short g_spell_target_type_name_ids[11] = {798, 799, 800, 801, 802, 803,
                                                          804, 805, 806, 807, 807};
static char g_spell_target_mark4[] = "\xef\xbf\xb4";
static char g_spell_target_mark0[] = "\xef\xbf\xb0";
static char g_spell_target_mark1[] = "\xef\xbf\xb1";
static char g_spell_target_mark2[] = "\xef\xbf\xb2";
static char g_spell_target_mark3[] = "\xef\xbf\xb3";
const char* g_spell_target_parentheticals[11] = {
    g_spell_target_mark4, g_spell_target_mark2, g_spell_target_mark4, g_spell_target_mark2,
    g_spell_target_mark3, g_spell_target_mark1, g_spell_target_mark0, g_spell_target_mark4,
    g_spell_target_mark0, g_spell_target_mark4, g_spell_target_mark4,
};
unsigned short g_spell_range_name_ids[4] = {1307, 1308, 1309, 1310};
static char g_plus_space[] = "+ ";

W8SpellInfoDialog::W8SpellInfoDialog(unsigned int spell)
    : m_spell(spell), m_timer(0.05f, 1), m_animation_frame(0)
{
    SetOrigin(0x9c, 0x5a);
    SetExtent(0x14a, 0x12c);
    SetBackground(g_spell_info_background_path, 0);
    UseInfoPopup({m_text_area, m_scroll_bar, &m_button});
    if (!PopulateText()) {
        throw std::runtime_error("Cannot configure dialog controls");
    }

    int x = m_x;
    m_scroll_bar.SetLayout(x + 0x12b, m_y + 0x43, m_text_area.GetTotalLineCount(), 0,
                           m_text_area.GetLineHeight(), 0xb9);

    CreateCloseButton(m_button, 0x11a, 0x104);
}

void W8SpellInfoDialog::Draw(SDL_Surface& target)
{
    W8DialogBase::Draw(target);
    DrawLabels(target);
    m_text_area.Paint(target);
    m_scroll_bar.Draw(target);
    m_button.Draw(target);
    const auto& animation = g_spell_realm_animations[Data().spells[m_spell].realm];
    CatalogArtwork::Load(animation.image, 0).draw(target, m_animation_frame, m_x + 0xe, m_y + 0xe);
}

bool W8SpellInfoDialog::PopulateText()
{
    W8ControlsRect bounds;
    const W8SpellRuntimeRecord* record;
    std::string text;
    unsigned int spellbook_mask;
    unsigned int book;
    int count;
    W8SpellTargetType target_type;
    int duration_per_level;
    int duration_base;
    int ui_units;
    int ui_units_lvl;
    int display;
    int display_base;

    bounds.left = m_x + 0x11;
    bounds.top = m_y + 0x43;
    bounds.right = m_x + 0x11f;
    bounds.bottom = m_y + 0xfc;
    m_text_area.Configure(&bounds, g_wiz_text_font_secondary, 0);
    m_text_area.SetEntrySpacing(1);

    record = &Data().spells[m_spell];
    text.clear();
    spellbook_mask = static_cast<unsigned int>(record->wizardry_spell != 0) |
                     (record->divinity_spell != 0 ? W8_SPELLBOOK_DIVINITY : W8_SPELLBOOK_NONE) |
                     (record->alchemy_spell != 0 ? W8_SPELLBOOK_ALCHEMY : W8_SPELLBOOK_NONE) |
                     (record->psionics_spell != 0 ? W8_SPELLBOOK_PSIONICS : W8_SPELLBOOK_NONE);
    count = 0;
    for (book = 0; book < 4; ++book) {
        if ((spellbook_mask & (1 << book)) != 0) {
            if (count > 0) {
                text += g_comma_space;
            }
            text += Data().strings[g_spellbook_name_ids[book]];
            ++count;
        }
    }
    m_text_area.AddEntry(Data().strings[0x11c], text, 10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x11d],
                         Data().strings[g_spell_usage_name_ids[record->usable_when]], 10, 0xf, 0);

    target_type = GetSpellTargetType(m_spell, false);
    m_text_area.AddEntry(Data().strings[0x11e],
                         fmt::format("{} {}",
                                     Data().strings[g_spell_target_type_name_ids[target_type]],
                                     g_spell_target_parentheticals[target_type]),
                         10, 0xf, 0);
    m_text_area.AddEntry(Data().strings[0x11f],
                         Data().strings[g_spell_range_name_ids[record->range_category]], 10, 0xf,
                         0);

    if (record->show_effect_dice != 0 &&
        (record->effect_dice.base != 0 || record->effect_dice.count != 0)) {
        if (record->effect_dice.count == 0) {
            m_text_area.AddEntry(Data().strings[0x120],
                                 fmt::format("{:d} {}", static_cast<int>(record->effect_dice.base),
                                             Data().strings[0x121]),
                                 10, 0xf, 0);
        } else {
            m_text_area.AddEntry(
                Data().strings[0x120],
                fmt::format("{:d}-{:d} {}",
                            record->effect_dice.count + static_cast<int>(record->effect_dice.base),
                            record->effect_dice.Maximum(), Data().strings[0x121]),
                10, 0xf, 0);
        }
    }

    duration_per_level = record->duration_per_level;
    duration_base = record->duration;
    if (duration_per_level != 0 || duration_base != 0) {
        text.clear();
        ui_units = 0;
        display = 0;
        if (duration_per_level > 0) {
            if (duration_per_level == 9999) {
                ui_units = 0x129;
                display = 0;
            } else if (duration_per_level * 10 < 300) {
                ui_units = 0x123;
                display = duration_per_level;
            } else if (duration_per_level * 0x78 < 0x3840) {
                display = (duration_per_level * 10) / 0x3c;
                ui_units = 0x125;
            } else {
                display = (duration_per_level * 0x78) / 0xe10;
                ui_units = 0x127;
            }
            if (display > 0) {
                text += fmt::format("{:d} ", display);
            }
            if (duration_base > 0) {
                text += g_plus_space;
            }
        }
        ui_units_lvl = ui_units;
        display_base = 0;
        if (duration_base > 0) {
            if (duration_base == 9999) {
                ui_units_lvl = 0x129;
                display_base = 0;
            } else if (duration_base * 10 < 300) {
                ui_units_lvl = 0x123;
                display_base = duration_base;
            } else if (duration_base * 0x78 < 0x3840) {
                display_base = (duration_base * 10) / 0x3c;
                ui_units_lvl = 0x125;
            } else {
                display_base = (duration_base * 0x78) / 0xe10;
                ui_units_lvl = 0x127;
            }
            text += fmt::format("{:d} ", display_base);
        }
        if ((duration_per_level == 1 && display_base == 0) ||
            (duration_per_level == 0 && display_base == 1)) {
            if (ui_units_lvl == 0x123) {
                ui_units_lvl = 0x124;
            } else if (ui_units_lvl == 0x125) {
                ui_units_lvl = 0x126;
            } else if (ui_units_lvl == 0x127) {
                ui_units_lvl = 0x128;
            }
        }
        text += Data().strings[ui_units_lvl];
        if (display_base > 0) {
            text += Data().strings[0x12a];
        }
        m_text_area.AddEntry(Data().strings[0x122], text, 10, 0xf, 0);
    }

    text.clear();
    if ((!(text = GetStringFromStringDatabase(g_spell_effect_database_path, m_spell, 0, 0))
              .empty()) != 0 &&
        text[0] != '\0') {
        m_text_area.AddEntry(Data().strings[0x12b], text, 10, 0xf, 0);
    }
    text.clear();
    if ((!(text = GetStringFromStringDatabase(g_spell_desc_database_path, m_spell, 0, 0))
              .empty()) != 0 &&
        text[0] != '\0') {
        m_text_area.AddEntry(Data().strings[0x12c], text, 10, 0xf, 0);
    }
    return true;
}

void W8SpellInfoDialog::DrawLabels(SDL_Surface& target)
{
    std::string text;
    std::int16_t width;
    const W8SpellRuntimeRecord* record = &Data().spells[m_spell];

    g_wiz_text_font_secondary.resetPalette();
    text = record->display_name;
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0x25 + (0xe7 - width) / 2, m_y + 0x12},
             text);
    text = Data().strings[0x118];
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0x25 + (0x53 - width) / 2, m_y + 0x24},
             text);
    if (record->power_class == W8_SPELL_POWER_FIXED) {
        text = Data().strings[0x11a];
    } else {
        text = Data().strings[0x119];
    }
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0x25 + (0x7b - width) / 2, m_y + 0x33},
             text);
    text = fmt::format("{:d}", record->spell_level);
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0x7c + (0x16 - width) / 2, m_y + 0x24},
             text);
    text = fmt::format("{:d} {}", record->spell_point_cost, Data().strings[0x11b]);
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(target, g_wiz_text_font_secondary, {m_x + 0xa4 + (0x25 - width) / 2, m_y + 0x33},
             text);
}

bool W8SpellInfoDialog::ProcessInput()
{
    const int steps = static_cast<int>(m_timer.GetProgress());
    if (steps > 0) {
        const auto& animation = g_spell_realm_animations[Data().spells[m_spell].realm];
        m_animation_frame = (m_animation_frame + steps) % animation.frame_count;
        m_dirty = true;
    }
    return W8DialogBase::ProcessInput();
}
