#include "wiz8/local_screens/MGSSpellCasting.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_rect.h>
#include <fmt/format.h>

#include "Video2.h"
#include "character_skills.h"
#include "compat/native.h"
#include "layouts/main_game_screen.h"
#include "layouts/npc_dialogue.h"
#include "local_code/HealthStaminaMana.h"
#include "surrender/srDebug.h"
#include "timer.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/engine_code/Cursor3d.h"
#include "wiz8/engine_code/Spells.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/layouts/targeting.h"
#include "wiz8/learned_spells.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/spell_ids.h"
#include "wiz8/ui_layer.h"
#include "wiz8/utility.h"
#include "wiz8/world_cursor.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

#define SPELLCASTING_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\MGSSpellCasting.cpp"

/* The first realm skill id; character->skill_unlocks[first + realm] counts
   the spells known in that realm. */

/* Realm-name string ids for the spell-casting realm-button tooltips. */
// GLOBAL: WIZ8 0x0064C840
static int g_spell_realm_help_string_ids[6] = {0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c};

// GLOBAL: WIZ8 0x0069BF3C
std::unique_ptr<W8SpellCastingView> gpSCSV;

W8SpellPowerClass W8SpellCastingView::PowerClass() const
{
    return draft.detail == W8_SPELL_NONE ? W8_SPELL_POWER_CLASS_NONE
                                         : g_spell_records[draft.detail].power_class;
}

unsigned int W8SpellCastingView::AffordablePowerLevels() const
{
    if (draft.detail == W8_SPELL_NONE) return 0;
    const auto& spell = g_spell_records[draft.detail];
    return std::clamp(GetCharacterRealmSpellPoints(caster, spell.realm) / spell.spell_point_cost, 0,
                      7);
}

static void CreateSpellCastingViewControls(void);
static void SelectSpellPowerPip7(void);
static void SelectSpellPowerPip8(void);
static void SpellCastingDialogResult(bool accepted);
static void PreviewSpellPowerPipHover(int power_level);
void UpdateSpellRealmPointDisplays(void);
static void UpdateSpellPowerPips(void);
static void RefreshSpellPowerPip(int pip);
static void RebuildSpellCastingList(int spell_id);
static void SetSpellListLineColor(int index, char color);
static void SelectSpellCastingRow(std::optional<int> index);
static void SelectSpellCastingRealm(W8SpellRealm realm);
static void SelectSpellCastingListRow(std::optional<int> index);
void TryCommitSpellCast(void);
void ShowSpellCastingError(int spell_id);

/* Builds the three spell-casting panels and all of their controls: the six
   realm buttons with their animated icons, the nine power-level pips, the
   spell-name label and the cancel button. */
// FUNCTION: WIZ8 0x0059E1F0
static void CreateSpellCastingViewControls(void)
{
    const auto make_button = [](int artwork, const std::array<int, 5>& frames, SDL_Rect bounds,
                                int help, std::function<void()> action,
                                std::function<void(bool)> on_hover = {}) {
        auto button = std::make_unique<W8ButtonWidget>(gpSCSV->input);

        button->Configure(ButtonGraphics::Load(CatalogArtwork::Load(artwork, 0).spriteSheet(),
                                               frames[0], frames[1], frames[2], frames[3],
                                               frames[4]),
                          {.on_click = [action](auto*) { AfterUiDispatch(action); },
                           .on_hover = std::move(on_hover),
                           .policy = {.primary_toggle = ButtonToggle::Release,
                                      .input_rule = ButtonInputRule::PrimaryPressedState},
                           .tooltip = help,
                           .priority = 0,
                           .menu_artwork = true});
        button->SetPosition(bounds.x, bounds.y);
        button->Resize(bounds.w, bounds.h);
        return button;
    };
    constexpr SDL_Point realm_positions[] = {{0x1e, 0xd},  {0x71, 0xd},  {0x12, 0x23},
                                             {0x7d, 0x23}, {0x1e, 0x39}, {0x71, 0x39}};
    constexpr int text_left[] = {0x1e, 0x85, 0x12, 0x91, 0x1e, 0x85};
    for (int realm = 0; realm < 6; ++realm) {
        const int frame = (realm % 2) * 4;
        const auto pos = realm_positions[realm];
        auto& button = gpSCSV->realm_buttons[realm];
        button = make_button(
            0x191, {frame + 2, frame, frame + 1, frame + 3, frame + 3},
            {0x17 + pos.x, 0x166 + pos.y, 75, 22}, -1,
            [realm] { SelectSpellCastingRealm(static_cast<W8SpellRealm>(realm)); },
            [](bool) { gpSCSV->dirty[0] = true; });
        const auto tooltip = [realm] {
            const auto& name = g_localized_strings[g_spell_realm_help_string_ids[realm]];
            if (!gpSCSV->realm_buttons[realm]->IsEnabled()) return std::string(name);
            return fmt::format("{} ({}: {:d})", name, g_localized_strings[0x2d],
                               g_status.buffers.Char[g_status.selected_character]
                                   .skills[W8_SKILL_FIRE_MAGIC + realm]
                                   .level);
        };
        button->SetTooltip(tooltip);
        auto& text = gpSCSV->realm_text[realm];
        text.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
        text.SetLayoutBounds(0x17 + text_left[realm], 0x166 + pos.y, 0x17 + text_left[realm] + 55,
                             0x166 + pos.y + 22);
    }
    constexpr SDL_Point pip_positions[] = {{0x38, 2},    {0x48, 4},    {0x56, 0x10},
                                           {0x5b, 0x23}, {0x52, 0x38}, {0x3a, 0x41},
                                           {0x1f, 0x3a}, {0x10, 0x1f}, {0x10, 0x1f}};
    constexpr SDL_Point pip_centers[] = {{0x3f, 9},    {0x50, 0xc},  {0x5f, 0x19},
                                         {0x65, 0x2d}, {0x5d, 0x43}, {0x46, 0x4d},
                                         {0x2c, 0x47}, {0x1e, 0x2d}, {0x1e, 0x2d}};
    for (int pip = 0; pip < 9; ++pip) {
        const int base = pip == 7 ? 0x30 : pip == 8 ? 0x2a : pip * 6;
        auto& frames = gpSCSV->power_frames[pip];
        frames = {base + 3, base, base + 1, base + 2, base + 4};
        if (pip == 8) {
            frames[3] = base;
            frames[4] = base + 1;
        }
        const int radius = 7 + std::min(pip, 7);
        auto& button = gpSCSV->power_controls[pip];
        button = make_button(
            0x192, frames,
            {0x1a4 + pip_positions[pip].x, 0x166 + pip_positions[pip].y, radius * 2 + 1,
             radius * 2 + 1},
            46 + pip,
            [pip] {
                if (pip == 7) {
                    SelectSpellPowerPip7();
                } else if (pip == 8) {
                    SelectSpellPowerPip8();
                } else {
                    SelectSpellPowerLevel(pip);
                    if (!gpSCSV->power_controls[pip]->IsPressed()) {
                        PreviewSpellPowerPipHover(pip);
                    }
                }
            },
            [pip](bool entered) { PreviewSpellPowerPipHover(entered ? pip : -1); });
        button->SetCircularHitArea({0x1a4 + pip_centers[pip].x, 0x166 + pip_centers[pip].y},
                                   radius);
    }
    gpSCSV->spell_name.SetLayoutBounds(0x1b2, 0x170, 0x1d4, 0x17e);
    gpSCSV->spell_name.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (!gpSCSV->name_target) {
        gpSCSV->name_target = &gpSCSV->input.add(
            SDL_Rect{0x1b2, 0x170, 35, 15}, [](const UiPointerEvent&) { return UiResult::Pass; });
        gpSCSV->name_target->setPassive();
        gpSCSV->name_target->setTooltip([] { return std::string(g_localized_strings[55]); });
    }
    gpSCSV->cancel =
        make_button(0x8e, {7, 4, 5, -1, 6}, {0x23a, 0x16c, 27, 27}, 17, ResetSpellCastingSelection);
    gpSCSV->cancel->Policy().primary_toggle = ButtonToggle::None;
    gpSCSV->dirty.fill(true);

    if (gXStatus.fCampMode) {
        for (int index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (g_main_game_state) {
                DisableMainGamePortraitSlot(index);
                g_main_game_state->edge_targets[index]->setEnabled(false);
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

    if (!IsPartySlotEligible(party_slot)) {
        return 0;
    }
    UpdateScreenOverlays(0);
    gpSCSV = std::make_unique<W8SpellCastingView>();
    if (g_level_block->combat_end_notification) {
        DestroySubMenuControls();
    }
    gpSCSV->location_id = -1;
    gpSCSV->interact_id.reset();
    CloseMainGameOverlays();
    RefreshMainGameLayout();
    CreateSpellCastingViewControls();
    RestoreSpellCastingRegions();
    gpSCSV->input.setEnabled(true);
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    g_level_block->text_box_visible = false;
    SetTextBoxRegionBounds(0xea, 0x16e, 0x18c, 0x1ba);
    gpSCSV->iSpellRealm = W8_SPELL_REALM_NONE;
    SelectSpellCastingPartySlot(party_slot);
    gpSCSV->selected_spell_index.reset();
    SelectSpellCastingCharacter(party_slot);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    SyncWorldAdvancement();
    return 1;
}

/* Discard the picker, recompute layout and advancement policy, and resume
   its suspended NPC interaction when one exists. */
// FUNCTION: WIZ8 0x0059F2B0
void CloseSpellCastingView(void)
{
    int location_id;
    std::optional<W8NpcDialogueLayout> interact_id;
    W8NpcState* npc;

    if (!gpSCSV) return;
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
    location_id = gpSCSV->location_id;
    interact_id = gpSCSV->interact_id;
    gpSCSV.reset();
    RefreshMainGameLayout();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    RequestRedrawCombatBar();
    RequestRedraw(W8_MAIN_REDRAW_SUBMENU_BUTTONS);
    SyncWorldAdvancement();
    if (IsWorldCursorVisible()) {
        ToggleWorldCursor();
    }
    if (LockInteractionSuspended() && !IsScreenTransitionPending()) {
        OpenLockInteraction(0);
    }
    if (TrapInteractionSuspended() && !IsScreenTransitionPending()) {
        OpenTrapInteraction(0);
    }
    if (interact_id && !IsScreenTransitionPending()) {
        npc = FindNpcBindingForMonster(FindMonster(location_id));
        BeginNpcDialogue(npc, 0, std::nullopt, 0, 1);
        SwitchNpcDialogueLayout(*interact_id);
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
    gpSCSV->draft.detail = 0;
    gpSCSV->uiSpellIndex.reset();
    SelectSpellCastingRow(std::nullopt);
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
        int spell_id = g_status.buffers.XChar[party_slot].spell.detail;
        SelectSpellCastingRealm(g_spell_records[spell_id].realm);
        if (spell_id == W8_SPELL_IDENTIFY_ITEM) {
            gpSCSV->uiSpellIndex.reset();
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
    const auto levels = gpSCSV->AffordablePowerLevels();
    unsigned int pip;
    for (pip = 0; pip < levels; ++pip) {
        RefreshSpellPowerPip(pip);
    }
    if (levels < 7) {
        for (pip = levels; pip < 7; ++pip) {
            gpSCSV->power_controls[pip]->SetEnabled(false);
        }
    }
}

// FUNCTION: WIZ8 0x0059F710
static void UpdateSpellPowerPips(void)
{
    const auto levels = gpSCSV->AffordablePowerLevels();
    unsigned int pip;
    W8SpellCastRating rating;
    gpSCSV->dirty[2] = true;

    if (levels == 0 || gpSCSV->PowerClass() == W8_SPELL_POWER_FIXED) {
        for (pip = 0; pip < W8_SPELL_POWER_PIP_COUNT; ++pip) {
            gpSCSV->power_controls[pip]->SetEnabled(false);
        }
    } else {
        RefreshSpellPowerPip(7);
        RefreshSpellPowerPip(8);
        if (gpSCSV->PowerClass() == W8_SPELL_POWER_SELECTABLE) {
            RefreshAvailableSpellPowerPips();
            gpSCSV->power_controls[7]->SetVisible(false);
            gpSCSV->power_controls[8]->SetVisible(false);
        } else if (gpSCSV->PowerClass() == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) {
            gpSCSV->power_controls[7]->SetVisible(!gXStatus.fCombatMode);
            RefreshAvailableSpellPowerPips();
            gpSCSV->power_controls[8]->SetVisible(false);
        } else if (gpSCSV->PowerClass() == W8_SPELL_POWER_MAXIMUM) {
            for (pip = 0; pip < 7; ++pip) {
                gpSCSV->power_controls[pip]->SetEnabled(false);
            }
            gpSCSV->power_controls[8]->SetVisible(true);
            gpSCSV->power_controls[7]->SetVisible(false);
        }
    }
    if (gpSCSV->PowerClass() == W8_SPELL_POWER_MAXIMUM) {
        if (levels != 0) {
            for (pip = 0; pip < levels; ++pip) {
                gpSCSV->power_controls[pip]->SetEnabled(false);
                rating = GetSpellCastRating(gpSCSV->caster, gpSCSV->draft.detail, pip + 1);
                gpSCSV->power_frames[pip][0] = (pip + rating * 9) * 6;
            }
        }
        if (levels < 7) {
            for (pip = levels; pip < 7; ++pip) {
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

    if (gpSCSV->draft.detail == 0) {
        srAssertFail("gpSCSV->draft.detail != SPELL_NONE", SPELLCASTING_CPP, 0x303, 0);
    }
    if (pip == 7 || pip == 8) {
        level = 8;
    } else {
        level = pip + 1;
    }
    gpSCSV->power_controls[pip]->SetEnabled(true);
    rating = GetSpellCastRating(gpSCSV->caster, gpSCSV->draft.detail, level);
    frame = (pip + rating * 9) * 6;
    auto& frames = gpSCSV->power_frames[pip];
    frames[1] = frame;
    frames[2] = pip == 8 ? frame : frame + 1;
    frames[3] = frame + 2;
    frames[4] = pip == 8 ? frame + 2 : frame + 4;
    gpSCSV->dirty[2] = true;
}

/* Evaluate each learned spell once, then display castable spells first while
   retaining learned order within each category. The rows own their metadata. */
static void RebuildSpellCastingList(int spell_id)
{
    ResetEditorStatusLine(-1);
    gpSCSV->uiSpellIndex.reset();
    gpSCSV->selected_spell_index.reset();
    auto& rows = gpSCSV->spells;
    rows.clear();
    const auto realm = gpSCSV->iSpellRealm;
    const auto count = gpSCSV->caster->skill_unlocks[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm];
    for (unsigned int index = 0; index < count; ++index) {
        const int id = gpSCSV->learned.spell_ids_by_realm[realm][index];
        const bool usable = SpellUsableNow(id, false);
        const bool castable =
            g_spell_records[id].spell_point_cost <= gpSCSV->caster->iSPLeft[realm] && usable &&
            SpellHasAnyValidTarget(CharacterPointerToPartySlot(gpSCSV->caster), id, false) &&
            !IsTeleportCastMissingAnchor(gpSCSV->caster, id);
        const signed char color =
            castable ? (IsSpellBlockedForCharacter(gpSCSV->caster, id) ? 4 : 15) : 0;
        if (color != 15 &&
            (gXStatus.fCampMode || LockInteractionSuspended() || TrapInteractionSuspended()) &&
            !usable)
            continue;
        rows.push_back({id, color});
    }
    std::ranges::stable_partition(rows, [](const auto& row) { return row.color == 15; });
    std::optional<int> selected;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const auto& spell = g_spell_records[row.spell];
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("{} {}",
                               g_spell_target_parentheticals[GetSpellTargetType(row.spell, false)],
                               spell.display_name),
                   2);
        AppendTextBoxLine(fmt::format("{:d}", spell.spell_point_cost), 2);
        if (row.color != 15) SetSpellListLineColor(index, row.color);
        if (row.spell == spell_id && row.color == 15) selected = index;
    }
    if (!selected) {
        ScrollTextBoxTo(0);
    } else {
        SelectSpellCastingListRow(selected);
        ScrollTextBoxTo(*selected);
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

    if (color == -1) {
        if (gpSCSV->uiSpellIndex == index) {
            color = 3;
        } else {
            color = gpSCSV->spells[index].color;
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
static void SelectSpellCastingRow(std::optional<int> index)
{
    if (!index) {
        if (gpSCSV->selected_spell_index) {
            SetSpellListLineColor(*gpSCSV->selected_spell_index, -1);
            gpSCSV->selected_spell_index.reset();
            RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
            return;
        }
    } else {
        const auto previous = gpSCSV->selected_spell_index;
        if (previous && index != previous) {
            SetSpellListLineColor(*previous, -1);
        }
        SetSpellListLineColor(*index, 5);
        gpSCSV->selected_spell_index = index;
    }
    RequestRedraw(W8_MAIN_REDRAW_TEXT_BOX);
}

/* Opens the spell-casting view when needed and selects the given spell's
   realm and list row. The interact and location ids ride along so a pending
   interaction can resume after the cast. */
// FUNCTION: WIZ8 0x005A0110
void BeginSpellCast(int spell_id, int location_id, std::optional<W8NpcDialogueLayout> interact_id)
{
    W8SpellRealm realm;

    if (!gXStatus.fNpcDialogueMode) {
        if (gXStatus.fLockInteractMode) {
            EndLockInteractMode(0);
        }
    } else {
        EndNpcDialogueSession(false);
    }
    if (!gpSCSV) {
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
    constexpr SDL_Point icons[] = {{0x55, 0xf},  {0x73, 0xf},  {0x49, 0x25},
                                   {0x7f, 0x25}, {0x55, 0x3b}, {0x73, 0x3b}};
    for (int panel = 0; panel < 3; ++panel) {
        if (!active && !view.dirty[panel]) continue;
        CatalogArtwork::Load(400, 0).drawAndInvalidate(target, panel, origins[panel], 0x166);
        if (panel == 0) {
            for (int realm = 0; realm < 6; ++realm) {
                auto& button = *view.realm_buttons[realm];
                button.Refresh(true);
                const int offset = button.IsPressed() ? 1 : 0;
                view.realm_text[realm].RenderText(target, offset, offset, true);
                const auto& animation = g_spell_realm_animations[realm];
                const int frame = !button.IsEnabled()         ? animation.frame_count
                                  : view.iSpellRealm == realm ? view.realm_anim_frame
                                                              : animation.initial_frame;
                CatalogArtwork::Load(0x193 + realm, 0)
                    .drawAndInvalidate(target, frame, 0x17 + icons[realm].x,
                                       0x166 + icons[realm].y);
            }
        } else if (panel == 1) {
            RedrawTextBoxScrollChrome();
        } else {
            for (int pip = 0; pip < 9; ++pip) {
                const auto& f = view.power_frames[pip];
                view.power_controls[pip]->SetImage(ButtonGraphics::Load(
                    CatalogArtwork::Load(0x192, 0).spriteSheet(), f[0], f[1], f[2], f[3], f[4]));
                view.power_controls[pip]->Refresh();
            }
            view.cancel->Refresh(true);
            view.spell_name.RenderToTarget(0, true, target);
        }
        view.dirty[panel] = false;
    }
}

// FUNCTION: WIZ8 0x005A02F0
void SpellCastingNoticeClosed(bool)
{
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
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
        gpSCSV->spell_name.SetText("?", g_wiz_text_font_secondary);
        gpSCSV->draft.detail_block.spell.power_level = 8;
        gpSCSV->dirty[2] = true;
        return;
    }
    gpSCSV->spell_name.SetRenderMode(4);
    gpSCSV->spell_name.SetText("", g_wiz_text_font_secondary);
    gpSCSV->draft.detail_block.spell.power_level = 0;
    gpSCSV->dirty[2] = true;
}

/* The max-power pip: while its secondary state is lit it prices the name
   label with the max cast, otherwise it relights and repaints the label. */
// FUNCTION: WIZ8 0x005A0660
static void SelectSpellPowerPip8(void)
{
    if (gpSCSV->power_controls[8]->IsPressed()) {
        gpSCSV->spell_name.SetRenderMode(4);
        gpSCSV->spell_name.SetText("?", g_wiz_text_font_secondary);
        gpSCSV->draft.detail_block.spell.power_level = 8;
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
    if (power_level == -1 || (gpSCSV->power_controls[power_level]->IsPressed() == 0 &&
                              gpSCSV->draft.detail_block.spell.power_level == power_level + 1)) {
        for (pip = 0; pip < 7; ++pip) {
            gpSCSV->power_controls[pip]->SetPressed(false);
        }
        gpSCSV->draft.detail_block.spell.power_level = 0;
        if (gpSCSV->PowerClass() == W8_SPELL_POWER_MAXIMUM) {
            gpSCSV->power_controls[8]->SetPressed(true);
            if (gpSCSV->power_controls[8]->IsPressed() == 0) {
                gpSCSV->power_controls[8]->SetPressed(true);
            } else {
                gpSCSV->spell_name.SetRenderMode(4);
                gpSCSV->spell_name.SetText("?", g_wiz_text_font_secondary);
                gpSCSV->draft.detail_block.spell.power_level = 8;
            }
            gpSCSV->dirty[2] = true;
            return;
        }
        gpSCSV->spell_name.SetRenderMode(4);
        if (gpSCSV->PowerClass() == W8_SPELL_POWER_FIXED) {
            gpSCSV->draft.detail_block.spell.power_level = 1;
            text = fmt::format("{:d}", g_spell_records[gpSCSV->draft.detail].spell_point_cost);
        } else {
            text = "";
        }
    } else {
        gpSCSV->draft.detail_block.spell.power_level = power_level + 1;
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
                                       g_spell_records[gpSCSV->draft.detail].spell_point_cost);
    }
    gpSCSV->spell_name.SetText(text, g_wiz_text_font_secondary);
}

/* Hover preview for a power pip: clears the spell-name plate and shows the
   cast cost for the hovered level (or restores the selected cost / empty
   label when the pointer leaves). Only power classes 0 and 1 participate. */
// FUNCTION: WIZ8 0x005A0910
static void PreviewSpellPowerPipHover(int power_level)
{
    std::string text;

    if (power_level < 7) {
        if ((gpSCSV->PowerClass() == W8_SPELL_POWER_SELECTABLE ||
             gpSCSV->PowerClass() == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) &&
            (power_level == -1 || (gpSCSV->power_controls[power_level]->IsVisible() &&
                                   gpSCSV->power_controls[power_level]->IsEnabled()))) {
            gpSCSV->dirty[2] = true;
            if (power_level != -1) {
                gpSCSV->spell_name.SetRenderMode(6);
                text =
                    fmt::format("{:d}", (power_level + 1) *
                                            g_spell_records[gpSCSV->draft.detail].spell_point_cost);
                gpSCSV->spell_name.SetText(text, g_wiz_text_font_secondary);
                return;
            }
            if (gpSCSV->PowerClass() == W8_SPELL_POWER_SELECTABLE) {
                gpSCSV->spell_name.SetRenderMode(4);
                if (gpSCSV->draft.detail_block.spell.power_level != 0) {
                    text = fmt::format("{:d}",
                                       gpSCSV->draft.detail_block.spell.power_level *
                                           g_spell_records[gpSCSV->draft.detail].spell_point_cost);
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
static void SpellCastingDialogResult(bool accepted)
{
    const auto selected_index = gpSCSV->uiSpellIndex;
    if (accepted) {
        gpSCSV->dialog_confirmed = true;
        return;
    }
    ClearSpellCastingChoice();
    if (selected_index) SetSpellListLineColor(*selected_index, -1);
}

// FUNCTION: WIZ8 0x005A0B90
void ResetSpellCastingSelection(void)
{
    if (gXStatus.fCampMode) {
        ResetEditorStatusLine(-1);
    }
    CloseSpellCastingView();
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
    std::optional<int> line =
        g_level_block->text_lines[g_status.text_line_cursor] + event.local.y / 11;
    if (!event.inside || *line < 0 || *line >= static_cast<int>(gpSCSV->spells.size()))
        line.reset();
    if (line != gpSCSV->selected_spell_index) SelectSpellCastingRow(line);
    if (line && event.action == UiPointerAction::Up) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([line = *line] { SelectSpellCastingListRow(line); });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const auto spell = gpSCSV->spells[*line].spell;
            AfterUiDispatch([spell] {
                auto dialog = std::make_unique<W8SpellInfoDialog>(spell);
                dialog->SetText("");
                dialog->cleanup = [cursor = gXStatus.iCurrentCursor] {
                    SetTargetCursor(cursor);
                    RequestRedraw(W8_MAIN_REDRAW_ALL);
                };
                OpenModal(std::move(dialog));
            });
        }
    }
    return UiResult::Handled;
}

/* The spell-list row click: re-colors the rows, remembers the spell, then
   either prices its power levels or reports why it cannot be cast. */
// FUNCTION: WIZ8 0x005A1150
static void SelectSpellCastingListRow(std::optional<int> index)
{
    char color;
    int cost;
    W8SpellPowerClass power_class;
    int spell_id;
    W8SpellTargetType target_type;
    unsigned int levels;
    W8TargetNeed needed;

    if (!index) return;
    const auto previous = gpSCSV->uiSpellIndex;
    gpSCSV->uiSpellIndex = index;
    if (previous) {
        SetSpellListLineColor(*previous, -1);
    }
    spell_id = gpSCSV->spells[*gpSCSV->uiSpellIndex].spell;
    if (spell_id == W8_SPELL_NONE) {
        return;
    }
    color = gpSCSV->spells[*index].color;
    if (color == 0 || color == 4) {
        gpSCSV->uiSpellIndex.reset();
        ConfigureSpellTargetFilter(W8_TARGET_TYPE_NONE, W8_TARGET_NEED_NONE);
        ShowSpellCastingError(spell_id);
        QueueCharacterEvent(&g_status.buffers.Char[g_status.selected_character], 0x35, 0,
                            kCharacterEventFullVolume);
        return;
    }
    SetSpellListLineColor(*gpSCSV->uiSpellIndex, 3);
    power_class = g_spell_records[spell_id].power_class;
    if (power_class == W8_SPELL_POWER_CLASS_NONE) {
        srAssertFail("iSpellPowerClass != BAD_INDEX", SPELLCASTING_CPP, 0x834, 0);
    }
    cost = g_spell_records[spell_id].spell_point_cost;
    levels = GetCharacterRealmSpellPoints(&g_status.buffers.Char[g_status.selected_character],
                                          g_spell_records[spell_id].realm) /
             cost;
    levels = std::clamp(levels, 0u, 7u);
    if (levels == 0) {
        gpSCSV->uiSpellIndex.reset();
        return;
    }
    gpSCSV->draft.detail = spell_id;
    needed = GetTargetNeededForSpellFriendly(spell_id, false, g_level_block == nullptr);
    target_type = GetSpellTargetType(spell_id, false);
    ConfigureSpellTargetFilter(target_type, needed);
    SelectSpellPowerLevel(-1);
    UpdateSpellPowerPips();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

/* Lets a fully priced spell commit once its target is valid; spells 0x49 and
   0x4b first raise a confirmation notice unless one was already answered. */
// FUNCTION: WIZ8 0x005A1370
void TryCommitSpellCast(void)
{
    bool ready;

    ready = false;
    if (gpSCSV->draft.detail != 0 && (gpSCSV->draft.detail_block.spell.power_level != 0 ||
                                      gpSCSV->PowerClass() == W8_SPELL_POWER_FIXED)) {
        ready = IsSpellTargetOfNeededKind(&gpSCSV->draft.target, gpSCSV->draft.detail,
                                          g_level_block == nullptr);
    }
    if (gpSCSV->draft.detail == 0x4b && !gpSCSV->dialog_confirmed) {
        if (!IsModalOpen() &&
            g_status.buffers.Char[g_status.selected_character].has_saved_location) {
            ShowMainGameNoticeLine(g_localized_strings[0x7a4], SpellCastingDialogResult, true,
                                   true);
        }
    } else if (gpSCSV->draft.detail == 0x49 && !gpSCSV->dialog_confirmed && !IsModalOpen()) {
        ShowMainGameNoticeLine(g_localized_strings[0x7a5], SpellCastingDialogResult, true, true);
    }
    if (ready && !IsModalOpen()) {
        if (gpSCSV->draft.detail != 0x17) {
            if (gXStatus.fCampMode) {
                ResetEditorStatusLine(-1);
            }
            SetCharacterSpell(gpSCSV->caster, gpSCSV->draft);
            CloseSpellCastingView();
            return;
        }
        OpenCharacterScreenForPartySlot(CharacterPointerToPartySlot(gpSCSV->caster), true);
    }
}

/* Shows why the supplied spell cannot be cast without editing the draft. */
// FUNCTION: WIZ8 0x005A14D0
void ShowSpellCastingError(int spell_id)
{
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
}
