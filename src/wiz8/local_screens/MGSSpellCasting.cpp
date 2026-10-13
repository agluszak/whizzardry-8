#include <utility>
#include <SDL3/SDL_mouse.h>
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/button_interaction.h"
#include "wiz8/ui_layer.h"
#include <functional>
#include <memory>
#include <algorithm>
#include <SDL3/SDL_rect.h>
#include <array>
#include <fmt/format.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "Video2.h"
#include "wiz8/spell_ids.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/integer_constants.h"
#include "timer.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/engine_code/Cursor3d.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/learned_spells.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/sr_api.h"
#include "wiz8/utility.h"
#include "wiz8/world_cursor.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/xstatus.h"
#include "character_skills.h"
#include "compat/native.h"
#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "local_code/HealthStaminaMana.h"

struct W8NpcState;

#define SPELLCASTING_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\MGSSpellCasting.cpp"

/* The first realm skill id; character->skill_unlocks[first + realm] counts
   the spells known in that realm. */

/* Realm-name string ids for the spell-casting realm-button tooltips. */
// GLOBAL: WIZ8 0x0064C840
static int g_spell_realm_help_string_ids[6] = {0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c};


// GLOBAL: WIZ8 0x0069BF3C
std::unique_ptr<W8SpellCastingView> gpSCSV;

int GetSpellCastingPowerIndex(void)
{
    if (!gpSCSV) {
        return -1;
    }
    return gpSCSV->iSpellPower;
}

static void CreateSpellCastingViewControls(void);
static void SelectSpellPowerPip7(void);
static void SelectSpellPowerPip8(void);
static void SpellCastingDialogResult(W8DialogBase* dialog);
static void PreviewSpellPowerPipHover(int power_level);
void UpdateSpellRealmPointDisplays(void);
static void UpdateSpellPowerPips(void);
static void RefreshSpellPowerPip(int pip);
static void RebuildSpellCastingList(int spell_id);
static void SetSpellListLineColor(int index, char color);
static void SelectSpellCastingRow(int index);
static void SelectSpellCastingRealm(W8SpellRealm realm);
static void SelectSpellCastingListRow(int index);
void TryCommitSpellCast(void);
void ShowSpellCastingError(int spell_id);

/* Builds the three spell-casting panels and all of their controls: the six
   realm buttons with their animated icons, the nine power-level pips, the
   spell-name label and the cancel button. */
// FUNCTION: WIZ8 0x0059E1F0
static void CreateSpellCastingViewControls(void)
{
    const auto make_button = [](int artwork, const std::array<int, 5>& frames,
                                SDL_Rect bounds, int help, std::function<void()> action) {
        auto button = std::make_unique<W8ButtonWidget>(gpSCSV->input);
        button->Configure(CatalogArtwork::Load(artwork, 0).spriteSheet(),
            frames[0], frames[1], frames[2], frames[3], frames[4],
            [action](auto*) { AfterUiDispatch(action); }, {}, false, 0, help, {}, {});
        button->policy.controller_activation = true;
        button->policy.primary_toggle = ButtonToggle::Release;
        button->SetPosition(bounds.x, bounds.y);
        button->Resize(bounds.w, bounds.h);
        return button;
    };
    constexpr SDL_Point realm_positions[] = {{0x1e, 0xd}, {0x71, 0xd}, {0x12, 0x23},
        {0x7d, 0x23}, {0x1e, 0x39}, {0x71, 0x39}};
    constexpr int text_left[] = {0x1e, 0x85, 0x12, 0x91, 0x1e, 0x85};
    for (int realm = 0; realm < 6; ++realm) {
        const int frame = (realm % 2) * 4;
        const auto pos = realm_positions[realm];
        auto& button = gpSCSV->realm_buttons[realm];
        button = make_button(0x191, {frame + 2, frame, frame + 1, frame + 3, frame + 3},
            {0x17 + pos.x, 0x166 + pos.y, 75, 22}, -1,
            [realm] { SelectSpellCastingRealm(static_cast<W8SpellRealm>(realm)); });
        const auto tooltip = [realm] {
            const auto& name = g_localized_strings[g_spell_realm_help_string_ids[realm]];
            if (!gpSCSV->realm_buttons[realm]->IsEnabled()) return std::string(name);
            return fmt::format("{} ({}: {:d})", name, g_localized_strings[0x2d],
                g_status.buffers.Char[g_status.selected_character]
                    .skills[W8_SKILL_FIRE_MAGIC + realm].level);
        };
        button->SetTooltip(tooltip);
        button->on_hover = [tooltip](bool entered) {
            gpSCSV->dirty[0] = true;
        };
        auto& text = gpSCSV->realm_text[realm];
        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        text.SetLayoutBounds(0x17 + text_left[realm], 0x166 + pos.y,
            0x17 + text_left[realm] + 55, 0x166 + pos.y + 22);
    }
    constexpr SDL_Point pip_positions[] = {{0x38, 2}, {0x48, 4}, {0x56, 0x10},
        {0x5b, 0x23}, {0x52, 0x38}, {0x3a, 0x41}, {0x1f, 0x3a}, {0x10, 0x1f}, {0x10, 0x1f}};
    constexpr SDL_Point pip_centers[] = {{0x3f, 9}, {0x50, 0xc}, {0x5f, 0x19},
        {0x65, 0x2d}, {0x5d, 0x43}, {0x46, 0x4d}, {0x2c, 0x47}, {0x1e, 0x2d}, {0x1e, 0x2d}};
    for (int pip = 0; pip < 9; ++pip) {
        const int base = pip == 7 ? 0x30 : pip == 8 ? 0x2a : pip * 6;
        auto& frames = gpSCSV->power_frames[pip];
        frames = {base + 3, base, base + 1, base + 2, base + 4};
        if (pip == 8) { frames[3] = base; frames[4] = base + 1; }
        const int radius = 7 + std::min(pip, 7);
        auto& button = gpSCSV->power_controls[pip];
        button = make_button(0x192, frames,
            {0x1a4 + pip_positions[pip].x, 0x166 + pip_positions[pip].y,
             radius * 2 + 1, radius * 2 + 1}, 46 + pip, [pip] {
                if (pip == 7) SelectSpellPowerPip7();
                else if (pip == 8) SelectSpellPowerPip8();
                else {
                    SelectSpellPowerLevel(pip);
                    if (!gpSCSV->power_controls[pip]->IsPressed()) PreviewSpellPowerPipHover(pip);
                }
            });
        button->SetCircularHitArea({0x1a4 + pip_centers[pip].x, 0x166 + pip_centers[pip].y}, radius);
        button->on_hover = [pip](bool entered) { PreviewSpellPowerPipHover(entered ? pip : -1); };
    }
    gpSCSV->spell_name.SetLayoutBounds(0x1b2, 0x170, 0x1d4, 0x17e);
    gpSCSV->spell_name.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (!gpSCSV->name_target) {
        gpSCSV->name_target = &gpSCSV->input.add(SDL_Rect{0x1b2, 0x170, 35, 15},
            [](const UiPointerEvent&) { return UiResult::Pass; });
        gpSCSV->name_target->setTooltip([] { return std::string(g_localized_strings[55]); });
    }
    gpSCSV->cancel = make_button(0x8e, {7, 4, 5, -1, 6},
        {0x23a, 0x16c, 27, 27}, 17, ResetSpellCastingSelection);
    gpSCSV->cancel->policy.primary_toggle = ButtonToggle::None;
    gpSCSV->dirty.fill(true);

    if (gXStatus.fCampMode) {
        for (int index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (g_main_input) {
                DisableMainGamePortraitSlot(index);
                g_main_input->edge_targets[index]->setEnabled(false);
            }
        }
    }
}

/* Opens the spell-casting view for one party slot: allocates and clears the
   view state, swaps the game mode, builds the panels and redirects the region
   sets the view takes over. */
// FUNCTION: WIZ8 0x0059F0E0
unsigned char OpenSpellCastingView(int party_slot)
{
    W8MainUiMode mode;

    if (!IsPartySlotEligible(party_slot)) {
        return 0;
    }
    if (!gpSCSV) {
        gpSCSV = std::make_unique<W8SpellCastingView>();
    }
    UpdateScreenOverlays(0);
    gXStatus.fSpellCastMode = true;
    if (g_level_block->combat_end_notification != -1) {
        DestroySubMenuControls();
    }
    gpSCSV->location_id = -1;
    gpSCSV->interact_id = W8_DIALOGUE_LAYOUT_UNSET;
    CloseMainGameOverlays();
    mode = g_settings.main_ui_mode;
    SetMainGameOverlayViewport(mode);
    gpSCSV->saved_game_mode = mode;
    CreateSpellCastingViewControls();
    RestoreSpellCastingRegions();
    gpSCSV->input.setEnabled(true);
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    g_level_block->text_box_visible = false;
    SetTextBoxRegionBounds(0xea, 0x16e, 0x18c, 0x1ba);
    gpSCSV->iSpellRealm = W8_SPELL_REALM_NONE;
    SelectSpellCastingPartySlot(party_slot);
    gpSCSV->selected_spell_index = -1;
    SelectSpellCastingCharacter(party_slot);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    PauseMainGameWorld();
    gpSCSV->override_spell = 0;
    return 1;
}

/* Tears the spell-casting view down: restores the regions and game mode,
   frees the view state, and resumes the world. A pending interact id picks
   its NPC interaction back up. */
// FUNCTION: WIZ8 0x0059F2B0
void CloseSpellCastingView(void)
{
    int location_id;
    W8NpcDialogueLayout interact_id;
    W8NpcState* npc;

    if (!gpSCSV->closing) {
        gpSCSV->input.setEnabled(false);
        SetTextBoxScrollInput(false);
        g_level_block->action_panel_visible = 0;
        SetTargetingMode(W8_TARGET_NEED_NONE);
        if (!gXStatus.fCampMode) {
            ResetEditorStatusLine(-1);
        }
        g_level_block->text_box_visible = true;
        SelectTextBox(gXStatus.fCombatMode);
        SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
        gXStatus.fSpellCastMode = false;
        ApplyMainGameModeFlag(gpSCSV->saved_game_mode, true);
        RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
        RequestRedrawCombatBar();
        RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
        location_id = gpSCSV->location_id;
        interact_id = gpSCSV->interact_id;
        gpSCSV.reset();
        ResumeMainGameWorld();
        if (IsWorldCursorVisible()) {
            ToggleWorldCursor();
        }
        if (gXStatus.fLockInteract && !IsScreenTransitionPending()) {
            OpenLockInteraction(0);
        }
        if (gXStatus.fTrapInteract && !IsScreenTransitionPending()) {
            OpenTrapInteraction(0);
        }
        if (interact_id != W8_DIALOGUE_LAYOUT_UNSET && !IsScreenTransitionPending()) {
            npc = FindNpcBindingForMonster(FindMonster(location_id));
            BeginNpcDialogue(npc, 0, -1, 0, 1);
            SwitchNpcDialogueLayout(interact_id);
        }
    }
}

/* Re-enables the region set and inputs the spell-casting view owns. */
// FUNCTION: WIZ8 0x0059F440
void RestoreSpellCastingRegions(void)
{
    SetTextBoxEnabled(true);
    SetTextBoxScrollInput(true);
    g_level_block->action_panel_visible = 1;
    SetTextBoxMuteInput(false);
    SetTextBoxChannelTabInput(false);
}

/* Shared choice reset for caster/realm changes and a cancelled dialog.
   This descriptive helper keeps the view open and retains callback order. */
static void ClearSpellCastingChoice()
{
    gpSCSV->uiSpellToCast = 0;
    gpSCSV->iSpellPowerClass = W8_SPELL_POWER_CLASS_NONE;
    gpSCSV->uiSpellIndex = -1;
    SelectSpellCastingRow(-1);
    gpSCSV->uiPowerLevels = 0;
    SelectSpellPowerLevel(-1);
    gpSCSV->dialog_confirmed = false;
    UpdateSpellPowerPips();
    SelectSpellCastingPartySlot(g_status.selected_character);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

/* Points the view at a new caster: rebuilds the learned-spell buckets and the
   realm/power displays, then either restores the pending cast's realm and
   list or clears a realm the new caster has no spell points in. */
// FUNCTION: WIZ8 0x0059F490
void SelectSpellCastingCharacter(int party_slot)
{
    W8SpellRealm realm;

    if (!CharacterHasCastableSpell(&g_status.buffers.Char[party_slot]) ||
        !IsPartySlotEligible(party_slot)) {
        ResetEditorStatusLine(-1);
        CloseSpellCastingView();
        return;
    }
    gpSCSV->caster = &g_status.buffers.Char[party_slot];
    BuildLearnedSpellState(&gpSCSV->learned, gpSCSV->caster);
    UpdateSpellRealmPointDisplays();
    ClearSpellCastingChoice();
    if (GetAffordableSpellPowerLevel(party_slot) != 0) {
        int spell_id = g_status.buffers.XChar[party_slot].spell_id;
        SelectSpellCastingRealm(g_spell_records[spell_id].realm);
        if (spell_id == W8_SPELL_IDENTIFY_ITEM) {
            gpSCSV->uiSpellIndex = -1;
            return;
        }
        RebuildSpellCastingList(spell_id);
        return;
    }
    realm = gpSCSV->iSpellRealm;
    if (realm == W8_SPELL_REALM_NONE) {
        return;
    }
    if (gpSCSV->caster->sp_max[realm] == 0) {
        gpSCSV->dirty[0] = true;
        gpSCSV->iSpellRealm = W8_SPELL_REALM_NONE;
        ResetEditorStatusLine(-1);
        return;
    }
    RebuildSpellCastingList(W8_SPELL_NONE);
}

/* Writes each realm's "current/max" spell-point caption and enables the realm
   button and icon for the pools the caster actually has. */
// FUNCTION: WIZ8 0x0059F660
void UpdateSpellRealmPointDisplays(void)
{
    int realm;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        bool has_realm = gpSCSV->caster->sp_max[realm] != 0;
        gpSCSV->realm_buttons[realm]->SetEnabled(has_realm);
        gpSCSV->realm_text[realm].SetFontStateIndex(has_realm ? -1 : 0);
        gpSCSV->dirty[0] = true;
        gpSCSV->realm_text[realm].SetText(
            fmt::format(
                "{:d}/{:d}",
                GetCharacterRealmSpellPoints(gpSCSV->caster, static_cast<W8SpellRealm>(realm)),
                gpSCSV->caster->sp_max[realm]),
            g_wiz_text_font_secondary);
    }
}

/* Enables and frames the power-level pips for the chosen spell's power class:
   class zero shows one pip per affordable level, class one keeps the
   seventh pip live out of combat and class two shows the max pip alone. */
static void RefreshAvailableSpellPowerPips()
{
    unsigned int pip;
    for (pip = 0; pip < gpSCSV->uiPowerLevels; ++pip) {
        RefreshSpellPowerPip(pip);
    }
    if (gpSCSV->uiPowerLevels < 7) {
        for (pip = gpSCSV->uiPowerLevels; pip < 7; ++pip) {
            gpSCSV->power_controls[pip]->SetEnabled(false);
        }
    }
}

// FUNCTION: WIZ8 0x0059F710
static void UpdateSpellPowerPips(void)
{
    unsigned int pip;
    W8SpellCastRating rating;
    gpSCSV->dirty[2] = true;

    if (gpSCSV->uiPowerLevels == 0 || gpSCSV->iSpellPowerClass == W8_SPELL_POWER_FIXED) {
        for (pip = 0; pip < W8_SPELL_POWER_PIP_COUNT; ++pip) {
            gpSCSV->power_controls[pip]->SetEnabled(false);
        }
    } else {
        RefreshSpellPowerPip(7);
        RefreshSpellPowerPip(8);
        if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_SELECTABLE) {
            RefreshAvailableSpellPowerPips();
            gpSCSV->power_controls[7]->SetVisible(false);
            gpSCSV->power_controls[8]->SetVisible(false);
        } else if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) {
            gpSCSV->power_controls[7]->SetVisible(!gXStatus.fCombatMode);
            RefreshAvailableSpellPowerPips();
            gpSCSV->power_controls[8]->SetVisible(false);
        } else if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_MAXIMUM) {
            for (pip = 0; pip < 7; ++pip) {
                gpSCSV->power_controls[pip]->SetEnabled(false);
            }
            gpSCSV->power_controls[8]->SetVisible(true);
            gpSCSV->power_controls[7]->SetVisible(false);
        }
    }
    if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_MAXIMUM) {
        if (gpSCSV->uiPowerLevels != 0) {
            for (pip = 0; pip < gpSCSV->uiPowerLevels; ++pip) {
                if (gpSCSV->uiSpellToCast == 0) {
                    srAssertFail("gpSCSV->uiSpellToCast != SPELL_NONE", SPELLCASTING_CPP, 0x338, 0);
                }
                gpSCSV->power_controls[pip]->SetEnabled(false);
                rating = GetSpellCastRating(gpSCSV->caster, gpSCSV->uiSpellToCast, pip + 1);
                gpSCSV->power_frames[pip][0] = (pip + rating * 9) * 6;
            }
        }
        if (gpSCSV->uiPowerLevels < 7) {
            for (pip = gpSCSV->uiPowerLevels; pip < 7; ++pip) {
                gpSCSV->power_frames[pip][0] = pip * 6 + 3;
            }
            return;
        }
    } else {
        for (pip = 0; pip < 7; ++pip) {
            gpSCSV->power_frames[pip][0] = pip * 6 + 3;
        }
    }
}

/* Recomputes one pip's four sprite frames from the spell's safety rating at
   the level the pip stands for; pips seven and eight price the max cast. */
// FUNCTION: WIZ8 0x0059F9D0
static void RefreshSpellPowerPip(int pip)
{
    W8SpellCastRating rating;
    int level;
    int frame;

    if (gpSCSV->uiSpellToCast == 0) {
        srAssertFail("gpSCSV->uiSpellToCast != SPELL_NONE", SPELLCASTING_CPP, 0x303, 0);
    }
    if (pip == 7 || pip == 8) {
        level = 8;
    } else {
        level = pip + 1;
    }
    gpSCSV->power_controls[pip]->SetEnabled(true);
    rating = GetSpellCastRating(gpSCSV->caster, gpSCSV->uiSpellToCast, level);
    frame = (pip + rating * 9) * 6;
    auto& frames = gpSCSV->power_frames[pip];
    frames[1] = frame;
    frames[2] = pip == 8 ? frame : frame + 1;
    frames[3] = frame + 2;
    frames[4] = pip == 8 ? frame + 2 : frame + 4;
    gpSCSV->dirty[2] = true;
}

static void AppendSpellCastingListEntry(int id, W8SpellRuntimeRecord* spell, char color)
{
    ++gpSCSV->uiSpellsInList;
    gpSCSV->uiSpells[gpSCSV->uiSpellsInList - 1] = id;
    gpSCSV->alt_colors[gpSCSV->uiSpellsInList - 1] = color;
    std::string line;
    line = fmt::format("{} {}", g_spell_target_parentheticals[GetSpellTargetType(id, false)],
                       spell->display_name);
    ShowNotice(W8_FONT_PALETTE_TEXT_BOX, line, 2);
    AppendTextBoxLine(fmt::format("{:d}", spell->spell_point_cost), 2);
}

/* Rebuilds the realm's spell list: pass zero appends every castable spell,
   pass one appends the unaffordable and blocked ones with their own colors.
   A nonzero spell id scrolls the list to and selects that spell's row. */
// FUNCTION: WIZ8 0x0059FAD0
static void RebuildSpellCastingList(int spell_id)
{
    int index;
    W8SpellRealm realm;
    int selected;
    unsigned char pass;
    W8SpellRuntimeRecord* spell;

    selected = -1;
    ResetEditorStatusLine(-1);
    gpSCSV->uiSpellIndex = -1;
    gpSCSV->selected_spell_index = -1;
    gpSCSV->uiSpellsInList = 0;
    realm = gpSCSV->iSpellRealm;
    for (index = 0; index < 0x15e; ++index) {
        gpSCSV->uiSpells[index] = 0;
        gpSCSV->alt_colors[index] = 0xff;
    }
    if (gpSCSV->caster->skill_unlocks[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm] != 0) {
        pass = 0;
        do {
            index = 0;
            while (index <
                   static_cast<int>(gpSCSV->caster->skill_unlocks[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm])) {
                int id = gpSCSV->learned.spell_ids_by_realm[realm][index];
                gpSCSV->override_spell = id;
                spell = &g_spell_records[id];
                if (spell->spell_point_cost <= gpSCSV->caster->iSPLeft[realm] &&
                    SpellUsableNow(id, false) &&
                    SpellHasAnyValidTarget(CharacterPointerToPartySlot(gpSCSV->caster), id,
                                           false) &&
                    !IsTeleportCastMissingAnchor(gpSCSV->caster, id)) {
                    if (IsSpellBlockedForCharacter(gpSCSV->caster, id)) {
                        if (pass == 1 &&
                            ((!gXStatus.fCampMode && !gXStatus.fLockInteract &&
                              !gXStatus.fTrapInteract) ||
                             SpellUsableNow(id, false)) &&
                            gpSCSV->uiSpellsInList + 1 <= 0x15e) {
                            AppendSpellCastingListEntry(id, spell, 4);
                            SetSpellListLineColor(gpSCSV->uiSpellsInList - 1, 4);
                        }
                    } else if (pass == 0) {
                        if (gpSCSV->uiSpellsInList + 1 <= 0x15e) {
                            AppendSpellCastingListEntry(id, spell, 0xf);
                        }
                        if (id == spell_id) {
                            selected = gpSCSV->uiSpellsInList - 1;
                        }
                    }
                } else if (pass == 1 &&
                           ((!gXStatus.fCampMode && !gXStatus.fLockInteract &&
                             !gXStatus.fTrapInteract) ||
                            SpellUsableNow(id, false)) &&
                           gpSCSV->uiSpellsInList + 1 <= 0x15e) {
                    AppendSpellCastingListEntry(id, spell, 0);
                    SetSpellListLineColor(gpSCSV->uiSpellsInList - 1, 0);
                }
                ++index;
            }
            gpSCSV->override_spell = 0;
            ++pass;
        } while (pass < 2);
    }
    if (spell_id == W8_SPELL_NONE || selected == -1) {
        ScrollTextBoxTo(0);
    } else {
        SelectSpellCastingListRow(selected);
        ScrollTextBoxTo(selected);
    }
    RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
}

/* Recolours one spell-list line; -1 keeps the row's own color, or the
   selected-row color when the row is the selected spell. */
// FUNCTION: WIZ8 0x0059FFA0
static void SetSpellListLineColor(int index, char color)
{
    W8MessageStorageRecord* line;
    size_t length;

    if (gpSCSV->uiSpellsInList < static_cast<unsigned int>(index)) {
        srAssertFail("uiSpellIndex <= gpSCSV->uiSpellsInList", SPELLCASTING_CPP, 0x3df, 0);
    }
    if (color == -1) {
        if (gpSCSV->uiSpellIndex == index) {
            color = 3;
        } else {
            color = gpSCSV->alt_colors[index];
            if (color == -1) {
                srAssertFail("bAltColor != -1", SPELLCASTING_CPP, 0x3f1, 0);
            }
        }
    }
    line = &g_message_storage[2][index];
    line->highlight_start = 2;
    length = line->text.size();
    line->highlight_color = color;
    line->highlight_stop = length;
}

/* Marks one spell-list row as selected, restoring the previous row's own
   color first; -1 just clears the selection. */
// FUNCTION: WIZ8 0x005A0040
static void SelectSpellCastingRow(int index)
{
    int previous;

    if (index == -1) {
        if (gpSCSV->selected_spell_index != -1) {
            SetSpellListLineColor(gpSCSV->selected_spell_index, -1);
            gpSCSV->selected_spell_index = -1;
            RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
            return;
        }
    } else {
        if (index < 0) {
            srAssertFail("iSpellIndex >= 0", SPELLCASTING_CPP, 0x406, 0);
        }
        if (index > static_cast<int>(gpSCSV->uiSpellsInList)) {
            srAssertFail("iSpellIndex <= (INT32) gpSCSV->uiSpellsInList", SPELLCASTING_CPP, 0x407,
                         0);
        }
        previous = gpSCSV->selected_spell_index;
        if (index != previous && previous != -1) {
            SetSpellListLineColor(previous, -1);
        }
        SetSpellListLineColor(index, 5);
        gpSCSV->selected_spell_index = index;
    }
    RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
}

/* Opens the spell-casting view when needed and selects the given spell's
   realm and list row. The interact and location ids ride along so a pending
   interaction can resume after the cast. */
// FUNCTION: WIZ8 0x005A0110
void BeginSpellCast(int spell_id, int location_id, W8NpcDialogueLayout interact_id)
{
    W8SpellRealm realm;

    if (!gXStatus.fNpcDialogueMode) {
        if (gXStatus.fLockInteractMode) {
            EndLockInteractMode(0);
        }
    } else {
        EndNpcDialogueSession(false);
    }
    if (!gXStatus.fSpellCastMode) {
        OpenSpellCastingView(g_status.selected_character);
    }
    gpSCSV->interact_id = interact_id;
    gpSCSV->location_id = location_id;
    if (!CanCharacterCastSpell(&g_status.buffers.Char[g_status.selected_character], spell_id)) {
        return;
    }
    realm = W8_SPELL_REALM_NONE;
    switch (g_spell_records[spell_id].realm) {
    case W8_SPELL_REALM_FIRE:
        SelectSpellCastingRealm(W8_SPELL_REALM_FIRE);
        RebuildSpellCastingList(spell_id);
        return;
    case W8_SPELL_REALM_WATER:
        SelectSpellCastingRealm(W8_SPELL_REALM_WATER);
        RebuildSpellCastingList(spell_id);
        return;
    case W8_SPELL_REALM_AIR:
        SelectSpellCastingRealm(W8_SPELL_REALM_AIR);
        RebuildSpellCastingList(spell_id);
        return;
    case W8_SPELL_REALM_EARTH:
        SelectSpellCastingRealm(W8_SPELL_REALM_EARTH);
        RebuildSpellCastingList(spell_id);
        return;
    case W8_SPELL_REALM_MENTAL:
        SelectSpellCastingRealm(W8_SPELL_REALM_MENTAL);
        RebuildSpellCastingList(spell_id);
        return;
    case W8_SPELL_REALM_DIVINE:
        realm = W8_SPELL_REALM_DIVINE;
        break;
    default:
        break;
    }
    SelectSpellCastingRealm(realm);
    RebuildSpellCastingList(spell_id);
}

/* Redraws every live spell-casting panel; the middle panel's pending text
   update is flushed through the message storage. */
// FUNCTION: WIZ8 0x005A0270
void SetSpellCastingPanelsActive(bool active)
{
    auto& view = *gpSCSV;
    auto& target = GetFrameBufferSurface();
    for (const auto& button : view.realm_buttons) view.dirty[0] |= button->m_dirty;
    for (const auto& button : view.power_controls) view.dirty[2] |= button->m_dirty;
    view.dirty[2] |= view.cancel->m_dirty;
    constexpr int origins[] = {0x17, 0xe6, 0x1a4};
    constexpr SDL_Point icons[] = {{0x55, 0xf}, {0x73, 0xf}, {0x49, 0x25},
        {0x7f, 0x25}, {0x55, 0x3b}, {0x73, 0x3b}};
    for (int panel = 0; panel < 3; ++panel) {
        if (!active && !view.dirty[panel]) continue;
        CatalogArtwork::Load(400, 0).drawAndInvalidate(target, panel, origins[panel], 0x166);
        if (panel == 0) {
            for (int realm = 0; realm < 6; ++realm) {
                auto& button = *view.realm_buttons[realm];
                button.m_dirty = true;
                button.Draw(target);
                const int offset = button.IsPressed() ? 1 : 0;
                view.realm_text[realm].RenderText(target, offset, offset, true);
                const auto& animation = g_spell_realm_animations[realm];
                const int frame = !button.IsEnabled() ? animation.frame_count :
                    view.iSpellRealm == realm ? view.realm_anim_frame : animation.initial_frame;
                CatalogArtwork::Load(0x193 + realm, 0).drawAndInvalidate(target, frame,
                    0x17 + icons[realm].x, 0x166 + icons[realm].y);
            }
        } else if (panel == 1) {
            RedrawTextBoxScrollChrome();
        } else {
            for (int pip = 0; pip < 9; ++pip) {
                const auto& f = view.power_frames[pip];
                view.power_controls[pip]->SetImage(ButtonGraphics::Load(
                    CatalogArtwork::Load(0x192, 0).spriteSheet(), f[0], f[1], f[2], f[3], f[4]));
                view.power_controls[pip]->Draw(target);
            }
            view.cancel->m_dirty = true;
            view.cancel->Draw(target);
            view.spell_name.RenderToTarget(0, true, target);
        }
        view.dirty[panel] = false;
    }
}

// FUNCTION: WIZ8 0x005A02F0
void SpellCastingNoticeClosed(W8DialogBase* dialog)
{
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x005A0300
void InvalidateSpellCastingDescription(void)
{
    gpSCSV->dirty[1] = true;
}

/* Switches the view to another realm: the old realm's icon returns to its
   first frame and the new one starts its frame timer, then the whole
   selection state resets. */
// FUNCTION: WIZ8 0x005A0380
static void SelectSpellCastingRealm(W8SpellRealm realm)
{
    W8SpellRealm previous;

    previous = gpSCSV->iSpellRealm;
    if (previous != realm) {
        if (previous != W8_SPELL_REALM_NONE) {
            gpSCSV->dirty[0] = true;
            gpSCSV->realm_buttons[gpSCSV->iSpellRealm]->SetPressed(false);
            gpSCSV->realm_buttons[gpSCSV->iSpellRealm]->m_dirty = true;
        }
        gpSCSV->iSpellRealm = realm;
        gpSCSV->realm_anim_frame = g_spell_realm_animations[gpSCSV->iSpellRealm].initial_frame;
        gpSCSV->dirty[0] = true;
        gpSCSV->realm_anim_timer = w8_get_ticks() + (0x32);
        RebuildSpellCastingList(W8_SPELL_NONE);
        ClearSpellCastingChoice();
    }
    if (realm != W8_SPELL_REALM_NONE) {
        gpSCSV->realm_buttons[realm]->SetPressed(true);
        gpSCSV->realm_buttons[realm]->m_dirty = true;
    }
}

/* The seventh power pip: while its secondary state is lit it clears the lower
   pips and prices the name label with the max cast, otherwise it empties the
   label. */
// FUNCTION: WIZ8 0x005A0570
static void SelectSpellPowerPip7(void)
{
    int pip;

    if (gpSCSV->power_controls[7]->IsPressed()) {
        for (pip = 0; pip < 7; ++pip) {
            auto& control = gpSCSV->power_controls[pip];
            if (control->IsEnabled()) {
                control->SetPressed(false);
            }
        }
        gpSCSV->spell_name.SetRenderMode(4);
        gpSCSV->spell_name.SetText(
            "?", g_wiz_text_font_secondary);
        gpSCSV->iSpellPower = 7;
        gpSCSV->dirty[2] = true;
        return;
    }
    gpSCSV->spell_name.SetRenderMode(4);
    gpSCSV->spell_name.SetText("",
                                                                        g_wiz_text_font_secondary);
    gpSCSV->iSpellPower = -1;
    gpSCSV->dirty[2] = true;
}

/* The max-power pip: while its secondary state is lit it prices the name
   label with the max cast, otherwise it relights and repaints the label. */
// FUNCTION: WIZ8 0x005A0660
static void SelectSpellPowerPip8(void)
{
    if (gpSCSV->power_controls[8]->IsPressed()) {
        gpSCSV->spell_name.SetRenderMode(4);
        gpSCSV->spell_name.SetText(
            "?", g_wiz_text_font_secondary);
        gpSCSV->iSpellPower = 7;
        gpSCSV->dirty[2] = true;
        return;
    }
    gpSCSV->power_controls[8]->SetPressed(true);
    gpSCSV->dirty[2] = true;
}

/* Records the chosen power level: pips through the level light, pips above it
   go dark, and the name label shows the cast's total cost. */
// FUNCTION: WIZ8 0x005A06F0
void SelectSpellPowerLevel(int power_level)
{
    int pip;
    std::string text;

    gpSCSV->dirty[2] = true;
    if (power_level == -1 ||
        (gpSCSV->power_controls[power_level]->IsPressed() == 0 &&
         gpSCSV->iSpellPower == power_level)) {
        for (pip = 0; pip < 7; ++pip) {
            gpSCSV->power_controls[pip]->SetPressed(false);
        }
        gpSCSV->iSpellPower = -1;
        if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_MAXIMUM) {
            gpSCSV->power_controls[8]->SetPressed(true);
            if (gpSCSV->power_controls[8]->IsPressed() == 0) {
                gpSCSV->power_controls[8]->SetPressed(true);
            } else {
                gpSCSV->spell_name.SetRenderMode(4);
                gpSCSV->spell_name.SetText(
                    "?", g_wiz_text_font_secondary);
                gpSCSV->iSpellPower = 7;
            }
            gpSCSV->dirty[2] = true;
            return;
        }
        gpSCSV->spell_name.SetRenderMode(4);
        if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_FIXED) {
            text = fmt::format("{:d}", g_spell_records[gpSCSV->uiSpellToCast].spell_point_cost);
        } else {
            text = "";
        }
    } else {
        gpSCSV->iSpellPower = power_level;
        if (power_level >= 0) {
            for (pip = 0; pip <= power_level; ++pip) {
                gpSCSV->power_controls[pip]->SetPressed(true);
            }
        }
        if (power_level + 1 < 8) {
            for (pip = power_level + 1; pip < 8; ++pip) {
                gpSCSV->power_controls[pip]->SetPressed(false);
            }
        }
        gpSCSV->spell_name.SetRenderMode(4);
        text = fmt::format("{:d}", (power_level + 1) *
                                       g_spell_records[gpSCSV->uiSpellToCast].spell_point_cost);
    }
    gpSCSV->spell_name.SetText(text,
                                                                        g_wiz_text_font_secondary);
}

/* Hover preview for a power pip: clears the spell-name plate and shows the
   cast cost for the hovered level (or restores the selected cost / empty
   label when the pointer leaves). Only power classes 0 and 1 participate. */
// FUNCTION: WIZ8 0x005A0910
static void PreviewSpellPowerPipHover(int power_level)
{
    std::string text;

    if (power_level < 7) {
        if ((gpSCSV->iSpellPowerClass == W8_SPELL_POWER_SELECTABLE ||
             gpSCSV->iSpellPowerClass == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) &&
            (power_level == -1 || (gpSCSV->power_controls[power_level]->IsVisible() &&
                                   gpSCSV->power_controls[power_level]->IsEnabled()))) {
            gpSCSV->dirty[2] = true;
            if (power_level != -1) {
                gpSCSV->spell_name.SetRenderMode(6);
                text = fmt::format("{:d}",
                                   (power_level + 1) *
                                       g_spell_records[gpSCSV->uiSpellToCast].spell_point_cost);
                gpSCSV->spell_name.SetText(text, g_wiz_text_font_secondary);
                return;
            }
            if (gpSCSV->iSpellPowerClass == W8_SPELL_POWER_SELECTABLE) {
                gpSCSV->spell_name.SetRenderMode(4);
                if (gpSCSV->iSpellPower != -1) {
                    text = fmt::format("{:d}",
                                       (gpSCSV->iSpellPower + 1) *
                                           g_spell_records[gpSCSV->uiSpellToCast].spell_point_cost);
                    gpSCSV->spell_name.SetText(text, g_wiz_text_font_secondary);
                    return;
                }
                gpSCSV->spell_name.SetText("", g_wiz_text_font_secondary);
            }
        }
    }
}

/* The confirmation dialog's destroy callback: a cancelled dialog unwinds the
   whole spell selection. */
// FUNCTION: WIZ8 0x005A0AE0
static void SpellCastingDialogResult(W8DialogBase* dialog)
{
    int index;

    index = gpSCSV->uiSpellIndex;
    if (GetDialogResult(dialog)) {
        gpSCSV->dialog_confirmed = true;
        return;
    }
    ClearSpellCastingChoice();
    SetSpellListLineColor(index, -1);
}

// FUNCTION: WIZ8 0x005A0B90
void ResetSpellCastingSelection(void)
{
    if (gXStatus.fCampMode) {
        ResetEditorStatusLine(-1);
    }
    CloseSpellCastingView();
    ClearSlotTargeting(g_status.selected_character);
}

/* Per-frame spell-casting pump: advances the selected realm's icon animation
   on its own clock, then lets a ready cast commit. */
// FUNCTION: WIZ8 0x005A0BC0
void CommitSpellCastingSelection(void)
{
    if (gpSCSV->iSpellRealm != W8_SPELL_REALM_NONE) {
        if (TimeUntilDeadline(gpSCSV->realm_anim_timer) == 0) {
            ++gpSCSV->realm_anim_frame;
            if (gpSCSV->realm_anim_frame ==
                g_spell_realm_animations[gpSCSV->iSpellRealm].frame_count) {
                gpSCSV->realm_anim_frame = 0;
            }
            gpSCSV->dirty[0] = true;
            gpSCSV->realm_anim_timer = w8_get_ticks() + (0x32);
            RequestRedraw(W8_MAIN_REDRAW_FRAME);
        }
    }
    TryCommitSpellCast();
}

UiResult HandleSpellTextBox(const UiPointerEvent& event)
{
    if (g_status.selected_character == -1 || gpSCSV->iSpellRealm == W8_SPELL_REALM_NONE)
        return UiResult::Handled;
    int line = g_level_block->text_lines[g_status.text_line_cursor] + event.local.y / 11;
    if (!event.inside || line < 0 || line >= static_cast<int>(gpSCSV->uiSpellsInList)) line = -1;
    if (line != gpSCSV->selected_spell_index) SelectSpellCastingRow(line);
    if (line != -1 && event.action == UiPointerAction::Up) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([line] { SelectSpellCastingListRow(line); });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const auto spell = gpSCSV->uiSpells[line];
            AfterUiDispatch([spell] {
                g_saved_target_cursor = gXStatus.iCurrentCursor;
                auto dialog = std::make_unique<W8SpellInfoDialog>(spell);
                dialog->SetText("");
                dialog->m_destroy_callback = RestoreTargetCursor;
                OpenModal(std::move(dialog));
            });
        }
    }
    return UiResult::Handled;
}

/* The spell-list row click: re-colors the rows, remembers the spell, then
   either prices its power levels or reports why it cannot be cast. */
// FUNCTION: WIZ8 0x005A1150
static void SelectSpellCastingListRow(int index)
{
    char color;
    int cost;
    W8SpellPowerClass power_class;
    int previous;
    int spell_id;
    W8SpellTargetType target_type;
    unsigned int levels;
    W8TargetNeed needed;

    previous = gpSCSV->uiSpellIndex;
    if (index == -1) {
        return;
    }
    gpSCSV->uiSpellIndex = index;
    if (previous != -1) {
        SetSpellListLineColor(previous, -1);
    }
    spell_id = gpSCSV->uiSpells[gpSCSV->uiSpellIndex];
    if (spell_id == W8_SPELL_NONE) {
        return;
    }
    color = gpSCSV->alt_colors[index];
    if (color == 0 || color == 4) {
        gpSCSV->uiSpellIndex = -1;
        ConfigureSpellTargetFilter(W8_TARGET_TYPE_NONE, W8_TARGET_NEED_NONE);
        ShowSpellCastingError(spell_id);
        QueueCharacterEvent(&g_status.buffers.Char[g_status.selected_character],
                            g_character_event_kind2, 0,
                            g_character_event_flags_mask | g_character_event_no_flags,
                            g_character_event_full_volume);
        return;
    }
    SetSpellListLineColor(gpSCSV->uiSpellIndex, 3);
    power_class = g_spell_records[spell_id].power_class;
    if (power_class == W8_SPELL_POWER_CLASS_NONE) {
        srAssertFail("iSpellPowerClass != BAD_INDEX", SPELLCASTING_CPP, 0x834, 0);
    }
    cost = g_spell_records[spell_id].spell_point_cost;
    levels = GetCharacterRealmSpellPoints(&g_status.buffers.Char[g_status.selected_character],
                                          g_spell_records[spell_id].realm) /
             cost;
    ClampUnsignedInteger(&levels, 0, 7);
    if (levels == 0) {
        gpSCSV->uiSpellIndex = -1;
        return;
    }
    gpSCSV->iSpellPowerClass = power_class;
    gpSCSV->uiPowerLevels = levels;
    gpSCSV->uiSpellToCast = spell_id;
    needed = GetTargetNeededForSpellFriendly(spell_id, false, W8_TARGETING_CONTEXT_CURRENT);
    target_type = GetSpellTargetType(spell_id, false);
    ConfigureSpellTargetFilter(target_type, needed);
    SelectSpellPowerLevel(-1);
    UpdateSpellPowerPips();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x005A1330
void SetSpellCastingMode(W8MainUiMode value)
{
    gpSCSV->saved_game_mode = value;
}

// FUNCTION: WIZ8 0x005A1350
int GetSpellCastingSelection(void)
{
    int result;

    result = gpSCSV->override_spell;
    if (result == 0) {
        result = gpSCSV->uiSpellToCast;
    }
    return result;
}

/* Lets a fully priced spell commit once its target is valid; spells 0x49 and
   0x4b first raise a confirmation notice unless one was already answered. */
// FUNCTION: WIZ8 0x005A1370
void TryCommitSpellCast(void)
{
    bool ready;

    ready = false;
    if (gpSCSV->uiSpellToCast != 0 &&
        (gpSCSV->iSpellPower != -1 || gpSCSV->iSpellPowerClass == W8_SPELL_POWER_FIXED)) {
        ready = IsSpellTargetOfNeededKind(g_status.selected_character, gpSCSV->uiSpellToCast);
    }
    if (gpSCSV->uiSpellToCast == 0x4b && !gpSCSV->dialog_confirmed) {
        if (!IsModalOpen() &&
            g_status.buffers.Char[g_status.selected_character].has_saved_location) {
            ShowMainGameNoticeLine(g_localized_strings[0x7a4], SpellCastingDialogResult, true, true);
        }
    } else if (gpSCSV->uiSpellToCast == 0x49 && !gpSCSV->dialog_confirmed && !IsModalOpen()) {
        ShowMainGameNoticeLine(g_localized_strings[0x7a5], SpellCastingDialogResult, true, true);
    }
    if (ready && !IsModalOpen()) {
        gpSCSV->closing = true;
        CommitSelectedSpellTarget();
        gpSCSV->closing = false;
        if (gpSCSV->uiSpellToCast != 0x17) {
            if (gXStatus.fCampMode) {
                ResetEditorStatusLine(-1);
            }
            SetCharacterSpell(gpSCSV->caster, gpSCSV->uiSpellToCast, gpSCSV->iSpellPower + 1);
            CloseSpellCastingView();
            return;
        }
        OpenCharacterScreenForPartySlot(CharacterPointerToPartySlot(gpSCSV->caster), true);
    }
}

/* Shows the notice line naming why a spell cannot be cast, then clears the
   detail spell. */
// FUNCTION: WIZ8 0x005A14D0
void ShowSpellCastingError(int spell_id)
{
    gpSCSV->override_spell = spell_id;
    if (!SpellUsableNow(spell_id, false)) {
        ShowMainGameNoticeLine(g_localized_strings[0x79e], SpellCastingNoticeClosed, true, false);
    } else if (GetCharacterRealmSpellPoints(gpSCSV->caster, gpSCSV->iSpellRealm) <
               g_spell_records[spell_id].spell_point_cost) {
        ShowMainGameNoticeLine(g_localized_strings[0x7a0], SpellCastingNoticeClosed, true, false);
    } else if (!SpellHasAnyValidTarget(CharacterPointerToPartySlot(gpSCSV->caster), spell_id,
                                       false)) {
        ShowMainGameNoticeLine(g_localized_strings[0x7a1], SpellCastingNoticeClosed, true, false);
    } else if (IsTeleportCastMissingAnchor(gpSCSV->caster, spell_id)) {
        ShowMainGameNoticeLine(g_localized_strings[0x7a2], SpellCastingNoticeClosed, true, false);
    } else if (IsSpellBlockedForCharacter(gpSCSV->caster, spell_id)) {
        ShowMainGameNoticeLine(g_localized_strings[0x79f], SpellCastingNoticeClosed, true, false);
    }
    gpSCSV->override_spell = 0;
}
