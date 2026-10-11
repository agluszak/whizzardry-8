#include <fmt/format.h>
#include <fmt/printf.h>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/local_screens/MGSSpellIcons.h"
#include "input.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/ButtonSound.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/Controls.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/local_code/TextControl.h"
#include "wiz8/regions.h"
#include "wiz8/xstatus.h"
#include "wiz8/engine_code/3dapi.h"
#include "gameplay_modifiers.h"
#include "layouts/main_game_screen.h"
#include "wiz8/video_object_catalog.h"

// GLOBAL: WIZ8 0x0069c260
static unsigned int g_effect_icon_help_duration;

// GLOBAL: WIZ8 0x0069C25C
unsigned int g_spell_icon_count;
/* Child spell-icon text controls under g_spell_icon_strip. */
// GLOBAL: WIZ8 0x0069C264
static constexpr unsigned int kPartyEffectIconCount = 9;
static W8TextControl* g_spell_icon_rows[12];
/* Right-side combat-effect icon rows under g_combat_effect_right_panel. */
// GLOBAL: WIZ8 0x0069C294
static W8TextControl* g_combat_effect_right_rows[6];
// GLOBAL: WIZ8 0x0069C2AC
unsigned int g_combat_effect_right_count;
// GLOBAL: WIZ8 0x0069C2B0
Controls* g_spell_icon_strip;
// GLOBAL: WIZ8 0x0069C2B4
unsigned int g_combat_effect_left_count;
// GLOBAL: WIZ8 0x0069C2B8
Controls* g_combat_effect_right_panel;
// GLOBAL: WIZ8 0x0069C2BC
Controls* g_combat_effect_left_panel;
/* Left-side combat-effect icon rows under g_combat_effect_left_panel. */
// GLOBAL: WIZ8 0x0069C2C0
static W8TextControl* g_combat_effect_left_rows[9];

#define MGSSPELLICONS_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\MGSSpellIcons.cpp"

static void RebuildCombatEffectHudRows(void);

// FUNCTION: WIZ8 0x005AE9D0
unsigned char CreateSpellIconHudControls(void)
{
    g_spell_icon_strip = 0;
    g_combat_effect_left_panel = 0;
    g_combat_effect_right_panel = 0;

    g_spell_icon_strip = new Controls(0x19d, 0, 0x280, 0x12, std::nullopt, 0);
    if (g_spell_icon_strip == 0) {
        return 0;
    }

    g_spell_icon_count = 0;
    g_combat_effect_left_panel = new Controls(0x81, 0x14, 0x13d, 0x28, std::nullopt, 0);
    if (g_combat_effect_left_panel == 0) {
        return 0;
    }

    g_combat_effect_left_count = 0;
    g_combat_effect_right_panel = new Controls(0x182, 0x14, 0x1ff, 0x28, std::nullopt, 0);
    if (g_combat_effect_right_panel == 0) {
        return 0;
    }

    g_combat_effect_right_count = 0;
    return 1;
}

/* Strip rows are detached before destruction. Destruction can change the
   live panel/count, so each iteration reads them again and clears the observer slot. */
static void DestroyEffectIconRows(Controls*& panel, W8TextControl** rows, const unsigned int& count)
{
    for (unsigned int index = 0; index < count; ++index) {
        auto row = panel->TakeControl(rows[index]);
        rows[index] = nullptr;
    }
}

static void ClearSpellIconHudRows()
{
    unsigned int index;
    DestroyEffectIconRows(g_spell_icon_strip, g_spell_icon_rows, g_spell_icon_count);
    /* Retail swept twelve catalog slots downward from the last party icon,
       running three past the strip into the neighbouring set. The screen now
       owns exactly the nine party-icon regions, and the sweep must skip the
       leave path that has already destroyed them. */
    if (g_main_effect_regions) {
        for (index = 0; index < kPartyEffectIconCount; ++index) {
            DisableRegionInput(g_main_effect_regions->index(kPartyEffectIconCount - 1 - index));
        }
    }
    g_spell_icon_count = 0;
}

// FUNCTION: WIZ8 0x005AEB20
void DestroySpellIconHudControls(void)
{
    ClearSpellIconHudRows();
    if (g_spell_icon_strip != 0) {
        delete g_spell_icon_strip;
    }

    DestroyCombatEffectHudRows();
    if (g_combat_effect_left_panel != 0) {
        delete g_combat_effect_left_panel;
    }
    if (g_combat_effect_right_panel != 0) {
        delete g_combat_effect_right_panel;
    }
}

/* Drop the live spell-icon rows and rebuild them from the current party spell
   slots, then enable and redraw the top strip when any icons remain. */
// FUNCTION: WIZ8 0x005AEBE0
void RefreshSpellIconHudRows(void)
{
    ClearSpellIconHudRows();
    RebuildSpellIconHudRows();
    g_spell_icon_strip->SetEnabled(true);
    if (g_spell_icon_count != 0) {
        g_spell_icon_strip->Redraw();
    }
}

/* Walk the twelve party effect slots and create one top-strip text control per
   active effect whose visual table entry is not BAD_INDEX. */
// FUNCTION: WIZ8 0x005AEC70
void RebuildSpellIconHudRows(void)
{
    int left;
    int right;
    int icon;
    int slot;
    W8TextControl* control;
    W8EffectSlot* effect;

    left = 0xd1;
    right = 0xe3;
    g_spell_icon_count = 0;
    for (slot = 0; slot < 12; ++slot) {
        effect = &g_status.effect_slots[slot];
        if (effect->active) {
            icon = g_effect_visual_table[effect->effect_id].hud_icon;
            if (icon == -1) {
                ReportAssertion("iSpellIcon != BAD_INDEX", MGSSPELLICONS_CPP, 0x108);
            }
            control = new W8TextControl(g_spell_icon_strip, g_main_effect_regions->index(8 - g_spell_icon_count),
                                        left, 0, right, 0x12, CatalogArtwork::Load(icon, 0), 0, -1,
                                        -1, -1, -1);
            g_spell_icon_rows[g_spell_icon_count] = control;
            control->Invalidate(false);
            EnableRegionInput(g_main_effect_regions->index(8 - g_spell_icon_count));
            ++g_spell_icon_count;
            right = left - 1;
            left -= 0x13;
        }
    }
}

/* Tear down combat-effect rows, rebuild them while combat is up, then enable
   and redraw the flanking panels. Portrait-refresh pending on slots 0/1 hides
   the matching panel. */
// FUNCTION: WIZ8 0x005AEF30
void RefreshCombatEffectHud(void)
{
    DestroyCombatEffectHudRows();
    if (!gXStatus.fCombatMode) {
        return;
    }
    RebuildCombatEffectHudRows();
    g_combat_effect_left_panel->SetEnabled(true);
    g_combat_effect_right_panel->SetEnabled(true);
    if (g_combat_effect_left_count != 0) {
        g_combat_effect_left_panel->Redraw();
    }
    if (g_combat_effect_right_count != 0) {
        g_combat_effect_right_panel->Redraw();
    }
    if (g_level_block->portrait_refresh_pending[0] != 0) {
        g_combat_effect_left_panel->SetEnabled(false);
    }
    if (g_level_block->portrait_refresh_pending[1] != 0) {
        g_combat_effect_right_panel->SetEnabled(false);
    }
}

/* Rebuild both combat-effect panels: nine left slots walk right from x=0,
   six right slots walk left from x=0x69, one text control per active effect. */
// FUNCTION: WIZ8 0x005AEFC0
static void RebuildCombatEffectHudRows(void)
{
    int left;
    int right;
    int icon;
    int slot;
    W8TextControl* control;
    W8EffectSlot* effect;

    g_combat_effect_left_panel->SetBounds(0x81, 0x14, 0x13d, 0x28);
    left = 0;
    right = 0x14;
    g_combat_effect_left_count = 0;
    for (slot = 0; slot < 9; ++slot) {
        effect = &g_combat_state->effect_slots[slot];
        if (effect->active) {
            icon = g_effect_visual_table[effect->effect_id].hud_icon;
            if (icon == -1) {
                ReportAssertion("iSpellIcon != BAD_INDEX", MGSSPELLICONS_CPP, 0x228);
            }
            control =
                new W8TextControl(g_combat_effect_left_panel,
                                  g_main_effect_regions->index(9 + g_combat_effect_left_count), left,
                                  0, right, 0x14, CatalogArtwork::Load(icon, 0), 0, -1, -1, -1,
                                  -1);
            g_combat_effect_left_rows[g_combat_effect_left_count] = control;
            control->Invalidate(false);
            EnableRegionInput(g_main_effect_regions->index(9 + g_combat_effect_left_count));
            ++g_combat_effect_left_count;
            left = right + 1;
            right += 0x15;
        }
    }
    g_combat_effect_right_panel->SetBounds(0x182, 0x14, 0x1ff, 0x28);
    left = 0x69;
    right = 0x7d;
    g_combat_effect_right_count = 0;
    for (slot = 0; slot < 6; ++slot) {
        effect = &g_combat_state->effect_slots0[slot];
        if (effect->active) {
            icon = g_effect_visual_table[effect->effect_id].hud_icon;
            if (icon == -1) {
                ReportAssertion("iSpellIcon != BAD_INDEX", MGSSPELLICONS_CPP, 0x25e);
            }
            control =
                new W8TextControl(
                    g_combat_effect_right_panel,
                    g_main_effect_regions->index(23 - g_combat_effect_right_count), left, 0, right,
                    0x14, CatalogArtwork::Load(icon, 0), 0, -1, -1, -1, -1);
            g_combat_effect_right_rows[g_combat_effect_right_count] = control;
            control->Invalidate(false);
            EnableRegionInput(g_main_effect_regions->index(23 - g_combat_effect_right_count));
            ++g_combat_effect_right_count;
            right = left - 1;
            left -= 0x15;
        }
    }
}
// FUNCTION: WIZ8 0x005AF210
void DestroyCombatEffectHudRows(void)
{
    unsigned int index;

    DestroyEffectIconRows(g_combat_effect_left_panel, g_combat_effect_left_rows,
                          g_combat_effect_left_count);
    if (g_main_effect_regions) {
        for (index = 0; index < 9; ++index) {
            DisableRegionInput(g_main_effect_regions->index(9 + index));
        }
    }
    g_combat_effect_left_count = 0;

    DestroyEffectIconRows(g_combat_effect_right_panel, g_combat_effect_right_rows,
                          g_combat_effect_right_count);
    if (g_main_effect_regions) {
        for (index = 0; index < 6; ++index) {
            DisableRegionInput(g_main_effect_regions->index(23 - index));
        }
    }
    g_combat_effect_right_count = 0;
}

/* Clear and invalidate the main-game effect strip while it is up. Moved here
   from Magic Effects.cpp: the TU report proves this hull's range covers
   0x005AF2D0. */
// FUNCTION: WIZ8 0x005af2d0
void InvalidateMainGameEffectHud(void)
{
    if (g_current_screen_state.id == W8_SCREEN_MAIN_GAME) {
        ClearSurfaceRect(0x7f, 0x14, 0x201, 0x28);
        InvalidateRegion(0x7f, 0x14, 0x201, 0x28, 0);
    }
}

static void ShowEffectIconHelp(const W8EffectSlot* slot, bool show_duration, int assertion_line)
{
    W8SpellRuntimeRecord* records = g_spell_records;
    int amount = slot->amount;
    unsigned int duration = slot->duration;
    int effect_id = slot->effect_id;
    g_effect_icon_help_duration = duration;
    std::string text = fmt::format("{}: ", records[effect_id].display_name);
    text += show_duration
                ? fmt::sprintf(g_localized_strings[0x79c], amount, g_effect_icon_help_duration)
                : fmt::sprintf(g_localized_strings[0x79d], amount);
    SetRegionHelpText(text);
}

// FUNCTION: WIZ8 0x005AED90
void ShowPartyEffectIconHelp(int slot_index)
{
    ShowEffectIconHelp(&g_status.effect_slots[slot_index], true, 0x15c);
}

static unsigned int FindActiveEffectSlotIndex(const W8EffectSlot* slots, unsigned int count,
                                              unsigned int active_index)
{
    unsigned int match = 0;
    for (unsigned int index = 0; index < count; ++index) {
        if (slots[index].active) {
            if (match == active_index) {
                return index;
            }
            ++match;
        }
    }
    return count;
}

/* All three effect strips use the same active-slot hover policy. The caller
   retains the combat guard and sound setup before supplying its current slots. */
static unsigned char DispatchEffectIconRegionEvent(const InputEvent* event, W8Region* region,
                                                   const W8EffectSlot* slots, unsigned int count,
                                                   void (*show_help)(int))
{
    unsigned int slot_index = FindActiveEffectSlotIndex(slots, count, region->callback_id);
    if (slot_index != count && event->type == InputEventType::MouseMove) {
        if ((region->flags & W8_REGION_MOUSE_LEAVE) != 0) {
            return 1;
        }
        if ((region->flags & W8_REGION_MOUSE_ENTER) != 0) {
            show_help(slot_index);
            return 1;
        }
        if (g_effect_icon_help_duration != slots[slot_index].duration) {
            show_help(slot_index);
            ResetRegionHelp(false);
        }
    }
    return 0;
}

// FUNCTION: WIZ8 0x005AEEA0
unsigned char PartyEffectIconRegionEvent(const InputEvent* event, W8Region* region)
{
    PushButtonSoundScheme(0, true);
    return DispatchEffectIconRegionEvent(event, region, g_status.effect_slots, 12,
                                         ShowPartyEffectIconHelp);
}

/* Left combat-effect strip (nine slots at g_combat_state->effect_slots). */

// FUNCTION: WIZ8 0x005AF300
void ShowCombatLeftEffectIconHelp(int slot_index)
{
    ShowEffectIconHelp(&g_combat_state->effect_slots[slot_index], false, 0x2e1);
}

// FUNCTION: WIZ8 0x005AF410
void ShowCombatRightEffectIconHelp(int slot_index)
{
    ShowEffectIconHelp(&g_combat_state->effect_slots0[slot_index], true, 0x30d);
}

/* Top-row party effect icons: map callback_id onto the Nth active party
   effect slot and refresh help on enter. */

// FUNCTION: WIZ8 0x005AF530
unsigned char CombatLeftEffectIconRegionEvent(const InputEvent* event, W8Region* region)
{
    if (!gXStatus.fCombatMode) {
        return 0;
    }
    PushButtonSoundScheme(0, true);
    return DispatchEffectIconRegionEvent(event, region, g_combat_state->effect_slots, 9,
                                         ShowCombatLeftEffectIconHelp);
}

/* Right combat-effect strip (six slots at g_combat_state->effect_slots0). */

// FUNCTION: WIZ8 0x005AF5E0
unsigned char CombatRightEffectIconRegionEvent(const InputEvent* event, W8Region* region)
{
    if (!gXStatus.fCombatMode) {
        return 0;
    }
    PushButtonSoundScheme(0, true);
    return DispatchEffectIconRegionEvent(event, region, g_combat_state->effect_slots0, 6,
                                         ShowCombatRightEffectIconHelp);
}
