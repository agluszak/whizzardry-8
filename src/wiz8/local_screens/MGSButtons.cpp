#include "wiz8/local_screens/MGSButtons.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "Font.h"
#include "Video2.h"
#include "compat/native.h"
#include "conditions.h"
#include "equipment_slots.h"
#include "layouts/item_instance.h"
#include "layouts/main_game_screen.h"
#include "layouts/targeting.h"
#include "timer.h"
#include "wiz8/assertions.h"
#include "wiz8/button_interaction.h"
#include "wiz8/catalog_artwork.h"
#include "wiz8/character_skills.h"
#include "wiz8/cursor.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSFormation.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/ui_input.h"
#include "wiz8/ui_layer.h"
#include "wiz8/xstatus.h"

#define MGSBUTTONS_CPP "C:\\Projects\\Wizardry 8\\Local Screens\\MGSButtons.cpp"

// GLOBAL: WIZ8 0x0061e988
unsigned short g_action_kind_message_ids[12] = {
    0x050f, 0x0510, 0x0511, 0x0512, 0x0513, 0x0514, 0x0515, 0x0516, 0x0517, 0x0518, 0x0519, 0x051a,
};

void DestroySubMenuPanel()
{
    if (g_main_game_state) g_main_game_state->submenu.reset();
}

/* Five caption rows suffice: Berserk and Pray require mutually exclusive
   professions, leaving room for the cancel action. */
// GLOBAL: WIZ8 0x0069B8EC

static void SubMenuPanelCloseButton(W8ButtonWidget* button);
static void SubMenuPanelFormationButton(W8ButtonWidget* button);
static void MainGameOptionsDiskButton(W8ButtonWidget* button);
static void MainGameCombatConfirmButton(W8ButtonWidget* button);
static void MainGameCombatStanceSecondary(W8ButtonWidget* button);
static void MainGameRoofButton0(W8ButtonWidget* button);
static void MainGameRoofButton1(W8ButtonWidget* button);
static void MainGameRoofButton2(W8ButtonWidget* button);
static void MainGameLayoutRadarButton(W8ButtonWidget* button);
static void MainGameLayoutActionPanelButton(W8ButtonWidget* button);
static void MainGameLayoutFormationButton(W8ButtonWidget* button);

int GetAttackMenuWeaponOffset(W8Skill skill)
{
    int base;
    switch (skill) {
    case W8_SKILL_MACE_FLAIL:
        base = 3;
        break;
    case W8_SKILL_AXE:
        base = 4;
        break;
    case W8_SKILL_POLEARM:
        base = 7;
        break;
    case W8_SKILL_STAFF_WAND:
        base = 6;
        break;
    case W8_SKILL_BOW:
        base = 1;
        break;
    case W8_SKILL_THROWING_SLING:
        base = 2;
        break;
    case W8_SKILL_MODERN_WEAPONS:
        base = 8;
        break;
    case W8_SKILL_MARTIAL_ARTS:
        base = 5;
        break;
    default:
        base = 0;
        break;
    }
    return base;
}

// FUNCTION: WIZ8 0x005963E0
void RefreshSubMenuPanel(bool invalidate)
{
    auto& submenu = g_main_game_state->submenu;
    if (!submenu) return;
    submenu->dirty |= invalidate;
    auto& target = GetFrameBufferSurface();
    if (submenu->dirty) {
        CatalogArtwork::Load(0x7f, 0).drawAndInvalidate(target, submenu->background,
                                                        submenu->bounds.x, 0x1c3);
        for (auto& button : submenu->buttons) button->m_dirty = true;
        submenu->dirty = false;
    }
    for (auto& button : submenu->buttons) button->Refresh();
}

static void ChooseSubMenuAction(W8ActionKind action)
{
    ChooseAction(g_status.selected_character, action, -1, 0, false, 1);
    DrawSubMenuCharacterAction();
    DestroySubMenuControls();
}

// FUNCTION: WIZ8 0x00596430
void SubMenuSelectAttack(void)
{
    ChooseSubMenuAction(W8_ACTION_ATTACK);
}

// FUNCTION: WIZ8 0x005964D0
void SubMenuSelectBerserk(void)
{
    ChooseSubMenuAction(W8_ACTION_BERSERK);
}

// FUNCTION: WIZ8 0x00596570
void SubMenuSelectBreathe(void)
{
    ChooseSubMenuAction(W8_ACTION_BREATHE);
}

// FUNCTION: WIZ8 0x00596610
void SubMenuSelectTurnUndead(void)
{
    ChooseSubMenuAction(W8_ACTION_TURN_UNDEAD);
}

// FUNCTION: WIZ8 0x005966B0
void SubMenuSelectPray(void)
{
    ChooseSubMenuAction(W8_ACTION_PRAY);
}

// FUNCTION: WIZ8 0x00596750
void SubMenuSelectDefend(void)
{
    ChooseSubMenuAction(W8_ACTION_DEFEND);
}

// FUNCTION: WIZ8 0x00596800
void SubMenuSelectProtect(void)
{
    ChooseSubMenuAction(W8_ACTION_PROTECT);
}

// FUNCTION: WIZ8 0x005968A0
void SubMenuSelectEquip(void)
{
    ChooseSubMenuAction(W8_ACTION_EQUIP);
}

// FUNCTION: WIZ8 0x00596940
void SubMenuOpenUseItemView(void)
{
    DestroySubMenuControls();
    OpenUseItemSelectView(g_status.selected_character);
}

// FUNCTION: WIZ8 0x005969D0
void SubMenuUseRecordedItem(void)
{
    DestroySubMenuControls();
    StartCharacterItemUse(g_status.selected_character);
}

// FUNCTION: WIZ8 0x00596A60
void SubMenuOpenSpellView(void)
{
    DestroySubMenuControls();
    OpenSpellCastingView(g_status.selected_character);
}

// FUNCTION: WIZ8 0x00596AF0
void SubMenuCastRecordedSpell(void)
{
    DestroySubMenuControls();
    StartCharacterSpellCast(g_status.selected_character, 0);
}

// FUNCTION: WIZ8 0x00596B90
void SubMenuSelectRun(void)
{
    if (!AnyCharacterEngaged()) {
        return;
    }
    ChooseSubMenuAction(W8_ACTION_RUN);
}

// FUNCTION: WIZ8 0x00596C40
void SubMenuSelectWalk(void)
{
    if (!AnyCharacterEngaged()) {
        return;
    }
    ChooseSubMenuAction(W8_ACTION_WALK);
}

void OpenSubMenuPanel(short notification)
{
    if (g_level_block->combat_end_notification != -1) {
        DestroySubMenuControls();
    }
    UpdateScreenOverlays(0);
    if (!BuildSubMenuPanel(notification)) {
        DestroySubMenuControls();
    }
    SetSubMenuButtonTooltips(0);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x00596CF0
void ResetSubMenuPanel(void)
{
    short saved_notification;

    saved_notification = g_level_block->combat_end_notification;
    DestroySubMenuControls();
    OpenSubMenuPanel(saved_notification);
}

// FUNCTION: WIZ8 0x00596EC0
static void CreateSubMenuScrollButtons()
{
    int i;

    g_main_game_state->hud_buttons.submenu_scroll[0].Configure(
        ButtonGraphics::Load("Data\\Main Interface\\main_scroll_arrows.sti", 3, 0, 1, 2, 2),
        {.on_click = [](auto*) { ScrollSubMenuCharacter(0); }, .tooltip = 0x4b});
    g_main_game_state->hud_buttons.submenu_scroll[1].Configure(
        ButtonGraphics::Load("Data\\Main Interface\\main_scroll_arrows.sti", 7, 4, 5, 6, 6),
        {.on_click = [](auto*) { ScrollSubMenuCharacter(1); }, .tooltip = 0x4c});
    for (i = 0; i < 2; ++i) {
        g_main_game_state->hud_buttons.submenu_scroll[i].SetPosition(
            g_scroll_button_positions[i].x, g_scroll_button_positions[i].y);
    }
}

// FUNCTION: WIZ8 0x005978D0
static void SubMenuPanelCloseButton(W8ButtonWidget* button)
{
    OpenSubMenuPanel(9);
}

// FUNCTION: WIZ8 0x00597A10
static void SubMenuPanelFormationButton(W8ButtonWidget* button)
{
    if (!gXStatus.fReviewCharacterMode) {
        OpenFormationPanel();
    } else {
        CloseFormationPanel();
    }
}

// FUNCTION: WIZ8 0x00597670
static void CreateSubMenuPanelButtons()
{
    int index;

    g_main_game_state->hud_buttons.submenu_panel[0].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x21, 0x1e, 0x1f, 0x20, 0x22),
        {.on_click = SubMenuPanelCloseButton, .tooltip = 0x4d});
    g_main_game_state->hud_buttons.submenu_panel[1].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x26, 0x23, 0x24, 0x25, 0x27),
        {.on_click = SubMenuPanelFormationButton,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x4e});
    for (index = 0; index < 2; ++index) {
        g_main_game_state->hud_buttons.submenu_panel[index].SetPosition(
            g_submenu_panel_button_positions[index].x, g_submenu_panel_button_positions[index].y);
    }
}

// FUNCTION: WIZ8 0x00597B70
static void MainGameOptionsDiskButton(W8ButtonWidget* button)
{
    if (CanInterruptLevelMovement()) {
        g_pending_screen_state.mode = 3;
        g_pending_screen_state.id = W8_SCREEN_OPTIONS;
    }
}

// FUNCTION: WIZ8 0x00597A30
static void CreateOptionsDiskButton()
{
    g_main_game_state->hud_buttons.options_disk.Configure(
        ButtonGraphics::Load(g_options_disk_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameOptionsDiskButton, .tooltip = 0x41});
    g_main_game_state->hud_buttons.options_disk.SetPosition(g_options_disk_position.x,
                                                            g_options_disk_position.y);
}

/* Redraw the options-disk chrome and button; disabled under dialogue / lock /
   trap / camp interact modes. */
// FUNCTION: WIZ8 0x00597AF0
void RedrawOptionsDiskButton(void)
{
    bool enabled;

    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 2, 0, 0x1c2);
    g_main_game_state->hud_buttons.options_disk.m_dirty = true;
    if (!gXStatus.fNpcDialogueMode && !gXStatus.fLockInteractMode && !gXStatus.fTrapInteractMode &&
        !gXStatus.fCampMode && !LockInteractionSuspended() && !TrapInteractionSuspended()) {
        enabled = true;
    } else {
        enabled = false;
    }
    g_main_game_state->hud_buttons.options_disk.SetEnabled(enabled);
    g_main_game_state->hud_buttons.options_disk.Refresh();
}

/* Pick which of the five combat-stance buttons is visible for the live combat
   / continuous-combat state, then enable and Draw that one. */
// FUNCTION: WIZ8 0x00597D70
void UpdateCombatStanceButtons(void)
{
    unsigned int stance;
    unsigned char index;
    bool enabled;

    if (!gXStatus.fCombatMode) {
        for (auto& button : g_main_game_state->hud_buttons.combat_stance) {
            button.SetVisible(false);
        }
        return;
    }

    if (g_settings.continuous_combat == 0) {
        stance = g_combat_state->execution_active != 0 ? 3U : 0U;
    } else if ((TimeUntilDeadline(g_combat_state->combat_ui_timer) == 0 &&
                CombatMayAdvanceContinuously()) ||
               g_combat_state->party_surprised) {
        stance = g_combat_state->round_active ? 1U : 4U;
    } else {
        stance = 2;
    }

    for (index = 0; index < 5; ++index) {
        g_main_game_state->hud_buttons.combat_stance[index].SetVisible(index == stance);
    }
    if (!gpSCSV && !ItemUseDraft() && !gXStatus.fReviewCharacterMode) {
        enabled = true;
    } else {
        enabled = false;
    }
    g_main_game_state->hud_buttons.combat_stance[stance].SetEnabled(enabled);
    g_main_game_state->hud_buttons.combat_stance[stance].Refresh();
}

/* Dirties the five combat-stance buttons and refreshes which one is live. */
// FUNCTION: WIZ8 0x00597D30
void RedrawCombatStanceButtons(void)
{
    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 3, 0x262, 0x1c2);
    for (int index = 0; index < 5; ++index) {
        g_main_game_state->hud_buttons.combat_stance[index].m_dirty = true;
    }
    UpdateCombatStanceButtons();
}

// FUNCTION: WIZ8 0x00597E70
static void MainGameCombatConfirmButton(W8ButtonWidget* button)
{
    if (!gXStatus.fCombatMode) {
        return;
    }
    if (g_combat_state->execution_active != 0) {
        TogglePartyCombatStance();
        return;
    }
    BeginCombatExecution();
    if (g_settings.continuous_combat == 0) {
        return;
    }
    if (TimeUntilDeadline(g_combat_state->combat_ui_timer) == 0) {
        return;
    }
    g_combat_state->combat_ui_timer = w8_get_ticks() + (0);
}

// FUNCTION: WIZ8 0x00597ED0
static void MainGameCombatStanceSecondary(W8ButtonWidget* button)
{
    if (gXStatus.fCombatMode) {
        TogglePartyCombatStance();
    }
}

// FUNCTION: WIZ8 0x00597B90
static void CreateCombatStanceButtons()
{
    int index;

    g_main_game_state->hud_buttons.combat_stance[0].Configure(
        ButtonGraphics::Load(g_attack_confirm_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameCombatConfirmButton,
         .on_right_click = MainGameCombatStanceSecondary,
         .tooltip = 0x4f});
    g_main_game_state->hud_buttons.combat_stance[1].Configure(
        ButtonGraphics::Load(g_combat_stop_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameCombatConfirmButton,
         .on_right_click = MainGameCombatStanceSecondary,
         .tooltip = 0x50});
    g_main_game_state->hud_buttons.combat_stance[2].Configure(
        ButtonGraphics::Load(g_cont_start_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameCombatConfirmButton,
         .on_right_click = MainGameCombatStanceSecondary,
         .tooltip = 0x4f});
    g_main_game_state->hud_buttons.combat_stance[3].Configure(
        ButtonGraphics::Load(g_cont_toggle_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameCombatConfirmButton,
         .on_right_click = MainGameCombatStanceSecondary,
         .tooltip = 0x51});
    g_main_game_state->hud_buttons.combat_stance[4].Configure(
        ButtonGraphics::Load(g_cont_pending_path, 3, 0, 1, 2, 2),
        {.on_click = MainGameCombatConfirmButton,
         .on_right_click = MainGameCombatStanceSecondary,
         .tooltip = 0x50});
    for (index = 0; index < 5; ++index) {
        g_main_game_state->hud_buttons.combat_stance[index].SetPosition(
            g_combat_stance_positions[index].x, g_combat_stance_positions[index].y);
    }
}

// FUNCTION: WIZ8 0x00598270
static void MainGameRoofButton0(W8ButtonWidget* button)
{
    SetMainUiPreference(W8_MAIN_UI_MODE_PORTRAITS);
    RequestRedraw(W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS | W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x00598290
static void MainGameRoofButton1(W8ButtonWidget* button)
{
    SetMainUiPreference(W8_MAIN_UI_MODE_FORMATION);
    RequestRedraw(W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS | W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x005982B0
static void MainGameRoofButton2(W8ButtonWidget* button)
{
    SetMainUiPreference(W8_MAIN_UI_MODE_RADAR);
    RequestRedraw(W8_MAIN_REDRAW_ROOF_AND_SPELL_ICONS | W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x00597EE0
static void CreateRoofButtons()
{
    int index;

    g_main_game_state->hud_buttons.roof[0].Configure(
        ButtonGraphics::Load(g_roof_buttons_path, 9, 0, 6, 3, 6),
        {.on_click = MainGameRoofButton0,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x38});
    g_main_game_state->hud_buttons.roof[1].Configure(
        ButtonGraphics::Load(g_roof_buttons_path, 10, 1, 7, 4, 7),
        {.on_click = MainGameRoofButton1,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x39});
    g_main_game_state->hud_buttons.roof[2].Configure(
        ButtonGraphics::Load(g_roof_buttons_path, 0xb, 2, 8, 5, 8),
        {.on_click = MainGameRoofButton2,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x3a});
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS) {
        g_main_game_state->hud_buttons.roof[0].SetPressed(true);
    } else if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_FORMATION) {
        g_main_game_state->hud_buttons.roof[1].SetPressed(true);
    } else if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_RADAR) {
        g_main_game_state->hud_buttons.roof[2].SetPressed(true);
    }
    for (index = 0; index < 3; ++index) {
        g_main_game_state->hud_buttons.roof[index].SetPosition(g_roof_button_positions[index].x,
                                                               g_roof_button_positions[index].y);
    }
}

/* Draw the roof chrome strip, mark each roof button dirty, sync enable/press
   state, and raise redraw bit 0x100000 for the second pass. */
// FUNCTION: WIZ8 0x00598060
void RedrawRoofButtons(void)
{
    CatalogArtwork::Load(0x85, 0).draw(GetFrameBufferSurface(), 0, 0, 0);
    for (int index = 0; index < 3; ++index) {
        g_main_game_state->hud_buttons.roof[index].m_dirty = true;
    }
    UpdateRoofButtons();
    RequestRedraw(W8_MAIN_REDRAW_COMBAT_PROGRESS);
}

/* Visibility and enablement for the three roof buttons, then Draw. The third
   button (radar) hides under NPC/spell/item/lock/trap modes; the first two
   disable under NPC dialogue or camp. */
// FUNCTION: WIZ8 0x005980B0
void UpdateRoofButtons(void)
{
    bool visible;
    bool enabled;

    if (!gXStatus.fNpcDialogueMode && !gpSCSV && !ItemUseDraft() && !gXStatus.fLockInteractMode &&
        !gXStatus.fTrapInteractMode) {
        visible = true;
    } else {
        visible = false;
    }
    g_main_game_state->hud_buttons.roof[2].SetVisible(visible);
    if (!gXStatus.fNpcDialogueMode && !gXStatus.fCampMode) {
        g_main_game_state->hud_buttons.roof[0].SetEnabled(true);
        enabled = true;
    } else {
        g_main_game_state->hud_buttons.roof[0].SetEnabled(false);
        enabled = false;
    }
    g_main_game_state->hud_buttons.roof[1].SetEnabled(enabled);
    SyncRoofButtonPressedState();
    for (auto& button : g_main_game_state->hud_buttons.roof) {
        button.Refresh();
    }
}

/* Mirror GetEffectiveMainUiMode() onto the three roof buttons' pressed
   state (portraits / formation / radar). */
// FUNCTION: WIZ8 0x00598150
void SyncRoofButtonPressedState(void)
{
    if (GetEffectiveMainUiMode() == 0) {
        if (g_main_game_state->hud_buttons.roof[0].IsPressed() == 0) {
            g_main_game_state->hud_buttons.roof[0].SetPressed(true);
        }
        if (g_main_game_state->hud_buttons.roof[1].IsPressed()) {
            g_main_game_state->hud_buttons.roof[1].SetPressed(false);
        }
        if (g_main_game_state->hud_buttons.roof[2].IsPressed()) {
            g_main_game_state->hud_buttons.roof[2].SetPressed(false);
        }
    } else if (GetEffectiveMainUiMode() == 1) {
        if (g_main_game_state->hud_buttons.roof[1].IsPressed() == 0) {
            g_main_game_state->hud_buttons.roof[1].SetPressed(true);
        }
        if (g_main_game_state->hud_buttons.roof[0].IsPressed()) {
            g_main_game_state->hud_buttons.roof[0].SetPressed(false);
        }
        if (g_main_game_state->hud_buttons.roof[2].IsPressed()) {
            g_main_game_state->hud_buttons.roof[2].SetPressed(false);
        }
    } else if (GetEffectiveMainUiMode() == 2) {
        if (g_main_game_state->hud_buttons.roof[2].IsPressed() == 0) {
            g_main_game_state->hud_buttons.roof[2].SetPressed(true);
        }
        if (g_main_game_state->hud_buttons.roof[0].IsPressed()) {
            g_main_game_state->hud_buttons.roof[0].SetPressed(false);
        }
        if (g_main_game_state->hud_buttons.roof[1].IsPressed()) {
            g_main_game_state->hud_buttons.roof[1].SetPressed(false);
        }
    }
}

// FUNCTION: WIZ8 0x00598670
static void MainGameLayoutRadarButton(W8ButtonWidget* button)
{
    if (g_level_block->radar_map_visible != 0) {
        if (!g_level_block->formation_board_visible && g_level_block->action_panel_visible == 0) {
            SetMainUiPreference(W8_MAIN_UI_MODE_RADAR);
            return;
        }
        g_settings.formation_radar_map_preference = 0;
        RefreshMainGameLayout();
        return;
    }
    g_settings.formation_radar_map_preference = 1;
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_RADAR) {
        g_settings.formation_action_panel_preference = 0;
        g_settings.formation_board_preference = 0;
        SetMainUiPreference(W8_MAIN_UI_MODE_FORMATION);
        return;
    }
    RefreshMainGameLayout();
}

// FUNCTION: WIZ8 0x005986E0
static void MainGameLayoutActionPanelButton(W8ButtonWidget* button)
{
    if (g_level_block->action_panel_visible != 0) {
        if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_FORMATION) {
            if (g_level_block->radar_map_visible == 0 && !g_level_block->formation_board_visible) {
                SetMainUiPreference(W8_MAIN_UI_MODE_RADAR);
            }
            g_settings.formation_action_panel_preference = 0;
            RefreshMainGameLayout();
            return;
        }
        if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS) {
            g_settings.portraits_action_panel_preference = 0;
        }
        RefreshMainGameLayout();
        return;
    }
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_RADAR) {
        g_settings.formation_radar_map_preference = 0;
        g_settings.formation_action_panel_preference = 1;
        g_settings.formation_board_preference = 0;
        SetMainUiPreference(W8_MAIN_UI_MODE_FORMATION);
        return;
    }
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_PORTRAITS) {
        g_settings.portraits_action_panel_preference = 1;
        RefreshMainGameLayout();
        return;
    }
    g_settings.formation_action_panel_preference = 1;
    RefreshMainGameLayout();
}

// FUNCTION: WIZ8 0x005987A0
static void MainGameLayoutFormationButton(W8ButtonWidget* button)
{
    if (g_level_block->formation_board_visible) {
        if (g_level_block->radar_map_visible == 0 && g_level_block->action_panel_visible == 0) {
            SetMainUiPreference(W8_MAIN_UI_MODE_RADAR);
            return;
        }
        g_settings.formation_board_preference = 0;
        RefreshMainGameLayout();
        return;
    }
    g_settings.formation_board_preference = 1;
    if (GetEffectiveMainUiMode() == W8_MAIN_UI_MODE_RADAR) {
        g_settings.formation_radar_map_preference = 0;
        g_settings.formation_action_panel_preference = 0;
        SetMainUiPreference(W8_MAIN_UI_MODE_FORMATION);
        return;
    }
    RefreshMainGameLayout();
}

/* Draw the left layout-arrow strip, mark each arrow dirty, then raise/lower
   the paired arrows from the live action-panel / formation / radar flags.
   Modal NPC/spell/item/lock/trap modes hide the whole bank. */
// FUNCTION: WIZ8 0x00598490
void RedrawLayoutArrowButtons(void)
{
    bool enabled;
    W8ButtonWidget* draw_button;

    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 0, 0, 0x166);
    for (auto& button : g_main_game_state->hud_buttons.layout_arrows) {
        button.m_dirty = true;
    }
    if (gXStatus.fNpcDialogueMode || gpSCSV || ItemUseDraft() || gXStatus.fLockInteractMode ||
        gXStatus.fTrapInteractMode) {
        for (auto& button : g_main_game_state->hud_buttons.layout_arrows) {
            button.SetVisible(false);
        }
        return;
    }

    enabled = GetEffectiveMainUiMode() != 0;
    if (enabled) {
        g_main_game_state->hud_buttons.layout_arrows[0].SetEnabled(true);
    } else {
        g_main_game_state->hud_buttons.layout_arrows[0].SetEnabled(false);
    }
    g_main_game_state->hud_buttons.layout_arrows[2].SetEnabled(enabled);

    if (g_level_block->radar_map_visible == 0) {
        g_main_game_state->hud_buttons.layout_arrows[3].SetVisible(true);
        g_main_game_state->hud_buttons.layout_arrows[0].SetVisible(false);
        draw_button = &g_main_game_state->hud_buttons.layout_arrows[3];
    } else {
        g_main_game_state->hud_buttons.layout_arrows[0].SetVisible(true);
        g_main_game_state->hud_buttons.layout_arrows[3].SetVisible(false);
        draw_button = &g_main_game_state->hud_buttons.layout_arrows[0];
    }
    draw_button->Refresh();

    if (g_level_block->action_panel_visible == 0) {
        g_main_game_state->hud_buttons.layout_arrows[4].SetVisible(true);
        g_main_game_state->hud_buttons.layout_arrows[1].SetVisible(false);
        draw_button = &g_main_game_state->hud_buttons.layout_arrows[4];
    } else {
        g_main_game_state->hud_buttons.layout_arrows[1].SetVisible(true);
        g_main_game_state->hud_buttons.layout_arrows[4].SetVisible(false);
        draw_button = &g_main_game_state->hud_buttons.layout_arrows[1];
    }
    draw_button->Refresh();

    if (g_level_block->formation_board_visible) {
        g_main_game_state->hud_buttons.layout_arrows[2].SetVisible(true);
        g_main_game_state->hud_buttons.layout_arrows[5].SetVisible(false);
        g_main_game_state->hud_buttons.layout_arrows[2].Refresh();
        return;
    }
    g_main_game_state->hud_buttons.layout_arrows[5].SetVisible(true);
    g_main_game_state->hud_buttons.layout_arrows[2].SetVisible(false);
    g_main_game_state->hud_buttons.layout_arrows[5].Refresh();
}

// FUNCTION: WIZ8 0x005982D0
static void CreateLayoutArrowButtons()
{
    int index;

    g_main_game_state->hud_buttons.layout_arrows[0].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 6, 0, 1, BUTTON_NO_IMAGE, 2),
        {.on_click = MainGameLayoutRadarButton, .tooltip = 0x3e});
    g_main_game_state->hud_buttons.layout_arrows[1].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 0xe, 8, 9, BUTTON_NO_IMAGE, 10),
        {.on_click = MainGameLayoutActionPanelButton, .tooltip = 0x3f});
    g_main_game_state->hud_buttons.layout_arrows[2].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 0x16, 0x10, 0x11, BUTTON_NO_IMAGE, 0x12),
        {.on_click = MainGameLayoutFormationButton, .tooltip = 0x40});
    g_main_game_state->hud_buttons.layout_arrows[3].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 7, 3, 4, BUTTON_NO_IMAGE, 5),
        {.on_click = MainGameLayoutRadarButton, .tooltip = 0x3b});
    g_main_game_state->hud_buttons.layout_arrows[4].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 0xf, 0xb, 0xc, BUTTON_NO_IMAGE, 0xd),
        {.on_click = MainGameLayoutActionPanelButton, .tooltip = 0x3c});
    g_main_game_state->hud_buttons.layout_arrows[5].Configure(
        ButtonGraphics::Load(g_layout_arrows_path, 0x17, 0x13, 0x14, BUTTON_NO_IMAGE, 0x15),
        {.on_click = MainGameLayoutFormationButton, .tooltip = 0x3d});
    for (index = 0; index < 6; ++index) {
        g_main_game_state->hud_buttons.layout_arrows[index].SetPosition(
            g_layout_arrow_positions[index].x, g_layout_arrow_positions[index].y);
    }
}

// FUNCTION: WIZ8 0x00598AB0
void ConfigureMainGameHudButtons(void)
{
    CreateSubMenuButtons();
    CreateSubMenuScrollButtons();
    CreateSubMenuPanelButtons();
    CreateOptionsDiskButton();
    CreateCombatStanceButtons();
    CreateRoofButtons();
    CreateLayoutArrowButtons();
}

static void SetMenuButtonBanksEnabled(bool enabled)
{
    int i;
    for (i = 0; i < 2; ++i) {
        g_main_game_state->hud_buttons.submenu_scroll[i].SetEnabled(enabled);
    }
    g_main_game_state->hud_buttons.options_disk.SetEnabled(enabled);
    for (i = 0; i < 3; ++i) {
        g_main_game_state->hud_buttons.roof[i].SetEnabled(enabled);
    }
    for (i = 0; i < 6; ++i) {
        g_main_game_state->hud_buttons.layout_arrows[i].SetEnabled(enabled);
    }
}

/* Re-enable the button banks the surprise sequence took down. */
// FUNCTION: WIZ8 0x00598c10
void EnableMenuButtonBanks(void)
{
    SetMenuButtonBanksEnabled(true);
}

/* Disable the same button banks while the surprise sequence runs. */
// FUNCTION: WIZ8 0x00598c70
void DisableMenuButtonBanks(void)
{
    SetMenuButtonBanksEnabled(false);
}

// FUNCTION: WIZ8 0x00596FE0
void DrawSubMenuCharacterAction(void)
{
    unsigned short slot;
    W8Character* character;
    W8PartySlotRow* row;
    W8ActionKind action;
    W8MonsterInfo* monster_info;
    std::string name;
    std::string text;
    std::string second;
    std::int16_t width;
    std::int16_t separator;
    std::int16_t trailing;

    slot = static_cast<unsigned short>(g_status.selected_character);
    if (slot > 7) {
        return;
    }
    row = &g_status.buffers.XChar[slot];
    g_smfnt_font.setPalette(g_font_state_palettes[row->party_order_index]);
    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 6, 0x157, 0x1c2);
    character = &g_status.buffers.Char[slot];
    text = fmt::format("{} - {}", character->name,
                       g_localized_strings[g_profession_name_message_ids[character->iProfession]]);
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                          {(0xb9 - g_smfnt_font.textWidth(text)) / 2 + 0x157, 0x1c6}, text);
    if (!gXStatus.fCombatMode) {
        if (character->highest_condition == W8_CONDITION_NONE) {
            return;
        }
        text = g_localized_strings[g_condition_notices[character->highest_condition].name];
    } else {
        action = row->committed_action.kind;
        switch (action) {
        case W8_ACTION_CAST_SPELL:
            text = fmt::format("{} - {} ({:d})", g_localized_strings[g_action_kind_message_ids[7]],
                               g_spell_records[row->committed_action.detail].display_name,
                               row->committed_action.detail_block.spell.power_level);
            break;
        case W8_ACTION_USE_ITEM:
            text = fmt::format("{} - {}", g_localized_strings[g_action_kind_message_ids[8]],
                               g_spell_records[g_item_records[row->committed_action.detail_block
                                                                  .item_use.item->iItemNo]
                                                   .spell_id]
                                   .display_name);
            break;
        case W8_ACTION_ATTACK:
            text = fmt::format("{} - ", g_localized_strings[g_action_kind_message_ids[0]]);
            if (character->Hand[0].in_play) {
                if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1) {
                    text += g_localized_strings[0x5b8];
                } else {
                    text +=
                        g_localized_strings[g_generic_item_name_notice[GetItemUnidentifiedNameIndex(
                            &character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON])]];
                }
            }
            if (!character->Hand[1].in_play) {
                if (!character->Hand[0].in_play) {
                    text += g_localized_strings[0x5b8];
                }
            } else {
                if (character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1) {
                    second = g_localized_strings[0x5b8];
                } else {
                    second =
                        g_localized_strings[g_generic_item_name_notice[GetItemUnidentifiedNameIndex(
                            &character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON])]];
                }
                if (!character->Hand[0].in_play) {
                    text += second;
                } else {
                    width = g_smfnt_font.textWidth(text);
                    separator = g_smfnt_font.textWidth("/)");
                    trailing = g_smfnt_font.textWidth(second);
                    if (static_cast<unsigned int>(trailing + width + separator) < 0xb9) {
                        text += "/";
                        text += second;
                    }
                }
            }
            break;
        case W8_ACTION_PROTECT:
            if (row->committed_action.target.iType == W8_TARGET_KIND_CHARACTER) {
                text = fmt::format("{} - {}", g_localized_strings[g_action_kind_message_ids[5]],
                                   g_status.buffers.Char[row->committed_action.target.iChar].name);
            } else if (row->committed_action.target.iType == W8_TARGET_KIND_MONSTER) {
                monster_info = FindMonster(row->committed_action.target.iMonsterID);
                name = GetMonsterName(monster_info, 0, 0);
                text =
                    fmt::format("{} - {}", g_localized_strings[g_action_kind_message_ids[5]], name);
            } else {
                text = fmt::format("{} ", g_localized_strings[g_action_kind_message_ids[5]]);
            }
            break;
        case W8_ACTION_NONE:
            text = g_localized_strings[0x7e1];
            break;
        default:
            text = fmt::format("{} ", g_localized_strings[g_action_kind_message_ids[action]]);
            break;
        }
    }
    DrawTextAndInvalidate(GetFrameBufferSurface(), g_smfnt_font,
                          {(0xb9 - g_smfnt_font.textWidth(text)) / 2 + 0x157, 0x1d1}, text);
}

/* Enable the fight/review panel buttons from combat engagement and mode
   flags, then Draw both. */
// FUNCTION: WIZ8 0x00597790
void UpdateSubMenuPanelButtons(void)
{
    bool enabled;

    if (!gXStatus.fCombatMode || !g_combat_state->round_active || gXStatus.fSurprisePossible ||
        gXStatus.fLockInteractMode || gXStatus.fTrapInteractMode || gXStatus.fNpcDialogueMode ||
        gpSCSV || ItemUseDraft() || g_level_block->combat_end_notification != -1) {
        enabled = false;
    } else {
        enabled = AnyCharacterEngaged();
    }
    g_main_game_state->hud_buttons.submenu_panel[0].SetEnabled(enabled);

    if (!gXStatus.fSurprisePossible && !gXStatus.fLockInteractMode && !gXStatus.fTrapInteractMode &&
        !gXStatus.fNpcDialogueMode && !gpSCSV && !ItemUseDraft() &&
        g_level_block->combat_end_notification == -1 && AnyCharacterEngaged()) {
        g_main_game_state->hud_buttons.submenu_panel[1].SetEnabled(true);
        if (!gXStatus.fReviewCharacterMode) {
            if (g_main_game_state->hud_buttons.submenu_panel[1].IsPressed()) {
                g_main_game_state->hud_buttons.submenu_panel[1].SetPressed(false);
            }
        } else if (g_main_game_state->hud_buttons.submenu_panel[1].IsPressed() == 0) {
            g_main_game_state->hud_buttons.submenu_panel[1].SetPressed(true);
        }
    } else {
        g_main_game_state->hud_buttons.submenu_panel[1].SetEnabled(false);
    }

    for (auto& button : g_main_game_state->hud_buttons.submenu_panel) {
        button.Refresh();
    }
}

static void UpdateSubMenuScrollButtons()
{

    const bool enabled = g_level_block->combat_end_notification == -1 &&
                         !gXStatus.fNpcDialogueMode && !gXStatus.fCampMode;
    for (auto& button : g_main_game_state->hud_buttons.submenu_scroll) {
        button.SetEnabled(enabled);
    }
    for (auto& button : g_main_game_state->hud_buttons.submenu_scroll) {
        button.Refresh();
    }
}

/* Redraw the bottom sub-menu strip: bank chrome, the nine bank buttons, panel
   chrome/buttons, character-action caption, and the scroll arrows. */
// FUNCTION: WIZ8 0x00598810
void RedrawSubMenuButtons(void)
{
    int index;
    int frame;

    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 4, 0x1e, 0x1c2);
    frame = !gXStatus.fCombatMode ? 8 : 9;
    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), frame, 0xc4, 0x1c2);

    for (auto& button : g_main_game_state->hud_buttons.submenu) {
        button.m_dirty = true;
    }
    for (index = 0; index < 9; ++index) {
        auto& button = g_main_game_state->hud_buttons.submenu[index];
        UpdateSubMenuButton(index);
        button.Refresh();
    }

    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 7, 0x210, 0x1c2);
    for (auto& button : g_main_game_state->hud_buttons.submenu_panel) {
        button.m_dirty = true;
    }
    UpdateSubMenuPanelButtons();

    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 5, 0x124, 0x1c2);
    CatalogArtwork::Load(0x7e, 0).drawAndInvalidate(GetFrameBufferSurface(), 6, 0x157, 0x1c2);
    if (g_status.buffers.XChar[g_status.selected_character].fOccupied) {
        DrawSubMenuCharacterAction();
    }

    for (auto& button : g_main_game_state->hud_buttons.submenu_scroll) {
        button.m_dirty = true;
    }
    UpdateSubMenuScrollButtons();
}

/* The eligible-slot scan runs in two halves per direction: from the current
   slot to the edge, then wrapping from the far edge back to the current slot.
   The `slot != current` check inside the first half is unreachable for valid
   slots, but retail tests it anyway. */
// FUNCTION: WIZ8 0x00597570
void ScrollSubMenuCharacter(char direction)
{
    short current;
    short selected;
    short slot;

    if (g_status.selected_character < 0 || g_status.selected_character >= 8) {
        throw std::logic_error("ScrollSubMenuCharacter: selected character is not a party slot");
    }
    current = static_cast<short>(g_status.selected_character);
    selected = current;
    switch (direction) {
    case 1:
        for (slot = current + 1; slot < 8; ++slot) {
            if (!IsPartySlotEligible(slot)) {
                continue;
            }
            selected = slot;
            if (slot != current) {
                goto done;
            }
            break;
        }
        for (slot = 0; slot <= current; ++slot) {
            if (IsPartySlotEligible(slot)) {
                selected = slot;
                goto done;
            }
        }
        break;
    case 0:
        for (slot = current - 1; slot >= 0; --slot) {
            if (!IsPartySlotEligible(slot)) {
                continue;
            }
            selected = slot;
            if (slot != current) {
                goto done;
            }
            break;
        }
        for (slot = 7; slot >= current; --slot) {
            if (IsPartySlotEligible(slot)) {
                selected = slot;
                goto done;
            }
        }
        break;
    }
done:
    SelectPartyCharacter(selected);
    RequestRedraw(1 << current);
    RequestRedraw(1 << g_status.selected_character);
    RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
}

// FUNCTION: WIZ8 0x005990F0
void SetSubMenuButtonTooltips(int enabled)
{
    auto& hud = g_main_game_state->hud_buttons;
    for (auto& button : hud.submenu) {
        button.SetTooltipEnabled(enabled);
    }
    for (auto& button : hud.submenu_scroll) {
        button.SetTooltipEnabled(enabled);
    }
    for (auto& button : hud.submenu_panel) {
        button.SetTooltipEnabled(enabled);
    }
}

/* Per-frame button refresh from DrawMainGameScreen: restate and draw the nine
   bank buttons, gate the scroll arrows on an idle submenu, enable the
   options/disk button outside modal modes, then refresh the panel, stance,
   roof and layout-arrow sets. */
// FUNCTION: WIZ8 0x005989B0
void UpdateMainGameButtons(void)
{
    int index;
    bool enabled;

    for (index = 0; index < 9; ++index) {
        auto& button = g_main_game_state->hud_buttons.submenu[index];
        UpdateSubMenuButton(index);
        button.Refresh();
    }
    UpdateSubMenuScrollButtons();
    UpdateSubMenuPanelButtons();
    enabled = !gXStatus.fNpcDialogueMode && !gXStatus.fLockInteractMode &&
              !gXStatus.fTrapInteractMode && !gXStatus.fCampMode && !LockInteractionSuspended() &&
              !TrapInteractionSuspended();
    g_main_game_state->hud_buttons.options_disk.SetEnabled(enabled);
    g_main_game_state->hud_buttons.options_disk.Refresh();
    UpdateCombatStanceButtons();
    UpdateRoofButtons();
}

/* While the combat-end submenu is up and the cursor has left its row band,
   run a 500 ms countdown; once it lapses, tear the panel and rows down like
   the background right-click path. */
// FUNCTION: WIZ8 0x00598FA0
void UpdateSubMenuAutoClose(void)
{
    auto& submenu = g_main_game_state->submenu;
    if (!submenu) {
        return;
    }
    const auto& bounds = submenu->bounds;
    if (IsCursorInRectangle(bounds.x, bounds.y, bounds.x + bounds.w - 1, bounds.y + bounds.h - 1)) {
        submenu->close_deadline.reset();
    } else if (!submenu->close_deadline) {
        submenu->close_deadline = w8_get_ticks() + 500;
    } else if (TimeUntilDeadline(*submenu->close_deadline) == 0) {
        DestroySubMenuControls();
    }
}

// FUNCTION: WIZ8 0x005957E0
void EnableSubMenuRegions(void)
{
    auto& submenu = g_main_game_state->submenu;
    if (submenu) submenu->input.setEnabled(true);
}

// FUNCTION: WIZ8 0x00595850
bool BuildSubMenuPanel(short notification)
{
    auto& submenu = g_main_game_state->submenu;
    submenu.reset();
    int left;
    W8SubMenuPage menu;
    int count;
    switch (notification) {
    case 6:
        menu = W8_SUBMENU_ATTACK;
        count = 5;
        left = g_submenu_button_positions[notification].x - 1;
        break;
    case 5:
        menu = W8_SUBMENU_DEFEND;
        count = 2;
        left = g_submenu_button_positions[notification].x - 1;
        break;
    case 3:
        menu = W8_SUBMENU_ITEMS;
        count = 3;
        left = g_submenu_button_positions[notification].x - 1;
        break;
    case 4:
        menu = W8_SUBMENU_SPELLS;
        count = 2;
        left = g_submenu_button_positions[notification].x - 1;
        break;
    case 9:
        menu = W8_SUBMENU_MOVE;
        count = 2;
        left = g_submenu_panel_button_positions[0].x - 1;
        break;
    default:
        return false;
    }
    std::vector<std::pair<int, W8SubMenuEntryState>> entries;
    for (int index = 0; index < count; ++index) {
        const auto state = GetSubMenuEntryState(menu, index, g_status.selected_character);
        if (state != W8_SUBMENU_ENTRY_UNAVAILABLE) {
            entries.emplace_back(index, state);
        }
    }
    if (entries.empty()) {
        return false;
    }
    // Berserk and Pray are mutually exclusive, leaving at most four action rows.
    constexpr std::array widths{0x2f, 0x42, 0x55, 0x67};
    W8_ASSERT(entries.size() <= widths.size());
    const int built = static_cast<int>(entries.size()) + 1;
    submenu = std::make_unique<MainGameSubMenu>();
    submenu->bounds = {left, 0x1c3, widths[entries.size() - 1] + 1, 0x1d};
    submenu->background = static_cast<int>(entries.size()) - 1;
    submenu->input.add(
        submenu->bounds,
        [](const UiPointerEvent& event) {
            if (event.action == UiPointerAction::Up && event.button == UiPointerButton::Right) {
                AfterUiDispatch(DestroySubMenuControls);
            }
            return UiResult::Handled;
        },
        -1);
    static constexpr void (*actions[5][5])() = {
        {SubMenuSelectAttack, SubMenuSelectBerserk, SubMenuSelectBreathe, SubMenuSelectTurnUndead,
         SubMenuSelectPray},
        {SubMenuSelectDefend, SubMenuSelectProtect},
        {SubMenuSelectEquip, SubMenuOpenUseItemView, SubMenuUseRecordedItem},
        {SubMenuOpenSpellView, SubMenuCastRecordedSpell},
        {SubMenuSelectWalk, SubMenuSelectRun}};
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto [entry, state] = entries[index];
        int message = g_submenu_entry_message_ids[menu * 5 + entry];
        int icon_delta = 0;
        if (menu == W8_SUBMENU_SPELLS && entry == 1) {
            message +=
                g_spell_records[g_status.buffers.XChar[g_status.selected_character].spell.detail]
                    .realm *
                7;
        } else if (menu == W8_SUBMENU_ATTACK && entry == 0) {
            const int base = GetAttackMenuWeaponOffset(
                g_status.buffers.Char[g_status.selected_character].Hand[0].weapon_skill);
            message += base * 7;
        }
        switch (state) {
        case W8_SUBMENU_ENTRY_UNUSABLE:
            message += 3;
            icon_delta = 2;
            break;
        case W8_SUBMENU_ENTRY_UNUSABLE_SELECTED:
            message += 4;
            icon_delta = 1;
            break;
        case W8_SUBMENU_ENTRY_USABLE_SELECTED:
            message += 1;
            icon_delta = 1;
            break;
        case W8_SUBMENU_ENTRY_USABLE:
            icon_delta = 2;
            break;
        case W8_SUBMENU_ENTRY_UNAVAILABLE:
            std::unreachable();
        }
        auto button = std::make_unique<W8ButtonWidget>(submenu->input);

        button->Configure(
            ButtonGraphics::Load(CatalogArtwork::Load(0x89, 0).spriteSheet(), -1, message,
                                 message + icon_delta, message, message + icon_delta),
            {.on_click = [action = actions[menu][entry]](auto*) { AfterUiDispatch(action); },
             .on_right_click = [](auto*) { AfterUiDispatch(DestroySubMenuControls); },
             .on_hover =
                 [menu, entry](bool hovered) {
                     if (menu == W8_SUBMENU_ITEMS && entry == 2 && hovered) {
                         g_ui_help_forced = true;
                     }
                 },
             .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
             .tooltip = g_submenu_entry_help_ids[menu * 5 + entry],
             .priority = 0,
             .menu_artwork = true});
        button->SetPosition(left + static_cast<int>(index) * 19 + 5, 0x1c8);
        button->Resize(18, 18);
        if (menu == W8_SUBMENU_ITEMS && entry == 2) {
            button->SetTooltip([] {
                const int slot = g_status.selected_character;
                if (!CanPartySlotUseRecordedItem(slot))
                    return std::string(g_localized_strings[0x5d]);
                const auto& row = g_status.buffers.XChar[slot];
                const auto* item = FindCharacterItemAt(slot, row.item_origin, row.item_slot);
                return fmt::format("{}: {}", g_localized_strings[0x5d],
                                   FormatItemDisplayName(item, false));
            });
        }
        submenu->buttons.push_back(std::move(button));
    }
    auto cancel = std::make_unique<W8ButtonWidget>(submenu->input);

    cancel->Configure(ButtonGraphics::Load(CatalogArtwork::Load(0x89, 0).spriteSheet(), -1, 0xbd,
                                           0xbf, 0xbd, 0xbf),
                      {.on_click = [](auto*) { AfterUiDispatch(DestroySubMenuControls); },
                       .on_right_click = [](auto*) { AfterUiDispatch(DestroySubMenuControls); },
                       .policy = {.input_rule = ButtonInputRule::PrimaryPressedState},
                       .tooltip = 0x11,
                       .priority = 0,
                       .menu_artwork = true});
    cancel->SetPosition(left + built * 19 - 14, 0x1c8);
    cancel->Resize(18, 18);
    submenu->buttons.push_back(std::move(cancel));
    g_level_block->combat_end_notification = notification;
    EnableSubMenuRegions();
    return true;
}

// FUNCTION: WIZ8 0x00596360
W8SubMenuEntryState CheckSubMenuActionUsable(int party_slot)
{
    unsigned char matches;

    matches = 0;
    if (g_level_block->selection_kind == g_status.buffers.XChar[party_slot].committed_action.kind &&
        !g_level_block->selection_settled) {
        matches = 1;
    }
    if (CharacterCanSwitchTo(party_slot, GetEditedActionContext(party_slot), true, false) &&
        SlotHasAnyValidTarget(party_slot)) {
        return static_cast<W8SubMenuEntryState>(matches);
    }
    return static_cast<W8SubMenuEntryState>(matches + W8_SUBMENU_ENTRY_UNUSABLE);
}

// FUNCTION: WIZ8 0x00595FE0
W8SubMenuEntryState GetSubMenuEntryState(short menu, short item, int party_slot)
{
    W8SubMenuEntryState state;
    W8Character* character;

    character = &g_status.buffers.Char[party_slot];
    state = W8_SUBMENU_ENTRY_UNAVAILABLE;
    MapSubMenuSelection(menu, item);
    switch (menu) {
    case W8_SUBMENU_ATTACK:
        switch (item) {
        case 1:
            if (CharacterHasTrait(character, W8_TRAIT_BERSERK)) {
                state = CheckSubMenuActionUsable(party_slot);
            }
            break;
        case 2:
            if (CharacterHasTrait(character, W8_TRAIT_BREATHE)) {
                state = CheckSubMenuActionUsable(party_slot);
            }
            break;
        case 4:
            if (CanPartySlotPray(party_slot)) {
                state = CheckSubMenuActionUsable(party_slot);
            }
            break;
        case 3:
            if (CanPartySlotTurnUndead(party_slot)) {
                state = CheckSubMenuActionUsable(party_slot);
            }
            break;
        case 0:
            state = CheckSubMenuActionUsable(party_slot);
            break;
        }
        break;
    case W8_SUBMENU_DEFEND:
        if (item >= 0 && item <= 1) {
            state = CheckSubMenuActionUsable(party_slot);
        }
        break;
    case W8_SUBMENU_ITEMS:
        if (item == 0) {
            if (gXStatus.fCombatMode) {
                state = CheckSubMenuActionUsable(party_slot);
            }
        } else if (item == 1) {
            if (CountUsableCharacterItems(character) != 0) {
                state = CheckSubMenuActionUsable(party_slot);
            }
        } else if (item == 2) {
            state = CanPartySlotUseRecordedItem(party_slot) ? W8_SUBMENU_ENTRY_USABLE
                                                            : W8_SUBMENU_ENTRY_UNAVAILABLE;
        }
        break;
    case W8_SUBMENU_SPELLS:
        if (item == 0) {
            if (CharacterHasCastableSpell(character)) {
                state = CheckSubMenuActionUsable(party_slot);
            }
        } else if (item == 1) {
            if (CanPartySlotCastRecordedSpell(party_slot)) {
                state = W8_SUBMENU_ENTRY_USABLE;
            } else {
                state = GetAffordableSpellPowerLevel(party_slot) != 0
                            ? W8_SUBMENU_ENTRY_USABLE
                            : W8_SUBMENU_ENTRY_UNAVAILABLE;
            }
        }
        break;
    case W8_SUBMENU_MOVE:
        if (item >= 0 && item <= 1) {
            state = W8_SUBMENU_ENTRY_USABLE;
        }
        break;
    }
    g_level_block->selection_kind = W8_ACTION_NONE;
    g_level_block->pending_action = -1;
    g_level_block->selection_settled = false;
    return state;
}

// FUNCTION: WIZ8 0x00596240
void MapSubMenuSelection(short menu, short item)
{
    W8ActionKind action;
    bool settled;

    action = W8_ACTION_NONE;
    settled = false;
    switch (menu) {
    case W8_SUBMENU_ATTACK:
        switch (item) {
        case 0:
            action = W8_ACTION_ATTACK;
            break;
        case 1:
            action = W8_ACTION_BERSERK;
            break;
        case 2:
            action = W8_ACTION_BREATHE;
            break;
        case 3:
            action = W8_ACTION_TURN_UNDEAD;
            break;
        case 4:
            action = W8_ACTION_PRAY;
            break;
        }
        break;
    case W8_SUBMENU_DEFEND:
        if (item == 0) {
            action = W8_ACTION_DEFEND;
        } else if (item == 1) {
            action = W8_ACTION_PROTECT;
        }
        break;
    case W8_SUBMENU_ITEMS:
        if (item == 0) {
            action = W8_ACTION_EQUIP;
        } else if (item == 1) {
            action = W8_ACTION_USE_ITEM;
        } else if (item == 2) {
            action = W8_ACTION_USE_ITEM;
            settled = true;
        }
        break;
    case W8_SUBMENU_SPELLS:
        if (item == 0) {
            action = W8_ACTION_CAST_SPELL;
        } else if (item == 1) {
            action = W8_ACTION_CAST_SPELL;
            settled = true;
        }
        break;
    case W8_SUBMENU_MOVE:
        if (item == 0) {
            action = W8_ACTION_WALK;
        } else if (item == 1) {
            action = W8_ACTION_RUN;
        }
        break;
    }
    g_level_block->selection_kind = action;
    g_level_block->pending_action = -1;
    g_level_block->selection_settled = settled;
}
