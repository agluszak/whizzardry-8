#include "wiz8/local_screens/RCSStatsPage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>

#include "glm/ext/vector_int2.hpp"
#include "local_code/RangeControl.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/combat/attack.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/character_tables.h"
#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/skills.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/platform/input.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/character_derivation.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/screen.h"
#include "wiz8/text_input.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"

/* The five skill-category blocks shared by the camp skills page: (x, y)
   origins, the row count that bounds each region, and the catalog frame each
   header draws. Category 4 sits below the right-hand column; the others fill
   the left and right columns. */
static glm::ivec2 g_camp_skill_category_positions[5] = {
    {0x160, 0x8}, {0x160, 0xcc}, {0x22, 0x9}, {0x22, 0x86}, {0x2a, 0x21},
};
static constexpr std::array<W8SkillCategory, 5> g_camp_skill_categories = {
    W8_SKILL_CATEGORY_WEAPON, W8_SKILL_CATEGORY_MISCELLANEOUS, W8_SKILL_CATEGORY_COMBAT,
    W8_SKILL_CATEGORY_MAGIC,  W8_SKILL_CATEGORY_EXPERT,
};
static int g_camp_skill_category_rows[5] = {0xb, 5, 8, 0xa, 7};
static int g_camp_skill_category_images[5] = {2, 3, 0, 1, 4};

static glm::ivec2 g_character_page2_category_geometry[5] = {
    {0xf9, 0x0a}, {0xf9, 0xcd}, {0x22, 0x0a}, {0x22, 0x87}, {0xf9, 0x120},
};
static int g_character_page2_category_frames[5] = {2, 3, 0, 1, 4};

struct W8PortraitGroup {
    int count;
    int portraits[14];
};
static W8PortraitGroup g_portrait_groups[12] = {
    {14, {0, 1, 2, 3, 76, 4, 5, 6, 7, 8, 9, 77, 10, 11}},
    {8, {12, 13, 14, 78, 15, 16, 17, 79}},
    {6, {18, 19, 20, 21, 22, 23}},
    {4, {24, 25, 26, 27}},
    {4, {28, 29, 30, 31}},
    {4, {32, 33, 34, 35}},
    {4, {36, 37, 38, 39}},
    {4, {40, 41, 42, 43}},
    {4, {44, 45, 46, 47}},
    {4, {48, 49, 50, 51}},
    {4, {52, 53, 54, 55}},
    {2, {56, 57}},
};

// FUNCTION: WIZ8 0x005c4540
W8CampStatsControls::W8CampStatsControls(W8CampStatsPage& owner) : page(owner)
{
    constexpr int y[] = {0xbe, 0xd6, 0xf3};
    for (int index = 0; index < 3; ++index) {
        const int frame = index * 5;
        auto& button = m_buttons[index];
        button = std::make_unique<W8ButtonWidget>(input);
        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0x145, 0).spriteSheet(), frame + 3, frame,
                                 frame + 2, index == 2 ? 15 : frame + 1,
                                 index == 2 ? 17 : frame + 4),
            {.on_click =
                 [this, index](auto*) { AfterUiDispatch([this, index] { SelectFilter(index); }); },
             .policy = {.primary_toggle = ButtonToggle::Release,
                        .input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = 0x954 + index,
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        button->SetPosition(0x13c, y[index]);
    }
    m_buttons[2]->SetPressed(page.effect_items_only);
    input.add(SDL_Rect{0x15d, 0xbe, 260, 248}, [this](const UiPointerEvent& event) {
        if (event.action != UiPointerAction::Wheel) return UiResult::Pass;
        page.stats_range->AdjustValue(event.wheel);
        return UiResult::Handled;
    });
}

void W8CampStatsControls::InvalidateAll()
{
    for (const auto& button : m_buttons) button->m_dirty = true;
}

void W8CampStatsControls::Redraw(SDL_Surface& target, bool force)
{
    for (const auto& button : m_buttons) {
        button->m_dirty |= force;
        button->Refresh();
    }
}

void W8CampStatsControls::SelectFilter(int index)
{
    if (index == 0) {
        if (m_buttons[0]->IsPressed()) {
            m_buttons[1]->SetPressed(false);
            page.effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        } else {
            page.effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        }
    } else if (index == 1) {
        if (m_buttons[1]->IsPressed()) {
            m_buttons[0]->SetPressed(false);
            page.effect_filter = W8_CAMP_EFFECT_FILTER_DETRIMENTAL;
        } else {
            page.effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        }
    } else {
        page.effect_items_only = m_buttons[2]->IsPressed();
    }
    page.FilterCampEffectList();
    page.InvalidateEffectList();
}

/* The stats page's full redraw: attribute rows with their base/effective
   delta bars, the profession's bonus skill, the trait grid, then the effect
   list region when its redraw flag is up. */
// FUNCTION: WIZ8 0x005c48b0
void W8CampStatsPage::DrawCampStatsPage(bool full)
{
    const auto& data = camp.game.rules().data;
    const auto& character = *camp.reviewed;
    std::string formatted;
    if (full) {
        CatalogArtwork::Load(0x142, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0xa5);
        constexpr int origin_x = 0;
        constexpr int origin_y = 0xa5;
        int index;
        std::string text = data.strings[0x937];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                 {origin_x + 10 + ((0x11f - g_wiz_text_font_secondary.textWidth(text)) >> 1),
                  origin_y + 10},
                 text);
        int row_y = 0xbf;
        for (index = 0; index < 7; ++index) {
            const std::string& label = data.strings[g_attribute_label_ids[index]];
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {origin_x + 10 + ((0x7b - g_wiz_text_font_secondary.textWidth(label)) >> 1),
                      row_y - 0xa6 + origin_y},
                     label);
            unsigned int effective = character.attributes[index].effective;
            DrawCampValueBar(effective, character.attributes[index].base, 0x88, row_y);
            formatted = fmt::format("{:d}", effective);
            DrawText(
                GetFrameBufferSurface(), g_wiz_text_font_secondary,
                {origin_x + 0x108 + ((0x21 - g_wiz_text_font_secondary.textWidth(formatted)) >> 1),
                 row_y - 0xa6 + origin_y},
                formatted);
            row_y += 0xe;
        }
        text = data.strings[0x938];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                 {origin_x + 10 + ((0x11f - g_wiz_text_font_secondary.textWidth(text)) >> 1),
                  origin_y + 0x8d},
                 text);
        formatted = fmt::format(
            "{} {}",
            data.strings
                [g_character_skill_name_ids[g_profession_bonus_skills[character.iProfession]]],
            data.strings[0x8c5]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x10, 0x142}, formatted);
        int trait_count = 0;
        bool traits[0x20];
        for (index = 0; index < 0x20; ++index) {
            if (CharacterHasTrait(&character, static_cast<W8Trait>(index))) {
                traits[index] = true;
                ++trait_count;
            } else {
                traits[index] = false;
            }
        }
        int step = (trait_count < 8) ? 2 : 0;
        int trait_y = step + 0x14e;
        for (index = 0; index < 0x20; ++index) {
            if (traits[index]) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x10, trait_y},
                         data.strings[g_character_trait_name_ids[index]]);
                trait_y += step + 0xc;
            }
        }
        text = data.strings[0x939];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                 {origin_x + 0x13b + ((0x13b - g_wiz_text_font_secondary.textWidth(text)) >> 1),
                  origin_y + 10},
                 text);
        stats_controls->InvalidateAll();
        stats_range->InvalidateAll();
    }
    if (full || effect_list_dirty) {
        InvalidateRegion(0x15d, 0xbe, 0x260, 0x1b5);
        CatalogArtwork::Load(0x1b6, 0).drawRect(GetFrameBufferSurface(),
                                                SDL_Rect{0, 0, (0x260) - (0x15d), (0x1b5) - (0xbe)},
                                                0x15d, 0xbe);
        DrawCampEffectList(character);
    }
    stats_range->Redraw(GetFrameBufferSurface(), false);
}

/* 0x005C4D40/0x005C4E20: how many beneficial/detrimental lines the equipped
   item in `slot` contributes to its effect-list entry. The armor-class bonus
   counts only on slots that actually take armor (not the two weapon/shield
   rows, 0 and 4/5 and 10/11) and never on equip-class 5 items. */
// FUNCTION: WIZ8 0x005c4d40
static unsigned int CountEquipItemBenefits(const W8Character& character, int slot)
{
    const W8ItemDatabaseRecord* record = &Data().items[character.EquippedItem[slot].iItemNo];
    unsigned int count = record->attack_damage_bonus > 0;
    int index;
    for (index = 0; index < 0x10; ++index) {
        if (record->missile_values[index] != 0) {
            ++count;
        }
    }
    if (record->attack_hit_bonus > 0) {
        ++count;
    }
    if (record->slays_kind != 0xff) {
        ++count;
    }
    if (record->health_regen_bonus > 0) {
        ++count;
    }
    if (record->stamina_regen_bonus > 0) {
        ++count;
    }
    if (record->spell_regen_bonus > 0) {
        ++count;
    }
    if (record->armor_class_bonus > 0 && slot != 0 && slot != 4 && slot != 5 && slot != 10 &&
        slot != 0xb && record->equip_class != W8_ITEM_EQUIP_CLASS_SHIELD) {
        ++count;
    }
    if (record->modifier_0b3_index != -1 && record->modifier_0b3_value > 0) {
        ++count;
    }
    if (record->modifier_0b1_index != -1 && record->modifier_0b1_value > 0) {
        ++count;
    }
    for (index = 0; index < 6; ++index) {
        if (record->resistance_bonus[index] > 0) {
            ++count;
        }
    }
    return count;
}

// FUNCTION: WIZ8 0x005c4e20
static unsigned int CountEquipItemPenalties(const W8Character& character, int slot)
{
    const W8ItemDatabaseRecord* record = &Data().items[character.EquippedItem[slot].iItemNo];
    unsigned int count = record->attack_damage_bonus < 0;
    int index;
    if (record->attack_hit_bonus < 0) {
        ++count;
    }
    if (record->health_regen_bonus < 0) {
        ++count;
    }
    if (record->stamina_regen_bonus < 0) {
        ++count;
    }
    if (record->spell_regen_bonus < 0) {
        ++count;
    }
    if (record->armor_class_bonus < 0) {
        ++count;
    }
    for (index = 0; index < 6; ++index) {
        if (record->resistance_bonus[index] < 0) {
            ++count;
        }
    }
    if (record->modifier_0b3_index != -1 && record->modifier_0b3_value < 0) {
        ++count;
    }
    if (record->modifier_0b1_index != -1 && record->modifier_0b1_value < 0) {
        ++count;
    }
    if (record->binds_on_equip != 0 && character.EquippedItem[slot].bound) {
        ++count;
    }
    return count;
}

/* Rebuilds the effect list under the stats page: every active condition, then
   the enchantments, then one entry per worn item that has any modifier at all.
   Alternate-hand slots 8 and 9 are skipped, and unidentified items contribute
   nothing. */
// FUNCTION: WIZ8 0x005c4ee0
void W8CampStatsPage::RebuildCampEffectList(const W8Character& character)
{
    effect_list.clear();
    stats_range->SetValue(0);
    for (int condition = 0x13; condition >= 0; --condition) {
        if (character.uiCondition[condition] != 0) {
            W8CampEffectEntry entry{};
            entry.kind = W8_CAMP_EFFECT_CONDITION;
            entry.detrimental = true;
            entry.turns = character.uiCondition[condition];
            entry.lines = 1;
            if (entry.turns == W8_CONDITION_INDEFINITE) {
                entry.lines = 2;
            }
            if (condition == W8_CONDITION_DRAINED) {
                if (character.hp_adjustment != 0) {
                    ++entry.lines;
                }
                if (character.fatigue_penalty != 0) {
                    ++entry.lines;
                }
            }
            entry.index = condition;
            effect_list.push_back(entry);
        }
    }
    for (int index = 7; index >= 0; --index) {
        if (character.enchantments[index].turns != 0) {
            W8CampEffectEntry entry{};
            entry.kind = W8_CAMP_EFFECT_ENCHANTMENT;
            entry.beneficial = true;
            entry.enchantment = character.enchantments[index].power;
            entry.turns = character.enchantments[index].turns;
            entry.lines = 2;
            entry.index = index;
            effect_list.push_back(entry);
        }
    }
    for (int slot = 0; slot < 12; ++slot) {
        const W8ItemInstance* item = &character.EquippedItem[slot];
        if (slot != 8 && slot != 9 && item->identified && item->iItemNo != -1) {
            int beneficial = CountEquipItemBenefits(character, slot);
            int detrimental = CountEquipItemPenalties(character, slot);
            if (beneficial != 0 || detrimental != 0) {
                W8CampEffectEntry entry{};
                entry.beneficial = beneficial != 0;
                entry.detrimental = detrimental != 0;
                entry.lines = detrimental + beneficial + 1;

                entry.kind = W8_CAMP_EFFECT_EQUIPMENT;
                entry.turns = W8_CONDITION_INDEFINITE;
                entry.index = slot;
                effect_list.push_back(entry);
            }
        }
    }
    FilterCampEffectList();
}

/* Remarks each list entry against the current tab and beneficial/detrimental
   filter, tracks the first/last visible rows, and reprograms the scrollbar to
   the visible line total. */
bool W8CampStatsPage::ShowsEffect(const W8CampEffectEntry& entry) const
{
    return (entry.kind == W8_CAMP_EFFECT_EQUIPMENT) == effect_items_only &&
           (effect_filter == W8_CAMP_EFFECT_FILTER_ALL ||
            (effect_filter == W8_CAMP_EFFECT_FILTER_BENEFICIAL && entry.beneficial) ||
            (effect_filter == W8_CAMP_EFFECT_FILTER_DETRIMENTAL && entry.detrimental));
}

void W8CampStatsPage::FilterCampEffectList()
{
    int visible_lines = 0;
    for (const auto& entry : effect_list) {
        if (ShowsEffect(entry)) {
            visible_lines += entry.lines + 1;
        }
    }
    const int last = std::max(0, visible_lines - 0x11);
    stats_range->SetEnabled(last > 0);
    stats_range->SetRange(0, last);
    stats_range->InvalidateAll();
}

/* Renders one effect-list entry at `line` (in 0xe-pixel rows from
   y 0xbf) and advances the counter past its height plus one row of spacing.
   The kind selects conditions, enchantments or equipped items. */
// FUNCTION: WIZ8 0x005c54a0
static void DrawCampEffectEntry(const W8Character& character, const W8CampEffectEntry* entry,
                                int* line_out)
{
    std::string text;
    int index;
    int line = *line_out;
    if (entry->kind == W8_CAMP_EFFECT_CONDITION) {
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                 fmt::format("{} {}", Data().strings[0x8d1],
                             Data().strings[kConditionNotices[entry->index].name]));
        g_wiz_text_font_secondary.resetPalette();
        int next = line + 1;
        if (entry->turns == W8_CONDITION_INDEFINITE) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {0x15e, (line + 1) * 0xe + 0xbf}, Data().strings[0x8d2]);
            next = line + 2;
        }
        line = next;
        if (entry->index == 1) {
            if (character.hp_adjustment != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {0x15e, line * 0xe + 0xbf},
                         fmt::format("{}: {:+d}", Data().strings[0x8da], character.hp_adjustment));
                ++line;
            }
            if (character.fatigue_penalty != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {0x15e, line * 0xe + 0xbf},
                         fmt::format("{}: {:+d}", Data().strings[0x8dc],
                                     -static_cast<int>(character.fatigue_penalty)));
                ++line;
            }
        }
    } else if (entry->kind == W8_CAMP_EFFECT_ENCHANTMENT) {
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                 fmt::format("{} {} ({:d})", Data().strings[0x8d4],
                             Data().strings[g_enchantment_notices[entry->index]],
                             entry->enchantment));
        g_wiz_text_font_secondary.resetPalette();
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                 {0x15e, (line + 1) * 0xe + 0xbf},
                 fmt::format("{:d} {}", entry->turns, Data().strings[0x8d3]));
        *line_out = line + 3;
        return;
    } else if (entry->kind == W8_CAMP_EFFECT_EQUIPMENT) {
        const W8ItemDatabaseRecord* record =
            &Data().items[character.EquippedItem[entry->index].iItemNo];
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                 GetItemDisplayName(Data(), &character.EquippedItem[entry->index]));
        g_wiz_text_font_secondary.resetPalette();
        int next = line + 1;
        if (record->attack_damage_bonus != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {0x15e, (line + 1) * 0xe + 0xbf},
                     fmt::format("{} {:+d}", Data().strings[0x8b1], record->attack_damage_bonus));
            next = line + 2;
        }
        line = next;
        std::string value_text;
        for (index = 0; index < 0x10; ++index) {
            if (record->missile_values[index] != 0) {
                text = "";
                text += Data().strings[g_attack_effect_name_ids[index]];
                text += " ";
                value_text = fmt::format("{:d}%", record->missile_values[index]);
                text += value_text;
                if (index == 2) {
                    text += " (";
                    text += Data().strings[0x8d5];
                    value_text = fmt::format(" {:d})", record->missile_magnitude);
                    text += value_text;
                }
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {0x15e, line * 0xe + 0xbf},
                         fmt::format("{}: {}", Data().strings[0x8d6], text));
                ++line;
            }
        }
        if (record->attack_hit_bonus != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{} {:+d}", Data().strings[0x8b7], record->attack_hit_bonus));
            ++line;
        }
        if (record->slays_kind != 0xff) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {}", Data().strings[0x8d8],
                                 Data().strings[g_special_category_name_ids[record->slays_kind]]));
            ++line;
        }
        if (record->health_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8d9], record->health_regen_bonus));
            ++line;
        }
        if (record->health_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8da], record->health_regen_bonus));
            ++line;
        }
        if (record->stamina_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8db], record->stamina_regen_bonus));
            ++line;
        }
        if (record->stamina_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8dc], record->stamina_regen_bonus));
            ++line;
        }
        if (record->spell_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8dd], record->spell_regen_bonus));
            ++line;
        }
        if (record->spell_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{}: {:+d}", Data().strings[0x8de], record->spell_regen_bonus));
            ++line;
        }
        if (record->armor_class_bonus != 0 &&
            (record->armor_class_bonus < 0 ||
             (entry->index != 0 && entry->index != 4 && entry->index != 5 && entry->index != 10 &&
              entry->index != 0xb && record->equip_class != W8_ITEM_EQUIP_CLASS_SHIELD))) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     fmt::format("{} {:+d}", Data().strings[0x8df], record->armor_class_bonus));
            ++line;
        }
        if (record->modifier_0b3_index != -1) {
            DrawText(
                GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                fmt::format(
                    "{} {:+d}",
                    Data().strings[g_character_description_first_ids[record->modifier_0b3_index]],
                    record->modifier_0b3_value));
            ++line;
        }
        if (record->modifier_0b1_index != -1) {
            DrawText(
                GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                fmt::format("{} {:+d}",
                            Data().strings[g_character_skill_name_ids[record->modifier_0b1_index]],
                            record->modifier_0b1_value));
            ++line;
        }
        for (index = 0; index < 6; ++index) {
            if (record->resistance_bonus[index] != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {0x15e, line * 0xe + 0xbf},
                         fmt::format("{} {} {:+d}%", Data().strings[g_attr_table1[6 + index]],
                                     Data().strings[0x8e0], record->resistance_bonus[index]));
                ++line;
            }
        }
        if (record->binds_on_equip != 0 && character.EquippedItem[entry->index].bound != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf},
                     Data().strings[0x8e1]);
            *line_out = line + 2;
            return;
        }
    }
    *line_out = line + 1;
}

/* Draws the visible slice of the effect list into the page's clipping
   window, skipping entries that sit entirely above the scrolled view. */
// FUNCTION: WIZ8 0x005c53c0
void W8CampStatsPage::DrawCampEffectList(const W8Character& character)
{
    SDL_Rect effect_clip{0, 0xbe, 0x280, 0xee};
    ScopedClipRect clipping(GetFrameBufferSurface(), effect_clip);
    int line = -stats_range->m_value;
    auto count = effect_list.size();
    for (unsigned int pos = 0; pos < count; ++pos) {
        if (line > 0x10) {
            break;
        }
        auto& entry = effect_list[pos];
        if (ShowsEffect(entry)) {
            if (line + entry.lines < 0) {
                line += entry.lines + 1;
            } else {
                DrawCampEffectEntry(character, &entry, &line);
            }
        }
    }
}

/* Wheel input over the stats page's effect-list region steps the
   scrollbar. */
/* Creates the five skill-category regions once per camp session and points
   them at the list handler. Category 4 is the overflow block under the right
   column. */
// FUNCTION: WIZ8 0x005c5cd0
void W8CampSkillsPage::Activate()
{
    skill_input = std::make_unique<UiLayer>();
    for (auto category :
         {W8_SKILL_CATEGORY_WEAPON, W8_SKILL_CATEGORY_MISCELLANEOUS, W8_SKILL_CATEGORY_COMBAT,
          W8_SKILL_CATEGORY_MAGIC, W8_SKILL_CATEGORY_EXPERT}) {
        const auto category_index = static_cast<unsigned int>(category);
        int x = g_camp_skill_category_positions[category_index].x;
        int y = g_camp_skill_category_positions[category_index].y;
        if (category == W8_SKILL_CATEGORY_EXPERT) {
            x += 0x136;
        } else {
            y += 0xa5;
        }
        auto& target = skill_input->add(
            SDL_Rect{x, y, 110, g_camp_skill_category_rows[category_index] * 14 + 1},
            [this, category](const UiPointerEvent& event) {
                return HandleCampSkillList(event, category);
            });
        target.setTooltip([this] {
            return skill_hover_row < 0 ? std::string{} : camp.game.rules().data.strings[0x958];
        });
    }
}

// FUNCTION: WIZ8 0x005c5d70
void W8CampSkillsPage::Deactivate()
{
    skill_input.reset();
    for (auto& skill : camp.reviewed->skills) skill.improved = false;
}

/* Unlike the gameplay best-skill query, the camp display includes every
   occupied slot, regardless of health or conditions. Ties count as best. */
static bool IsBestCampSkillLevel(W8Skill skill, unsigned int level)
{
    for (int slot = 0; slot < 8; ++slot) {
        if (CurrentGame().party.members[slot].has_value() &&
            level < CurrentGame().party.characters[slot].skills[skill].level) {
            return false;
        }
    }
    return true;
}

/* The skills page's full redraw: the two column panels, the five category
   headers, then every known skill's name and level with the base/current
   delta bar. */
// FUNCTION: WIZ8 0x005c5d80
void W8CampSkillsPage::DrawCampSkillsPage(bool full)
{
    const auto& data = camp.game.rules().data;
    if (full) {
        bool has_fifth = false;
        int skill;
        for (skill = 0; skill < 0x29; ++skill) {
            if ((camp.reviewed->skills[skill].active || camp.reviewed->skills[skill].level != 0) &&
                g_skill_attributes[skill].category == W8_SKILL_CATEGORY_EXPERT) {
                has_fifth = true;
                break;
            }
        }
        CatalogArtwork::Load(0x141, 0).drawAndInvalidate(GetFrameBufferSurface(), has_fifth, 0x136,
                                                         0);
        CatalogArtwork::Load(0x141, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0, 0xa5);
        for (W8SkillCategory category : g_camp_skill_categories) {
            const auto category_index = static_cast<std::size_t>(category);
            if (category != W8_SKILL_CATEGORY_EXPERT || has_fifth) {
                int x = g_camp_skill_category_positions[category_index].x;
                int y = g_camp_skill_category_positions[category_index].y;
                if (category == W8_SKILL_CATEGORY_EXPERT) {
                    x += 0x136;
                } else {
                    y += 0xa5;
                }
                CatalogArtwork::Load(0x144, 0).draw(
                    GetFrameBufferSurface(),
                    static_cast<short>(g_camp_skill_category_images[category_index]), x - 0x16,
                    y - 3);
            }
        }
        int category_count[5] = {0, 0, 0, 0, 0};
        for (skill = 0; skill < 0x29; ++skill) {
            W8CharacterSkill* value = &camp.reviewed->skills[skill];
            if (value->active || value->points != 0 || value->level != 0) {
                W8SkillCategory category = g_skill_attributes[skill].category;
                const int category_index = static_cast<int>(category);
                int left = g_camp_skill_category_positions[category_index].x;
                int top = g_camp_skill_category_positions[category_index].y;
                if (category == W8_SKILL_CATEGORY_EXPERT) {
                    left += 0x136;
                } else {
                    top += 0xa5;
                }
                top += category_count[category_index] * 0xe;
                CatalogArtwork::Load(0x141, 0).draw(GetFrameBufferSurface(), 3, left, top);
                unsigned int level = value->level;
                unsigned int base = value->points;
                int gained;
                int lost;
                if (level < base) {
                    lost = base - level;
                    gained = 0;
                } else {
                    gained = level - base;
                    lost = 0;
                    level = base;
                }
                SDL_Rect clip;
                clip.y = 0;
                clip.h = 0x1e0;
                if (level != 0) {
                    clip.x = left + 0x6f;
                    clip.w = level;
                    ScopedClipRect clipped(GetFrameBufferSurface(), clip);
                    CatalogArtwork::Load(0x143, 0).draw(GetFrameBufferSurface(), 0, left + 0x6f,
                                                        top + 2);
                }
                if (gained == 0) {
                    if (lost != 0) {
                        clip.x = level + 0x6f + left;
                        clip.w = lost;
                        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
                        CatalogArtwork::Load(0x143, 0).drawAndInvalidate(GetFrameBufferSurface(), 2,
                                                                         left + 0x6f, top + 2);
                    }
                } else {
                    clip.x = level + 0x6f + left;
                    clip.w = gained;
                    ScopedClipRect clipped(GetFrameBufferSurface(), clip);
                    CatalogArtwork::Load(0x143, 0).draw(GetFrameBufferSurface(), 1, left + 0x6f,
                                                        top + 2);
                }
                Palette16 palette;
                if (!camp.game.status.game_started || value->level == 0) {
                    palette = g_font_state_palettes[W8_FONT_PALETTE_GRAY];
                    if (value->active) {
                        palette = g_wiz_text_font_secondary.glyphs->colors;
                    }
                } else {
                    bool best = IsBestCampSkillLevel(static_cast<W8Skill>(skill), value->level);
                    if (!best) {
                        palette = g_font_state_palettes[W8_FONT_PALETTE_GRAY];
                        if (value->active) {
                            palette = g_wiz_text_font_secondary.glyphs->colors;
                        }
                    } else {
                        palette = g_font_state_palettes[W8_FONT_PALETTE_BEIGE];
                        if (value->active) {
                            palette = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
                        }
                    }
                }
                g_wiz_text_font_secondary.setPalette(palette);
                short width = g_wiz_text_font_secondary.textWidth(
                    data.strings[g_character_skill_name_ids[skill]]);
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {(0x6b - width) / 2 + 2 + left, top + 1},
                         data.strings[g_character_skill_name_ids[skill]]);
                palette = g_wiz_text_font_secondary.glyphs->colors;
                if (value->improved) {
                    palette = g_font_state_palettes[W8_FONT_PALETTE_GREEN];
                }
                g_wiz_text_font_secondary.setPalette(palette);
                short value_width = g_wiz_text_font_secondary.codepointPrefixWidth(
                    fmt::format("{:d}", value->level), 3);
                DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                                      {(0x24 - value_width) / 2 + 0xee + left, top + 1},
                                      fmt::format("{:d}", value->level));
                g_wiz_text_font_secondary.resetPalette();
                g_wiz_text_font_secondary.setShade(Font::kNormalShade);
                ++category_count[category_index];
            }
        }
    }
}

/* The skills page's region handler: hover updates the help line with the
   skill's info hint, and a right click opens the skill info dialog. The
   dialog's second argument is raised when the character's level is the best
   among the occupied party slots. */
// FUNCTION: WIZ8 0x005c6230
UiResult W8CampSkillsPage::HandleCampSkillList(const UiPointerEvent& event,
                                               W8SkillCategory category)
{
    int row = (event.local.y - 1) / 0xe;
    int skill = -1;
    int occurrence = 0;
    for (int index = 0; index < 0x29; ++index) {
        if (g_skill_attributes[index].category == category &&
            (camp.reviewed->skills[index].active || camp.reviewed->skills[index].level != 0)) {
            if (occurrence == row) {
                skill = index;
                break;
            }
            ++occurrence;
        }
    }

    if (event.action == UiPointerAction::Enter || event.action == UiPointerAction::Move ||
        event.action == UiPointerAction::Leave) {
        const int hover = event.inside && skill != -1 ? row : -1;
        if (hover != skill_hover_row) {
            ResetUiHelp(true);
            skill_hover_row = hover;
        }
    }
    if (event.action == UiPointerAction::Up && event.inside &&
        event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK) &&
        skill != -1) {
        AfterUiDispatch([this, skill] {
            bool best = false;
            if (camp.game.status.game_started && camp.reviewed->skills[skill].level != 0) {
                best = IsBestCampSkillLevel(static_cast<W8Skill>(skill),
                                            camp.reviewed->skills[skill].level);
            }
            auto dialog = std::make_unique<W8SkillInfoDialog>(
                static_cast<W8Skill>(skill), best, !camp.reviewed->skills[skill].active,
                skill == g_profession_bonus_skills[camp.reviewed->iProfession]);
            camp.dialogs.Display(std::move(dialog));
        });
    }
    return UiResult::Handled;
}

/* The character screen's fourth page (name, portrait, personality and voice)
   and second page (skills). W8_SCREEN_CHARACTER is the camp screen's character
   editor, so both classes sit in this unit between the skills-page handler and
   the CGSSpellsPage anchor; their retail span 0x005C6460..0x005C7D60 matches
   demo 0x005CFAD0..0x005D0FE0 inside the same hull. */

W8CharacterPersonalityPage::W8CharacterPersonalityPage(CharacterScreen& owner)
    : W8CharacterPage(owner, CatalogArtwork::Load(0x105)),
      anim_timer(0.4f, TimerClock::Presentation), m_animation_active(false)
{
    auto* character = &screen.draft.character;
    constexpr SDL_Point positions[] = {
        {100, 0x19}, {0x144, 0x19}, {100, 0x67}, {0x144, 0x67}, {0x16d, 0x155}};
    for (int button_index = 0; button_index < 5; ++button_index) {
        const auto action = static_cast<W8CharacterPortraitAction>(button_index);
        const int frame = button_index == 4 ? 20 : button_index % 2 ? 15 : 10;
        auto& button = m_buttons[button_index];
        button = std::make_unique<W8ButtonWidget>(input);
        button->Configure(ButtonGraphics::Load(CatalogArtwork::Load(0x10a, 0).spriteSheet(),
                                               frame + 3, frame, frame + 1, frame + 2, frame + 4),
                          {.on_click =
                               [this, action](auto*) {
                                   AfterUiDispatch([this, action] { ChangePortrait(action); });
                               },
                           .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = action == W8CharacterPortraitAction::show_summary ? 0xf5 : -1,
                           .priority = UI_PRIORITY_LOWEST,
                           .menu_artwork = true});

        button->SetPosition(0xc3 + positions[button_index].x, 0x2b + positions[button_index].y);
    }
    character->voice = std::clamp(int(character->voice), 0, 1);
    for (int index = 0; index < 11; ++index) {
        const bool voice = index >= 9;
        const int x = 0xc3 + (voice ? 0x21 : (index % 3) * 0x80 + 0x24);
        const int y = 0x2b + (voice ? (index == 9 ? 0x140 : 0x15d) : (index / 3) * 14 + 0x107);
        if (m_choices[index]) input.remove(*m_choices[index]);
        m_choices[index] =
            &input.add(SDL_Rect{x, y, voice ? 73 : 128, voice ? 15 : 14},
                       [this, index, voice](const UiPointerEvent& event) {
                           if (event.action != UiPointerAction::Up || event.captured_buttons)
                               PlayButtonSound(event.action);
                           m_choices_dirty = true;
                           if (event.action == UiPointerAction::Up && event.inside &&
                               event.button == UiPointerButton::Left && event.captured_buttons) {
                               AfterUiDispatch([this, index, voice] {
                                   if (voice)
                                       screen.draft.character.voice = index - 9;
                                   else
                                       screen.draft.character.personality = index;
                                   m_description_dirty = true;
                                   screen.UpdateNavigation(this);
                               });
                           }
                           return UiResult::Handled;
                       });
    }
}

void W8CharacterPersonalityPage::Activate()
{
    input.setEnabled(true);
    m_instructions_dirty = true;
    auto& fields = m_text_fields.emplace(input, g_wiz_text_mono_font);
    fields.UseGameStyle();
    fields.Add(W8CharacterPage::bounds.x + 0x97, W8CharacterPage::bounds.y + 0xab, 0x106, 0x10,
               0x7f, screen.draft.character.name_part_2, 0x27, 0xf, true);
    fields.Add(W8CharacterPage::bounds.x + 0x97, W8CharacterPage::bounds.y + 0xc7, 0x106, 0x10,
               0x7f, screen.draft.character.name, 9, 0xf, true);
    if (fields.Text(0).empty())
        fields.Focus(0);
    else if (fields.Text(1).empty())
        fields.Focus(1);
    m_choices_dirty = true;
}

void W8CharacterPersonalityPage::Deactivate()
{
    input.setEnabled(false);
    m_text_fields.reset();
}

void W8CharacterPersonalityPage::Accept()
{
    if (screen.m_mode == W8_CHARACTER_CREATE) {
        InvalidateAndRecalculateCharacterClassData((&screen.draft.character));
    } else {
        W8Character* original = screen.GetOriginalCharacter();
        screen.draft.character.personality = original->personality;
        screen.draft.character.portrait_index = original->portrait_index;
        screen.draft.character.voice = original->voice;
        screen.draft.character.name_part_2 = original->name_part_2;
        screen.draft.character.name = original->name;
    }
    Refresh();
    InvalidateAll();
    screen.UpdateNavigation(this);
}

void W8CharacterPersonalityPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    *next_enabled =
        m_text_fields && m_text_fields->Text(0).size() != 0 && m_text_fields->Text(1).size() != 0;
    if (screen.m_mode != W8_CHARACTER_CREATE) {
        W8Character* original = screen.GetOriginalCharacter();
        *exit_enabled = false;
        if (screen.draft.character.personality == original->personality &&
            screen.draft.character.portrait_index == original->portrait_index &&
            screen.draft.character.voice == original->voice &&
            strcmp(screen.draft.character.name_part_2.c_str(), original->name_part_2.c_str()) ==
                0 &&
            strcmp(screen.draft.character.name.c_str(), original->name.c_str()) == 0) {
            return;
        }
        *exit_enabled = true;
    } else {
        *exit_enabled = true;
    }
}

void W8CharacterPersonalityPage::HandleInput(const InputEvent* input)
{
    if (screen.HasDialog()) return;
    if (!IsTypingEvent(*input)) {
        return;
    }

    const char32_t character = input->type == InputEventType::Text ? input->text : 0;
    if (character != 0 && character < 0x80 && strchr("\\/:*?\"<>|", int(character)) != 0) {
        return;
    }
    if (!m_text_fields) return;
    if (input->key == SDLK_TAB) {
        m_text_fields->FocusNext();
        return;
    }
    if (!m_text_fields->HandleInput(*input)) return;

    const auto field = m_text_fields->FocusedId();
    if (field == 0) {
        screen.draft.character.name_part_2 = m_text_fields->Text(0);
    } else if (field == 1) {
        screen.draft.character.name = m_text_fields->Text(1);
    }
    screen.UpdateNavigation(this);
}

void W8CharacterPersonalityPage::Refresh()
{
    if (m_text_fields) {
        m_text_fields->SetText(0, screen.draft.character.name_part_2);
        m_text_fields->SetText(1, screen.draft.character.name);
    }
    m_choices_dirty = true;
}

/* The final page's whole redraw: advance the portrait animation, draw the
   fixed text labels and the nine personality labels, then let the dirty
   portrait, description and name-row blocks refresh themselves. */
void W8CharacterPersonalityPage::Redraw(SDL_Surface& target, bool full_redraw)
{
    if (m_animation_active) {
        int elapsed = static_cast<int>(anim_timer.GetProgress());
        if (elapsed > 0) {
            m_animation_frame = (m_animation_frame + elapsed) % 3;
            CatalogArtwork::Load(0x105, 0).drawAndInvalidate(target, m_animation_frame + 2,
                                                             W8CharacterPage::bounds.x + 0x156,
                                                             W8CharacterPage::bounds.y + 0x137);
        }
    }

    bool redraw = m_active && m_dirty;
    W8CharacterPage::Redraw(target, full_redraw);
    for (auto& button : m_buttons) {
        button->m_dirty |= redraw || full_redraw;
        button->Refresh();
    }
    if (redraw || full_redraw || m_choices_dirty) {
        for (int index = 0; index < 11; ++index) {
            const bool voice = index >= 9;
            const int selected =
                voice ? screen.draft.character.voice + 9 : screen.draft.character.personality;
            const int frame = 5 + (selected == index ? 2 : 0) + m_choices[index]->hovered();
            const int x = 0xc3 + (voice ? 0x21 : (index % 3) * 0x80 + 0x24);
            const int y = 0x2b + (voice ? (index == 9 ? 0x140 : 0x15d) : (index / 3) * 14 + 0x107);
            CatalogArtwork::Load(0x105, 0).drawAndInvalidate(target, frame, x, y);
        }
        m_choices_dirty = false;
    }
    if (!screen.HasDialog() && m_text_fields) {
        m_text_fields->RenderAll(GetFrameBufferSurface());
    }

    if (redraw) {
        W8TextBuffer text;
        W8ControlsRect bounds;

        bounds.left = W8CharacterPage::bounds.x + 0x2c;
        bounds.right = W8CharacterPage::bounds.x + 100;
        bounds.top = W8CharacterPage::bounds.y + 0x19;
        bounds.bottom = W8CharacterPage::bounds.y + 0x31;
        text.RenderString(&bounds, Data().strings[0xee], g_wiz_text_font_secondary, true, target);

        bounds.top = W8CharacterPage::bounds.y + 0x67;
        bounds.bottom = W8CharacterPage::bounds.y + 0x7f;
        text.RenderString(&bounds, Data().strings[0xf0], g_wiz_text_font_secondary, true, target);

        bounds.left = W8CharacterPage::bounds.x + 0x160;
        bounds.right = W8CharacterPage::bounds.x + 0x198;
        text.RenderString(&bounds, Data().strings[0xf1], g_wiz_text_font_secondary, true, target);

        bounds.top = W8CharacterPage::bounds.y + 0x19;
        bounds.bottom = W8CharacterPage::bounds.y + 0x31;
        text.RenderString(&bounds, Data().strings[0xef], g_wiz_text_font_secondary, true, target);

        bounds.left = W8CharacterPage::bounds.x + 0x24;
        bounds.right = W8CharacterPage::bounds.x + 0x92;
        bounds.top = W8CharacterPage::bounds.y + 0xaa;
        bounds.bottom = W8CharacterPage::bounds.y + 0xbc;
        text.RenderString(&bounds, Data().strings[0x84], g_wiz_text_font_secondary, true, target);

        bounds.top = W8CharacterPage::bounds.y + 0xc6;
        bounds.bottom = W8CharacterPage::bounds.y + 0xd8;
        text.RenderString(&bounds, Data().strings[0x85], g_wiz_text_font_secondary, true, target);

        bounds.left = W8CharacterPage::bounds.x + 0x18;
        bounds.top = W8CharacterPage::bounds.y + 0xee;
        bounds.right = W8CharacterPage::bounds.x + 0x1a8;
        bounds.bottom = W8CharacterPage::bounds.y + 0xfa;
        text.RenderString(&bounds, Data().strings[0x86], g_wiz_text_font_secondary, true, target);

        int index = 0;
        for (const unsigned short* message_id = g_personality_message_ids;
             message_id < g_personality_message_ids + 9; ++message_id, ++index) {
            bounds.left = (index % 3) * 0x80 + 0x30 + W8CharacterPage::bounds.x;
            bounds.right = bounds.left + 0x80;
            bounds.top = W8CharacterPage::bounds.y + 0x106 + (index / 3) * 0xe;
            bounds.bottom = bounds.top + 0xe;
            text.SetClipBounds(bounds);
            text.SetLayoutBounds(&bounds, true);
            text.SetLayoutMode(g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft);
            text.RenderString(Data().strings[*message_id], g_wiz_text_font_secondary, true, target);
        }

        bounds.top = W8CharacterPage::bounds.y + 0x13f;
        bounds.bottom = W8CharacterPage::bounds.y + 0x14d;
        bounds.left = W8CharacterPage::bounds.x + 0x29;
        bounds.right = W8CharacterPage::bounds.x + 0x69;
        text.SetLayoutMode(g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop);
        text.RenderString(&bounds, Data().strings[0x8d], g_wiz_text_font_secondary, true, target);

        bounds.top = W8CharacterPage::bounds.y + 0x15c;
        bounds.bottom = W8CharacterPage::bounds.y + 0x16a;
        text.RenderString(&bounds, Data().strings[0x8e], g_wiz_text_font_secondary, true, target);

        m_portrait_dirty = true;
        m_description_dirty = true;
        m_animation_active = false;
    }

    if (m_portrait_dirty) {
        CatalogArtwork::Load(0x11, screen.draft.character.portrait_index)
            .drawAndInvalidate(target, 0, W8CharacterPage::bounds.x + 0x86,
                               W8CharacterPage::bounds.y + 5, {}, BitmapKey::Opaque);
        m_portrait_dirty = false;
    }

    if (m_description_dirty) {
        W8TextBuffer text;
        W8ControlsRect bounds;

        bounds.top = W8CharacterPage::bounds.y + 0x13a;
        bounds.bottom = W8CharacterPage::bounds.y + 0x16a;
        bounds.left = W8CharacterPage::bounds.x + 0x70;
        bounds.right = W8CharacterPage::bounds.x + 0x150;
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.SetText(GetCharacterQuoteText(screen.draft.character, 0), g_wiz_text_font_secondary);
        text.FillBounds(target, 0x8000);
        text.RenderToTarget(0, true, target);
        m_description_dirty = false;
    }

    if (m_instructions_dirty) {
        W8TextBuffer text;
        W8ControlsRect bounds = {9, 0xec, 0xbd, 0x184};
        text.RenderString(&bounds, Data().strings[0xe9], g_wiz_text_font_secondary, true, target);
        m_instructions_dirty = false;
    }
}

void W8CharacterPersonalityPage::ChangePortrait(W8CharacterPortraitAction action)
{
    int portrait = screen.draft.character.portrait_index;
    if (action == W8CharacterPortraitAction::next_group) {
        int group = g_portrait_descriptors[portrait].group + 1;
        if (group > 11) group = 0;
        screen.draft.character.portrait_index = g_portrait_groups[group].portraits[0];
        m_portrait_dirty = true;
    } else if (action == W8CharacterPortraitAction::previous_group) {
        int group = g_portrait_descriptors[portrait].group - 1;
        if (group < 0) group = 11;
        screen.draft.character.portrait_index = g_portrait_groups[group].portraits[0];
        m_portrait_dirty = true;
    } else if (action == W8CharacterPortraitAction::next_portrait ||
               action == W8CharacterPortraitAction::previous_portrait) {
        int group = g_portrait_descriptors[portrait].group;
        W8PortraitGroup* portraits = &g_portrait_groups[group];
        int index = 0;
        while (index < portraits->count && portraits->portraits[index] != portrait) {
            ++index;
        }
        if (action == W8CharacterPortraitAction::next_portrait) {
            ++index;
            if (index >= portraits->count) index = 0;
        } else {
            --index;
            if (index < 0) index = portraits->count - 1;
        }
        screen.draft.character.portrait_index = portraits->portraits[index];
        m_portrait_dirty = true;
    } else if (action == W8CharacterPortraitAction::show_summary) {
        m_animation_active = true;
        m_animation_frame = 2;
        anim_timer.Restart();
        ShadowRect(GetFrameBufferSurface(), {0, 0, 0x281, 0x1e1});
        ResetTransientRenderScenes();
        screen.ShowCharacterSummary();
    }

    if (action != W8CharacterPortraitAction::show_summary) {
        screen.UpdateNavigation(this);
    }
}

W8CharacterSkillsPage::W8CharacterSkillsPage(CharacterScreen& owner)
    : W8CharacterPage(owner, CatalogArtwork::Load(0x108))
{
    int category_count[5] = {0, 0, 0, 0, 0};
    for (auto& skill_attribute : g_skill_attributes) {
        const auto category = skill_attribute.category;
        const int category_index = static_cast<int>(category);
        W8CharacterPageEntry* entry =
            &AddEntry(g_character_page2_category_geometry[category_index].x,
                      g_character_page2_category_geometry[category_index].y +
                          category_count[category_index] * 0xe,
                      true);
        entry->on_adjust = [this, entry](int delta) { AdjustEntry(entry, delta); };
        entry->on_info = [this, entry] { ShowEntryInfo(entry); };
        ++category_count[category_index];
    }
}

void W8CharacterSkillsPage::Activate()
{
    input.setEnabled(true);
    Refresh();
    m_contentDirty = true;
    m_instructions_dirty = true;
}

void W8CharacterSkillsPage::Deactivate()
{
    input.setEnabled(false);
}

void W8CharacterSkillsPage::Accept()
{
    screen.draft.refundAllSkillPoints();
    InvalidateAll();
    m_contentDirty = true;
    screen.UpdateNavigation(this);
    for (const auto& entry : m_entries) {
        entry->UpdateButtons();
    }
}

void W8CharacterSkillsPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    *next_enabled = screen.draft.skillsComplete();
    *exit_enabled =
        screen.draft.allocation.skill_points_remaining < screen.draft.allocation.skill_points_total;
    for (const auto& entry : m_entries) entry->SetIncrementAllowed(!*next_enabled);
}

void W8CharacterSkillsPage::AdjustEntry(W8CharacterPageEntry* entry, int delta)
{
    entry->MarkDirty();
    m_contentDirty = true;
    screen.draft.adjustSkill(entry->m_id, delta);
    Refresh();
    screen.UpdateNavigation(this);
}

void W8CharacterSkillsPage::ShowEntryInfo(W8CharacterPageEntry* entry)
{
    screen.ShowSkillInfo(static_cast<W8Skill>(entry->m_id));
}

void W8CharacterSkillsPage::Redraw(SDL_Surface& target, bool full_redraw)
{
    bool redraw = static_cast<unsigned char>(m_active && m_dirty);
    if (m_force_redraw) {
        UpdateEntries();
        InvalidateAll();
        redraw = true;
        m_force_redraw = false;
    }
    W8CharacterPage::Redraw(target, full_redraw);

    if (redraw) {
        for (int category = 0; category < 5; ++category) {
            if (category != 4 || m_show_fifth_category) {
                CatalogArtwork::Load(0x144, 0).draw(
                    target, static_cast<short>(g_character_page2_category_frames[category]),
                    W8CharacterPage::bounds.x + g_character_page2_category_geometry[category].x -
                        0x16,
                    W8CharacterPage::bounds.y + g_character_page2_category_geometry[category].y -
                        3);
            }
        }
        if (!m_show_fifth_category) {
            CatalogArtwork::Load(0x108, 0).draw(target, 1, W8CharacterPage::bounds.x,
                                                W8CharacterPage::bounds.y + 0x118);
        }
    }

    if (m_instructions_dirty) {
        W8TextBuffer text;
        W8ControlsRect bounds = {4, 0xec, 0xc2, 0x162};
        text.RenderString(&bounds, Data().strings[0xe8], g_wiz_text_font_secondary, true, target);
        bounds.top = 0x162;
        bounds.right = 0x8f;
        bounds.bottom = 0x179;
        text.RenderString(&bounds, Data().strings[0xe3], g_wiz_text_font_secondary, true, target);
        bounds.top = 0x184;
        bounds.bottom = 0x19b;
        text.RenderString(&bounds, Data().strings[0xe4], g_wiz_text_font_secondary, true, target);
        bounds.left = 0x8f;
        bounds.top = 0x162;
        bounds.right = 0xbf;
        bounds.bottom = 0x179;
        CatalogArtwork::Load(0x107, 0).draw(target, 5, 0x8f, 0x162);
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(fmt::format("{:d}", screen.draft.allocation.skill_step_limit),
                          g_options_detail_font, true, target);
        m_instructions_dirty = false;
    }

    if (m_contentDirty) {
        W8TextBuffer text;
        W8ControlsRect bounds = {0x8f, 0x184, 0xbf, 0x19b};
        CatalogArtwork::Load(0x107, 0).draw(target, 5, 0x8f, 0x184);
        text.SetClipBounds(bounds);
        text.SetLayoutBounds(&bounds, true);
        text.RenderString(fmt::format("{:d}/{:d}", screen.draft.allocation.skill_points_remaining,
                                      screen.draft.allocation.skill_points_total),
                          g_options_detail_font, true, target);
        m_contentDirty = false;
    }
}

void W8CharacterSkillsPage::UpdateEntries()
{
    int index;
    for (index = 0; index < 0x29; ++index) {
        m_entries[index]->SetEnabled(false);
    }

    int category_count[5] = {0, 0, 0, 0, 0};
    m_show_fifth_category = false;
    for (int skill = 0; skill < 0x29; ++skill) {
        W8CharacterSkill* value = &screen.draft.character.skills[skill];
        if (value->active || value->points != 0) {
            const auto category = g_skill_attributes[skill].category;
            const int category_index = static_cast<int>(category);
            int entry_index = 0;
            int occurrence = 0;
            for (; entry_index < 0x29; ++entry_index) {
                if (g_skill_attributes[entry_index].category == category &&
                    occurrence++ == category_count[category_index]) {
                    break;
                }
            }
            W8CharacterPageEntry* entry = m_entries[entry_index].get();
            ++category_count[category_index];
            if (category == W8_SKILL_CATEGORY_EXPERT) m_show_fifth_category = true;
            entry->SetContent(skill, Data().strings[g_character_skill_name_ids[skill]],
                              &value->points, &screen.draft.allocation.skill_points_spent[skill],
                              &screen.draft.allocation.skill_limits[skill], 0x101);
            entry->SetLabelFontState(
                skill == g_profession_bonus_skills[screen.draft.character.iProfession]
                    ? std::optional{3}
                    : std::nullopt);
        }
    }
}

void W8CharacterSkillsPage::Refresh()
{
    m_force_redraw = true;
}

W8CampStatsPage::W8CampStatsPage(CampScreen& owner) : camp(owner)
{
    stats_range = std::make_unique<W8RangeControl>(camp.scroll_input, 0x264, 0xbe, 0x276, 0x1b5);
    stats_range->on_change = [this](auto*) { effect_list_dirty = true; };
    stats_controls = std::make_unique<W8CampStatsControls>(*this);
}

void W8CampStatsPage::Activate()
{
    const auto& character = *camp.reviewed;
    RebuildCampEffectList(character);
    stats_range->SetActive(true);
    stats_controls->input.setEnabled(true);
}

void W8CampStatsPage::Deactivate()
{
    stats_range->SetActive(false);
    stats_controls->input.setEnabled(false);
    effect_list.clear();
}

void W8CampStatsPage::RefreshCharacter()
{
    const auto& character = *camp.reviewed;
    RebuildCampEffectList(character);
    camp.character_info.InvalidateAll();
}

void W8CampStatsPage::DrawControls()
{
    stats_range->Redraw(GetFrameBufferSurface(), false);
    stats_controls->Redraw(GetFrameBufferSurface(), false);
}
