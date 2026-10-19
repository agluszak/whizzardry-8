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
void W8CampScreenState::SetCampSpellRangesEnabled(bool enable)
{
    W8RangeControl* spell_range;
    int realm;
    int second;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        spell_range = spell_ranges[realm].get();
        spell_range->SetActive(enable);
        if (enable) {
            second = reviewed->skill_unlocks[0x1c + realm] - 8;
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
void W8CampScreenState::RefreshCampSpellRanges(void)
{
    W8RangeControl* spell_range;
    int realm;
    int second;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        spell_range = spell_ranges[realm].get();
        spell_range->SetActive(true);
        second = reviewed->skill_unlocks[0x1c + realm] - 8;
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
void W8CampScreenState::DrawCampSpellPages(void)
{
    W8CampScreenState* state;
    W8Character* character = reviewed;
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

    if ((redraw_flags & W8_CAMP_REDRAW_RESISTANCES) != 0 && !gpSCSV) {
        DrawCampResistances();
    }
    if (redraw_flags == W8_CAMP_REDRAW_ALL) {
        CatalogArtwork::Load(0x140, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0, 0xa5);
    }
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        if ((redraw_flags & (W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm)) == 0) {
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
        state = this;
        state->spell_ranges[realm]->Redraw(GetFrameBufferSurface(), true);
    }
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        if (dialog != 0 && realm != 0 && realm != 3) {
            continue;
        }
        if ((redraw_flags & ((W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm) | 0x10000)) == 0) {
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
void W8CampScreenState::DrawCampResistances(void)
{
    W8Character* character = reviewed;
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
UiResult W8CampScreenState::HandleCampSpellList(const UiPointerEvent& event, int realm)
{

    int row = (event.local.y - 1) / 13;
    const int visible =
        std::min(8, int(reviewed->skill_unlocks[0x1c + realm]) - int(learned_spells.scroll[realm]));
    if (!event.inside || row < 0 || row >= visible) row = -1;
    if (row != selected_spell_row || event.action == UiPointerAction::Enter ||
        event.action == UiPointerAction::Leave) {
        selected_spell_row = row;
        redraw_flags |= W8_CAMP_REDRAW_REALM_SPELLS_FIRST << realm;
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
void W8CampScreenState::OpenSpellInfoDialog(unsigned int spell_id)
{
    auto dialog = std::make_unique<W8SpellInfoDialog>(spell_id);
    dialog->SetText("");
    DisplayCampDialog(std::move(dialog));
}

/* The items-page redraw driver, run once per update: each pending group of
   redraw flags is cleared by repainting that block, and the two bottom/right
   panels repaint when their own bits are raised. */
void W8CampScreenState::RedrawCampItemsPage(void)
{
    g_wiz_text_font_secondary.setShade(Font::kNormalShade);
    if ((redraw_flags & W8_CAMP_REDRAW_CHARACTER_INFO) != 0) {
        DrawCampCharacterInfo();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_BACKPACK) != 0) {
        DrawCampBackpackItems();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_EQUIPMENT) != 0) {
        DrawCampEquipmentItems();
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL) != 0) {
        DrawCampItemPool();
        item_range->Redraw(GetFrameBufferSurface(), true);
    } else {
        item_range->Redraw(GetFrameBufferSurface(), false);
    }
    RefreshCampActionPanel((redraw_flags & W8_CAMP_REDRAW_ACTION_PANEL) != 0);
    RefreshItemsTabPanel((redraw_flags & W8_CAMP_REDRAW_REALM_TABS) != 0);
}

/* The character block of the items page: name-free value fields, the
   experience bar, the six realm icons with their point pools, the attribute
   rows and the armor summaries. */
void W8CampScreenState::DrawCampCharacterInfo(void)
{
    W8Character* character = reviewed;
    int index;
    int realm;
    int top;
    unsigned int filled;
    SDL_Rect clip;

    if (info_page != W8_CAMP_INFO_PAGE_ITEMS) {
        character_info->InvalidateAll();
        item_controls.dirty = true;
        return;
    }
    CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x136, 0);
    if (character->experience != character->experience_previous_goal) {
        if (character->experience < character->experience_goal) {
            filled = (character->experience - character->experience_previous_goal) * 0x67 /
                     (character->experience_goal - character->experience_previous_goal);
        } else {
            filled = 0x67;
        }
        if (filled != 0) {
            clip = {0x1c0, 0, static_cast<int>(filled), 0x1e0};
            ScopedClipRect clipped(GetFrameBufferSurface(), clip);
            CatalogArtwork::Load(0x11d, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x1c0,
                                                             0x26);
        }
    }
    DrawRcsText(g_localized_strings[0x91b], 0x159, 10, 0xce,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x91c], 0x15e, 0x18, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x91d], 0x15e, 0x26, 0x65,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text_buffer = FormatUnsignedIntegerWithCommas(character->experience);
    DrawRcsText(text_buffer, 0x1c0, 0x18, 0x67,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text_buffer = FormatUnsignedIntegerWithCommas(character->experience_goal);
    DrawRcsText(text_buffer, 0x1c0, 0x26, 0x67,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x91e], 0x144, 0x3a, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x91f], 0x144, 0x48, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x920], 0x144, 0x56, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text_buffer = fmt::format("{:d}/{:d}", character->hp_current, character->uiHPMax);
    DrawRcsText(text_buffer, 0x18d, 0x3a, 0x2c,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text_buffer = fmt::format("{:d}/{:d}", character->stamina, character->uiStaminaMax);
    DrawRcsText(text_buffer, 0x18d, 0x48, 0x2c,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (character->load_category != 0) {
        g_wiz_text_font_secondary.setPalette(
            g_font_state_palettes[g_load_category_palettes[character->load_category]]);
    }
    text_buffer = fmt::format("{:d}/{:d}", character->total_carried_weight / 10,
                              character->carrying_capacity / 10);
    DrawRcsTextJustified(text_buffer, 0x18d, 0x56, 0x2c, 0xc,
                         g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    text_buffer = fmt::format("{}: {:d}, {}: {:d}", g_localized_strings[0x8c0],
                              character->inventory_weight / 10, g_localized_strings[0x8c1],
                              character->party_weight_share / 10);
    item_controls.help_text = text_buffer;
    g_wiz_text_font_secondary.resetPalette();
    DrawRcsText(g_localized_strings[0x921], 0x144, 0x72, 0x75,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    top = 0x3a;
    for (index = 0; index < 7; ++index) {
        DrawRcsText(g_localized_strings[g_attribute_label_ids[index]], 0x1c2, top, 0x4c,
                    g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
        text_buffer = fmt::format("{:d}", character->attributes[index].effective);
        DrawRcsText(text_buffer, 0x212, top, 0x14,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        top += 0xe;
    }
    top = 0xd;
    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        unsigned int frame;
        if (character->sp_max[realm] == 0) {
            frame = g_spell_realm_animations[realm].frame_count;
        } else {
            text_buffer = fmt::format(
                "{:d}/{:d}",
                GetCharacterRealmSpellPoints(character, static_cast<W8SpellRealm>(realm)),
                character->sp_max[realm]);
            DrawTallRcsText(text_buffer, 0x242, top, 0x32,
                            g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
            frame = g_spell_realm_animations[realm].initial_frame;
        }
        CatalogArtwork::Load(g_character_resistance_images[realm], 0)
            .drawAndInvalidate(GetFrameBufferSurface(), frame, 0x22d, top);
        top += 0x18;
    }
    DrawRcsText(g_localized_strings[0x922], 0x144, 0x80, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text_buffer = fmt::format("{:d}", character->armor_class_total);
    DrawRcsText(text_buffer, 0x18d, 0x80, 0x2c,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    DrawRcsText(g_localized_strings[0x923], 0x144, 0x8e, 0x45,
                g_W8TextBufferAlignLeft | g_W8TextBufferAlignMiddle);
    text_buffer = fmt::format("{:d}", character->armor_class_average);
    DrawRcsText(text_buffer, 0x18d, 0x8e, 0x2c,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    item_controls.dirty = true;
}

/* The eight backpack cells under the character block, flag bits 1..8 of
   item_redraw_flags; bit 0 repaints the header strip and caption. */
void W8CampScreenState::DrawCampBackpackItems(void)
{
    W8Character* character = reviewed;
    unsigned int slot;
    int left;
    int top;
    int right;
    int bottom;
    int item_id;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_BACKPACK_HEADER) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 1, 0, 0xa5);
        DrawRcsBoldText(g_localized_strings[0x92b], 0xc, 0xae, 0x5d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (slot = 0; slot < 8; ++slot) {
        left = (slot & 1) * 0x31 + 0xb;
        top = (slot >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot)) == 0) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        item_id = character->backpack[slot].iItemNo;
        if (item_id == -1 || CanCharacterUseItem(character, item_id)) {
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
        } else {
            CatalogArtwork::Load(0x11a, 0).draw(GetFrameBufferSurface(), 0, left, top);
        }
        if (item_id != -1) {
            DrawSprite(GetFrameBufferSurface(), *ItemSprites(item_id), 0, left + 1, top + 1);
            DrawCampItemQuantity(&character->backpack[slot], left + 1, top + 0x28, 0x2c);
            if (!character->backpack[slot].identified) {
                CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(), 0, left, top);
            }
        }
        if (item_id != -1 && g_item_records[item_id].binds_on_equip != 0 &&
            character->backpack[slot].bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (inventory_targets[slot]->hovered() &&
            (item_id != -1 || ((g_status.party.held.item.iItemNo != -1) &&
                               item_action != W8_CAMP_ITEM_ACTION_MOVE))) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
}

/* The twelve equipment cells; bit 9 of item_redraw_flags repaints the paper
   doll backdrop and race/gender portrait, bits 10..21 the slots. */
void W8CampScreenState::DrawCampEquipmentItems(void)
{
    W8Character* character = reviewed;
    const W8CampScreenRegion* region;
    unsigned int slot;
    int item_id;
    int frame;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_PAPER_DOLL) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0x71, 0xa5);
        CatalogArtwork::Load(
            g_race_portrait_images[character->iRace * W8_RACE_GNOME + character->gender], 0)
            .drawAndInvalidate(GetFrameBufferSurface(), 0, 0xc2, 0xa5);
        redraw_flags |= W8_CAMP_REDRAW_ACTION_PANEL;
    }
    for (slot = 0; slot < 12; ++slot) {
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot)) == 0) {
            continue;
        }
        region = &g_camp_screen_regions[slot];
        InvalidateRegion(region->x, region->y, region->x + region->width,
                         region->y + region->height);
        CatalogArtwork::Load(0x1b6, 0).drawRect(
            GetFrameBufferSurface(),
            SDL_Rect{0, 0, (region->x + region->width) - (region->x),
                     (region->y + region->height) - (region->y)},
            region->x, region->y);
        item_id = character->EquippedItem[slot].iItemNo;
        if (item_id == -1) {
            if (slot == 7 || slot == 9) {
                int paired =
                    character->EquippedItem[GetPairedEquipSlot(static_cast<W8EquipSlot>(slot))]
                        .iItemNo;
                if (paired != -1 && (g_item_records[paired].flags & W8_ITEM_FLAG_TWO_HANDED) != 0) {
                    CatalogArtwork::Load(0x146, 0).drawAndInvalidate(GetFrameBufferSurface(), 0,
                                                                     region->x, region->y);
                }
            }
        } else {
            DrawSpriteAndInvalidate(GetFrameBufferSurface(), *ItemSprites(item_id), 1, region->x,
                                    region->y);
            DrawCampItemQuantity(&character->EquippedItem[slot], region->x + 2,
                                 region->y + region->height - 0xd, region->width - 4);
            if (!character->EquippedItem[slot].identified) {
                CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(),
                                                    static_cast<short>(region->unidentified_frame),
                                                    region->x, region->y);
            }
        }
        switch (slot) {
        case W8_EQUIP_SLOT_HEAD:
            frame = 0;
            break;
        case W8_EQUIP_SLOT_TORSO:
            frame = 1;
            break;
        case W8_EQUIP_SLOT_HANDS:
            frame = 3;
            break;
        case W8_EQUIP_SLOT_LEGS:
            frame = 2;
            break;
        case 0xb:
            frame = 4;
            break;
        default:
            goto no_armor_label;
        }
        text_buffer = fmt::format("{:d}", character->armor_class_by_location[frame]);
        CatalogArtwork::Load(0x11c, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, region->x + 2,
                                                         region->y + 1);
        DrawRcsText(text_buffer, region->x + 2, region->y + 2, 0x11,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    no_armor_label:
        CatalogArtwork::Load(0x115, 0).drawAndInvalidate(GetFrameBufferSurface(), region->frame + 3,
                                                         region->x - 2, region->y - 2);
        if (item_id != -1 && g_item_records[item_id].binds_on_equip != 0 &&
            character->EquippedItem[slot].bound) {
            CatalogArtwork::Load(0x115, 0).drawAndInvalidate(
                GetFrameBufferSurface(), region->frame + 2, region->x - 2, region->y - 2);
        }
        if (inventory_targets[kCampEquipmentRegionOffset + slot]->hovered()) {
            if (!(g_status.party.held.item.iItemNo != -1)) {
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                       !CanEquipItemInSlot(character, g_status.party.held.item.iItemNo,
                                           static_cast<unsigned char>(slot), true) ||
                       !CanCharacterUseItem(character, g_status.party.held.item.iItemNo)) {
                if (!(g_status.party.held.item.iItemNo != -1) ||
                    item_action != W8_CAMP_ITEM_ACTION_MOVE) {
                    continue;
                }
                if (character->EquippedItem[slot].iItemNo == -1) {
                    continue;
                }
            }
            frame = region->frame;
        } else {
            if (!(g_status.party.held.item.iItemNo != -1) ||
                item_action == W8_CAMP_ITEM_ACTION_MOVE ||
                !IsPartySlotEligible(ReviewedPartySlot()) ||
                !CanEquipItemInSlot(character, g_status.party.held.item.iItemNo,
                                    static_cast<unsigned char>(slot), true) ||
                !CanCharacterUseItem(character, g_status.party.held.item.iItemNo)) {
                continue;
            }
            frame = region->frame + 1;
        }
        CatalogArtwork::Load(0x115, 0).drawAndInvalidate(GetFrameBufferSurface(), frame,
                                                         region->x - 2, region->y - 2);
    }
}

/* The shared party item pool: the bottom-right grid of up to eight visible
   cells, plus the header strip and the gold counter when bit 22 is raised. */
void W8CampScreenState::DrawCampItemPool(void)
{
    W8Character* character = reviewed;
    unsigned int visible;
    unsigned int i;
    int left;
    int top;
    int right;
    int bottom;
    W8ItemInstance* item;

    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL_HEADER) != 0) {
        CatalogArtwork::Load(0x114, 0).drawAndInvalidate(GetFrameBufferSurface(), 3, 0x1de, 0xa5);
        CatalogArtwork::Load(0x118, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0x202, 0x1a6);
        redraw_flags |= W8_CAMP_REDRAW_REALM_TABS;
    }
    if (!g_status.game_started) {
        return;
    }
    if ((item_redraw_flags & W8_CAMP_ITEM_REDRAW_POOL_HEADER) != 0) {
        DrawRcsBoldText(g_localized_strings[0x92c], 0x1e6, 0xae, 0x8d,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        text_buffer = FormatUnsignedIntegerWithCommas(g_status.party.gold);
        DrawRcsText(text_buffer, 0x229, 0x1a9, 0x30,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    }
    for (i = 0;; ++i) {
        visible = item_list_count - item_scroll;
        if (visible > 8) {
            visible = 8;
        }
        if (i >= visible) {
            break;
        }
        left = (i & 1) * 0x31 + 0x200;
        top = (i >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        item = &g_status.party.item_pool[item_list[item_scroll + i]];
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST << i)) == 0) {
            continue;
        }
        InvalidateRegion(left, top, right, bottom);
        if (!CanCharacterUseItem(character, item->iItemNo)) {
            CatalogArtwork::Load(0x11a, 0).draw(GetFrameBufferSurface(), 0, left, top);
        } else {
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
        }
        DrawSprite(GetFrameBufferSurface(), *ItemSprites(item->iItemNo), 0, left + 1, top + 1);
        DrawCampItemQuantity(item, left + 1, top + 0x28, 0x2c);
        if (!item->identified) {
            CatalogArtwork::Load(0x11b, 0).draw(GetFrameBufferSurface(), 0, left, top);
        }
        if (g_item_records[item->iItemNo].binds_on_equip != 0 && item->bound) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x32, left, top);
        }
        if (pool_targets[i]->hovered()) {
            CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
        }
    }
    for (i = item_list_count - item_scroll; i < 8; ++i) {
        left = (i & 1) * 0x31 + 0x200;
        top = (i >> 1) * 0x39 + 0xc0;
        right = left + 0x2e;
        bottom = top + 0x36;
        if ((item_redraw_flags & (W8_CAMP_ITEM_REDRAW_POOL_CELL_FIRST << i)) != 0) {
            InvalidateRegion(left, top, right, bottom);
            CatalogArtwork::Load(0x1b6, 0).drawRect(
                GetFrameBufferSurface(), SDL_Rect{0, 0, (right) - (left), (bottom) - (top)}, left,
                top);
            if (pool_targets[i]->hovered() && (g_status.party.held.item.iItemNo != -1) &&
                item_action != W8_CAMP_ITEM_ACTION_MOVE) {
                CatalogArtwork::Load(0x115, 0).draw(GetFrameBufferSurface(), 0x30, left, top);
            }
        }
    }
}

/* The stack count or charge figure in a cell's bottom-right corner, keyed off
   the item record's quantity kind; charge figures ride the alternate font
   palettes and restore the default afterwards. */
void W8CampScreenState::DrawCampItemQuantity(W8ItemInstance* item, int left, int top, int width)
{
    int height;

    switch (g_item_records[item->iItemNo].quantity_kind) {
    case W8_ITEM_QUANTITY_NONE:
        break;
    case W8_ITEM_QUANTITY_STACK:
        if (item->stack_count > 1) {
            text_buffer = fmt::format("{:d}", item->stack_count);
            DrawRcsText(text_buffer, left, top, width,
                        g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        }
        break;
    case W8_ITEM_QUANTITY_CHARGES:
    case W8_ITEM_QUANTITY_USES:
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_BLUE]);
        if (!item->identified) {
            text_buffer = "?";
        } else {
            text_buffer = fmt::format("{:d}", item->uses_or_charges);
        }
        height = g_wiz_text_font_secondary.height();
        DrawRcsTextJustified(text_buffer, left, top, width, height,
                             g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        break;
    case W8_ITEM_QUANTITY_SHOTS:
        g_wiz_text_font_secondary.setPalette(g_font_state_palettes[W8_FONT_PALETTE_YELLOW]);
        text_buffer = fmt::format("{:d}", item->uses_or_charges);
        height = g_wiz_text_font_secondary.height();
        DrawRcsTextJustified(text_buffer, left, top, width, height,
                             g_W8TextBufferAlignMiddle | g_W8TextBufferAlignRight);
        break;
    }
    g_wiz_text_font_secondary.resetPalette();
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
    screen.item_controls.dirty = true;
    DrawRcsText(g_localized_strings[0x935], 0x15e, 0x84, 0x4e,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    screen.text_buffer = fmt::format("{:d}", screen.reviewed->kill_count);
    DrawRcsText(screen.text_buffer, 0x1ae, 0x84, 0x20,
                g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    DrawRcsText(g_localized_strings[0x936], 0x15e, 0x92, 0x4e,
                g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
    screen.text_buffer = fmt::format("{:d}", screen.reviewed->death_count);
    DrawRcsText(screen.text_buffer, 0x1ae, 0x92, 0x20,
                g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    if (m_combat_view) {
        DrawRcsText(g_localized_strings[0x8b0], 0x15b, 10, 0x11d,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
        DrawRcsText(g_localized_strings[0x8b1], 0x15e, 0x30, 0x4e,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
        screen.text_buffer = fmt::format("{:d}", screen.reviewed->initiative);
        DrawRcsText(screen.text_buffer, 0x1ae, 0x30, 0x20,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
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
                screen.text_buffer = g_localized_strings[0x95c];
                screen.text_buffer += "?";
                screen.text_buffer += g_localized_strings[0x95b];
                screen.text_buffer += "?";
                screen.text_buffer += g_localized_strings[0x95a];
                screen.text_buffer += "?";
                m_help[hand + 2] = screen.text_buffer;
                screen.text_buffer = g_localized_strings[0x959];
                screen.text_buffer += "?";
                screen.text_buffer += g_localized_strings[0x95a];
                screen.text_buffer += "?";
                m_help[hand] = screen.text_buffer;
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
            screen.text_buffer =
                fmt::format("{:+d}", attack->damage_bonus + screen.reviewed->bonus.damage_bonus);
            DrawRcsText(screen.text_buffer, x, 0x30, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = fmt::format("{:d}-{:d}", minimum, maximum);
            DrawRcsText(screen.text_buffer, x, 0x3e, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = g_localized_strings[0x95c];
            screen.text_buffer += fmt::format(" {:d}, ", dice.Minimum());
            screen.text_buffer += g_localized_strings[0x95b];
            screen.text_buffer += fmt::format(" {:d}, ", dice.Maximum());
            screen.text_buffer += g_localized_strings[0x95a];
            screen.text_buffer += fmt::format(" {:+d}%", static_cast<int>(damage_bonus));
            m_help[hand + 2] = screen.text_buffer;
            screen.text_buffer = fmt::format("{:d}", skill_bonus + hit_bonus);
            DrawRcsText(screen.text_buffer, x, 0x4c, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = g_localized_strings[0x959];
            screen.text_buffer += fmt::format(" {:d}, ", skill_bonus);
            screen.text_buffer += g_localized_strings[0x95a];
            screen.text_buffer += fmt::format(" {:+d}", hit_bonus);
            m_help[hand] = screen.text_buffer;
            screen.text_buffer = fmt::format("{:d}", attack->attacks);
            DrawRcsText(screen.text_buffer, x, 0x5a, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = fmt::format("{:d}", attack->swings);
            DrawRcsText(screen.text_buffer, x, 0x68, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = fmt::format("{:+d}", hit_bonus);
            DrawRcsText(screen.text_buffer, x, 0x76, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer =
                fmt::format("{:+d}", attack->attack_bonus + screen.reviewed->bonus.attack_bonus);
            DrawRcsText(screen.text_buffer, x, 0x84, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            screen.text_buffer = fmt::format("{:+d}%", static_cast<int>(damage_bonus));
            DrawRcsText(screen.text_buffer, x, 0x92, 0x20,
                        g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
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
                screen.text_buffer = fmt::format("{:+d}", value);
                DrawRcsText(screen.text_buffer, value_x, y, 0x20,
                            g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
            }
        }
        DrawRcsText(g_localized_strings[0x8bf], 0x1da, 0x84, 0x7c,
                    g_W8TextBufferAlignMiddle | g_W8TextBufferAlignLeft);
        screen.text_buffer = fmt::format("{:d}%", screen.reviewed->damage_reduction);
        DrawRcsText(screen.text_buffer, 600, 0x84, 0x20,
                    g_W8TextBufferAlignCenter | g_W8TextBufferAlignMiddle);
    }
}

void W8CampScreenState::CreateCampInput()
{
    inventory_input.setEnabled(false);
    pool_input.setEnabled(false);
    spell_input.setEnabled(false);
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
        inventory_targets[slot] =
            &inventory_input.add(SDL_Rect{(slot & 1) * 49 + 11, (slot / 2) * 57 + 192, 47, 55},
                                 [this, slot](const UiPointerEvent& event) {
                                     return HandleCampItem(event, slot, W8ItemStorage::Backpack);
                                 });
        pool_targets[slot] =
            &pool_input.add(SDL_Rect{(slot & 1) * 49 + 512, (slot / 2) * 57 + 192, 47, 55},
                            [this, slot](const UiPointerEvent& event) {
                                return HandleCampItem(event, slot, W8ItemStorage::Pool);
                            });
    }
    for (int slot = 0; slot < 12; ++slot) {
        const auto& bounds = g_camp_screen_regions[slot];
        inventory_targets[8 + slot] =
            &inventory_input.add(SDL_Rect{bounds.x, bounds.y, bounds.width + 1, bounds.height + 1},
                                 [this, slot](const UiPointerEvent& event) {
                                     return HandleCampItem(event, slot, W8ItemStorage::Equipment);
                                 });
    }
    for (int realm = 0; realm < 6; ++realm) {
        spell_targets[realm] =
            &spell_input.add(SDL_Rect{(realm % 3) * 213 + 29, (realm / 3) * 140 + 195, 154, 102},
                             [this, realm](const UiPointerEvent& event) {
                                 return HandleCampSpellList(event, realm);
                             });
    }
}

bool CampScreenEnter()
{
    if (!g_camp_screen) g_camp_screen = std::make_unique<W8CampScreenState>();
    return g_camp_screen->Enter(CurrentScreenState());
}

bool W8CampScreenState::Enter(const W8ScreenStateRuntime& request)
{

    ClearPrimarySurface();
    reviewed = request.character;
    W8CampItemAction initial_item_action;
    if (request.parameter_4 == 0) {
        pending_item_action.identifying_character = 0;
        initial_item_action = W8_CAMP_ITEM_ACTION_NONE;
    } else {
        pending_item_action.identifying_character = request.parameter_4;
        initial_item_action = W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL;
        wiz8::sound::play(s_general_magic_sound);
        SetTargetingMode(W8_TARGET_NEED_ITEM);
    }
    gXStatus.item_drag.reset();
    CreatePartyPortraitEventRegions();
    CreateCampInput();
    item_action = initial_item_action;
    for (unsigned char& item_filter : item_filters) {
        item_filter = 0;
    }
    item_timer_active = false;
    item_timer_expired = false;
    CreateCampButtonPanel();
    CreateCampItemControls();
    CreateCampHeaderButtons();
    page = W8_CAMP_PAGE_ITEMS;
    info_page = W8_CAMP_INFO_PAGE_ITEMS;
    item_range = std::make_unique<W8RangeControl>(scroll_input, 0x263, 0xc1, 0x275, 0x1a1);
    item_range->on_change = [this](auto* range) {
        item_scroll = range->m_value << 1;
        if (((GetInputState().modifiers & CTRL_DOWN) != 0))
            redraw_flags |= W8_CAMP_REDRAW_ALL;
        else
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
    };
    for (int range_index = 0; range_index < 6; ++range_index) {
        const int x = (range_index % 3) * 0xd5;
        const int y = (range_index / 3) * 0x8c;
        auto& range = spell_ranges[range_index];
        range =
            std::make_unique<W8RangeControl>(scroll_input, x + 0xbb, y + 0xc3, x + 0xcd, y + 0x129);
        range->on_change = [this, range_index](auto* scroll) {
            learned_spells.scroll[range_index] = scroll->m_value;
            redraw_flags |= W8_CAMP_REDRAW_REALM_SPELLS_FIRST << range_index;
        };
    }
    effect_list.clear();
    effect_items_only = true;
    effect_filter = W8_CAMP_EFFECT_FILTER_ALL;
    stats_range = std::make_unique<W8RangeControl>(scroll_input, 0x264, 0xbe, 0x276, 0x1b5);
    stats_range->on_change = [this](auto* range) {
        effect_scroll = range->m_value;
        redraw_flags |= W8_CAMP_REDRAW_EFFECT_LIST;
    };
    stats_controls = std::make_unique<W8CampStatsControls>(*this);
    character_info = std::make_unique<W8CampCharacterInfo>(*this);
    item_icons_drawn = false;
    ActivateCampPage();
    animation_timer = w8_get_ticks() + (50);
    for (unsigned int animation = 0; animation < 6; ++animation) {
        animation_frames[animation] = Random(g_spell_realm_animations[animation].frame_count);
    }
    if (g_combat.active() &&
        g_combat.state()->equip_phase != W8CombatEquipPhase::action_selection) {
        if (g_combat.state()->equip_pending == 1) {
            for (int slot = 0; slot < 8; ++slot) {
                if (g_status.party.rows[slot].fOccupied && IsPartySlotEligible(slot) &&
                    g_status.party.rows[slot].pending_action == W8_ACTION_EQUIP) {
                    text_buffer = fmt::format("{} {}", g_status.party.characters[slot].name,
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
                        text_buffer = fmt::format("{}", g_status.party.characters[slot].name);
                    } else {
                        if (count == g_combat.state()->equip_pending) {
                            text_buffer += " ";
                            text_buffer += fmt::sprintf(g_localized_strings[0x918],
                                                        g_status.party.characters[slot].name);
                            goto show_equip_message;
                        }
                        text_buffer += ", ";
                        text_buffer += g_status.party.characters[slot].name;
                    }
                }
            }
        }
        Fail("fFoundEquipChar");
    show_equip_message:
        ShowNotice(text_buffer, 0, true, false);
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
                    if (item_action == W8_CAMP_ITEM_ACTION_NONE || page != W8_CAMP_PAGE_ITEMS) {
                        DismissSelectedPartyCharacter();
                    } else {
                        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
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
    if (page == W8_CAMP_PAGE_SPELLS && !TimeUntilDeadline(animation_timer)) {
        for (unsigned int realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
            ++animation_frames[realm];
            if (animation_frames[realm] == g_spell_realm_animations[realm].frame_count) {
                animation_frames[realm] = 0;
            }
        }
        animation_timer = w8_get_ticks() + (50);
        redraw_flags |= 0x10000;
    }
    if (page == W8_CAMP_PAGE_ITEMS && item_timer_active && !item_timer_expired &&
        !TimeUntilDeadline(item_timer)) {
        item_timer_expired = true;
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT;
    }
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
    if (!ResolvePendingCampCharacter(true)) return;
    BeginCombatRound();
    ReturnToPreviousScreen();
}

/* The camp screen's per-frame update: on the first frame after combat mode
   ends it re-raises every redraw flag, then repaints whichever page is active
   together with the level-up/dismissal panels and the dialog on top of the
   frame. */
unsigned int W8CampScreenState::ChangedItemCells()
{
    auto& shown = shown_inventory;
    unsigned int flags = 0;
    const bool same_character = shown.character == reviewed;
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (!same_character || shown.backpack[slot] != reviewed->backpack[slot])
            flags |= W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot;
    for (unsigned int slot = 0; slot < 12; ++slot)
        if (!same_character || shown.equipment[slot] != reviewed->EquippedItem[slot])
            flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot;
    const auto pool = std::span{g_status.party.item_pool}.first(g_status.party.item_count);
    if (!std::ranges::equal(shown.pool, pool)) flags |= W8_CAMP_ITEM_REDRAW_POOL;
    shown.character = reviewed;
    std::ranges::copy(reviewed->backpack, shown.backpack.begin());
    std::ranges::copy(reviewed->EquippedItem, shown.equipment.begin());
    shown.pool.assign(pool.begin(), pool.end());
    return flags;
}

void W8CampScreenState::DrawCampScreen(void)
{
    unsigned int index;

    if (reviewed) item_redraw_flags |= ChangedItemCells();

    if (((GetInputState().modifiers & CTRL_DOWN) == 0) && item_icons_drawn) {
        redraw_flags |= W8_CAMP_REDRAW_ALL;
        item_icons_drawn = false;
    }
    if (redraw_flags != 0 || item_redraw_flags != 0 || (g_status.party.held.item.iItemNo != -1)) {
        if (redraw_flags == W8_CAMP_REDRAW_ALL) {
            item_redraw_flags = 0xffffffff;
            if (dialog != 0) {
                dialog->m_dirty = true;
            }
        }
        DrawCampHeader();
        switch (page) {
        case W8_CAMP_PAGE_ITEMS:
            RedrawCampItemsPage();
            break;
        case W8_CAMP_PAGE_STATS:
            DrawCampStatsPage();
            break;
        case W8_CAMP_PAGE_SKILLS:
            DrawCampSkillsPage();
            break;
        case W8_CAMP_PAGE_SPELLS:
            DrawCampSpellPages();
            break;
        case W8_CAMP_PAGE_REGENERATION:
            DrawCampRegenStats();
            break;
        }
        RefreshCampItemActions((redraw_flags & W8_CAMP_REDRAW_ITEM_ACTIONS) != 0);
        g_calligraphy_font.setShade(Font::kNormalShade);
        item_redraw_flags = 0;
        redraw_flags = 0;
    }
    character_info->Redraw(GetFrameBufferSurface(), false);
    if (page == W8_CAMP_PAGE_ITEMS) {
        RefreshCampActionPanel(false);
        RefreshItemsTabPanel(false);
        RefreshCampSecondaryPanel(false);
        item_range->Redraw(GetFrameBufferSurface(), false);
        if (((GetInputState().modifiers & CTRL_DOWN) != 0) && dialog == 0) {
            DrawCampItemIcons();
            item_icons_drawn = true;
        }
    } else if (page == W8_CAMP_PAGE_STATS) {
        stats_range->Redraw(GetFrameBufferSurface(), false);
        stats_controls->Redraw(GetFrameBufferSurface(), false);
    } else if (page == W8_CAMP_PAGE_SPELLS) {
        for (index = 0; index < 6; ++index) {
            spell_ranges[index]->Redraw(GetFrameBufferSurface(), false);
        }
    }
    RefreshCampItemActions(false);
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
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
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
        inventory_input.setEnabled(true);
        EnableCampActionButtons();
        UpdateCampItemFilters();
        EnableCampSecondaryPanel();
        SetCampInfoPageMode(info_page);
        item_range->SetActive(true);
        if (g_status.game_started) {
            pool_input.setEnabled(true);
            item_scroll = 0;
            RebuildCampItemList();
            return;
        }
        break;
    case W8_CAMP_PAGE_STATS:
        RebuildCampEffectList();
        stats_range->SetActive(true);
        stats_controls->input.setEnabled(true);
        character_info->SetActive(true);
        return;
    case W8_CAMP_PAGE_SKILLS:
        CreateCampSkillRegions();
        return;
    case W8_CAMP_PAGE_SPELLS:
        spell_input.setEnabled(true);
        BuildLearnedSpellState(&learned_spells, reviewed);
        RefreshCampSpellRanges();
        selected_spell_row = -1;
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
        inventory_input.setEnabled(false);
        DisableItemsRealmTabs();
        DisableCampActionButtons();
        DisableCampSecondaryPanel();
        character_info->SetActive(false);
        item_range->SetActive(false);
        if (g_status.game_started) {
            pool_input.setEnabled(false);
            return;
        }
        break;
    case W8_CAMP_PAGE_STATS:
        stats_range->SetActive(false);
        stats_controls->input.setEnabled(false);
        character_info->SetActive(false);
        effect_list.clear();
        break;
    case W8_CAMP_PAGE_SKILLS:
        DisableCampSkillRegions();
        for (index = 0; index < 0x29; ++index) {
            reviewed->skills[index].improved = false;
        }
        return;
    case W8_CAMP_PAGE_SPELLS:
        spell_input.setEnabled(false);
        SetCampSpellRangesEnabled(false);
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

/* Equip-slot-group item filters are exclusive: selecting one clears the others. */
void W8CampScreenState::ClearOtherCampItemGroupFilters(W8CampItemFilter filter)
{
    unsigned int index;

    for (index = 2; index < 6; ++index) {
        if (index != filter) {
            item_filters[index] = 0;
        }
    }
}

/* Rebuild the visible item list from the party pool under the item filters:
   flag 0 restricts to items the displayed character can use, flag 1 to
   unidentified items, and flags 2-5 each admit one equip-slot group. The
   scrollbar's range and value track the count; the scroll position snaps back
   inside an emptied or shrunken list. */
void W8CampScreenState::RebuildCampItemList(void)
{
    unsigned int index;
    unsigned char filter;
    W8ItemInstance* pool;
    W8RangeControl* scrollbar;
    int rows;

    item_list_count = 0;
    filter = 0;
    for (index = 0; index < 6; ++index) {
        if (index != 0 && index != 1 && item_filters[index] != 0) {
            filter |= 1 << index;
        }
    }
    pool = g_status.party.item_pool;
    for (index = 0; index < g_status.party.item_count; ++index, ++pool) {
        if (pool->iItemNo != -1 &&
            (item_filters[W8_CAMP_ITEM_FILTER_USABLE] == 0 ||
             CanCharacterUseItem(reviewed, pool->iItemNo)) &&
            (item_filters[W8_CAMP_ITEM_FILTER_UNIDENTIFIED] == 0 || !pool->identified) &&
            (filter == 0 || (filter & static_cast<unsigned char>(
                                          1 << GetItemEquipSlotGroup(pool->iItemNo))) != 0)) {
            item_list[item_list_count] = index;
            ++item_list_count;
        }
    }
    scrollbar = item_range.get();
    scrollbar->SetEnabled(item_list_count > 8);
    rows = item_list_count - 8;
    if (rows > 0) {
        scrollbar->SetRange(0, rows % 2 == 0 ? rows / 2 : rows / 2 + 1);
    }
    if (item_list_count == 0) {
        item_scroll = 0;
    } else if (item_list_count <= item_scroll) {
        item_scroll = (item_list_count - 1) / 8 * 8;
    }
    if ((item_scroll & 1) == 0) {
        scrollbar->SetValue(item_scroll >> 1);
    } else {
        scrollbar->SetValue((item_scroll >> 1) + 1);
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

/* The shared click handler for the camp item regions: a backpack slot arrives
   with origin 0, a worn slot with origin 1 and a party-pool row with origin
   2. The screen's item_action picks the interpretation - plain handling,
   identify, stack split, use, use held item on item - and the held item can
   merge onto the clicked stack, swap with it, or drop into the pool. */
void W8CampScreenState::SelectPendingCampCharacter()
{
    SelectCampCharacter(*g_status.party.companion_inventory);
    if (IsPartySlotEligible(ReviewedPartySlot())) {
        QueueCharacterEvent((&g_status.party.characters[*g_status.party.companion_inventory]), 109,
                            0, kCharacterEventFullVolume);
    } else {
        std::string text =
            fmt::sprintf(g_localized_strings[0x931],
                         g_status.party.characters[*g_status.party.companion_inventory].name);
        ShowNotice(text, 0, true, false);
    }
}

void W8CampScreenState::HandleCampItemClick(W8ItemLocation location)
{
    W8ItemInstance* item = g_status.party.itemAt(location);
    const auto origin = location.storage;
    const unsigned int slot_index = location.slot;
    W8CombatSlot target;
    short related_kind;
    bool changed = false;
    bool merged = false;
    bool partially_merged = false;
    bool merge_tried = false;
    bool same_kind = false;
    bool choose_character;
    unsigned int index;
    unsigned int old_pool_count;
    int result;
    int party_slot;
    W8EquipSlot paired_slot;
    W8EquipSlot equip_slot = static_cast<W8EquipSlot>(slot_index);
    bool reidentify;
    W8ItemInstance* paired;
    W8Character* character;
    std::string text;

    if (item == 0) {
        Fail("pPCItem != NULL");
    }

    /* A held stackable whose name kind the clicked item merges with counts as
       the same item for the use-merge path below. */
    if ((g_status.party.held.item.iItemNo != -1) && item->iItemNo != -1 &&
        GetItemMergeKind(item->iItemNo, &related_kind) &&
        g_item_records[g_status.party.held.item.iItemNo].unidentified_name_index == related_kind) {
        same_kind = true;
    }
    if (item->iItemNo == -1 && !(g_status.party.held.item.iItemNo != -1)) {
        return;
    }
    if (origin == W8ItemStorage::Equipment && (g_status.party.held.item.iItemNo != -1) &&
        item_action != W8_CAMP_ITEM_ACTION_MOVE) {
        if (!same_kind &&
            !CanEquipItemInSlot(reviewed, g_status.party.held.item.iItemNo, slot_index, true)) {
            return;
        }
        if (!CanCharacterUseItem(reviewed, g_status.party.held.item.iItemNo)) {
            return;
        }
    }
    if (item_action == W8_CAMP_ITEM_ACTION_CAST_SPELL ||
        item_action == W8_CAMP_ITEM_ACTION_USE_ON_CHARACTER) {
        return;
    }
    if (!g_status.game_started) {
        text = g_localized_strings[0x900];
        ShowNotice(text, 0, true, false);
        return;
    }
    character = &g_status.party.characters[ReviewedPartySlot()];
    if (character->uiCondition[W8_CONDITION_MISSING] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x907];
        ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_WEBBED] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x908];
        ShowNotice(text, 0, true, false);
        return;
    }
    if (character->uiCondition[W8_CONDITION_TURNCOAT] != 0 &&
        (origin == W8ItemStorage::Equipment || origin == W8ItemStorage::Backpack)) {
        text = g_localized_strings[0x909];
        ShowNotice(text, 0, true, false);
        return;
    }
    if (character->highest_condition >= W8_CONDITION_TURNCOAT && origin != W8ItemStorage::Pool &&
        (g_status.party.held.item.iItemNo != -1) &&
        g_status.party.held.actor != ReviewedPartySlot()) {
        text = g_localized_strings[0x901];
        ShowNotice(text, 0, true, false);
        return;
    }

    /* In combat an item click spends the character's action allowance unless
       it only touches a weapon-class slot or folds into the same-kind merge.
       A weapon or shield being moved onto a slot it cannot pair with stays
       gated as well. */
    bool gated = true;
    if (g_combat.active() && origin != W8ItemStorage::Pool) {
        if (!(g_status.party.held.item.iItemNo != -1) ||
            ((g_item_records[g_status.party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
              g_item_records[g_status.party.held.item.iItemNo].equip_class ==
                  W8_ITEM_EQUIP_CLASS_AMMUNITION) &&
             g_status.party.held.actor == ReviewedPartySlot() &&
             (origin != W8ItemStorage::Equipment ||
              HeldItemFitsPairedSlot(ReviewedPartySlot(), equip_slot)))) {
            if (item->iItemNo == -1 ||
                g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON ||
                g_item_records[item->iItemNo].equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION ||
                same_kind) {
                gated = false;
            }
        }
    }
    if (gated && !IsCampActionAllowed(ReviewedPartySlot())) {
        return;
    }
    if (((GetInputState().modifiers & SHIFT_DOWN) != 0)) {
        TakeItemUnitToHand(location);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY_SPELL) {
        party_slot = CharacterPointerToPartySlot(pending_item_action.identifying_character);
        if (!CanItemLeaveItsSlot(item)) {
            QueueCharacterEvent(&g_status.party.characters[party_slot], 0x35, 0,
                                kCharacterEventFullVolume);
            return;
        }
        if (pending_item_action.identifying_character == 0) {
            Fail("gpIdentifyingPC != NULL");
        }
        old_pool_count = g_status.party.item_count;
        reidentify = false;
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_ITEM;
        target.item = location;
        StartBreathCycle(party_slot, false);
        W8PartySlotRow* row = &g_status.party.rows[party_slot];
        if (row->pending_action == W8_ACTION_USE_ITEM &&
            g_item_records[g_status.party.itemAt(row->pending_action_detail.item_use.item)->iItemNo]
                    .spell_id == W8_SPELL_IDENTIFY_ITEM) {
            reidentify = true;
            result = CommitPartySlotItemUse(
                party_slot, g_status.party.itemAt(row->pending_action_detail.item_use.item),
                &target);
        } else {
            result = CommitPartySlotSpell(party_slot, W8_SPELL_IDENTIFY_ITEM, 8, &target);
        }
        if (origin == W8ItemStorage::Pool && reidentify &&
            old_pool_count != g_status.party.item_count) {
            item =
                &g_status.party.item_pool[g_status.party.item_count - old_pool_count + slot_index];
            RebuildCampItemList();

            redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        }
        if (result == 1 && item->iItemNo != -1) {
            OpenItemInfoDialog(item);
            if (!item->identified) {
                QueueCharacterEvent(&g_status.party.characters[party_slot], 0x35, 0,
                                    kCharacterEventFullVolume);
            }
        }
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_USE_ON_ITEM) {
        UseHeldItemOnItem(item);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_IDENTIFY) {
        IdentifyAndOpenItemInfo(item);
        return;
    }
    if (item_action == W8_CAMP_ITEM_ACTION_SPLIT_STACK) {
        if (item->iItemNo != -1) {
            OpenSplitStackDialog(location);
            return;
        }
    } else if (item_action == W8_CAMP_ITEM_ACTION_USE) {
        if (CanCharacterUseItemEntry(reviewed, item) != 0) {
            UseCampItem(location);
        }
        return;
    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
        if ((g_status.party.held.item.iItemNo != -1)) {
            if (item->iItemNo == -1) {
                SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
                return;
            }
            if (g_status.party.companion_inventory) {
                SelectPendingCampCharacter();
                return;
            }
            MergeItemStacksWithHeld(location);
            return;
        }
        if (item->iItemNo == -1) {
            SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
            return;
        }
    }

    /* Equipped items that may not be removed get one warning dialog and a
       bound mark; the next click then unequips them. */
    if (item->iItemNo != -1 && origin == W8ItemStorage::Equipment) {
        if (!CanUnequipSlotItem(reviewed, equip_slot)) {
            text = g_localized_strings[0x90b];
            ShowNotice(text, 0, true, false);
            if (item->bound) {
                return;
            }
            BindEquippedItem(reviewed, equip_slot);
            return;
        }
        item->bound = true;
    }

    // Changing a weapon hand here supersedes a weapon-set swap queued for the round.
    const auto cancel_queued_swap = [&] {
        if (origin == W8ItemStorage::Equipment &&
            GetPairedEquipSlot(equip_slot) != W8_EQUIP_SLOT_NONE)
            g_status.party.rows[ReviewedPartySlot()].weapon_swap_pending = false;
    };
    if (same_kind) {
        MergeItemUses(ReviewedPartySlot(), location, {W8ItemStorage::Hand});
        SetCampItemActionMode(W8_CAMP_ITEM_ACTION_NONE);
        changed = true;
        cancel_queued_swap();
        RebuildCampItemList();
    } else {
        /* Fold the held stack onto a matching stack; for the pool the scan
           continues across every row until the held stack is consumed. */
        if ((g_status.party.held.item.iItemNo != -1) &&
            g_item_records[g_status.party.held.item.iItemNo].quantity_kind ==
                W8_ITEM_QUANTITY_STACK) {
            if (item->iItemNo != -1 && item->iItemNo == g_status.party.held.item.iItemNo) {
                const auto outcome = MergeInventoryStacks(location, {W8ItemStorage::Hand});
                merged = outcome.complete;
                partially_merged = outcome.changed && !outcome.complete;
                merge_tried = true;
            }
            if (!merged && origin == W8ItemStorage::Pool) {
                for (index = 0; index < g_status.party.item_count; ++index) {
                    const auto outcome = MergeInventoryStacks({W8ItemStorage::Pool, 0, index},
                                                              {W8ItemStorage::Hand});
                    partially_merged |= outcome.changed && !outcome.complete;
                    if (outcome.complete) {
                        merged = true;
                        break;
                    }
                }
            }
        }
        if (origin == W8ItemStorage::Pool) {
            if (!merged) {
                if ((g_status.party.held.item.iItemNo != -1)) {
                    if (g_status.party.companion_inventory) {
                        SelectPendingCampCharacter();
                    } else if (InsertHeldItemInPool(slot_index)) {
                        changed = true;
                        RebuildCampItemList();
                    } else {
                        text = g_localized_strings[0x90c];
                        ShowNotice(text, 0, true, false);
                    }
                } else if (slot_index < g_status.party.item_count) {
                    /* An empty hand picks the clicked pool row up. */
                    ExchangeInventoryItems(location, {W8ItemStorage::Hand},
                                           static_cast<unsigned>(g_status.selected_character));
                    changed = true;
                    if (item_action == W8_CAMP_ITEM_ACTION_DROP) {
                        DropHeldCampItem();
                        RebuildCampItemList();
                    } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE) {
                        SetHandCursors();
                        RebuildCampItemList();
                    } else {
                        RebuildCampItemList();
                    }
                }
            }
        } else {
            if (!merge_tried &&
                ((!(g_status.party.held.item.iItemNo != -1) && item->iItemNo != -1) ||
                 ((g_status.party.held.item.iItemNo != -1) && !merged))) {
                if (!g_status.party.companion_inventory ||
                    (&g_status.party.characters[*g_status.party.companion_inventory]) == reviewed) {
                    MarkCampCharacterPending(item);
                    if ((g_status.party.held.item.iItemNo != -1)) {
                        choose_character = g_status.party.held.actor == -1;
                        DeliverExceptionalItemReaction(&g_status.party.held.item, choose_character,
                                                       reviewed);
                    }
                    if (origin == W8ItemStorage::Equipment &&
                        (g_status.party.held.item.iItemNo != -1) &&
                        !HeldItemFitsPairedSlot(ReviewedPartySlot(), equip_slot)) {
                        paired_slot = GetPairedEquipSlot(equip_slot);
                        paired = &reviewed->EquippedItem[paired_slot];
                        if (!CanUnequipSlotItem(reviewed, paired_slot)) {
                            text = g_localized_strings[0x916];
                            ShowNotice(text, 0, true, false);
                            if (paired->bound) {
                                return;
                            }
                            BindEquippedItem(reviewed, paired_slot);
                            return;
                        }
                        if (item->iItemNo == -1) {
                            MarkCampCharacterPending(paired);
                        }
                        if (!EquipHeldItemAndStowPartner(ReviewedPartySlot(), equip_slot)) {
                            ShowNotice(g_localized_strings[0x90c], 0, true, false);
                            return;
                        }
                        changed = true;
                    } else {
                        ExchangeInventoryItems(location, W8ItemLocation{W8ItemStorage::Hand},
                                               static_cast<unsigned>(ReviewedPartySlot()));
                        changed = true;
                        if (origin == W8ItemStorage::Backpack &&
                            reviewed->backpack[slot_index].iItemNo != -1 && Random(100) < 5) {
                            QueueCharacterEvent(reviewed, g_special_event14, 0,
                                                kCharacterEventFullVolume);
                        }
                    }
                } else {
                    SelectPendingCampCharacter();
                }
            }
            if (item_action == W8_CAMP_ITEM_ACTION_DROP &&
                (g_status.party.held.item.iItemNo != -1)) {
                DropHeldCampItem();
            } else if (item_action == W8_CAMP_ITEM_ACTION_MOVE &&
                       (g_status.party.held.item.iItemNo != -1)) {
                SetHandCursors();
            }
            if (changed) {
                cancel_queued_swap();
                RebuildCampItemList();
            }
        }
    }
    if (!changed && !partially_merged && !merged) {
        if (same_kind) {
            redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
        }
        return;
    }
    redraw_flags |= W8_CAMP_REDRAW_ITEM_ACTIONS;
    if (((GetInputState().modifiers & CTRL_DOWN) != 0)) {
        redraw_flags |= W8_CAMP_REDRAW_ALL;
    } else {
        if (origin == W8ItemStorage::Backpack) {
            if (partially_merged || merged) {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_BACKPACK;
            } else {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_BACKPACK_CELL_FIRST << slot_index;
            }
        } else if (origin == W8ItemStorage::Equipment) {
            if (partially_merged || merged) {
                item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELL_FIRST << slot_index;
            } else {
                redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
                redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
            }
        } else if (origin == W8ItemStorage::Pool) {
            item_redraw_flags |= W8_CAMP_ITEM_REDRAW_POOL;
        }
        item_redraw_flags |= W8_CAMP_ITEM_REDRAW_EQUIPMENT_CELLS;
    }
    redraw_flags |= W8_CAMP_REDRAW_CHARACTER_INFO;
    redraw_flags |= W8_CAMP_REDRAW_PORTRAIT;
}

void W8CampScreenState::TakeItemUnitToHand(W8ItemLocation source)
{
    const auto* item = g_status.party.itemAt(source);
    if (!item) return;
    if (item_action != W8_CAMP_ITEM_ACTION_NONE || !CanSplitItemStack(item)) return;
    if (!SplitItemStack(source, 1, ReviewedPartySlot())) return;
    RebuildCampItemList();
    redraw_flags |= W8_CAMP_REDRAW_ALL;
}

/* Commits the pending companion swap left by MarkCampCharacterPending:
   selects the new character and either queues its join event or opens the
   refusal dialog. Returns zero when a swap was pending and consumed, one when
   the caller may proceed. force ignores the same-character early out. */
bool W8CampScreenState::ResolvePendingCampCharacter(bool force)
{
    if (g_status.party.companion_inventory &&
        ((&g_status.party.characters[*g_status.party.companion_inventory]) != reviewed || force)) {
        SelectPendingCampCharacter();
        return false;
    }
    return true;
}

/* When the reviewed slot is one of the two companion slots and its bound NPC
   wants the item being used, the current character is remembered as pending a
   swap (cleared first so a failed attempt leaves none). */
void W8CampScreenState::MarkCampCharacterPending(W8ItemInstance* item)
{
    W8NpcState* npc;

    g_status.party.companion_inventory.reset();
    if (item->iItemNo != -1 && (ReviewedPartySlot() == 0 || ReviewedPartySlot() == 1)) {
        npc = GetNpcState(g_status.party.rows[ReviewedPartySlot()].npc_index);
        if (npc != 0 && NpcWantsItem(npc, item)) {
            g_status.party.companion_inventory = ReviewedPartySlot();
        }
    }
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
