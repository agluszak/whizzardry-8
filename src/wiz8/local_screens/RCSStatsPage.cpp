#include "himage.h"
#include "image.h"
#include <fmt/format.h>
#include <SDL3/SDL_rect.h>
#include <algorithm>
#include <string>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "wiz8/conditions.h"
#include "wiz8/local_screens/RCSStatsPage.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/integer_constants.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/cursor.h"
#include "wiz8/fonts.h"
#include "wiz8/regions.h"
#include "wiz8/text_input.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/local_code/CharGeneration.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/ControlSelection.h"
#include "wiz8/local_code/ControlsRect.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/dialog_code/StatInfoDialogs.h"
#include "Font.h"
#include "input.h"
#include "game_timer.h"
#include "local_code/CombatAttack.h"
#include "local_code/Controls.h"
#include "local_code/RangeControl.h"
#include "surrender/srMath.h"

/* 0x0069C514/0x0069C518: the stats page's text origin, initialized by the
   page renderer before every full redraw. */
// GLOBAL: WIZ8 0x0069c514
static int g_camp_stats_origin_y;
// GLOBAL: WIZ8 0x0069c518
static int g_camp_stats_origin_x;
// GLOBAL: WIZ8 0x0069c51c
unsigned int g_camp_stats_range_region_set;
// GLOBAL: WIZ8 0x0069c520
unsigned int g_camp_stats_controls_region_set;
/* The skills page row the cursor last hovered, kept so the help
   text only resets on a real change. */
// GLOBAL: WIZ8 0x0069c524
static int g_camp_skill_hover_row;
// GLOBAL: WIZ8 0x0069c528
unsigned int g_camp_skill_regions;

static unsigned char CampStatsMouseWheel(const InputEvent* event, W8Region*);
void DrawCampEffectList(void);
void DrawCampEffectEntry(W8CampEffectEntry* entry, int* line_out);

/* The five skill-category blocks shared by the camp skills page: (x, y)
   origins, the row count that bounds each region, and the catalog frame each
   header draws. Category 4 sits below the right-hand column; the others fill
   the left and right columns. */
// GLOBAL: WIZ8 0x0064ef40
static srVector2i g_camp_skill_category_positions[5] = {
    {0x160, 0x8}, {0x160, 0xcc}, {0x22, 0x9}, {0x22, 0x86}, {0x2a, 0x21},
};
// GLOBAL: WIZ8 0x0064ef68
static int g_camp_skill_category_rows[5] = {0xb, 5, 8, 0xa, 7};
// GLOBAL: WIZ8 0x0064ef7c
static int g_camp_skill_category_images[5] = {2, 3, 0, 1, 4};

// GLOBAL: WIZ8 0x0069c530
static unsigned int g_character_page2_region_set;
// GLOBAL: WIZ8 0x0069c52c
unsigned int g_character_page4_region_set;
// GLOBAL: WIZ8 0x0064ef90
static srVector2i g_character_page2_category_geometry[5] = {
    {0xf9, 0x0a}, {0xf9, 0xcd}, {0x22, 0x0a}, {0x22, 0x87}, {0xf9, 0x120},
};
// GLOBAL: WIZ8 0x0064efb8
static int g_character_page2_category_frames[5] = {2, 3, 0, 1, 4};

struct W8PortraitGroup {
    int count;
    int portraits[14];
};
// GLOBAL: WIZ8 0x00648950
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

// FUNCTION: WIZ8 0x005c4430
W8CampStatsRange::W8CampStatsRange()
{
    m_range = new W8RangeControl(0x264, 0xbe, 0x276, 0x1b5, &g_camp_stats_range_region_set);
    m_range->m_listener = this;
    m_range->SetEnabled(true);
}

// FUNCTION: WIZ8 0x005c44c0
W8CampStatsRange::~W8CampStatsRange()
{
    delete m_range;
}

// FUNCTION: WIZ8 0x005c44e0
void W8CampStatsRange::OnRangeChanged(W8RangeControl*)
{
    g_camp_screen->effect_scroll = m_range->m_value;
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_EFFECT_LIST;
}

/* The shared camp-range refresh, emitted inside this unit in both retail and
   the demo. Retail calls it directly on the stats and spell listeners as
   well, which is what proves the common W8CampRangeListener base. */
// FUNCTION: WIZ8 0x005c4510
void W8CampRangeListener::UpdateRange(bool range_changed)
{
    if (range_changed) {
        m_range->Invalidate(0);
    }
    m_range->Redraw();
}

// FUNCTION: WIZ8 0x005c4540
W8CampStatsControls::W8CampStatsControls()
{
    AcquireRegionSet(&g_camp_stats_controls_region_set);
    m_buttons[0] = new W8TextControl(this, -1, 0x13c, 0xbe, 0, 0, CatalogArtwork::Load(0x145, 0), 0, 1, 2, 4, 3);
    m_buttons[0]->AddLayoutFlags(g_W8TextControlLayoutLatchedImage | g_W8TextControlLayoutToggle);
    m_buttons[0]->m_listener = this;
    m_buttons[0]->EnableRegionHelp(0x954);
    m_buttons[1] = new W8TextControl(this, -1, 0x13c, 0xd6, 0, 0, CatalogArtwork::Load(0x145, 0), 5, 6, 7, 9, 8);
    m_buttons[1]->AddLayoutFlags(g_W8TextControlLayoutLatchedImage | g_W8TextControlLayoutToggle);
    m_buttons[1]->m_listener = this;
    m_buttons[1]->EnableRegionHelp(0x955);
    m_buttons[2] = new W8TextControl(this, -1, 0x13c, 0xf3, 0, 0, CatalogArtwork::Load(0x145, 0), 10, 15, 12, 17, 13);
    m_buttons[2]->AddLayoutFlags(g_W8TextControlLayoutLatchedImage | g_W8TextControlLayoutToggle);
    m_buttons[2]->m_listener = this;
    m_buttons[2]->EnableRegionHelp(0x956);
    if (g_camp_screen->effect_items_only) {
        m_buttons[2]->EnableSecondaryState(false);
    }
    unsigned int region = AddRegionToSet(g_camp_stats_controls_region_set);
    SetRegionCallback(region, CampStatsMouseWheel, 0);
    SetRegionBounds(region, 0x15d, 0xbe, 0x260, 0x1b5);
    Controls::SetEnabled(true);
}

// FUNCTION: WIZ8 0x005c47a0
W8CampStatsControls::~W8CampStatsControls()
{
    DestroyAllControls();
}

// FUNCTION: WIZ8 0x005c4800
void W8CampStatsControls::OnPrimary(W8TextControl* control)
{
    if (control == m_buttons[0]) {
        if (static_cast<unsigned char>(m_buttons[0]->m_stateFlags &
                                       g_W8TextControlStateSecondary)) {
            m_buttons[1]->DisableSecondaryState(false);
            g_camp_screen->effect_filter = W8_CAMP_EFFECT_FILTER_BENEFICIAL;
        } else {
            g_camp_screen->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        }
    } else if (control == m_buttons[1]) {
        if (static_cast<unsigned char>(m_buttons[1]->m_stateFlags &
                                       g_W8TextControlStateSecondary)) {
            m_buttons[0]->DisableSecondaryState(false);
            g_camp_screen->effect_filter = W8_CAMP_EFFECT_FILTER_DETRIMENTAL;
        } else {
            g_camp_screen->effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
        }
    } else {
        g_camp_screen->effect_items_only =
            static_cast<unsigned char>(m_buttons[2]->m_stateFlags &
                                       g_W8TextControlStateSecondary) != 0;
    }
    FilterCampEffectList();
    g_camp_screen->redraw_flags |= W8_CAMP_REDRAW_EFFECT_LIST;
}

/* The stats page's full redraw: attribute rows with their base/effective
   delta bars, the profession's bonus skill, the trait grid, then the effect
   list region when its redraw flag is up. */
// FUNCTION: WIZ8 0x005c48b0
void DrawCampStatsPage(void)
{
    if (g_camp_screen->redraw_flags == W8_CAMP_REDRAW_ALL) {
        CatalogArtwork::Load(0x142, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0xa5);
        g_camp_stats_origin_x = 0;
        g_camp_stats_origin_y = 0xa5;
        int index;
        std::string text = g_localized_strings[0x937];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {g_camp_stats_origin_x + 10 +
                         ((0x11f - g_wiz_text_font_secondary.textWidth(text)) >> 1), g_camp_stats_origin_y + 10}, text);
        int row_y = 0xbf;
        for (index = 0; index < 7; ++index) {
            std::string label = g_localized_strings[g_attribute_label_ids[index]];
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {g_camp_stats_origin_x + 10 +
                             ((0x7b - g_wiz_text_font_secondary.textWidth(label)) >> 1), row_y - 0xa6 + g_camp_stats_origin_y}, label);
            unsigned int effective = g_review_character->attributes[index].effective;
            DrawCampValueBar(effective, g_review_character->attributes[index].base, 0x88, row_y);
            g_camp_screen->text_buffer = fmt::format("{:d}", effective);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {g_camp_stats_origin_x + 0x108 +
                             ((0x21 - g_wiz_text_font_secondary.textWidth(g_camp_screen->text_buffer)) >>
                              1), row_y - 0xa6 + g_camp_stats_origin_y}, g_camp_screen->text_buffer);
            row_y += 0xe;
        }
        text = g_localized_strings[0x938];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {g_camp_stats_origin_x + 10 +
                         ((0x11f - g_wiz_text_font_secondary.textWidth(text)) >> 1), g_camp_stats_origin_y + 0x8d}, text);
        g_camp_screen->text_buffer = fmt::format(
            "{} {}",
            g_localized_strings[g_character_skill_name_ids
                                    [g_profession_bonus_skills[g_review_character->iProfession]]],
            g_localized_strings[0x8c5]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x10, 0x142}, g_camp_screen->text_buffer);
        int trait_count = 0;
        char traits[0x20];
        for (index = 0; index < 0x20; ++index) {
            if (CharacterHasTrait(g_review_character, static_cast<W8Trait>(index))) {
                traits[index] = 1;
                ++trait_count;
            } else {
                traits[index] = 0;
            }
        }
        int step = (trait_count < 8) ? 2 : 0;
        int trait_y = step + 0x14e;
        for (index = 0; index < 0x20; ++index) {
            if (traits[index] != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x10, trait_y}, g_localized_strings[g_character_trait_name_ids[index]]);
                trait_y += step + 0xc;
            }
        }
        text = g_localized_strings[0x939];
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {g_camp_stats_origin_x + 0x13b +
                         ((0x13b - g_wiz_text_font_secondary.textWidth(text)) >> 1), g_camp_stats_origin_y + 10}, text);
        g_camp_screen->stats_controls->Invalidate(0);
        g_camp_screen->stats_range->m_range->Invalidate(0);
    }
    if ((g_camp_screen->redraw_flags & W8_CAMP_REDRAW_EFFECT_LIST) != 0) {
        InvalidateRegion(0x15d, 0xbe, 0x260, 0x1b5, 0);
        CatalogArtwork::Load(0x1b6, 0).drawRect(GetFrameBufferSurface(), SDL_Rect{0, 0, (0x260) - (0x15d), (0x1b5) - (0xbe)}, 0x15d, 0xbe);
        DrawCampEffectList();
    }
    g_camp_screen->stats_range->m_range->Redraw();
}

/* 0x005C4D40/0x005C4E20: how many beneficial/detrimental lines the equipped
   item in `slot` contributes to its effect-list entry. The armor-class bonus
   counts only on slots that actually take armor (not the two weapon/shield
   rows, 0 and 4/5 and 10/11) and never on equip-class 5 items. */
// FUNCTION: WIZ8 0x005c4d40
unsigned int CountEquipItemBenefits(int slot)
{
    W8ItemDatabaseRecord* record = &g_item_records[g_review_character->EquippedItem[slot].iItemNo];
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
unsigned int CountEquipItemPenalties(int slot)
{
    W8ItemDatabaseRecord* record = &g_item_records[g_review_character->EquippedItem[slot].iItemNo];
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
    if (record->binds_on_equip != 0 && g_review_character->EquippedItem[slot].bound) {
        ++count;
    }
    return count;
}

/* Rebuilds the effect list under the stats page: every active condition, then
   the enchantments, then one entry per worn item that has any modifier at all.
   Alternate-hand slots 8 and 9 are skipped, and unidentified items contribute
   nothing. */
/* Descriptive name for appending an effect and updating both category counts. */
static void AddCampEffectEntry(W8CampScreenState* screen, W8CampEffectEntry* entry)
{
    screen->effect_list.push_back(*entry);
    if (entry->beneficial) {
        ++screen->effect_beneficial_count;
    }
    if (entry->detrimental) {
        ++screen->effect_detrimental_count;
    }
}

// FUNCTION: WIZ8 0x005c4ee0
void RebuildCampEffectList(void)
{
    W8CampScreenState* screen = g_camp_screen;
    screen->effect_list.clear();
    screen->effect_beneficial_count = 0;
    screen->effect_detrimental_count = 0;
    screen->effect_selection = 0;
    screen->effect_first_visible = 0;
    screen->effect_last_visible = 0;
    screen->effect_visible_lines = 0;
    screen->effect_scroll = 0;
    W8Character* character = g_review_character;
    for (int condition = 0x13; condition >= 0; --condition) {
        if (character->uiCondition[condition] != 0) {
            W8CampEffectEntry entry{};
            entry.kind = 0;
            entry.detrimental = true;
            entry.turns = character->uiCondition[condition];
            entry.lines = 1;
            if (entry.turns == W8_CONDITION_INDEFINITE) {
                entry.lines = 2;
            }
            if (condition == W8_CONDITION_DRAINED) {
                if (character->hp_adjustment != 0) {
                    ++entry.lines;
                }
                if (character->fatigue_penalty != 0) {
                    ++entry.lines;
                }
            }
            entry.index = condition;
            AddCampEffectEntry(screen, &entry);
        }
    }
    for (int index = 7; index >= 0; --index) {
        if (character->enchantments[index].turns != 0) {
            W8CampEffectEntry entry{};
            entry.kind = 1;
            entry.beneficial = true;
            entry.enchantment = character->enchantments[index].power;
            entry.turns = character->enchantments[index].turns;
            entry.lines = 2;
            entry.index = index;
            AddCampEffectEntry(screen, &entry);
        }
    }
    for (int slot = 0; slot < 12; ++slot) {
        W8ItemInstance* item = &g_review_character->EquippedItem[slot];
        if (slot != 8 && slot != 9 && item->identified && item->iItemNo != -1) {
            int beneficial = CountEquipItemBenefits(slot);
            int detrimental = CountEquipItemPenalties(slot);
            if (beneficial != 0 || detrimental != 0) {
                W8CampEffectEntry entry{};
                entry.beneficial = beneficial != 0;
                entry.detrimental = detrimental != 0;
                entry.lines = detrimental + beneficial + 1;
                entry.items = true;
                entry.kind = 2;
                entry.turns = W8_CONDITION_INDEFINITE;
                entry.index = slot;
                AddCampEffectEntry(screen, &entry);
            }
        }
    }
    FilterCampEffectList();
}

/* Remarks each list entry against the current tab and beneficial/detrimental
   filter, tracks the first/last visible rows, and reprograms the scrollbar to
   the visible line total. */
// FUNCTION: WIZ8 0x005c5240
void FilterCampEffectList(void)
{
    W8CampScreenState* screen = g_camp_screen;
    screen->effect_visible_lines = 0;
    bool any_visible = false;
    unsigned int count = screen->effect_list.size();
    for (unsigned int pos = 0; pos < count; ++pos) {
        auto& entry = screen->effect_list[pos];
        entry.visible = false;
        if (entry.items == screen->effect_items_only &&
            (screen->effect_filter == W8_CAMP_EFFECT_FILTER_ALL ||
             (screen->effect_filter == W8_CAMP_EFFECT_FILTER_BENEFICIAL && entry.beneficial) ||
             (screen->effect_filter == W8_CAMP_EFFECT_FILTER_DETRIMENTAL &&
              entry.detrimental))) {
            entry.visible = true;
            if (!any_visible) {
                screen->effect_first_visible = pos;
                any_visible = true;
            }
            screen->effect_last_visible = pos;
        }
        if (entry.visible) {
            screen->effect_visible_lines += entry.lines + 1;
        }
    }
    int second = screen->effect_visible_lines - 0x11;
    W8RangeControl* range = screen->stats_range->m_range;
    if (second < 1) {
        second = 0;
        range->SetRangeEnabled(false);
    } else {
        range->SetRangeEnabled(true);
        range->SetRange(0, second);
    }
    if (screen->effect_scroll > second) {
        screen->effect_scroll = second;
    }
    range->SetValue(screen->effect_scroll);
    range->Invalidate(0);
}

/* Draws the visible slice of the effect list into the page's clipping
   window, skipping entries that sit entirely above the scrolled view. */
// FUNCTION: WIZ8 0x005c53c0
void DrawCampEffectList(void)
{
    W8CampScreenState* screen = g_camp_screen;
    SDL_Rect effect_clip{0, 0xbe, 0x280, 0xee};
    ScopedClipRect clipping(GetFrameBufferSurface(), effect_clip);
    int line = -screen->effect_scroll;
    unsigned int count = screen->effect_list.size();
    for (unsigned int pos = 0; pos < count; ++pos) {
        if (line > 0x10) {
            break;
        }
        auto& entry = screen->effect_list[pos];
        if (entry.visible) {
            if (line + entry.lines < 0) {
                line += entry.lines + 1;
            } else {
                DrawCampEffectEntry(&entry, &line);
            }
        }
    }
}

/* Renders one effect-list entry at `line` (in 0xe-pixel rows from
   y 0xbf) and advances the counter past its height plus one row of spacing.
   Kind 0 is a condition, kind 1 an enchantment, kind 2 an equipped item. */
// FUNCTION: WIZ8 0x005c54a0
void DrawCampEffectEntry(W8CampEffectEntry* entry, int* line_out)
{
    int index;
    int line = *line_out;
    if (entry->kind == 0) {
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {}", g_localized_strings[0x8d1],
                                 g_localized_strings[g_condition_notices[entry->index].name]));
        g_wiz_text_font_secondary.resetPalette();
        int next = line + 1;
        if (entry->turns == W8_CONDITION_INDEFINITE) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, (line + 1) * 0xe + 0xbf}, g_localized_strings[0x8d2]);
            next = line + 2;
        }
        line = next;
        if (entry->index == 1) {
            if (g_review_character->hp_adjustment != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8da],
                                         g_review_character->hp_adjustment));
                ++line;
            }
            if (g_review_character->fatigue_penalty != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8dc],
                                         -static_cast<int>(g_review_character->fatigue_penalty)));
                ++line;
            }
        }
    } else if (entry->kind == 1) {
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {} ({:d})", g_localized_strings[0x8d4],
                                 g_localized_strings[g_enchantment_notices[entry->index]],
                                 entry->enchantment));
        g_wiz_text_font_secondary.resetPalette();
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, (line + 1) * 0xe + 0xbf}, fmt::format("{:d} {}", entry->turns, g_localized_strings[0x8d3]));
        *line_out = line + 3;
        return;
    } else if (entry->kind == 2) {
        W8ItemDatabaseRecord* record =
            &g_item_records[g_review_character->EquippedItem[entry->index].iItemNo];
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, GetItemDisplayName(&g_review_character->EquippedItem[entry->index]));
        g_wiz_text_font_secondary.resetPalette();
        int next = line + 1;
        if (record->attack_damage_bonus != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, (line + 1) * 0xe + 0xbf}, fmt::format("{} {:+d}", g_localized_strings[0x8b1], record->attack_damage_bonus));
            next = line + 2;
        }
        line = next;
        std::string value_text;
        for (index = 0; index < 0x10; ++index) {
            if (record->missile_values[index] != 0) {
                g_camp_screen->text_buffer = "";
                g_camp_screen->text_buffer += g_localized_strings[g_attack_effect_name_ids[index]];
                g_camp_screen->text_buffer += " ";
                value_text = fmt::format("{:d}%", record->missile_values[index]);
                g_camp_screen->text_buffer += value_text;
                if (index == 2) {
                    g_camp_screen->text_buffer += " (";
                    g_camp_screen->text_buffer += g_localized_strings[0x8d5];
                    value_text = fmt::format(" {:d})", record->missile_magnitude);
                    g_camp_screen->text_buffer += value_text;
                }
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {}", g_localized_strings[0x8d6], g_camp_screen->text_buffer));
                ++line;
            }
        }
        if (record->attack_hit_bonus != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {:+d}", g_localized_strings[0x8b7], record->attack_hit_bonus));
            ++line;
        }
        if (record->slays_kind != 0xff) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {}", g_localized_strings[0x8d8],
                            g_localized_strings[g_special_category_name_ids[record->slays_kind]]));
            ++line;
        }
        if (record->health_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8d9], record->health_regen_bonus));
            ++line;
        }
        if (record->health_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8da], record->health_regen_bonus));
            ++line;
        }
        if (record->stamina_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8db], record->stamina_regen_bonus));
            ++line;
        }
        if (record->stamina_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8dc], record->stamina_regen_bonus));
            ++line;
        }
        if (record->spell_regen_bonus > 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8dd], record->spell_regen_bonus));
            ++line;
        }
        if (record->spell_regen_bonus < 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{}: {:+d}", g_localized_strings[0x8de], record->spell_regen_bonus));
            ++line;
        }
        if (record->armor_class_bonus != 0 &&
            (record->armor_class_bonus < 0 ||
             (entry->index != 0 && entry->index != 4 && entry->index != 5 && entry->index != 10 &&
              entry->index != 0xb && record->equip_class != W8_ITEM_EQUIP_CLASS_SHIELD))) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {:+d}", g_localized_strings[0x8df], record->armor_class_bonus));
            ++line;
        }
        if (record->modifier_0b3_index != -1) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {:+d}",
                            g_localized_strings
                                [g_character_description_first_ids[record->modifier_0b3_index]],
                            record->modifier_0b3_value));
            ++line;
        }
        if (record->modifier_0b1_index != -1) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format(
                    "{} {:+d}",
                    g_localized_strings[g_character_skill_name_ids[record->modifier_0b1_index]],
                    record->modifier_0b1_value));
            ++line;
        }
        for (index = 0; index < 6; ++index) {
            if (record->resistance_bonus[index] != 0) {
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, fmt::format("{} {} {:+d}%", g_localized_strings[g_attr_table1[6 + index]],
                                g_localized_strings[0x8e0], record->resistance_bonus[index]));
                ++line;
            }
        }
        if (record->binds_on_equip != 0 &&
            g_review_character->EquippedItem[entry->index].bound != 0) {
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {0x15e, line * 0xe + 0xbf}, g_localized_strings[0x8e1]);
            *line_out = line + 2;
            return;
        }
    }
    *line_out = line + 1;
}

/* Wheel input over the stats page's effect-list region steps the
   scrollbar. */
// FUNCTION: WIZ8 0x005c5c60
static unsigned char CampStatsMouseWheel(const InputEvent* event, W8Region*)
{
    PushButtonSoundScheme(0, true);
    if (event->type != InputEventType::MouseWheel) {
        return 0;
    }
    g_camp_screen->stats_range->m_range->AdjustValue(event->wheel);
    return 1;
}

/* Creates the five skill-category regions once per camp session and points
   them at the list handler. Category 4 is the overflow block under the right
   column. */
// FUNCTION: WIZ8 0x005c5cd0
void CreateCampSkillRegions(void)
{
    if (g_camp_skill_regions == 0) {
        g_camp_skill_regions = CreateRegionSet();
        for (unsigned int category = 0; category < 5; ++category) {
            unsigned int region = AddRegionToSet(g_camp_skill_regions);
            SetRegionCallback(region, CampSkillListRegionHandler,
                              static_cast<unsigned short>(category));
            SetRegionHelp(region, true, -1);
            int x = g_camp_skill_category_positions[category].x;
            int y = g_camp_skill_category_positions[category].y;
            if (category == 4) {
                x += 0x136;
            } else {
                y += 0xa5;
            }
            SetRegionBounds(region, x, y, x + 0x6d, g_camp_skill_category_rows[category] * 0xe + y);
        }
    }
    RegionSetEnable(g_camp_skill_regions);
}

// FUNCTION: WIZ8 0x005c5d70
void DisableCampSkillRegions(void)
{
    RegionSetDisable(g_camp_skill_regions);
}

/* Unlike the gameplay best-skill query, the camp display includes every
   occupied slot, regardless of health or conditions. Ties count as best. */
static bool IsBestCampSkillLevel(W8Skill skill, unsigned int level)
{
    for (int slot = 0; slot < 8; ++slot) {
        if (g_status.buffers.XChar[slot].fOccupied &&
            level < g_status.buffers.Char[slot].skills[skill].level) {
            return false;
        }
    }
    return true;
}

/* The skills page's full redraw: the two column panels, the five category
   headers, then every known skill's name and level with the base/current
   delta bar. */
// FUNCTION: WIZ8 0x005c5d80
void DrawCampSkillsPage(void)
{
    if (g_camp_screen->redraw_flags == W8_CAMP_REDRAW_ALL) {
        bool has_fifth = false;
        int skill;
        for (skill = 0; skill < 0x29; ++skill) {
            if ((g_review_character->skills[skill].active ||
                 g_review_character->skills[skill].level != 0) &&
                g_skill_attributes[skill].category == 4) {
                has_fifth = true;
                break;
            }
        }
        CatalogArtwork::Load(0x141, 0).drawAndInvalidate(GetFrameBufferSurface(), has_fifth, 0x136, 0);
        CatalogArtwork::Load(0x141, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0, 0xa5);
        for (int category = 0; category < 5; ++category) {
            if (category != 4 || has_fifth) {
                int x = g_camp_skill_category_positions[category].x;
                int y = g_camp_skill_category_positions[category].y;
                if (category == 4) {
                    x += 0x136;
                } else {
                    y += 0xa5;
                }
                CatalogArtwork::Load(0x144, 0).draw(GetFrameBufferSurface(), static_cast<short>(g_camp_skill_category_images[category]), x - 0x16, y - 3);
            }
        }
        int category_count[5] = {0, 0, 0, 0, 0};
        for (skill = 0; skill < 0x29; ++skill) {
            W8CharacterSkill* value = &g_review_character->skills[skill];
            if (value->active || value->points != 0 || value->level != 0) {
                int category = g_skill_attributes[skill].category;
                int left = g_camp_skill_category_positions[category].x;
                int top = g_camp_skill_category_positions[category].y;
                if (category == 4) {
                    left += 0x136;
                } else {
                    top += 0xa5;
                }
                top += category_count[category] * 0xe;
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
                    CatalogArtwork::Load(0x143, 0).draw(GetFrameBufferSurface(), 0, left + 0x6f, top + 2);
                }
                if (gained == 0) {
                    if (lost != 0) {
                        clip.x = level + 0x6f + left;
                        clip.w = lost;
                        ScopedClipRect clipped(GetFrameBufferSurface(), clip);
                        CatalogArtwork::Load(0x143, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, left + 0x6f, top + 2);
                    }
                } else {
                    clip.x = level + 0x6f + left;
                    clip.w = gained;
                    ScopedClipRect clipped(GetFrameBufferSurface(), clip);
                    CatalogArtwork::Load(0x143, 0).draw(GetFrameBufferSurface(), 1, left + 0x6f, top + 2);
                }
                Palette16 palette;
                if (!g_status.game_started || value->level == 0) {
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
                short width = g_wiz_text_font_secondary.textWidth(g_localized_strings[g_character_skill_name_ids[skill]]);
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {(0x6b - width) / 2 + 2 + left, top + 1}, g_localized_strings[g_character_skill_name_ids[skill]]);
                palette = g_wiz_text_font_secondary.glyphs->colors;
                if (value->improved) {
                    palette = g_font_state_palettes[W8_FONT_PALETTE_GREEN];
                }
                g_wiz_text_font_secondary.setPalette(palette);
                short value_width = g_wiz_text_font_secondary.codepointPrefixWidth(fmt::format("{:d}", value->level), 3);
                DrawTextAndInvalidate(GetFrameBufferSurface(), g_wiz_text_font_secondary, {(0x24 - value_width) / 2 + 0xee + left, top + 1}, fmt::format("{:d}", value->level));
                g_wiz_text_font_secondary.resetPalette();
                g_wiz_text_font_secondary.setShade(Font::kNormalShade);
                ++category_count[category];
            }
        }
    }
}

/* The skills page's region handler: hover updates the help line with the
   skill's info hint, and a right click opens the skill info dialog. The
   dialog's second argument is raised when the character's level is the best
   among the occupied party slots. */
// FUNCTION: WIZ8 0x005c6230
unsigned char CampSkillListRegionHandler(const InputEvent* event, W8Region* region)
{
    int row = (GetEventCursorY(event) - region->y1 - 1) / 0xe;
    int skill = -1;
    int occurrence = 0;
    for (int index = 0; index < 0x29; ++index) {
        if (g_skill_attributes[index].category == static_cast<int>(region->callback_id) &&
            (g_review_character->skills[index].active ||
             g_review_character->skills[index].level != 0)) {
            if (occurrence == row) {
                skill = index;
                break;
            }
            ++occurrence;
        }
    }
    PushButtonSoundScheme(0, true);
    if (event->type == InputEventType::RightButtonDown) {
        region->flags |= W8_REGION_RIGHT_BUTTON_HELD;
    } else if (event->type != InputEventType::RightButtonUp) {
        if (event->type != InputEventType::MouseMove) {
            return 0;
        }
        if ((region->flags & W8_REGION_MOUSE_ENTER) != 0 || row != g_camp_skill_hover_row) {
            std::string text = skill == -1 ? std::string{} : g_localized_strings[0x958];
            SetRegionHelpText(text);
            ResetRegionHelp(true);
            g_camp_skill_hover_row = row;
        }
        return 0;
    }
    if ((region->flags & W8_REGION_RIGHT_BUTTON_HELD) != 0 && skill != -1) {
        bool best = false;
        if (g_status.game_started && g_review_character->skills[skill].level != 0) {
            best = IsBestCampSkillLevel(static_cast<W8Skill>(skill),
                                        g_review_character->skills[skill].level);
        }
        W8SkillInfoDialog* dialog = new W8SkillInfoDialog(
            static_cast<W8Skill>(skill), best, !g_review_character->skills[skill].active,
            skill == g_profession_bonus_skills[g_review_character->iProfession]);
        DisplayCampDialog(dialog);
    }
    return 1;
}

/* The character screen's fourth page (name, portrait, personality and voice)
   and second page (skills). W8_SCREEN_CHARACTER is the camp screen's character
   editor, so both classes sit in this unit between the skills-page handler and
   the CGSSpellsPage anchor; their retail span 0x005C6460..0x005C7D60 matches
   demo 0x005CFAD0..0x005D0FE0 inside the same hull. */

// VTABLE: WIZ8 0x005ef57c W8CharacterPage
// VTABLE: WIZ8 0x005ef578 W8ControlSelectionListener
// VTABLE: WIZ8 0x005ef570 W8TextControl::Listener
// class W8CharacterPersonalityPage
// FUNCTION: WIZ8 0x005c6460
void W8CharacterPersonalityPage::SetCharacter(W8Character* character,
                                              W8CharacterCreationState* creation_state, int mode)
{
    AcquireRegionSet(&g_character_page4_region_set);
    W8CharacterPage::SetCharacter(character, creation_state, mode);
    W8TextControl::Listener* action_listener = this;

    m_control5 =
        new W8TextControl(this, 0xffffffff, 100, 0x19, 0, 0, CatalogArtwork::Load(0x10a, 0), 10, 0xc, 0xb, 0xe, 0xd);
    m_control5->m_listener = action_listener;
    m_control4 = new W8TextControl(this, 0xffffffff, 0x144, 0x19, 0, 0, CatalogArtwork::Load(0x10a, 0), 0xf, 0x11, 0x10, 0x13, 0x12);
    m_control4->m_listener = action_listener;
    m_control7 =
        new W8TextControl(this, 0xffffffff, 100, 0x67, 0, 0, CatalogArtwork::Load(0x10a, 0), 10, 0xc, 0xb, 0xe, 0xd);
    m_control7->m_listener = action_listener;
    m_control6 = new W8TextControl(this, 0xffffffff, 0x144, 0x67, 0, 0, CatalogArtwork::Load(0x10a, 0), 0xf, 0x11, 0x10, 0x13, 0x12);
    m_control6->m_listener = action_listener;
    m_randomize = new W8TextControl(this, 0xffffffff, 0x16d, 0x155, 0, 0, CatalogArtwork::Load(0x10a, 0), 0x14, 0x16, 0x15, 0x18, 0x17);
    m_randomize->m_listener = action_listener;
    m_randomize->EnableRegionHelp(0xf5);

    int index;
    for (index = 0; index < 9; ++index) {
        int column = index % 3;
        int row = index / 3;
        W8TextControl* entry =
            new W8TextControl(this, 0xffffffff, column * 0x80 + 0x24, row * 0xe + 0x107, column * 0x80 + 0xa3, row * 0xe + 0x114, CatalogArtwork::Load(0x105, 0), 5, 7, 6, 8, -1);
        entry->AddLayoutFlags(g_W8TextControlLayoutImageAtOrigin);
        m_personality_selection.AddEntry(entry);
    }
    m_personality_selection.SetSelected(character->personality);
    m_personality_selection.m_selectionListener = this;

    for (index = 0; index < 2; ++index) {
        int top = index == 0 ? 0x140 : 0x15d;
        W8TextControl* entry = new W8TextControl(this, 0xffffffff, 0x21, top, 0x69, top + 0xe, CatalogArtwork::Load(0x105, 0), 5, 7, 6, 8, -1);
        entry->AddLayoutFlags(g_W8TextControlLayoutImageAtOrigin);
        m_voice_selection.AddEntry(entry);
    }
    character->voice = std::clamp(int(character->voice), 0, 1);
    m_voice_selection.SetSelected(character->voice);
    m_voice_selection.m_selectionListener = this;
}

// FUNCTION: WIZ8 0x005c6820
void W8CharacterPersonalityPage::Activate()
{
    EnableRegionSet(true);
    m_prepared = true;
    InitTextInputModeWithScheme(1);
    AddTextInputField(m_bounds.left + 0x97, m_bounds.top + 0xab, 0x106, 0x10, 0x7f,
                      m_character->name_part_2, 5, 0xf, 1);
    AddTextInputField(m_bounds.left + 0x97, m_bounds.top + 0xc7, 0x106, 0x10, 0x7f,
                      m_character->name, 9, 0xf, 1);
    if (GetTextInputFieldByteLength(0) == 0)
        SetActiveField(0);
    else if (GetTextInputFieldByteLength(1) == 0)
        SetActiveField(1);
    m_personality_selection.SetSelected(m_character->personality);
    m_voice_selection.SetSelected(m_character->voice);
}

// FUNCTION: WIZ8 0x005c68f0
void W8CharacterPersonalityPage::Deactivate()
{
    EnableRegionSet(false);
    RemoveTextInputField(1);
    RemoveTextInputField(0);
    KillTextInputMode();
}

// FUNCTION: WIZ8 0x005c6910
void W8CharacterPersonalityPage::Accept()
{
    if (m_mode == 0) {
        InvalidateAndRecalculateCharacterClassData(m_character);
    } else {
        W8Character* original = m_screen->GetOriginalCharacter();
        m_character->personality = original->personality;
        m_character->portrait_index = original->portrait_index;
        m_character->voice = original->voice;
        m_character->name_part_2 = original->name_part_2;
        m_character->name = original->name;
    }
    Refresh();
    Invalidate(0);
    m_screen->UpdateNavigation(this);
}

// FUNCTION: WIZ8 0x005c69a0
void W8CharacterPersonalityPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    *next_enabled = GetTextInputFieldByteLength(0) != 0 && GetTextInputFieldByteLength(1) != 0;
    if (m_mode != 0) {
        W8Character* original = m_screen->GetOriginalCharacter();
        *exit_enabled = false;
        if (m_character->personality == original->personality &&
            m_character->portrait_index == original->portrait_index &&
            m_character->voice == original->voice &&
            strcmp(m_character->name_part_2.c_str(), original->name_part_2.c_str()) == 0 &&
            strcmp(m_character->name.c_str(), original->name.c_str()) == 0) {
            return;
        }
        *exit_enabled = true;
    } else {
        *exit_enabled = true;
    }
}

// FUNCTION: WIZ8 0x005c6a60
void W8CharacterPersonalityPage::HandleInput(InputEvent* input)
{
    if (m_screen->HasDialog())
        return;
    if (!IsTypingEvent(*input)) {
        return;
    }

    const char32_t character = TypedCharacter(*input);
    if (character != 0 && character < 0x80 && strchr("\\/:*?\"<>|", int(character)) != 0) {
        return;
    }
    if (input->key == VK_TAB) {
        SelectNextField();
        return;
    }
    if (!HandleTextInput(input))
        return;

    short field = GetActiveTextInputField();
    if (field == 0) {
        m_character->name_part_2 = GetTextFromField(0);
    } else if (field == 1) {
        m_character->name = GetTextFromField(1);
    }
    m_screen->UpdateNavigation(this);
}

// FUNCTION: WIZ8 0x005c6b20
void W8CharacterPersonalityPage::Refresh()
{
    SetInputFieldText(0, m_character->name_part_2);
    SetInputFieldText(1, m_character->name);
    m_personality_selection.SetSelected(m_character->personality);
    m_voice_selection.SetSelected(m_character->voice);
}

/* The final page's whole redraw: advance the portrait animation, draw the
   fixed text labels and the nine personality labels, then let the dirty
   portrait, description and name-row blocks refresh themselves. */
// FUNCTION: WIZ8 0x005c6b70
void W8CharacterPersonalityPage::Redraw()
{
    if (m_animation_active) {
        int elapsed = static_cast<int>(anim_timer.GetProgress());
        if (elapsed > 0) {
            m_animation_frame = (m_animation_frame + elapsed) % 3;
            CatalogArtwork::Load(0x105, 0).drawAndInvalidate(GetFrameBufferSurface(), m_animation_frame + 2, m_bounds.left + 0x156, m_bounds.top + 0x137);
        }
    }

    bool redraw = m_fEnabled && m_fDirty;
    W8CharacterPage::Redraw();
    if (!m_screen->HasDialog()) {
        RenderAllTextFields();
    }

    if (redraw) {
        W8TextBuffer text;
        W8ControlsRect bounds;

        bounds.left = m_bounds.left + 0x2c;
        bounds.right = m_bounds.left + 100;
        bounds.top = m_bounds.top + 0x19;
        bounds.bottom = m_bounds.top + 0x31;
        text.RenderString(&bounds, g_localized_strings[0xee], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.top = m_bounds.top + 0x67;
        bounds.bottom = m_bounds.top + 0x7f;
        text.RenderString(&bounds, g_localized_strings[0xf0], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.left = m_bounds.left + 0x160;
        bounds.right = m_bounds.left + 0x198;
        text.RenderString(&bounds, g_localized_strings[0xf1], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.top = m_bounds.top + 0x19;
        bounds.bottom = m_bounds.top + 0x31;
        text.RenderString(&bounds, g_localized_strings[0xef], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.left = m_bounds.left + 0x24;
        bounds.right = m_bounds.left + 0x92;
        bounds.top = m_bounds.top + 0xaa;
        bounds.bottom = m_bounds.top + 0xbc;
        text.RenderString(&bounds, g_localized_strings[0x84], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.top = m_bounds.top + 0xc6;
        bounds.bottom = m_bounds.top + 0xd8;
        text.RenderString(&bounds, g_localized_strings[0x85], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.left = m_bounds.left + 0x18;
        bounds.top = m_bounds.top + 0xee;
        bounds.right = m_bounds.left + 0x1a8;
        bounds.bottom = m_bounds.top + 0xfa;
        text.RenderString(&bounds, g_localized_strings[0x86], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        int index = 0;
        for (const unsigned short* message_id = g_personality_message_ids;
             message_id < g_personality_message_ids + 9; ++message_id, ++index) {
            bounds.left = (index % 3) * 0x80 + 0x30 + m_bounds.left;
            bounds.right = bounds.left + 0x80;
            bounds.top = m_bounds.top + 0x106 + (index / 3) * 0xe;
            bounds.bottom = bounds.top + 0xe;
            text.SetLayoutBounds(&bounds, true, true);
            text.SetLayoutMode(g_W8TextBufferAlignTop | g_W8TextBufferAlignLeft);
            text.RenderString(g_localized_strings[*message_id], g_wiz_text_font_secondary, true,
                              GetFrameBufferSurface());
        }

        bounds.top = m_bounds.top + 0x13f;
        bounds.bottom = m_bounds.top + 0x14d;
        bounds.left = m_bounds.left + 0x29;
        bounds.right = m_bounds.left + 0x69;
        text.SetLayoutMode(g_W8TextBufferAlignCenter | g_W8TextBufferAlignTop);
        text.RenderString(&bounds, g_localized_strings[0x8d], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        bounds.top = m_bounds.top + 0x15c;
        bounds.bottom = m_bounds.top + 0x16a;
        text.RenderString(&bounds, g_localized_strings[0x8e], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());

        m_portrait_dirty = true;
        m_description_dirty = true;
        m_animation_active = false;
    }

    if (m_portrait_dirty) {
        CatalogArtwork::Load(0x11, m_character->portrait_index).drawAndInvalidate(GetFrameBufferSurface(), 0, m_bounds.left + 0x86, m_bounds.top + 5, {}, BitmapKey::Opaque);
        m_portrait_dirty = false;
    }

    if (m_description_dirty) {
        W8TextBuffer text;
        W8ControlsRect bounds;
        W8CharacterEvent element(m_character, g_effect0, 0, g_character_event_no_flags,
                                 g_character_event_full_volume);

        bounds.top = m_bounds.top + 0x13a;
        bounds.bottom = m_bounds.top + 0x16a;
        bounds.left = m_bounds.left + 0x70;
        bounds.right = m_bounds.left + 0x150;
        text.SetLayoutBounds(&bounds, true, true);
        text.SetText(element.GetQuoteText(), g_wiz_text_font_secondary);
        text.FillBounds(0x8000);
        text.RenderToTarget(0, true, GetFrameBufferSurface());
        m_description_dirty = false;
    }

    if (m_prepared) {
        W8TextBuffer text;
        W8ControlsRect bounds = {9, 0xec, 0xbd, 0x184};
        text.RenderString(&bounds, g_localized_strings[0xe9], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());
        m_prepared = false;
    }
}

// FUNCTION: WIZ8 0x005c7220
void W8CharacterPersonalityPage::OnPrimary(W8TextControl* control)
{
    int portrait = m_character->portrait_index;
    if (control == m_control4) {
        int group = g_portrait_descriptors[portrait].group + 1;
        if (group > 11)
            group = 0;
        m_character->portrait_index = g_portrait_groups[group].portraits[0];
        m_portrait_dirty = true;
    } else if (control == m_control5) {
        int group = g_portrait_descriptors[portrait].group - 1;
        if (group < 0)
            group = 11;
        m_character->portrait_index = g_portrait_groups[group].portraits[0];
        m_portrait_dirty = true;
    } else if (control == m_control6 || control == m_control7) {
        int group = g_portrait_descriptors[portrait].group;
        W8PortraitGroup* portraits = &g_portrait_groups[group];
        int index = 0;
        while (index < portraits->count && portraits->portraits[index] != portrait) {
            ++index;
        }
        if (control == m_control6) {
            ++index;
            if (index >= portraits->count)
                index = 0;
        } else {
            --index;
            if (index < 0)
                index = portraits->count - 1;
        }
        m_character->portrait_index = portraits->portraits[index];
        m_portrait_dirty = true;
    } else if (control == m_randomize) {
        m_animation_active = true;
        m_animation_frame = 2;
        anim_timer.Restart();
        ShadowRect(GetFrameBufferSurface(), {0, 0, 0x281, 0x1e1});
        ResetTransientRenderScenes();
        m_screen->ShowCharacterSummary();
    }

    if (control != m_randomize) {
        m_screen->UpdateNavigation(this);
    }
}

// FUNCTION: WIZ8 0x005c73b0
void W8CharacterPersonalityPage::OnSelectionChanged(W8ControlSelection* control, int selected)
{
    if (control == &m_voice_selection) {
        m_character->voice = selected;
    } else {
        m_character->personality = selected;
    }
    m_description_dirty = true;
    m_screen->UpdateNavigation(this);
}

// FUNCTION: WIZ8 0x005c73f0
W8CharacterPersonalityPage* CreateCharacterPersonalityPage()
{
    return new W8CharacterPersonalityPage;
}

// VTABLE: WIZ8 0x005ef5c8 W8CharacterPage
// VTABLE: WIZ8 0x005ef5c0 W8CharacterPageEntryListener
// class W8CharacterSkillsPage
// FUNCTION: WIZ8 0x005c7580
void W8CharacterSkillsPage::SetCharacter(W8Character* character,
                                         W8CharacterCreationState* creation_state, int mode)
{
    AcquireRegionSet(&g_character_page2_region_set);
    W8CharacterPage::SetCharacter(character, creation_state, mode);
    int category_count[5] = {0, 0, 0, 0, 0};
    for (int skill = 0; skill < 0x29; ++skill) {
        int category = g_skill_attributes[skill].category;
        W8CharacterPageEntry* entry = new W8CharacterPageEntry(
            this, g_character_page2_category_geometry[category].x,
            g_character_page2_category_geometry[category].y + category_count[category] * 0xe, true);
        AddEntry(entry);
        entry->m_listener = this;
        ++category_count[category];
    }
    nav_next_state = false;
}

// FUNCTION: WIZ8 0x005c76a0
void W8CharacterSkillsPage::Activate()
{
    EnableRegionSet(true);
    Refresh();
    m_dirty = true;
    m_prepared = true;
}

void W8CharacterSkillsPage::Deactivate()
{
    EnableRegionSet(false);
}

// FUNCTION: WIZ8 0x005c76c0
void W8CharacterSkillsPage::Accept()
{
    RefundAllSkillPoints(m_character, m_creation_state);
    Invalidate(0);
    m_dirty = true;
    m_screen->UpdateNavigation(this);
    for (int index = 0; index < static_cast<int>(m_entries.size()); ++index) {
        m_entries[index]->UpdateButtons();
    }
}

// FUNCTION: WIZ8 0x005c7720
void W8CharacterSkillsPage::GetNavigationState(bool* next_enabled, bool* exit_enabled)
{
    *next_enabled = m_creation_state->skills_complete;
    *exit_enabled = m_creation_state->skill_points_remaining < m_creation_state->skill_points_total;
    if (*next_enabled != nav_next_state) {
        for (int index = 0; index < static_cast<int>(m_entries.size()); ++index) {
            m_entries[index]->SetIncrementAllowed(!*next_enabled);
        }
        nav_next_state = *next_enabled;
    }
}

// FUNCTION: WIZ8 0x005c7790
void W8CharacterSkillsPage::AdjustEntry(W8CharacterPageEntry* entry, int delta)
{
    entry->MarkDirty();
    m_dirty = true;
    InitializeLevelUpAttributePool(m_character, m_creation_state, entry->m_id, delta);
    m_screen->UpdateNavigation(this);
}

// FUNCTION: WIZ8 0x005c77d0
void W8CharacterSkillsPage::ShowEntryInfo(W8CharacterPageEntry* entry)
{
    m_screen->ShowSkillInfo(static_cast<W8Skill>(entry->m_id));
}

// FUNCTION: WIZ8 0x005c77f0
void W8CharacterSkillsPage::Redraw()
{
    bool redraw = static_cast<unsigned char>(m_fEnabled && m_fDirty);
    if (m_force_redraw) {
        UpdateEntries();
        Invalidate(0);
        redraw = true;
        m_force_redraw = false;
    }
    W8CharacterPage::Redraw();

    if (redraw) {
        for (int category = 0; category < 5; ++category) {
            if (category != 4 || m_show_fifth_category) {
                CatalogArtwork::Load(0x144, 0).draw(GetFrameBufferSurface(), static_cast<short>(g_character_page2_category_frames[category]), m_bounds.left + g_character_page2_category_geometry[category].x -
                                     0x16, m_bounds.top + g_character_page2_category_geometry[category].y - 3);
            }
        }
        if (!m_show_fifth_category) {
            CatalogArtwork::Load(0x108, 0).draw(GetFrameBufferSurface(), 1, m_bounds.left, m_bounds.top + 0x118);
        }
    }

    if (m_prepared) {
        W8TextBuffer text;
        W8ControlsRect bounds = {4, 0xec, 0xc2, 0x162};
        text.RenderString(&bounds, g_localized_strings[0xe8], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());
        bounds.top = 0x162;
        bounds.right = 0x8f;
        bounds.bottom = 0x179;
        text.RenderString(&bounds, g_localized_strings[0xe3], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());
        bounds.top = 0x184;
        bounds.bottom = 0x19b;
        text.RenderString(&bounds, g_localized_strings[0xe4], g_wiz_text_font_secondary, true,
                          GetFrameBufferSurface());
        bounds.left = 0x8f;
        bounds.top = 0x162;
        bounds.right = 0xbf;
        bounds.bottom = 0x179;
        CatalogArtwork::Load(0x107, 0).draw(GetFrameBufferSurface(), 5, 0x8f, 0x162);
        text.SetLayoutBounds(&bounds, true, true);
        text.RenderString(fmt::format("{:d}", m_creation_state->skill_step_limit),
                          g_options_detail_font, true, GetFrameBufferSurface());
        m_prepared = false;
    }

    if (m_dirty) {
        W8TextBuffer text;
        W8ControlsRect bounds = {0x8f, 0x184, 0xbf, 0x19b};
        CatalogArtwork::Load(0x107, 0).draw(GetFrameBufferSurface(), 5, 0x8f, 0x184);
        text.SetLayoutBounds(&bounds, true, true);
        text.RenderString(fmt::format("{:d}/{:d}", m_creation_state->skill_points_remaining,
                                      m_creation_state->skill_points_total),
                          g_options_detail_font, true, GetFrameBufferSurface());
        m_dirty = false;
    }
}

// FUNCTION: WIZ8 0x005c7b50
void W8CharacterSkillsPage::UpdateEntries()
{
    int index;
    for (index = 0; index < 0x29; ++index) {
        m_entries[index]->SetEnabled(false);
    }

    int category_count[5] = {0, 0, 0, 0, 0};
    m_show_fifth_category = false;
    for (int skill = 0; skill < 0x29; ++skill) {
        W8CharacterSkill* value = &m_character->skills[skill];
        if (value->active || value->points != 0) {
            int category = g_skill_attributes[skill].category;
            int entry_index = 0;
            int occurrence = 0;
            for (; entry_index < 0x29; ++entry_index) {
                if (g_skill_attributes[entry_index].category == category &&
                    occurrence++ == category_count[category]) {
                    break;
                }
            }
            W8CharacterPageEntry* entry = m_entries[entry_index].get();
            ++category_count[category];
            if (category == 4)
                m_show_fifth_category = true;
            entry->SetContent(skill, g_localized_strings[g_character_skill_name_ids[skill]],
                              &value->points, &m_creation_state->skill_points_spent[skill],
                              &m_creation_state->skill_limits[skill], 0x101);
            entry->SetLabelFontState(
                skill == g_profession_bonus_skills[m_character->iProfession] ? 3 : -1);
        }
    }
}

// FUNCTION: WIZ8 0x005c7cc0
W8CharacterSkillsPage* CreateCharacterSkillsPage()
{
    return new W8CharacterSkillsPage;
}

// FUNCTION: WIZ8 0x005c7d30
void W8CharacterSkillsPage::Refresh()
{
    m_force_redraw = true;
}
