#include "wiz8/local_screens/MGSSpellCasting.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
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

#include "character_skills.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/checked_integer.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/dialog_code/DialogInterface.h"
#include "wiz8/dialog_code/SpellInfoDialog.h"
#include "wiz8/entities/cursor.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/learned_spells.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/learned_spells.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/TextBuffer.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/message_log.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/timer.h"
#include "wiz8/pointer.h"
#include "wiz8/render/frame.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/ui_layer.h"
#include "wiz8/world_cursor.h"

struct W8NpcState;

/* The first realm skill id; character->skill_unlocks[first + realm] counts
   the spells known in that realm. */

/* Realm-name string ids for the spell-casting realm-button tooltips. */
static int g_spell_realm_help_string_ids[6] = {0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c};

W8SpellCastingView* SpellPicker()
{
    auto* main_game = MainGame();
    return main_game ? main_game->spell_picker.get() : nullptr;
}

W8SpellPowerClass W8SpellCastingView::PowerClass() const
{
    return draft.detail == W8_SPELL_NONE ? W8_SPELL_POWER_CLASS_NONE
                                         : Data().spells[draft.detail].power_class;
}

unsigned int W8SpellCastingView::AffordablePowerLevels() const
{
    if (draft.detail == W8_SPELL_NONE) return 0;
    const auto& spell = Data().spells[draft.detail];
    return std::clamp(GetCharacterRealmSpellPoints(caster, spell.realm) / spell.spell_point_cost, 0,
                      7);
}

/* Builds the three spell-casting panels and all of their controls: the six
   realm buttons with their animated icons, the nine power-level pips, the
   spell-name label and the cancel button. */
void W8SpellCastingView::CreateControls()
{
    const auto make_button = [this](int artwork, const std::array<int, 5>& frames, SDL_Rect bounds,
                                    int help, std::function<void()> action,
                                    std::function<void(bool)> on_hover = {}) {
        auto button = std::make_unique<W8ButtonWidget>(input);

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
        auto& button = realm_buttons[realm];
        button = make_button(
            0x191, {frame + 2, frame, frame + 1, frame + 3, frame + 3},
            {0x17 + pos.x, 0x166 + pos.y, 75, 22}, -1,
            [this, realm] { SelectRealm(static_cast<W8SpellRealm>(realm)); },
            [this](bool) { dirty[0] = true; });
        const auto tooltip = [this, realm] {
            const auto& name = Data().strings[g_spell_realm_help_string_ids[realm]];
            if (!realm_buttons[realm]->IsEnabled()) return std::string(name);
            return fmt::format("{} ({}: {:d})", name, Data().strings[0x2d],
                               CurrentGame()
                                   .party.characters[CurrentGame().status.selected_character]
                                   .skills[W8_SKILL_FIRE_MAGIC + realm]
                                   .level);
        };
        button->SetTooltip(tooltip);
        auto& text = realm_text[realm];
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
        auto& frames = power_frames[pip];
        frames = {base + 3, base, base + 1, base + 2, base + 4};
        if (pip == 8) {
            frames[3] = base;
            frames[4] = base + 1;
        }
        const int radius = 7 + std::min(pip, 7);
        auto& button = power_controls[pip];
        button = make_button(
            0x192, frames,
            {0x1a4 + pip_positions[pip].x, 0x166 + pip_positions[pip].y, radius * 2 + 1,
             radius * 2 + 1},
            46 + pip,
            [this, pip] {
                if (pip == 7) {
                    SelectPip7();
                } else if (pip == 8) {
                    SelectPip8();
                } else {
                    SelectPowerLevel(pip);
                    if (!power_controls[pip]->IsPressed()) {
                        PreviewPowerPipHover(pip);
                    }
                }
            },
            [this, pip](bool entered) { PreviewPowerPipHover(entered ? pip : -1); });
        button->SetCircularHitArea({0x1a4 + pip_centers[pip].x, 0x166 + pip_centers[pip].y},
                                   radius);
    }
    spell_name.SetLayoutBounds(0x1b2, 0x170, 0x1d4, 0x17e);
    spell_name.SetLayoutMode(g_W8TextBufferAlignMiddle | g_W8TextBufferAlignCenter);
    if (!name_target) {
        name_target = &input.add(SDL_Rect{0x1b2, 0x170, 35, 15},
                                 [](const UiPointerEvent&) { return UiResult::Pass; });
        name_target->setPassive();
        name_target->setTooltip([] { return std::string(Data().strings[55]); });
    }
    cancel = make_button(0x8e, {7, 4, 5, -1, 6}, {0x23a, 0x16c, 27, 27}, 17, [this] { Cancel(); });
    cancel->Policy().primary_toggle = ButtonToggle::None;
}

/* Opens the spell-casting view for one party slot: the main game takes the
   picker, swaps its layout and redirects the region sets the view takes over. */
bool OpenSpellCastingView(int party_slot, std::move_only_function<void()> on_return)
{
    if (!IsPartySlotEligible(party_slot)) {
        return false;
    }
    UpdateScreenOverlays(0);
    auto& view =
        *(MainGame()->spell_picker = std::make_unique<W8SpellCastingView>(std::move(on_return)));
    view.CreateControls();
    if (MainGameHud()->open_submenu) {
        DestroySubMenuControls();
    }
    CloseMainGameOverlays();
    RefreshMainGameLayout();
    if (CurrentGame().activity.conversation_aside) {
        for (int index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            DisableMainGamePortraitSlot(index);
            MainGame()->edge_targets[index]->setEnabled(false);
        }
    }
    RestoreSpellCastingRegions();
    view.input.setEnabled(true);
    SelectTextBox(2);
    ResetEditorStatusLine(-1);
    MainGameHud()->text_box_visible = false;
    SetTextBoxRegionBounds(0xea, 0x16e, 0x18c, 0x1ba);
    SelectSpellCastingPartySlot(party_slot, view.draft);
    view.SelectCharacter(party_slot);
    if (auto* hud = MainGameHud()) {
        hud->repaint.layout = true;
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
    return true;
}

/* Discard the picker and restore the HUD it took over. */
void DismissSpellCastingView(void)
{
    auto* main_game = MainGame();
    if (!main_game || !main_game->spell_picker) return;
    main_game->spell_picker->input.setEnabled(false);
    SetTextBoxScrollInput(false);
    MainGameHud()->action_panel_visible = 0;
    SetTargetingMode(W8_TARGET_NEED_NONE);
    if (!CurrentGame().activity.conversation_aside) {
        ResetEditorStatusLine(-1);
    }
    MainGameHud()->text_box_visible = true;
    SelectTextBox(CurrentGame().combat.has_value());
    SetTextBoxRegionBounds(0xa8, 0x16e, 0x1c4, 0x1ba);
    main_game->spell_picker.reset();
    RefreshMainGameLayout();
    if (auto* hud = MainGameHud()) {
        hud->repaint.layout = true;
        hud->repaint.roof_and_spell_icons = true;
        hud->repaint.submenu_buttons = true;
    }
    if (IsWorldCursorVisible()) {
        ToggleWorldCursor();
    }
}

/* Discard the picker, then let whoever opened it take over again. */
void CloseSpellCastingView(void)
{
    auto* view = SpellPicker();
    if (!view) return;
    auto on_return = std::move(view->on_return);
    DismissSpellCastingView();
    if (on_return) on_return();
}

/* Re-enables the region set and inputs the spell-casting view owns. */
void RestoreSpellCastingRegions()
{
    SetTextBoxEnabled(true);
    SetTextBoxScrollInput(true);
    MainGameHud()->action_panel_visible = 1;
    SetTextBoxMuteInput(false);
    SetTextBoxChannelTabInput(false);
}

/* Shared choice reset for caster/realm changes and a cancelled dialog.
   This descriptive helper keeps the view open and retains callback order. */
void W8SpellCastingView::ClearChoice()
{
    draft.detail = 0;
    uiSpellIndex = -1;
    SelectRow(-1);
    SelectPowerLevel(-1);
    dialog_confirmed = false;
    UpdatePowerPips();
    SelectSpellCastingPartySlot(CurrentGame().status.selected_character, draft);
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
}

/* Points the view at a new caster: rebuilds the learned-spell buckets and the
   realm/power displays, then either restores the pending cast's realm and
   list or clears a realm the new caster has no spell points in. */
void W8SpellCastingView::SelectCharacter(int party_slot)
{
    W8SpellRealm realm;

    if (!CharacterHasCastableSpell(&CurrentGame().party.characters[party_slot]) ||
        !IsPartySlotEligible(party_slot)) {
        ResetEditorStatusLine(-1);
        CloseSpellCastingView();
        return;
    }
    caster = &CurrentGame().party.characters[party_slot];
    BuildLearnedSpellState(&learned, caster);
    UpdateRealmPointDisplays();
    ClearChoice();
    if (GetAffordableSpellPowerLevel(party_slot) != 0) {
        int spell_id = CurrentGame().party.rows[party_slot].remembered.spell.detail;
        SelectRealm(Data().spells[spell_id].realm);
        if (spell_id == W8_SPELL_IDENTIFY_ITEM) {
            uiSpellIndex = -1;
            return;
        }
        RebuildList(spell_id);
        return;
    }
    realm = iSpellRealm;
    if (realm == W8_SPELL_REALM_NONE) {
        return;
    }
    if (caster->sp_max[realm] == 0) {
        dirty[0] = true;
        iSpellRealm = W8_SPELL_REALM_NONE;
        ResetEditorStatusLine(-1);
        return;
    }
    RebuildList(W8_SPELL_NONE);
}

/* Writes each realm's "current/max" spell-point caption and enables the realm
   button and icon for the pools the caster actually has. */
void W8SpellCastingView::UpdateRealmPointDisplays()
{
    int realm;

    for (realm = 0; realm < W8_SPELL_REALM_COUNT; ++realm) {
        bool has_realm = caster->sp_max[realm] != 0;
        realm_buttons[realm]->SetEnabled(has_realm);
        realm_text[realm].SetFontStateIndex(has_realm ? -1 : 0);
        dirty[0] = true;
        realm_text[realm].SetText(
            fmt::format("{:d}/{:d}",
                        GetCharacterRealmSpellPoints(caster, static_cast<W8SpellRealm>(realm)),
                        caster->sp_max[realm]),
            g_wiz_text_font_secondary);
    }
}

/* Enables and frames the power-level pips for the chosen spell's power class:
   class zero shows one pip per affordable level, class one keeps the
   seventh pip live out of combat and class two shows the max pip alone. */
void W8SpellCastingView::RefreshAvailablePowerPips()
{
    const auto levels = AffordablePowerLevels();
    unsigned int pip;
    for (pip = 0; pip < levels; ++pip) {
        RefreshPowerPip(pip);
    }
    if (levels < 7) {
        for (pip = levels; pip < 7; ++pip) {
            power_controls[pip]->SetEnabled(false);
        }
    }
}

void W8SpellCastingView::UpdatePowerPips()
{
    const auto levels = AffordablePowerLevels();
    unsigned int pip;
    W8SpellCastRating rating;
    dirty[2] = true;

    if (levels == 0 || PowerClass() == W8_SPELL_POWER_FIXED) {
        for (pip = 0; pip < W8_SPELL_POWER_PIP_COUNT; ++pip) {
            power_controls[pip]->SetEnabled(false);
        }
    } else {
        RefreshPowerPip(7);
        RefreshPowerPip(8);
        if (PowerClass() == W8_SPELL_POWER_SELECTABLE) {
            RefreshAvailablePowerPips();
            power_controls[7]->SetVisible(false);
            power_controls[8]->SetVisible(false);
        } else if (PowerClass() == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) {
            power_controls[7]->SetVisible(!CurrentGame().combat.has_value());
            RefreshAvailablePowerPips();
            power_controls[8]->SetVisible(false);
        } else if (PowerClass() == W8_SPELL_POWER_MAXIMUM) {
            for (pip = 0; pip < 7; ++pip) {
                power_controls[pip]->SetEnabled(false);
            }
            power_controls[8]->SetVisible(true);
            power_controls[7]->SetVisible(false);
        }
    }
    if (PowerClass() == W8_SPELL_POWER_MAXIMUM) {
        if (levels != 0) {
            for (pip = 0; pip < levels; ++pip) {
                power_controls[pip]->SetEnabled(false);
                rating = GetSpellCastRating(caster, draft.detail, pip + 1);
                power_frames[pip][0] = (pip + rating * 9) * 6;
            }
        }
        if (levels < 7) {
            for (pip = levels; pip < 7; ++pip) {
                power_frames[pip][0] = pip * 6 + 3;
            }
            return;
        }
    } else {
        for (pip = 0; pip < 7; ++pip) {
            power_frames[pip][0] = pip * 6 + 3;
        }
    }
}

/* Recomputes one pip's four sprite frames from the spell's safety rating at
   the level the pip stands for; pips seven and eight price the max cast. */
void W8SpellCastingView::RefreshPowerPip(int pip)
{
    W8SpellCastRating rating;
    int level;
    int frame;

    assert(draft.detail != 0);
    if (pip == 7 || pip == 8) {
        level = 8;
    } else {
        level = pip + 1;
    }
    power_controls[pip]->SetEnabled(true);
    rating = GetSpellCastRating(caster, draft.detail, level);
    frame = (pip + rating * 9) * 6;
    auto& frames = power_frames[pip];
    frames[1] = frame;
    frames[2] = pip == 8 ? frame : frame + 1;
    frames[3] = frame + 2;
    frames[4] = pip == 8 ? frame + 2 : frame + 4;
    dirty[2] = true;
}

/* Evaluate each learned spell once, then display castable spells first while
   retaining learned order within each category. The rows own their metadata. */
void W8SpellCastingView::RebuildList(int spell_id)
{
    ResetEditorStatusLine(-1);
    uiSpellIndex = -1;
    selected_spell_index = -1;
    auto& rows = spells;
    rows.clear();
    const auto realm = iSpellRealm;
    const auto count = caster->skill_unlocks[static_cast<int>(W8_SKILL_FIRE_MAGIC) + realm];
    for (unsigned int index = 0; index < count; ++index) {
        const int id = learned.spell_ids_by_realm[realm][index];
        const bool usable = SpellUsableNow(id, false);
        const bool castable =
            Data().spells[id].spell_point_cost <= caster->iSPLeft[realm] && usable &&
            SpellHasAnyValidTarget(CharacterPointerToPartySlot(caster), id, false) &&
            !IsTeleportCastMissingAnchor(caster, id);
        const signed char color = castable ? (IsSpellBlockedForCharacter(caster, id) ? 4 : 15) : 0;
        if (color != 15 &&
            (CurrentGame().activity.conversation_aside || LockInteractionSuspended() ||
             TrapInteractionSuspended()) &&
            !usable)
            continue;
        rows.push_back({id, color});
    }
    std::ranges::stable_partition(rows, [](const auto& row) { return row.color == 15; });
    int selected = -1;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto& row = rows[index];
        const auto& spell = Data().spells[row.spell];
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX,
                   fmt::format("{} {}",
                               g_spell_target_parentheticals[GetSpellTargetType(row.spell, false)],
                               spell.display_name),
                   2);
        AppendTextBoxLine(fmt::format("{:d}", spell.spell_point_cost), 2);
        if (row.color != 15) SetLineColor(wiz8::checked_integer<int>(index), row.color);
        if (row.spell == spell_id && row.color == 15) selected = wiz8::checked_integer<int>(index);
    }
    if (selected == -1) {
        ScrollTextBoxTo(0);
    } else {
        SelectListRow(selected);
        ScrollTextBoxTo(selected);
    }
    if (auto* hud = MainGameHud()) hud->repaint.text_box = true;
}

/* Recolours one spell-list line; -1 keeps the row's own color, or the
   selected-row color when the row is the selected spell. */
void W8SpellCastingView::SetLineColor(int index, signed char color)
{
    W8MessageStorageRecord* line;
    size_t length;

    if (color == -1) {
        if (uiSpellIndex == index) {
            color = 3;
        } else {
            color = spells[index].color;
        }
    }
    line = &CurrentGame().message_log[2][index];
    line->highlight_start = 2;
    length = line->text.size();
    line->highlight_color = color;
    line->highlight_stop = length;
}

/* Marks one spell-list row as selected, restoring the previous row's own
   color first; -1 just clears the selection. */
void W8SpellCastingView::SelectRow(int index)
{
    int previous;

    if (index == -1) {
        if (selected_spell_index != -1) {
            SetLineColor(selected_spell_index, -1);
            selected_spell_index = -1;
            if (auto* hud = MainGameHud()) hud->repaint.text_box = true;
            return;
        }
    } else {
        previous = selected_spell_index;
        if (index != previous && previous != -1) {
            SetLineColor(previous, -1);
        }
        SetLineColor(index, 5);
        selected_spell_index = index;
    }
    if (auto* hud = MainGameHud()) hud->repaint.text_box = true;
}

/* Opens the spell-casting view for the selected character with the given
   spell's realm and list row selected. */
void BeginSpellCast(int spell_id, std::move_only_function<void()> on_return)
{
    const int slot = CurrentGame().status.selected_character;
    if (!OpenSpellCastingView(slot, std::move(on_return))) return;
    auto* view = SpellPicker();
    if (view && CanCharacterCastSpell(&CurrentGame().party.characters[slot], spell_id)) {
        view->SelectSpell(spell_id);
    }
}

void W8SpellCastingView::SelectSpell(int spell_id)
{
    SelectRealm(Data().spells[spell_id].realm);
    RebuildList(spell_id);
}

/* Redraws every live spell-casting panel; the middle panel's pending text
   update is flushed through the message storage. */
void W8SpellCastingView::PaintPanels(bool active)
{
    auto& view = *this;
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

void SpellCastingNoticeClosed(bool)
{
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
}

/* Switches the view to another realm: the old realm's icon returns to its
   first frame and the new one starts its frame timer, then the whole
   selection state resets. */
void W8SpellCastingView::SelectRealm(W8SpellRealm realm)
{
    W8SpellRealm previous;

    previous = iSpellRealm;
    if (previous != realm) {
        if (previous != W8_SPELL_REALM_NONE) {
            dirty[0] = true;
            realm_buttons[iSpellRealm]->SetPressed(false);
            realm_buttons[iSpellRealm]->m_dirty = true;
        }
        iSpellRealm = realm;
        realm_anim_frame = g_spell_realm_animations[iSpellRealm].initial_frame;
        dirty[0] = true;
        realm_anim_timer = MonotonicMilliseconds() + (0x32);
        RebuildList(W8_SPELL_NONE);
        ClearChoice();
    }
    if (realm != W8_SPELL_REALM_NONE) {
        realm_buttons[realm]->SetPressed(true);
        realm_buttons[realm]->m_dirty = true;
    }
}

/* The seventh power pip: while its secondary state is lit it clears the lower
   pips and prices the name label with the max cast, otherwise it empties the
   label. */
void W8SpellCastingView::SelectPip7()
{
    int pip;

    if (power_controls[7]->IsPressed()) {
        for (pip = 0; pip < 7; ++pip) {
            auto& control = power_controls[pip];
            if (control->IsEnabled()) {
                control->SetPressed(false);
            }
        }
        spell_name.SetRenderMode(4);
        spell_name.SetText("?", g_wiz_text_font_secondary);
        draft.detail_block.spell.power_level = 8;
        dirty[2] = true;
        return;
    }
    spell_name.SetRenderMode(4);
    spell_name.SetText("", g_wiz_text_font_secondary);
    draft.detail_block.spell.power_level = 0;
    dirty[2] = true;
}

/* The max-power pip: while its secondary state is lit it prices the name
   label with the max cast, otherwise it relights and repaints the label. */
void W8SpellCastingView::SelectPip8()
{
    if (power_controls[8]->IsPressed()) {
        spell_name.SetRenderMode(4);
        spell_name.SetText("?", g_wiz_text_font_secondary);
        draft.detail_block.spell.power_level = 8;
        dirty[2] = true;
        return;
    }
    power_controls[8]->SetPressed(true);
    dirty[2] = true;
}

/* Records the chosen power level: pips through the level light, pips above it
   go dark, and the name label shows the cast's total cost. */
void W8SpellCastingView::SelectPowerLevel(int power_level)
{
    int pip;
    std::string text;

    dirty[2] = true;
    if (power_level == -1 || (power_controls[power_level]->IsPressed() == 0 &&
                              draft.detail_block.spell.power_level == power_level + 1)) {
        for (pip = 0; pip < 7; ++pip) {
            power_controls[pip]->SetPressed(false);
        }
        draft.detail_block.spell.power_level = 0;
        if (PowerClass() == W8_SPELL_POWER_MAXIMUM) {
            power_controls[8]->SetPressed(true);
            if (power_controls[8]->IsPressed() == 0) {
                power_controls[8]->SetPressed(true);
            } else {
                spell_name.SetRenderMode(4);
                spell_name.SetText("?", g_wiz_text_font_secondary);
                draft.detail_block.spell.power_level = 8;
            }
            dirty[2] = true;
            return;
        }
        spell_name.SetRenderMode(4);
        if (PowerClass() == W8_SPELL_POWER_FIXED) {
            draft.detail_block.spell.power_level = 1;
            text = fmt::format("{:d}", Data().spells[draft.detail].spell_point_cost);
        } else {
            text = "";
        }
    } else {
        draft.detail_block.spell.power_level = power_level + 1;
        if (power_level >= 0) {
            for (pip = 0; pip <= power_level; ++pip) {
                power_controls[pip]->SetPressed(true);
            }
        }
        if (power_level + 1 < 8) {
            for (pip = power_level + 1; pip < 8; ++pip) {
                power_controls[pip]->SetPressed(false);
            }
        }
        spell_name.SetRenderMode(4);
        text =
            fmt::format("{:d}", (power_level + 1) * Data().spells[draft.detail].spell_point_cost);
    }
    spell_name.SetText(text, g_wiz_text_font_secondary);
}

/* Hover preview for a power pip: clears the spell-name plate and shows the
   cast cost for the hovered level (or restores the selected cost / empty
   label when the pointer leaves). Only power classes 0 and 1 participate. */
void W8SpellCastingView::PreviewPowerPipHover(int power_level)
{
    std::string text;

    if (power_level < 7) {
        if ((PowerClass() == W8_SPELL_POWER_SELECTABLE ||
             PowerClass() == W8_SPELL_POWER_REPEAT_OUT_OF_COMBAT) &&
            (power_level == -1 || (power_controls[power_level]->IsVisible() &&
                                   power_controls[power_level]->IsEnabled()))) {
            dirty[2] = true;
            if (power_level != -1) {
                spell_name.SetRenderMode(6);
                text = fmt::format("{:d}", (power_level + 1) *
                                               Data().spells[draft.detail].spell_point_cost);
                spell_name.SetText(text, g_wiz_text_font_secondary);
                return;
            }
            if (PowerClass() == W8_SPELL_POWER_SELECTABLE) {
                spell_name.SetRenderMode(4);
                if (draft.detail_block.spell.power_level != 0) {
                    text = fmt::format("{:d}", draft.detail_block.spell.power_level *
                                                   Data().spells[draft.detail].spell_point_cost);
                    spell_name.SetText(text, g_wiz_text_font_secondary);
                    return;
                }
                spell_name.SetText("", g_wiz_text_font_secondary);
            }
        }
    }
}

/* The confirmation dialog's destroy callback: a cancelled dialog unwinds the
   whole spell selection. */
void W8SpellCastingView::DialogResult(bool accepted)
{
    int index;

    index = uiSpellIndex;
    if (accepted) {
        dialog_confirmed = true;
        return;
    }
    ClearChoice();
    SetLineColor(index, -1);
}

void W8SpellCastingView::Cancel()
{
    if (CurrentGame().activity.conversation_aside) {
        ResetEditorStatusLine(-1);
    }
    CloseSpellCastingView();
}

/* Per-frame spell-casting pump: advances the selected realm's icon animation
   on its own clock, then lets a ready cast commit. */
void W8SpellCastingView::Update()
{
    if (iSpellRealm != W8_SPELL_REALM_NONE) {
        if (TimeUntilDeadline(realm_anim_timer) == 0) {
            ++realm_anim_frame;
            if (realm_anim_frame == g_spell_realm_animations[iSpellRealm].frame_count) {
                realm_anim_frame = 0;
            }
            dirty[0] = true;
            realm_anim_timer = MonotonicMilliseconds() + (0x32);
            if (auto* hud = MainGameHud()) hud->repaint.panels = true;
        }
    }
    TryCommit();
}

UiResult W8SpellCastingView::HandleTextBox(const UiPointerEvent& event)
{
    if (CurrentGame().status.selected_character == -1 || iSpellRealm == W8_SPELL_REALM_NONE)
        return UiResult::Handled;
    int line =
        MainGameHud()->text_lines[CurrentGame().status.text_line_cursor] + event.local.y / 11;
    if (!event.inside || line < 0 || line >= static_cast<int>(spells.size())) line = -1;
    if (line != selected_spell_index) SelectRow(line);
    if (line != -1 && event.action == UiPointerAction::Up) {
        if (event.button == UiPointerButton::Left && (event.captured_buttons & SDL_BUTTON_LMASK))
            AfterUiDispatch([this, line] { SelectListRow(line); });
        if (event.button == UiPointerButton::Right && (event.captured_buttons & SDL_BUTTON_RMASK)) {
            const auto spell = spells[line].spell;
            AfterUiDispatch([spell] {
                auto dialog = std::make_unique<W8SpellInfoDialog>(spell);
                dialog->SetText("");
                dialog->cleanup = [pointer = ScreenPointer().request()] {
                    ScreenPointer().restore(pointer);
                    if (auto* hud = MainGameHud()) hud->repaint.everything = true;
                };
                OpenModal(std::move(dialog));
            });
        }
    }
    return UiResult::Handled;
}

/* The spell-list row click: re-colors the rows, remembers the spell, then
   either prices its power levels or reports why it cannot be cast. */
void W8SpellCastingView::SelectListRow(int index)
{
    signed char color;
    int cost;
    int previous;
    int spell_id;
    W8SpellTargetType target_type;
    unsigned int levels;
    W8TargetNeed needed;

    previous = uiSpellIndex;
    if (index == -1) {
        return;
    }
    uiSpellIndex = index;
    if (previous != -1) {
        SetLineColor(previous, -1);
    }
    spell_id = spells[uiSpellIndex].spell;
    if (spell_id == W8_SPELL_NONE) {
        return;
    }
    color = spells[index].color;
    if (color == 0 || color == 4) {
        uiSpellIndex = -1;
        ConfigureSpellTargetFilter(W8_TARGET_TYPE_NONE, W8_TARGET_NEED_NONE, &draft);
        ShowError(spell_id);
        QueueCharacterEvent(
            &CurrentGame().party.characters[CurrentGame().status.selected_character], 0x35, 0,
            kCharacterEventFullVolume);
        return;
    }
    SetLineColor(uiSpellIndex, 3);
    assert(Data().spells[spell_id].power_class != W8_SPELL_POWER_CLASS_NONE);
    cost = Data().spells[spell_id].spell_point_cost;
    levels = GetCharacterRealmSpellPoints(
                 &CurrentGame().party.characters[CurrentGame().status.selected_character],
                 Data().spells[spell_id].realm) /
             cost;
    levels = std::clamp(levels, 0u, 7u);
    if (levels == 0) {
        uiSpellIndex = -1;
        return;
    }
    draft.detail = spell_id;
    needed = GetTargetNeededForSpellFriendly(spell_id, false, MainGameHud() == nullptr);
    target_type = GetSpellTargetType(spell_id, false);
    ConfigureSpellTargetFilter(target_type, needed, &draft);
    SelectPowerLevel(-1);
    UpdatePowerPips();
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
}

/* Lets a fully priced spell commit once its target is valid; spells 0x49 and
   0x4b first raise a confirmation notice unless one was already answered. */
void W8SpellCastingView::TryCommit()
{
    bool ready;

    ready = false;
    if (draft.detail != 0 &&
        (draft.detail_block.spell.power_level != 0 || PowerClass() == W8_SPELL_POWER_FIXED)) {
        ready = IsSpellTargetOfNeededKind(&draft.target, draft.detail, MainGameHud() == nullptr);
    }
    const auto result = [this](bool accepted) { DialogResult(accepted); };
    if (draft.detail == 0x4b && !dialog_confirmed) {
        if (!IsModalOpen() && CurrentGame()
                                  .party.characters[CurrentGame().status.selected_character]
                                  .has_saved_location) {
            ShowMainGameNoticeLine(Data().strings[0x7a4], result, true, true);
        }
    } else if (draft.detail == 0x49 && !dialog_confirmed && !IsModalOpen()) {
        ShowMainGameNoticeLine(Data().strings[0x7a5], result, true, true);
    }
    if (ready && !IsModalOpen()) {
        if (draft.detail != 0x17) {
            if (CurrentGame().activity.conversation_aside) {
                ResetEditorStatusLine(-1);
            }
            // Commit a copy: casting may close the picker.
            const auto chosen = draft;
            SetCharacterSpell(caster, chosen);
            CloseSpellCastingView();
            return;
        }
        OpenCharacterScreenForPartySlot(CharacterPointerToPartySlot(caster), true);
    }
}

/* Shows why the supplied spell cannot be cast without editing the draft. */
void W8SpellCastingView::ShowError(int spell_id)
{
    if (!SpellUsableNow(spell_id, false)) {
        ShowMainGameNoticeLine(Data().strings[0x79e], SpellCastingNoticeClosed, true, false);
    } else if (GetCharacterRealmSpellPoints(caster, iSpellRealm) <
               Data().spells[spell_id].spell_point_cost) {
        ShowMainGameNoticeLine(Data().strings[0x7a0], SpellCastingNoticeClosed, true, false);
    } else if (!SpellHasAnyValidTarget(CharacterPointerToPartySlot(caster), spell_id, false)) {
        ShowMainGameNoticeLine(Data().strings[0x7a1], SpellCastingNoticeClosed, true, false);
    } else if (IsTeleportCastMissingAnchor(caster, spell_id)) {
        ShowMainGameNoticeLine(Data().strings[0x7a2], SpellCastingNoticeClosed, true, false);
    } else if (IsSpellBlockedForCharacter(caster, spell_id)) {
        ShowMainGameNoticeLine(Data().strings[0x79f], SpellCastingNoticeClosed, true, false);
    }
}
