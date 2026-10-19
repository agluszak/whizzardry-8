#include "wiz8/local_screens/ReviewCharacterScreen.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_surface.h>
#include <fmt/format.h>
#include <fmt/printf.h>

#include "audio_resources.h"
#include "compat/native.h"
#include "dialog_code/DialogBase.h"
#include "dice.h"
#include "equipment_slots.h"
#include "gameplay_modifiers.h"
#include "glm/ext/vector_float4.hpp"
#include "layouts/learned_spells.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "local_code/ControlsRect.h"
#include "local_screens/RCSStatsPage.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/button_interaction.h"
#include "wiz8/canvas/Font.h"
#include "wiz8/canvas/himage.h"
#include "wiz8/canvas/image.h"
#include "wiz8/canvas/sprite.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_event_queue.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_stats.h"
#include "wiz8/conditions.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/MessageDialogBase.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/item_location.h"
#include "wiz8/item_rules.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/party_slot.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/learned_spells.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/RangeControl.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSPortraits.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/MainMenuScreen.h"
#include "wiz8/local_screens/PartySelectionScreen.h"
#include "wiz8/local_screens/PleaseWaitScreen.h"
#include "wiz8/local_screens/RCSCommon.h"
#include "wiz8/local_screens/RCSItemsPage.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/music_playlist.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/party_stats.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/input.h"
#include "wiz8/platform/random.h"
#include "wiz8/platform/timer.h"
#include "wiz8/render/compositor.h"
#include "wiz8/render/draw_shader.h"
#include "wiz8/render/frame.h"
#include "wiz8/spell_ids.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

/* Local Screens\ReviewCharacterScreen.cpp, named by entry's
   fFoundEquipChar assertion. This is state 6, reached both from the party
   selector and from the running game. */

std::unique_ptr<W8CampScreenState> g_camp_screen;

W8CampScreenState::~W8CampScreenState()
{
    RetireModal(std::move(dialog), false);
}
std::uint64_t g_fade_tick_base;
void (*g_fade_callback)(void);
unsigned char g_fade_flag;
unsigned int g_fade_duration;
std::unique_ptr<OverlayQuad> g_fade_overlay;
int g_fade_out;
wiz8::sound::Playback g_ending_sound;
bool g_ending_screen;
bool g_ending_autosave;

/* The font-state palette index selected for each load category
   while the weight line is drawn; zero leaves the default palette in place. */
int g_load_category_palettes[5] = {0xf, 3, 1, 5, 0};

/* One portrait frame per race and gender, race-major in threes. */
int g_race_portrait_images[0x30] = {
    0x125, 0x126, 0x125, 0x127, 0x128, 0x127, 0x129, 0x12a, 0x129, 0x12b, 0x12c, 0x12b,
    0x12d, 0x12e, 0x12d, 0x12f, 0x130, 0x12f, 0x131, 0x132, 0x131, 0x133, 0x134, 0x133,
    0x135, 0x136, 0x135, 0x137, 0x138, 0x137, 0x139, 0x13a, 0x139, 0x13b, 0x13b, 0x13b,
    0x13c, 0x13c, 0x13c, 0x13d, 0x13d, 0x13d, 0x13e, 0x13e, 0x13e, 0x13f, 0x13f, 0x13f,
};

/* The seven attribute label message ids, drawn top to bottom on
   the secondary panel. The last two entries swap relative to attribute order. */
int g_attribute_label_ids[7] = {0x924, 0x925, 0x926, 0x927, 0x928, 0x92a, 0x929};

/* The help-line weight breakdown, "<personal>: n, <party>: n". */

W8CampScreenRegion g_camp_screen_regions[12] = {
    {0x0bc, 0x0b0, 0x2d, 0x39, 0x00, 0x01, 0x0ee, 0x0c1, 0},
    {0x1af, 0x0c6, 0x28, 0x28, 0x28, 0x08, 0x1aa, 0x0de, 1},
    {0x1af, 0x0f1, 0x28, 0x28, 0x24, 0x08, 0x1aa, 0x0f1, 1},
    {0x178, 0x0b0, 0x26, 0x49, 0x2c, 0x09, 0x173, 0x0b0, 1},
    {0x086, 0x0f1, 0x39, 0x3a, 0x04, 0x02, 0x0c4, 0x11c, 0},
    {0x183, 0x101, 0x23, 0x2b, 0x20, 0x07, 0x17e, 0x10b, 1},
    {0x09e, 0x134, 0x22, 0x65, 0x0c, 0x03, 0x0a0, 0x134, 0},
    {0x190, 0x134, 0x22, 0x48, 0x18, 0x06, 0x1b0, 0x134, 1},
    {0x079, 0x134, 0x22, 0x65, 0x08, 0x03, 0x07b, 0x154, 0},
    {0x1b5, 0x134, 0x22, 0x48, 0x1c, 0x06, 0x1d1, 0x154, 1},
    {0x0cb, 0x167, 0x22, 0x4d, 0x10, 0x04, 0x0f2, 0x18f, 0},
    {0x16a, 0x17a, 0x1a, 0x3a, 0x14, 0x05, 0x165, 0x19f, 1}};

/* 0x0064DD14, 0x0064DD20, 0x0064DD28, 0x00648164: the spell page's small
   formats - zero-padded cost, plain number, plain string and the realm label
   prefix. */

/* Enable or disable the six spell-realm scrollbars together. While enabling, a
   realm whose learned spells fit the eight visible rows keeps its bar off. */
void W8CampSpellsPage::SetCampSpellRangesEnabled(bool enable)
{
    W8RangeControl* spell_range;
    int realm;
    int second;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        spell_range = spell_ranges[realm].get();
        spell_range->SetActive(enable);
        if (enable) {
            second = screen.reviewed->skill_unlocks[0x1c + realm] - 8;
            if (second < 1) {
                spell_range->SetEnabled(false);
            } else {
                spell_range->SetEnabled(true);
                spell_range->SetRange(0, second);
            }
        }
    }
}

/* Re-enable all six spell-realm scrollbars after the learned-spell lists were
   rebuilt for a new character. */
void W8CampSpellsPage::RefreshCampSpellRanges(void)
{
    W8RangeControl* spell_range;
    int realm;
    int second;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        spell_range = spell_ranges[realm].get();
        spell_range->SetActive(true);
        second = screen.reviewed->skill_unlocks[0x1c + realm] - 8;
        if (second < 1) {
            spell_range->SetEnabled(false);
        } else {
            spell_range->SetEnabled(true);
            spell_range->SetRange(0, second);
        }
    }
}

/* The spell page proper: one panel per realm, each showing the realm name and
   skill level, the spell-point pool and up to eight learned spell rows; the
   row under the cursor gets the highlight palette and unaffordable or
   unusable spells dim. */
void W8CampSpellsPage::DrawCampSpellPages(void)
{
    W8Character* character = screen.reviewed;
    const W8SpellRealmAnimation* animation;
    int realm;
    int row;
    int left;
    int top;
    int row_top;
    int spell_id;
    int width;
    unsigned int visible;
    Palette16 palette;

    if ((screen.redraw_flags & W8_CAMP_REDRAW_RESISTANCES) != 0 && !gpSCSV) {
        DrawCampResistances();
    }
    if (screen.redraw_flags == W8_CAMP_REDRAW_ALL) {
        CatalogArtwork::Load(0x140, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0, 0xa5);
    }
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        if ((screen.redraw_flags & (W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm)) == 0) {
            continue;
        }
        left = (realm % 3) * 0xd5 + 3;
        top = (realm / 3) * 0x8c + 0xaa;
        if (character->skill_unlocks[0x1c + realm] != 0) {
            CatalogArtwork::Load(0x140, 0).drawAndInvalidate(GetFrameBufferSurface(), 3, left, top);
            g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_GREEN]);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left + 0x1b, top + 8},
                     fmt::format("{}:", g_localized_strings[0x8c7]));
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left + 0x72, top + 8},
                     g_localized_strings[0x8c9]);
            g_wiz_text_font_secondary.resetPalette();
            width = g_wiz_text_font_secondary.textWidth(
                fmt::format("{}: ", g_localized_strings[0x8c7]));
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {left + 0x1b + width, top + 8},
                     fmt::format("{:3d}", character->skills[W8_SKILL_FIRE_MAGIC + realm].level));
            width = g_wiz_text_font_secondary.codepointPrefixWidth(
                fmt::format(
                    "{:d}/{:d}",
                    GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
                    character->sp_max[realm]),
                7);
            DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                     {left + 0xc8 - width, top + 8},
                     fmt::format(
                         "{:d}/{:d}",
                         GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
                         character->sp_max[realm]));
            visible = character->skill_unlocks[0x1c + realm];
            if (visible >= 8) {
                visible = 8;
            }
            row_top = top + 0x19;
            for (row = 0; static_cast<unsigned int>(row) < visible; ++row) {
                spell_id =
                    learned_spells.spell_ids_by_realm[realm][learned_spells.scroll[realm] + row];
                if (spell_targets[realm]->hovered() && selected_spell_row == row) {
                    palette = g_font_state_palettes[W8_FONT_PALETTE_YELLOW];
                } else if (g_spell_records[spell_id].spell_point_cost <=
                               character->iSPLeft[realm] &&
                           SpellUsableNow(spell_id, false)) {
                    palette = g_wiz_text_font_secondary.glyphs->colors;
                } else {
                    palette = g_font_state_palettes[W8_FONT_PALETTE_RED];
                }
                g_wiz_text_font_secondary.setPalette(palette);
                width = g_wiz_text_font_secondary.codepointPrefixWidth(
                    fmt::format("{:03d}", g_spell_records[spell_id].spell_point_cost), 3);
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left + 0x1c, row_top},
                         g_spell_records[spell_id].display_name);
                DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
                         {left + 0xb1 - width, row_top},
                         fmt::format("{:3d}", g_spell_records[spell_id].spell_point_cost));
                row_top += 0xd;
            }
            g_wiz_text_font_secondary.resetPalette();
        }
        spell_ranges[realm]->Redraw(GetFrameBufferSurface(), true);
    }
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        if (screen.dialog != 0 && realm != 0 && realm != 3) {
            continue;
        }
        if ((screen.redraw_flags & ((W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm) | 0x10000)) == 0) {
            continue;
        }
        animation = &g_spell_realm_animations[realm];
        if (character->sp_max[realm] != 0) {
            CatalogArtwork::Load(animation->image, 0)
                .drawAndInvalidate(GetFrameBufferSurface(), animation_frames[realm],
                                   (realm % 3) * 213 + 5, (realm / 3) * 140 + 0xac);
        } else {
            CatalogArtwork::Load(animation->image, 0)
                .drawAndInvalidate(GetFrameBufferSurface(), animation->initial_frame,
                                   (realm % 3) * 213 + 5, (realm / 3) * 140 + 0xac);
        }
    }
}

/* The six resistance bars along the spell page's top strip: each bar clips its
   fill to the learned portion, the overflow to the bonus band or the shortfall
   to the gap, then right-aligns the value inside the bar. */
void W8CampSpellsPage::DrawCampResistances(void)
{
    W8Character* character = screen.reviewed;
    const W8SpellRealmAnimation* animation;
    std::string text;
    int index;
    int left;
    int top;
    int width;

    CatalogArtwork::Load(0x140, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x136, 0);
    text = g_localized_strings[0x8ca];
    width = g_wiz_text_font_secondary.textWidth(text);
    DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary,
             {(0x134 - width) / 2 + 0x144, 0x1e}, text);
    for (index = 0; index < 6; ++index) {
        animation = &g_spell_realm_animations[index];
        left = (index & 1) * 156 + 0x144;
        top = (index >> 1) * 28 + 0x35;
        CatalogArtwork::Load(animation->image, 0)
            .draw(GetFrameBufferSurface(), animation->initial_frame, left, top);
        DrawCampValueBar(character->resistances[index].total, character->resistances[index].base,
                         left + 0x18, top + 4);
        width = g_wiz_text_font_secondary.codepointPrefixWidth(
            fmt::format("{:d}", character->resistances[index].total), 5);
        DrawText(GetFrameBufferSurface(), g_wiz_text_font_secondary, {left + 0x93 - width, top + 3},
                 fmt::format("{:d}", character->resistances[index].total));
    }
}

/* The spell-list region callback: the callback id is the realm index, the
   event's cursor position picks the highlighted row within the eight visible
   ones, wheel input scrolls the realm's range control, and activating a valid
   row opens its spell info dialog. */
UiResult W8CampSpellsPage::HandleCampSpellList(const UiPointerEvent& event, int realm)
{

    int row = (event.local.y - 1) / 13;
    const int visible = std::min(8, int(screen.reviewed->skill_unlocks[0x1c + realm]) -
                                        int(learned_spells.scroll[realm]));
    if (!event.inside || row < 0 || row >= visible) row = -1;
    if (row != selected_spell_row || event.action == UiPointerAction::Enter ||
        event.action == UiPointerAction::Leave) {
        selected_spell_row = row;
        screen.redraw_flags |= W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm;
    }
    if (event.action == UiPointerAction::Wheel) {
        spell_ranges[realm]->AdjustValue(event.wheel);
    } else if (event.action == UiPointerAction::Up && row != -1 && event.captured_buttons) {
        const auto spell =
            learned_spells.spell_ids_by_realm[realm][learned_spells.scroll[realm] + row];
        AfterUiDispatch([this, spell] { OpenSpellInfoDialog(spell); });
    }
    return UiResult::Handled;
}

/* Open the spell info dialog for one spell id off the camp spell lists. */
void W8CampSpellsPage::OpenSpellInfoDialog(unsigned int spell_id)
{
    auto dialog = std::make_unique<W8SpellInfoDialog>(spell_id);
    dialog->SetText("");
    screen.DisplayCampDialog(std::move(dialog));
}

W8CampCharacterInfo::W8CampCharacterInfo(W8CampScreenState& screen) : screen(screen)
{
    const auto button = [this](int frame, int help, bool combat) {
        auto result = std::make_unique<W8ButtonWidget>(input);
        result->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0x121, 0).spriteSheet(), frame + 3, frame,
                                 frame + 2, frame + 1, frame + 4),
            {.on_click = [this, combat](
                             auto*) { AfterUiDispatch([this, combat] { SetCombatView(combat); }); },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = help,
             .priority = UI_PRIORITY_LOWEST,
             .menu_artwork = true});

        result->SetPosition(0x13e, 0x8b);
        return result;
    };
    m_stats_tab = button(15, 0x960, false);
    m_combat_tab = button(10, 0x95f, true);
    constexpr SDL_Point positions[] = {{0x1d6, 0x4c}, {0x258, 0x4c}, {0x1d6, 0x3e}, {0x258, 0x3e}};
    for (int index = 0; index < 4; ++index) {
        m_values[index] = &input.add(SDL_Rect{positions[index].x, positions[index].y, 33, 13}, {});
        m_values[index]->setTooltip([this, index] { return m_help[index]; });
    }
    input.setEnabled(false);
}

void W8CampCharacterInfo::SetActive(bool enabled)
{
    m_active = enabled;
    input.setEnabled(enabled);
    if (enabled) SetCombatView(m_combat_view);
}

void W8CampCharacterInfo::SetCombatView(bool enabled)
{
    m_combat_view = enabled;
    m_stats_tab->SetVisible(enabled);
    m_combat_tab->SetVisible(!enabled);
    m_values[0]->setEnabled(enabled);
    m_values[1]->setEnabled(enabled);
    m_values[2]->setEnabled(enabled);
    m_values[3]->setEnabled(enabled);
    if (enabled) {
        m_backgroundImage = 0;
    } else if (screen.reviewed->armor_class_components[W8_AC_COMPONENT_REFLEXTION] > 0) {
        m_backgroundImage = 2;
    } else {
        m_backgroundImage = 1;
    }
    InvalidateAll();
}

unsigned short g_camp_armor_class_labels[12] = {1052, 1053, 1054, 1055, 1056, 1057,
                                                1058, 1059, 1060, 1061, 1062, 1063};

void W8CampCharacterInfo::Redraw(SDL_Surface& target, bool full_redraw)
{
    std::string text;
    bool redraw = m_active && m_dirty;
    if (!m_combat_view &&
        ((screen.reviewed->armor_class_components[W8_AC_COMPONENT_REFLEXTION] > 0 &&
          m_backgroundImage != 2) ||
         (screen.reviewed->armor_class_components[W8_AC_COMPONENT_REFLEXTION] <= 0 &&
          m_backgroundImage == 2))) {
        SetCombatView(false);
    }
    if (!m_active) return;
    redraw |= full_redraw || m_dirty;
    if (redraw)
        CatalogArtwork::Load(0x122, 0).drawAndInvalidate(target, m_backgroundImage, 0x136, 0);
    m_stats_tab->m_dirty |= redraw;
    m_combat_tab->m_dirty |= redraw;
    m_stats_tab->Refresh();
    m_combat_tab->Refresh();
    if (!redraw) return;
    m_dirty = false;
    screen.items.InvalidateControls();
    DrawRcsText(g_localized_strings[0x935], 0x15e, 0x84, 0x4e,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    text = fmt::format("{:d}", screen.reviewed->kill_count);
    DrawRcsText(text, 0x1ae, 0x84, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x936], 0x15e, 0x92, 0x4e,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    text = fmt::format("{:d}", screen.reviewed->death_count);
    DrawRcsText(text, 0x1ae, 0x92, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    if (m_combat_view) {
        DrawRcsText(g_localized_strings[0x8b0], 0x15b, 10, 0x11d,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8b1], 0x15e, 0x30, 0x4e,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
        text = fmt::format("{:d}", screen.reviewed->initiative);
        DrawRcsText(text, 0x1ae, 0x30, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        if (screen.reviewed->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1 &&
            screen.reviewed->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1) {
            DrawRcsText(g_localized_strings[0x8b5], 0x1d6, 0x22, 0x50,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            DrawRcsText(g_localized_strings[0x8b4], 0x228, 0x22, 0x50,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        } else {
            DrawRcsText(g_localized_strings[0x8b2], 0x1d6, 0x22, 0x50,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            DrawRcsText(g_localized_strings[0x8b3], 0x228, 0x22, 0x50,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        }
        DrawRcsText(g_localized_strings[0x8b6], 0x1f8, 0x30, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8bd], 0x1f8, 0x3e, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8bc], 0x1f8, 0x4c, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8ba], 0x1f8, 0x5a, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8bb], 0x1f8, 0x68, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8b7], 0x1f8, 0x76, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8b8], 0x1f8, 0x84, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8b9], 0x1f8, 0x92, 0x5e,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        bool unknown_partner =
            ItemHasSingledOutGenericName(
                screen.reviewed->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo) &&
            !screen.reviewed->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].identified;
        for (unsigned int hand = 0; hand < 2; ++hand) {
            W8HandAttack* attack = &screen.reviewed->Hand[hand];
            if (!attack->in_play) continue;
            int x = hand ? 600 : 0x1d6;
            if (screen.reviewed->EquippedItem[hand + 6].iItemNo != -1 &&
                (!screen.reviewed->EquippedItem[hand + 6].identified ||
                 (hand == 0 && unknown_partner))) {
                for (int row = 0; row < 8; ++row) {
                    DrawRcsText("?", x, 0x30 + row * 14, 0x20,
                                g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
                }
                text = g_localized_strings[0x95c];
                text += "?";
                text += g_localized_strings[0x95b];
                text += "?";
                text += g_localized_strings[0x95a];
                text += "?";
                m_help[hand + 2] = text;
                text = g_localized_strings[0x959];
                text += "?";
                text += g_localized_strings[0x95a];
                text += "?";
                m_help[hand] = text;
                continue;
            }
            W8Dice dice;
            GetCharacterHandDamageDice(screen.reviewed, hand, &dice);
            unsigned int damage_bonus = GetCharacterHandDamageBonus(screen.reviewed, hand);
            unsigned int minimum = (dice.Minimum() * (100 + damage_bonus) + 50) / 100;
            unsigned int maximum = (dice.Maximum() * (100 + damage_bonus) + 50) / 100;
            if (minimum < 2) minimum = 1;
            if (maximum < 2) maximum = 1;
            int hit_bonus = attack->hit_bonus + screen.reviewed->bonus.hit_bonus;
            int skill_bonus =
                (attack->attack_score < 0 ? attack->attack_score - 2 : attack->attack_score + 2) /
                5;
            text = fmt::format("{:+d}", attack->damage_bonus + screen.reviewed->bonus.damage_bonus);
            DrawRcsText(text, x, 0x30, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = fmt::format("{:d}-{:d}", minimum, maximum);
            DrawRcsText(text, x, 0x3e, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = g_localized_strings[0x95c];
            text += fmt::format(" {:d}, ", dice.Minimum());
            text += g_localized_strings[0x95b];
            text += fmt::format(" {:d}, ", dice.Maximum());
            text += g_localized_strings[0x95a];
            text += fmt::format(" {:+d}%", static_cast<int>(damage_bonus));
            m_help[hand + 2] = text;
            text = fmt::format("{:d}", skill_bonus + hit_bonus);
            DrawRcsText(text, x, 0x4c, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = g_localized_strings[0x959];
            text += fmt::format(" {:d}, ", skill_bonus);
            text += g_localized_strings[0x95a];
            text += fmt::format(" {:+d}", hit_bonus);
            m_help[hand] = text;
            text = fmt::format("{:d}", attack->attacks);
            DrawRcsText(text, x, 0x5a, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = fmt::format("{:d}", attack->swings);
            DrawRcsText(text, x, 0x68, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = fmt::format("{:+d}", hit_bonus);
            DrawRcsText(text, x, 0x76, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = fmt::format("{:+d}", attack->attack_bonus + screen.reviewed->bonus.attack_bonus);
            DrawRcsText(text, x, 0x84, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            text = fmt::format("{:+d}%", static_cast<int>(damage_bonus));
            DrawRcsText(text, x, 0x92, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        }
    } else {
        DrawRcsText(g_localized_strings[0x8be], 0x15b, 10, 0x11d,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        for (unsigned int component = 0; component < 12; ++component) {
            if (component == 11 &&
                screen.reviewed->armor_class_components[W8_AC_COMPONENT_REFLEXTION] == 0)
                continue;
            int y = (component % 6) * 14 + 0x22;
            int label_x = component / 6 ? 0x1ef : 0x15e;
            int value_x = component / 6 ? 600 : 0x1c7;
            DrawRcsText(g_localized_strings[g_camp_armor_class_labels[component]], label_x, y, 0x67,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
            int value = screen.reviewed->armor_class_components[component];
            if (value) {
                text = fmt::format("{:+d}", value);
                DrawRcsText(text, value_x, y, 0x20,
                            g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            }
        }
        DrawRcsText(g_localized_strings[0x8bf], 0x1da, 0x84, 0x7c,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
        text = fmt::format("{:d}%", screen.reviewed->damage_reduction);
        DrawRcsText(text, 600, 0x84, 0x20, g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    }
}

void W8CampScreenState::CreateCampInput()
{
    items.CreateInput();
    spells.CreateInput();
    main_targets[0] =
        &main_input.add(SDL_Rect{164, 12, 91, 73}, [this](const UiPointerEvent& event) {
            return HandleCampDismissPortrait(event);
        });
    main_targets[0]->setTooltip([] { return g_localized_strings[2368]; });
    main_targets[9] =
        &main_input.add(SDL_Rect{106, 96, 197, 14}, [this](const UiPointerEvent& event) {
            return HandleCampCharacterHeader(event);
        });
    main_targets[9]->setTooltip([] { return g_localized_strings[2362]; });
    main_targets[10] =
        &main_input.add(SDL_Rect{106, 124, 197, 10}, [this](const UiPointerEvent& event) {
            return HandleCampProfessionHistory(event);
        });
    main_targets[10]->setTooltip([] { return g_localized_strings[2363]; });
    for (int slot = 0; slot < 8; ++slot) {
        main_targets[1 + slot] = &main_input.add(
            SDL_Rect{(slot & 1) * 48 + 6, (slot / 2) * 39 + 5, 46, 37},
            [this, slot](const UiPointerEvent& event) { return HandleCampPortrait(event, slot); });
    }
}

bool CampScreenEnter()
{
    if (!g_camp_screen) g_camp_screen = std::make_unique<W8CampScreenState>();
    return g_camp_screen->Enter(CurrentScreenState());
}

bool W8CampScreenState::Enter(const W8ScreenStateRuntime& request)
{
    std::string text;

    ClearPrimarySurface();
    reviewed = request.character;
    if (request.parameter_4) {
        wiz8::sound::play(s_general_magic_sound);
        SetTargetingMode(W8_TARGET_NEED_ITEM);
    }
    gXStatus.item_drag.reset();
    CreatePartyPortraitEventRegions();
    CreateCampInput();
    items.Reset(request.parameter_4);
    CreateCampButtonPanel();
    items.CreateCampItemControls();
    CreateCampHeaderButtons();
    page = W8_CAMP_PAGE_ITEMS;
    spells.CreateControls();
    stats.CreateControls();
    character_info = std::make_unique<W8CampCharacterInfo>(*this);
    ActivateCampPage();
    if (g_combat.active() &&
        g_combat.state()->equip_phase != W8CombatEquipPhase::action_selection) {
        if (g_combat.state()->equip_pending == 1) {
            for (int slot = 0; slot < 8; ++slot) {
                if (g_status.party.rows[slot].fOccupied && IsPartySlotEligible(slot) &&
                    g_status.party.rows[slot].pending_action == W8_ACTION_EQUIP) {
                    text = fmt::format("{} {}", g_status.party.characters[slot].name,
                                       g_localized_strings[0x919]);
                    goto show_equip_message;
                }
            }
        } else {
            unsigned char count = 0;
            for (int slot = 0; slot < 8; ++slot) {
                if (g_status.party.rows[slot].fOccupied && IsPartySlotEligible(slot) &&
                    g_status.party.rows[slot].pending_action == W8_ACTION_EQUIP) {
                    ++count;
                    if (count == 1) {
                        text = fmt::format("{}", g_status.party.characters[slot].name);
                    } else {
                        if (count == g_combat.state()->equip_pending) {
                            text += " ";
                            text += fmt::sprintf(g_localized_strings[0x918],
                                                 g_status.party.characters[slot].name);
                            goto show_equip_message;
                        }
                        text += ", ";
                        text += g_status.party.characters[slot].name;
                    }
                }
            }
        }
        Fail("fFoundEquipChar");
    show_equip_message:
        ShowNotice(text, 0, true, false);
    }
    redraw_flags |= W8_CAMP_REDRAW_ALL;
    ResetTransientRenderScenes();
    SetPrimarySurfaceTextureHint2Enabled(false);
    if (!g_status.game_started) {
        StartMusicResource("MainMenu.MPL", 1, 1);
    }
    return 1;
}

void CampScreenFrame()
{
    g_camp_screen->Frame();
}

void W8CampScreenState::Frame()
{
    if (g_dev_mode) {
        RequestExitScreen();
    }
    ServiceMusicPlaylist();
    if (dialog) {
        const UiTransaction transaction;
        auto* running = dialog.get();
        if (!running->ProcessInput() && dialog.get() == running) {
            RetireModal(std::move(dialog), true);
            redraw_flags |= W8_CAMP_REDRAW_ALL;
        }
    }
    if (gXStatus.level_up_notice && SuspendedScreen() != W8_SCREEN_CHARACTER &&
        !g_combat.active() && !IsScreenTransitionPending()) {
        RefreshLevelUpReadyNotices();
    }
    if (!dialog)
        PumpUiInput({.unhandled = [&](const InputEvent& input) {
            if (input.type == InputEventType::KeyDown) {
                if (input.key == SDLK_ESCAPE) {
                    if (items.action() == W8_CAMP_ITEM_ACTION_NONE || page != W8_CAMP_PAGE_ITEMS) {
                        DismissSelectedPartyCharacter();
                    } else {
                        items.SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
                    }
                } else if (input.key == SDLK_P) {
                    if (g_status.game_started) {
                        SortPartyItemPool();
                    }
                } else if (input.key == SDLK_X && ((GetInputState().modifiers & ALT_DOWN) != 0) &&
                           ((GetInputState().modifiers & CTRL_DOWN) == 0) &&
                           ((GetInputState().modifiers & SHIFT_DOWN) == 0)) {
                    ShowNotice(g_localized_strings[0x832], OnQuitGameDialogClosed, true, true);
                }
            }
            return InputRoute::Handled;
        }});
    if (page == W8_CAMP_PAGE_SPELLS) spells.Tick();
    if (page == W8_CAMP_PAGE_ITEMS) items.Tick();
    if (header_mode == W8_CAMP_HEADER_SUMMARY) {
        gXStatus.character_event_queue->ProcessDeferredCharacterEvents();
        UpdateCharacterEventState();
    }
    DrawCampScreen();
}

bool CampScreenLeave(int)
{
    if (!g_camp_screen) return 1; // Already released when it was suspended.
    g_camp_screen->DeactivateCampPage();
    g_smfnt_font.resetPalette();
    g_calligraphy_font.resetPalette();
    g_calligraphy_shadow_font.resetPalette();
    g_wiz_text_font.resetPalette();
    g_camp_screen.reset();
    if (gXStatus.fCampMode) {
        gXStatus.dialogue_sync_pending = true;
    }
    return 1;
}

/* Leave camp. When an item is held out for the pending character, it goes to
   that character if the selected slot can carry it and the refusal dialog
   opens otherwise; with no pending character the camp simply closes into a
   fresh combat round. */
void W8CampScreenState::DismissSelectedPartyCharacter()
{
    if (!items.ResolvePendingCampCharacter(true)) return;
    BeginCombatRound();
    ReturnToPreviousScreen();
}

void W8CampScreenState::DrawCampScreen(void)
{
    items.BeginRedraw();
    if (redraw_flags != 0 || items.RedrawPending() || (g_status.party.held.item.iItemNo != -1)) {
        if (redraw_flags == W8_CAMP_REDRAW_ALL) {
            items.InvalidateAll();
            if (dialog != 0) {
                dialog->m_dirty = true;
            }
        }
        DrawCampHeader();
        switch (page) {
        case W8_CAMP_PAGE_ITEMS:
            items.RedrawCampItemsPage();
            break;
        case W8_CAMP_PAGE_STATS:
            stats.DrawCampStatsPage();
            break;
        case W8_CAMP_PAGE_SKILLS:
            skills.DrawCampSkillsPage();
            break;
        case W8_CAMP_PAGE_SPELLS:
            spells.DrawCampSpellPages();
            break;
        case W8_CAMP_PAGE_REGENERATION:
            DrawCampRegenStats();
            break;
        }
        items.RefreshCampItemActions((redraw_flags & W8_CAMP_REDRAW_ITEM_ACTIONS) != 0);
        g_calligraphy_font.setShade(Font::kNormalShade);
        items.EndRedraw();
        redraw_flags = 0;
    }
    character_info->Redraw(GetFrameBufferSurface(), false);
    if (page == W8_CAMP_PAGE_ITEMS) {
        items.DrawControls(dialog != nullptr);
    } else if (page == W8_CAMP_PAGE_STATS) {
        stats.DrawControls();
    } else if (page == W8_CAMP_PAGE_SPELLS) {
        spells.DrawControls();
    }
    items.RefreshCampItemActions(false);
    if (!g_combat.active()) {
        if (ReviewedPartySlot() != -1) {
            if (g_status.game_started ||
                (PreviousScreen() == W8_SCREEN_PARTY_SELECTION && PartySelectionInReviewMode())) {
                UpdateRcsLevelUpPanel();
            }
            if (g_combat.active()) {
                goto done;
            }
        }
        if (g_status.game_started) {
            UpdateRcsDismissPanel();
        }
    }
done:
    RedrawPortraitQuoteBubbles();
    if (dialog != 0) {
        dialog->RefreshModal();
    }
    RenderFrame();
}

void W8CampScreenState::SyncReviewCharInputRegion(void)
{
    if (ReviewedPartySlot() != -1 && g_status.party.rows[ReviewedPartySlot()].npc_index != -1) {
        main_targets[kCampCharacterHeaderRegionOffset]->setEnabled(false);
        return;
    }
    main_targets[kCampCharacterHeaderRegionOffset]->setEnabled(true);
}

/* Switch the active camp page: tear the old page down, record the new index,
   bring it up and repaint the whole character block. */
void W8CampScreenState::SwitchCampPage(W8CampPage page)
{
    DeactivateCampPage();
    this->page = page;
    ActivateCampPage();
    redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Enter the active camp page: resets the item action mode when leaving the
   items page, gates the party-portrait regions on whether a character is being
   reviewed, then enables the page's own region sets and controls. */
void W8CampScreenState::ActivateCampPage(void)
{
    unsigned int index;

    main_input.setEnabled(true);
    if (page != W8_CAMP_PAGE_ITEMS) {
        items.SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
    }
    if (ReviewedPartySlot() == -1) {
        for (index = 0; index < 8; ++index) {
            main_targets[kCampPartyPortraitRegionOffset + index]->setEnabled(false);
        }
    } else if (g_status.party.rows[ReviewedPartySlot()].npc_index == -1) {
        main_targets[kCampCharacterHeaderRegionOffset]->setEnabled(true);
    } else {
        main_targets[kCampCharacterHeaderRegionOffset]->setEnabled(false);
    }
    header_mode = W8_CAMP_HEADER_SUMMARY;
    redraw_flags |= 0x7ff;
    switch (page) {
    case W8_CAMP_PAGE_ITEMS:
        items.Activate();
        if (g_status.game_started) return;
        break;
    case W8_CAMP_PAGE_STATS:
        stats.Activate();
        return;
    case W8_CAMP_PAGE_SKILLS:
        skills.CreateCampSkillRegions();
        return;
    case W8_CAMP_PAGE_SPELLS:
        spells.Activate();
        break;
    default:
        break;
    }
}

/* Leave the active camp page: disables the page's region sets and controls and
   releases the page's rebuilt state - the effect list on the stats page, the
   skill-improvement flags on the skills page. */
void W8CampScreenState::DeactivateCampPage(void)
{
    unsigned int index;

    switch (page) {
    case W8_CAMP_PAGE_ITEMS:
        items.Deactivate();
        if (g_status.game_started) return;
        break;
    case W8_CAMP_PAGE_STATS:
        stats.Deactivate();
        break;
    case W8_CAMP_PAGE_SKILLS:
        skills.DisableCampSkillRegions();
        for (index = 0; index < 0x29; ++index) {
            reviewed->skills[index].improved = false;
        }
        return;
    case W8_CAMP_PAGE_SPELLS:
        spells.Deactivate();
        break;
    default:
        break;
    }
}

/* Camp page 4: the reviewed character's health, stamina and per-realm spell
   point regeneration rates. */
void W8CampScreenState::DrawCampRegenStats(void)
{
    unsigned int index;

    g_calligraphy_font.setShade(Font::kNormalShade);
    g_calligraphy_font.setShade(0);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font, {0x14a, 5},
                          g_localized_strings[0x8ce]);
    g_calligraphy_font.setShade(Font::kNormalShade);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font, {0x221, 5},
                          fmt::format("{:6.3f}", reviewed->health_regen_rate));
    g_calligraphy_font.setShade(0);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font, {0x14a, 0x14},
                          g_localized_strings[0x8cd]);
    g_calligraphy_font.setShade(Font::kNormalShade);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font, {0x221, 0x14},
                          fmt::format("{:6.3f}", reviewed->stamina_regen_rate));
    g_calligraphy_font.setShade(0);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font, {0x14a, 0x23},
                          g_localized_strings[0x8d0]);
    g_calligraphy_font.setShade(Font::kNormalShade);
    for (index = 0; index < 6; ++index) {
        DrawTextAndInvalidate(GetFrameBufferSurface(), g_calligraphy_font,
                              {static_cast<int>(index * 0x14 + 0x1fe), 0x23},
                              fmt::format("{:6.3f}/", reviewed->spell_regen_rates[index * 2]));
    }
}

void W8CampScreenState::SetCampHeaderMode(W8CampHeaderMode mode)
{
    header_mode = mode;
    redraw_flags |= 0x7ff;
}

void W8CampScreenState::DisplayCampDialog(std::unique_ptr<W8DialogBase> dialog)
{
    RetireModal(std::exchange(this->dialog, std::move(dialog)), false);
    if (this->dialog) this->dialog->InputLayer().setModal(true);
}

void ShowCampNoticeLine(std::string_view text, std::function<void(bool)> callback,
                        bool confirmation, bool cancel)
{
    g_camp_screen->ShowNotice(text, std::move(callback), confirmation, cancel);
}

void W8CampScreenState::ShowNotice(std::string_view text, std::function<void(bool)> callback,
                                   bool confirmation, bool cancel)
{
    W8MessageDialogBase* dialog = CreateMessageDialog();
    dialog->SetClientExtent(0xfa, 200);
    dialog->SetMessage(text, 1, 0x32, confirmation, cancel, true, true, 0, 0x15e);
    dialog->on_complete = std::move(callback);
    DisplayCampDialog(std::unique_ptr<W8DialogBase>(dialog));
}

/* Whether the camp item action may proceed for the reviewed slot: outside
   combat always; in combat the sleeping/paralyzed/unconscious/dead conditions
   and the equip-in-progress actions each carry their own refusal message. */
bool IsCampActionAllowed(int party_slot)
{
    std::string message;
    W8PartySlotRow* row;
    W8Character* character;

    if (!g_combat.active()) {
        return true;
    }
    character = &g_status.party.characters[party_slot];
    if (character->uiCondition[W8_CONDITION_DEAD] != 0 ||
        character->uiCondition[W8_CONDITION_UNCONSCIOUS] != 0 ||
        character->uiCondition[W8_CONDITION_PARALYZED] != 0 ||
        character->uiCondition[W8_CONDITION_ASLEEP] != 0) {
        if (g_combat.state()->equip_phase != W8CombatEquipPhase::action_selection) {
            return true;
        }
        message = g_localized_strings[0x90a];
    } else {
        row = &g_status.party.rows[party_slot];
        if (g_combat.state()->equip_phase == W8CombatEquipPhase::action_selection) {
            if (g_combat.state()->characters[party_slot].dead) {
                if (row->pending_action == W8_ACTION_EQUIP) {
                    message = g_localized_strings[0x903];
                } else if (row->committed_action.kind == W8_ACTION_EQUIP) {
                    message = g_localized_strings[0x904];
                } else {
                    message = g_localized_strings[0x902];
                }
            } else if (row->committed_action.kind == W8_ACTION_EQUIP) {
                message = g_localized_strings[0x903];
            } else {
                message = g_localized_strings[0x902];
            }
        } else {
            if (row->pending_action == W8_ACTION_EQUIP) {
                return true;
            }
            if (row->committed_action.kind == W8_ACTION_EQUIP) {
                message = g_localized_strings[0x904];
            } else {
                message = g_localized_strings[0x902];
            }
        }
    }
    ShowCampNoticeLine(message, 0, true, false);
    return false;
}

bool IsEquippableItemClass(W8ItemInstance* item)
{
    char equip_class = g_item_records[item->iItemNo].equip_class;
    if (equip_class != W8_ITEM_EQUIP_CLASS_THROWN_WEAPON &&
        equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        return false;
    }
    return true;
}

/* Books a party slot's spell cast: stamps the row's cast-spell action with
   the spell id, power level and target, spends the spell points and fatigues
   the caster, then plays fizzle/learned/general-cast audio for the result. */
int CommitPartySlotSpell(int party_slot, int spell_id, int power_level, W8CombatSlot* target)
{
    int cost;
    int result;
    W8PartySlotRow* row;

    StartBreathCycle(party_slot, false);
    row = &g_status.party.rows[party_slot];
    row->pending_action = W8_ACTION_CAST_SPELL;
    row->attack_mode[0] = spell_id;
    row->attack_mode[1] = -1;
    row->pending_action_detail.spell.power_level = power_level;
    row->pending_action_detail.spell.unused = 0;
    row->target_out_of_combat = *target;
    result = ExecuteCharacterSpellCast(party_slot, spell_id, power_level, &cost, false);
    SetPartySlotSpell(party_slot, spell_id, power_level, target);
    FatigueCharacter(party_slot, cost, true, 0);
    if (result != 1) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
        return result;
    }
    if (spell_id == W8_SPELL_IDENTIFY_ITEM && g_status.party.itemAt(target->item)->identified) {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Learned.wav");
        return 1;
    }
    wiz8::sound::play(s_general_magic_sound);
    return 1;
}

/* Books a party slot's item use: stamps the row's use-item action with the
   item and target, resolves the use, stages the slot's item detail for the
   follow-up bookkeeping, and fatigues the user by the attempt's reported
   cost (or the standard use-item cost when no use happened). The audio cues
   mirror the spell path: fizzle, learned for a successful identify, general
   magic otherwise. */
int CommitPartySlotItemUse(int party_slot, W8ItemInstance* item, W8CombatSlot* target)
{
    int uses;
    int result;
    W8PartySlotRow* row;
    W8ItemInstance* used;

    StartBreathCycle(party_slot, false);
    row = &g_status.party.rows[party_slot];
    row->pending_action = W8_ACTION_USE_ITEM;
    row->attack_mode[0] = -1;
    row->attack_mode[1] = -1;
    row->pending_action_detail.item_use.kind = -1;
    row->pending_action_detail.item_use.item = g_status.party.locateItem(item);
    row->target_out_of_combat = *target;
    const auto spell = g_item_records[item->iItemNo].spell_id;
    StagePartySlotItemUse(party_slot, item, target);
    result = UseItem(party_slot, g_status.party.locateItem(item), &uses);
    if (uses == -1) {
        uses = CharacterActionFatigueCost(party_slot, W8_ACTION_USE_ITEM);
    }
    FatigueCharacter(party_slot, uses, true, 0);
    if (result == 1) {
        used = g_status.party.itemAt(target->item);
        if (spell == W8_SPELL_IDENTIFY_ITEM && used && used->iItemNo != -1 && used->identified) {
            wiz8::sound::play("Data\\Sound\\Misc\\Spell Learned.wav");
        } else {
            wiz8::sound::play(s_general_magic_sound);
        }
    } else {
        wiz8::sound::play("Data\\Sound\\Misc\\Spell Fizzle 01.wav");
    }
    return result;
}

void BeginEndgameSequence(void)
{
    int fade_to_black = 0;
    int fade_code = 0x5dc;
    bool endgame_variant = false;

    g_status.endgame_started = true;
    UpdateHeldItemCursor();
    if (GetFact(W8_FACT_ENDING_BOFFO_ONE) != 0) {
        fade_to_black = 1;
    } else if (GetFact(W8_FACT_ENDGAME_JOIN_SAVANT) != 0) {
        fade_code = 1;
        endgame_variant = true;
    }
    SelectUiInputOwner(W8_SCREEN_NONE); // No screen takes input during the fade.
    VideoRemoveToolTip();
    g_level_block->review_transition_active = true;
    BeginScreenFade(fade_to_black, 0, fade_code, ShowEndingScreen, true, endgame_variant);
}

/* Begin a timed full-screen fade: spawn a 640x480 colored quad over the UI,
   switch its blend shader for the requested ramp direction and seed the
   fade state. `callback` runs from UpdateScreenFade once the ramp
   finishes. `fade_to_black` selects the subtractive ramp (white quad
   darkening to black); `fade_out` selects the direction the opacity runs. */
void BeginScreenFade(int fade_to_black, int fade_out, int duration, void (*callback)(void),
                     bool fullscreen_scene_last, char render_each_tick)
{
    DrawShader shader;
    glm::vec4 color;

    g_fade_duration = duration;
    g_fade_out = fade_out;
    g_fade_callback = callback;
    g_fade_flag = render_each_tick;
    g_level_block->review_transition_done = true;
    if (fullscreen_scene_last) {
        SetFullscreenSceneLast(1);
    }
    if (fade_to_black != 0) {
        color.x = 1.0f;
        color.y = 1.0f;
        color.z = 1.0f;
    } else {
        color.x = 0.0f;
        color.y = 0.0f;
        color.z = 0.0f;
    }
    color.w = 1.0f;
    g_fade_overlay = makeColorOverlay(0x280, 0x1e0, color, true);
    positionOverlay(g_fade_overlay.get(), 0, 0, false);
    shader = g_fade_overlay->shader;
    if (fade_to_black == 0) {
        shader.value = (shader.value & ~0x6040) | 0xa0;
    } else {
        shader.value = (shader.value & ~0x20c0) | 0x4020;
    }
    g_fade_overlay->shader = shader;
    g_fade_overlay->color.a = fade_out != 0 ? 1.0f : 0.0f;
    g_fade_tick_base = w8_get_ticks();
}

/* Advance the pending screen fade: interpolate the overlay's opacity over
   g_fade_duration (reversed when fading back out) and render a
   frame per tick while g_fade_flag is set. On completion a
   fade-in snaps the quad opaque and renders twice, then the overlay is
   released and the stored callback runs. Returns g_fade_flag. */
unsigned char UpdateScreenFade(void)
{
    if (!g_level_block->review_transition_done) {
        return 0;
    }
    std::uint64_t elapsed = w8_get_ticks() - g_fade_tick_base;
    if (g_fade_duration < elapsed) {
        g_level_block->review_transition_done = false;
        if (g_fade_out == 0) {
            g_fade_overlay->color.a = 1.0f;
            RenderFrame();
            RenderFrame();
        }
        g_fade_overlay.reset();
        SetFullscreenSceneLast(0);
        if (g_fade_callback != 0) {
            g_fade_callback();
        }
        return g_fade_flag;
    }
    float progress = static_cast<float>(elapsed) / g_fade_duration;
    OverlayQuad* material = g_fade_overlay.get();
    if (g_fade_out == 0) {
        material->color.a = progress;
    } else {
        material->color.a = 1.0f - progress;
    }
    if (g_fade_flag != 0) {
        RenderFrame();
    }
    return g_fade_flag;
}

/* Start the party-death transition: Iron Man runs delete the current saves
   first, then the death sting and CombatLose playlist start while the input
   regions collapse to the modal review region and the fade carries the
   screen to DrawPartyDeathScreen. */
void BeginPartyDeath(void)
{
    if (g_level_block->review_transition_active) {
        return;
    }
    if (g_status.iron_man && !gXStatus.party_moving) {
        DeleteCurrentSaveFiles();
    }
    if (gXStatus.fSurprisePossible) {
        RestoreSurpriseView();
    }
    UpdateHeldItemCursor();
    wiz8::sound::play("Data\\Sound\\Misc\\PartyDead.wav");
    StartMusicResource("CombatLose.MPL", 0, 1);
    SelectUiInputOwner(W8_SCREEN_NONE); // No screen takes input during the fade.
    VideoRemoveToolTip();
    BeginScreenFade(1, 0, 0x7d0, DrawPartyDeathScreen, true, 0);
    g_level_block->review_transition_active = true;
}

/* Pump the post-fade review/death state: on the first tick an armed ending
   autosave writes the next free Ending slot and reports it on the main-menu
   message line; a dirty level reloads; any queued key-down or button-up
   schedules the closing fade back through EndReviewTransition. */
void PumpReviewTransition(void)
{
    InputEvent input;
    bool exit_review;

    if (!g_level_block->review_transition_active || g_level_block->review_transition_done) {
        return;
    }
    if (g_ending_autosave) {
        if (auto name = FindFreeEndingSaveName()) {
            SaveGame(name->c_str(), nullptr);
            g_pending_main_menu_message =
                (fmt::format("{} {}.{}", g_localized_strings[0x78b], *name, g_save_extension));
        }
        g_ending_autosave = false;
    }
    if (GetLoadedLevelID() != -1) {
        LoadCurrentLevelData();
    }
    exit_review = false;
    while (auto next_input = PollInputEvent()) {
        input = *next_input;
        switch (input.type) {
        case InputEventType::KeyDown:
        case InputEventType::LeftButtonUp:
        case InputEventType::RightButtonUp:
            exit_review = true;
            break;
        default:
            break;
        }
    }
    if (exit_review) {
        BeginScreenFade(0, 0, 0x320, EndReviewTransition, true, 1);
    }
    RenderFrame();
}

/* Fade-completion callback for party death: draw the review backdrop and
   the centered party-death line over it, drop the radar/formation panels,
   then schedule the holding fade that waits for input. */
void DrawPartyDeathScreen(void)
{
    std::string text;

    CatalogArtwork::Load(0x1df, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
    if (gXStatus.party_moving) {
        text = g_localized_strings[0x777];
    } else {
        text = g_localized_strings[0x778];
    }
    DrawText(GetFrameBufferSurface(), g_level_load_font,
             {0x276 - g_level_load_font.textWidth(text), 0x1c7}, text);
    SetRadarMapVisible(false);
    SetFormationBoardVisible(false);
    VideoRemoveToolTip();
    g_world_render_enabled = 0;
    BeginScreenFade(1, 1, 0x4b0, 0, true, 1);
}

/* Fade-completion callback leaving the review/death screen: release the
   modal region, stop the ending voice-over, reset the main-game mode and
   leave the transition. The endgame path continues to the credits screen;
   party death just stops the playlist. */
void EndReviewTransition(void)
{
    if (g_ending_sound != nullptr) {
        wiz8::sound::stop(g_ending_sound);
        g_ending_sound = nullptr;
    }
    g_world_render_enabled = 1;
    ResetMainGameMode();
    g_level_block->review_transition_active = false;
    if (g_ending_screen) {
        RequestScreen({.id = W8_SCREEN_CREDITS});
        g_ending_screen = false;
        return;
    }
    StopMusicPlaylist(true);
}

/* Fade-completion callback for the ending sequence: pick the ending's
   backdrop, string-table text and voice-over from the ending facts, draw
   them over a cleared screen and schedule the holding fade. Fact 0x1a2 is
   the losing ending - it swaps in the CombatLose playlist and skips the
   credits-screen handoff and autosave arming. */
void ShowEndingScreen(void)
{
    int fade_to_black;
    bool schedule_fade;
    const char* music;
    const char* sound;
    int image;
    std::string text;
    W8ControlsRect bounds;

    fade_to_black = 0;
    schedule_fade = true;
    music = "EndCredit.MPL";
    g_ending_screen = true;
    g_ending_autosave = true;
    if (GetFact(W8_FACT_ENDGAME_JOIN_SAVANT) != 0) {
        image = 0x1e1;
        schedule_fade = false;
        text = g_localized_strings[0x787];
        sound = "Data\\Sound\\npcs\\VOC_ENDGAME1\\VOC_ENDGAME1_005.mp3";
    } else if (GetFact(W8_FACT_QUE_ENDGAME2) != 0) {
        image = 0x1e2;
        text = g_localized_strings[0x788];
        sound = "Data\\Sound\\npcs\\VOC_ENDGAME2\\VOC_ENDGAME2_005.mp3";
    } else if (GetFact(W8_FACT_ENDGAME_SAVANT_PHOON_SPLIT) != 0) {
        image = 0x1e2;
        text = g_localized_strings[0x789];
        sound = "Data\\Sound\\npcs\\VOC_ENDGAME3\\VOC_ENDGAME3_000.mp3";
    } else if (GetFact(W8_FACT_ENDING_BOFFO_ONE) != 0) {
        image = 0x1e3;
        fade_to_black = 1;
        text = g_localized_strings[0x78a];
        sound = "Data\\Sound\\npcs\\VOC_ENDGAME4\\VOC_ENDGAME4_000.mp3";
        music = "CombatLose.MPL";
        g_ending_screen = false;
        g_ending_autosave = false;
    } else {
        image = 0x1df;
        sound = "";
        text.clear();
    }
    CatalogArtwork::Load(image, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0);
    bounds.left = 0x46;
    bounds.top = 0;
    bounds.right = 0x239;
    bounds.bottom = 0x1d0;
    {
        W8TextBuffer buffer(&bounds, text, g_options_detail_font, g_W8TextBufferAlignBottom, 4);
        buffer.RenderToTarget(0, false, GetFrameBufferSurface());
    }
    SetRadarMapVisible(false);
    SetFormationBoardVisible(false);
    VideoRemoveToolTip();
    g_world_render_enabled = 0;
    if (*music != 0) {
        StartMusicResource(music, 0, 1);
    }
    if (*sound != 0) {
        g_ending_sound = wiz8::sound::stream(
            sound, {.volume = std::uint32_t(g_settings.voice_volume * 0x7f / 0xff)});
    }
    if (schedule_fade) {
        BeginScreenFade(fade_to_black, 1, 0x4b0, 0, true, 1);
    }
}

void W8CampSpellsPage::CreateInput()
{
    spell_input.setEnabled(false);
    for (int realm = 0; realm < 6; ++realm) {
        spell_targets[realm] =
            &spell_input.add(SDL_Rect{(realm % 3) * 213 + 29, (realm / 3) * 140 + 195, 154, 102},
                             [this, realm](const UiPointerEvent& event) {
                                 return HandleCampSpellList(event, realm);
                             });
    }
}

void W8CampSpellsPage::CreateControls()
{
    for (int range_index = 0; range_index < 6; ++range_index) {
        const int x = (range_index % 3) * 0xd5;
        const int y = (range_index / 3) * 0x8c;
        auto& range = spell_ranges[range_index];
        range = std::make_unique<W8RangeControl>(screen.scroll_input, x + 0xbb, y + 0xc3, x + 0xcd,
                                                 y + 0x129);
        range->on_change = [this, range_index](auto* scroll) {
            learned_spells.scroll[range_index] = scroll->m_value;
            screen.redraw_flags |= W8_CAMP_REDRAW_REALM_SPELLS_FIRST << range_index;
        };
    }
    animation_timer = w8_get_ticks() + (50);
    for (unsigned int animation = 0; animation < 6; ++animation) {
        animation_frames[animation] = Random(g_spell_realm_animations[animation].frame_count);
    }
}

void W8CampSpellsPage::Activate()
{
    spell_input.setEnabled(true);
    ReviewedCharacterChanged();
    selected_spell_row = -1;
}

void W8CampSpellsPage::Deactivate()
{
    spell_input.setEnabled(false);
    SetCampSpellRangesEnabled(false);
}

void W8CampSpellsPage::ReviewedCharacterChanged()
{
    BuildLearnedSpellState(&learned_spells, screen.reviewed);
    RefreshCampSpellRanges();
}

void W8CampSpellsPage::Tick()
{
    if (TimeUntilDeadline(animation_timer)) return;
    for (unsigned int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        ++animation_frames[realm];
        if (animation_frames[realm] == g_spell_realm_animations[realm].frame_count) {
            animation_frames[realm] = 0;
        }
    }
    animation_timer = w8_get_ticks() + (50);
    screen.redraw_flags |= 0x10000;
}

void W8CampSpellsPage::DrawControls()
{
    for (auto& range : spell_ranges) range->Redraw(GetFrameBufferSurface(), false);
}
