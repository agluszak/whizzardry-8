

#include "wiz8/local_screens/MGSPortraitCombat.h"

#include "layouts/main_game_screen.h"
#include "surrender/srMath.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/screen_state.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/ui_input.h"
#include "wiz8/xstatus.h"

// GLOBAL: WIZ8 0x0064C238
char g_submenu_icons_path[] = "Data\\Main Interface\\icons_standard.sti";
// GLOBAL: WIZ8 0x0064C260
char g_submenu_combat_icons_path[] = "Data\\Main Interface\\icon_combat_toggle.sti";
/* The (x, y) of the nine bank buttons. */
// GLOBAL: WIZ8 0x0064C290
srVector2i g_submenu_button_positions[9] = {
    {40, 452},  {70, 452},  {100, 452}, {130, 452}, {160, 452},
    {232, 452}, {262, 452}, {199, 452}, {200, 452},
};
/* The (x, y) of the two scroll arrows. */
// GLOBAL: WIZ8 0x0064C330
srVector2i g_scroll_button_positions[2] = {{300, 456}, {323, 456}};
/* The (x, y) of the two panel buttons (close and formation). */
// GLOBAL: WIZ8 0x0064C378
srVector2i g_submenu_panel_button_positions[2] = {{541, 452}, {571, 452}};
// GLOBAL: WIZ8 0x0064C388
char g_options_disk_path[] = "Data\\Main Interface\\options_disk.sti";
// GLOBAL: WIZ8 0x0064C3B0
srVector2i g_options_disk_position = {0, 450};
// GLOBAL: WIZ8 0x0064C3B8
char g_attack_confirm_path[] = "Data\\Main Interface\\attack_confirm.sti";
// GLOBAL: WIZ8 0x0064C3E0
char g_combat_stop_path[] = "Data\\Main Interface\\combat_stop.sti";
// GLOBAL: WIZ8 0x0064C404
char g_cont_start_path[] = "Data\\Main Interface\\cont_start.sti";
// GLOBAL: WIZ8 0x0064C428
char g_cont_toggle_path[] = "Data\\Main Interface\\cont_toggle.sti";
// GLOBAL: WIZ8 0x0064C44C
char g_cont_pending_path[] = "Data\\Main Interface\\cont_pending.sti";
/* All five combat-stance buttons share the same screen origin. */
// GLOBAL: WIZ8 0x0064C478
srVector2i g_combat_stance_positions[5] = {
    {610, 450}, {610, 450}, {610, 450}, {610, 450}, {610, 450},
};
// GLOBAL: WIZ8 0x0064C4A0
char g_roof_buttons_path[] = "Data\\Main Interface\\main_roof_buttons.sti";
// GLOBAL: WIZ8 0x0064C4D0
srVector2i g_roof_button_positions[3] = {{10, 1}, {39, 1}, {68, 1}};
// GLOBAL: WIZ8 0x0064C4E8
char g_layout_arrows_path[] = "Data\\Main Interface\\main_layout_arrows.sti";
/* Left column then right column; both columns share the same x in retail. */
// GLOBAL: WIZ8 0x0064C518
srVector2i g_layout_arrow_positions[6] = {
    {0, 371}, {0, 394}, {0, 417}, {0, 371}, {0, 394}, {0, 417},
};
/* The (menu, item) keyed message indexes both menus build rows from. */
// GLOBAL: WIZ8 0x0064C548
short g_submenu_entry_message_ids[25] = {
    0,  63, 70,  77,  84, 91, 98, -1,  -1,  -1, 154, 161, 168,
    -1, -1, 105, 112, -1, -1, -1, 175, 182, -1, -1,  -1,
};
/* The matching help indexes. */
// GLOBAL: WIZ8 0x0064C57C
int g_submenu_entry_help_ids[25] = {
    82, 83, 84, 85, 86, 87, 88, -1, -1, -1, 91, 92, -1,
    -1, -1, 89, -1, -1, -1, -1, 95, 94, -1, -1, -1,
};

static void SubMenuButtonPendingScreen(W8ButtonWidget* button);
static void SubMenuButtonSurprise(W8ButtonWidget* button);
static void SubMenuButtonToggleFlag(W8ButtonWidget* button);
static void SubMenuButtonUseItem(W8ButtonWidget* button);
static void SubMenuButtonSpellView(W8ButtonWidget* button);
static void SubMenuButtonOpenMenu0(W8ButtonWidget* button);
static void SubMenuButtonOpenMenu1(W8ButtonWidget* button);
static void SubMenuButtonToggleCombat(W8ButtonWidget* button);

// FUNCTION: WIZ8 0x00594AF0
unsigned char CreateSubMenuButtons(void)
{
    int index;

    g_main_game_buttons.submenu[0].Configure(
        g_submenu_icons_path, {0x2b, 0x28, 0x29, 0x2a, 0x2c},
        {.on_click = SubMenuButtonPendingScreen, .tooltip = 0x42});
    g_main_game_buttons.submenu[1].Configure(g_submenu_icons_path, {0x3, 0x0, 0x1, 0x2, 0x4},
                                             {.on_click = SubMenuButtonSurprise, .tooltip = 0x43});
    g_main_game_buttons.submenu[2].Configure(
        g_submenu_icons_path, {0x8, 0x5, 0x6, 0x7, 0x9},
        {.on_click = SubMenuButtonToggleFlag, .toggles = true, .tooltip = 0x44});
    g_main_game_buttons.submenu[3].Configure(
        g_submenu_icons_path, {0x17, 0x14, 0x15, 0x16, 0x18},
        {.on_click = SubMenuButtonUseItem, .toggles = true, .tooltip = 0x45});
    g_main_game_buttons.submenu[4].Configure(
        g_submenu_icons_path, {0x1c, 0x19, 0x1a, 0x1b, 0x1d},
        {.on_click = SubMenuButtonSpellView, .toggles = true, .tooltip = 0x46});
    g_main_game_buttons.submenu[5].Configure(g_submenu_icons_path, {0x12, 0xf, 0x10, 0x11, 0x13},
                                             {.on_click = SubMenuButtonOpenMenu1, .tooltip = 0x4a});
    g_main_game_buttons.submenu[6].Configure(g_submenu_icons_path, {0xd, 0xa, 0xb, 0xc, 0xe},
                                             {.on_click = SubMenuButtonOpenMenu0, .tooltip = 0x49});
    g_main_game_buttons.submenu[7].Configure(
        g_submenu_combat_icons_path, {0x3, 0x0, 0x1, 0x2, 0x2},
        {.on_click = SubMenuButtonToggleCombat, .tooltip = 0x47});
    g_main_game_buttons.submenu[8].Configure(
        g_submenu_combat_icons_path, {0x7, 0x4, 0x5, 0x6, 0x6},
        {.on_click = SubMenuButtonToggleCombat, .tooltip = 0x48});
    for (index = 0; index < 9; ++index) {
        g_main_game_buttons.submenu[index].SetPosition(g_submenu_button_positions[index].x,
                                                       g_submenu_button_positions[index].y);
    }
    return 1;
}

// FUNCTION: WIZ8 0x00594D20
void UpdateSubMenuButton(int index)
{
    switch (index) {
    case 5:
    case 6:
    case 8:
        g_main_game_buttons.submenu[index].SetVisible(gXStatus.fCombatMode);
        break;
    case 7:
        g_main_game_buttons.submenu[index].SetVisible(!gXStatus.fCombatMode);
        break;
    }
    if (gXStatus.fNpcDialogueMode &&
        (index != 0 || IsNpcDialogueCursorActive() != 0 || CanOpenNpcDialogue())) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (gXStatus.fLockInteractMode || gXStatus.fTrapInteractMode || gXStatus.fReviewCharacterMode) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (gXStatus.fItemSelectMode && index != 3) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (gXStatus.fSpellCastMode && index != 4) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (gXStatus.fSurprisePossible || gXStatus.fCampMode) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (gXStatus.fCombatMode && !g_combat_state->round_active) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (g_level_block->combat_end_notification != -1) {
        g_main_game_buttons.submenu[index].SetEnabled(false);
        return;
    }
    switch (index) {
    case 0:
        if (gXStatus.fCombatMode) {
            g_main_game_buttons.submenu[0].SetEnabled(false);
        } else {
            g_main_game_buttons.submenu[0].SetEnabled(true);
        }
        break;
    case 1:
        if (gXStatus.fCombatMode) {
            g_main_game_buttons.submenu[1].SetEnabled(false);
        } else {
            g_main_game_buttons.submenu[1].SetEnabled(true);
        }
        break;
    case 2:
        if (gXStatus.fCombatMode) {
            g_main_game_buttons.submenu[2].SetEnabled(false);
        } else {
            g_main_game_buttons.submenu[2].SetEnabled(true);
        }
        /* Two armed, not one `IsPressed() != (mode != 0)`. The retail tests the
           mode first and then branches on IsPressed in each arm, giving two
           IsPressed and two SetPressed calls for this case alone
           (0x00594E92/0x00594EA7 under search_mode, 0x00594EAE/0x00594EC3
           under its negation). */
        if (g_status.search_mode != 0) {
            if (g_main_game_buttons.submenu[2].IsPressed() == 0) {
                g_main_game_buttons.submenu[2].SetPressed(true);
            }
        } else {
            if (g_main_game_buttons.submenu[2].IsPressed()) {
                g_main_game_buttons.submenu[2].SetPressed(false);
            }
        }
        break;
    case 3:
        if (!IsPartySlotEligible(g_status.selected_character)) {
            g_main_game_buttons.submenu[3].SetEnabled(false);
            break;
        }
        g_main_game_buttons.submenu[3].SetEnabled(true);
        /* As case 2, with fItemSelectMode: 0x00594F07/0x00594F1C then
           0x00594F23/0x00594F38. */
        if (gXStatus.fItemSelectMode) {
            if (g_main_game_buttons.submenu[3].IsPressed() == 0) {
                g_main_game_buttons.submenu[3].SetPressed(true);
            }
        } else {
            if (g_main_game_buttons.submenu[3].IsPressed()) {
                g_main_game_buttons.submenu[3].SetPressed(false);
            }
        }
        break;
    case 4:
        g_main_game_buttons.submenu[4].SetEnabled(
            IsPartySlotEligible(g_status.selected_character) &&
            CharacterHasCastableSpell(&g_status.buffers.Char[g_status.selected_character]));
        /* As case 2, with fSpellCastMode: 0x00594FA0/0x00594FB5 then
           0x00594FBC/0x00594FCD. This case's SetEnabled stays a single call -
           the retail's `? :` collapses into one at 0x00594F8C. */
        if (gXStatus.fSpellCastMode) {
            if (g_main_game_buttons.submenu[4].IsPressed() == 0) {
                g_main_game_buttons.submenu[4].SetPressed(true);
            }
        } else {
            if (g_main_game_buttons.submenu[4].IsPressed()) {
                g_main_game_buttons.submenu[4].SetPressed(false);
            }
        }
        break;
    case 5:
    case 6:
        g_main_game_buttons.submenu[index].SetEnabled(gXStatus.fCombatMode);
        if (!IsPartySlotEligible(g_status.selected_character)) {
            g_main_game_buttons.submenu[index].SetEnabled(false);
        }
        break;
    case 7:
        if (gXStatus.fCombatMode) {
            g_main_game_buttons.submenu[7].SetEnabled(false);
        } else {
            g_main_game_buttons.submenu[7].SetEnabled(true);
        }
        break;
    case 8:
        if (gXStatus.fCombatMode) {
            g_main_game_buttons.submenu[8].SetEnabled(true);
        } else {
            g_main_game_buttons.submenu[8].SetEnabled(false);
        }
        break;
    }
}

// FUNCTION: WIZ8 0x00595090
static void SubMenuButtonPendingScreen(W8ButtonWidget* button)
{
    button->SetPressed(false);
    button->m_dirty = true;
    if (gXStatus.fNpcDialogueMode) {
        gXStatus.fCampMode = true;
    }
    SetPendingScreenState(W8_SCREEN_JOURNAL);
}

// FUNCTION: WIZ8 0x005950C0
static void SubMenuButtonSurprise(W8ButtonWidget* button)
{
    RequestCamp();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    DrawSubMenuCharacterAction();
}

// FUNCTION: WIZ8 0x005950E0
static void SubMenuButtonToggleFlag(W8ButtonWidget* button)
{
    ToggleSearchMode();
    button->SetPressed(g_status.search_mode ? 1 : 0);
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    DrawSubMenuCharacterAction();
}

// FUNCTION: WIZ8 0x00595110
static void SubMenuButtonUseItem(W8ButtonWidget* button)
{
    if (gXStatus.fItemSelectMode) {
        CloseUseItemSelectView();
        return;
    }
    if (gXStatus.fCombatMode) {
        OpenSubMenuPanel(3);
        ReleaseUiCapture();
        return;
    }
    OpenUseItemSelectView(g_status.selected_character);
}

// FUNCTION: WIZ8 0x00595280
static void SubMenuButtonSpellView(W8ButtonWidget* button)
{
    if (CharacterHasCastableSpell(&g_status.buffers.Char[g_status.selected_character])) {
        if (gXStatus.fSpellCastMode) {
            CloseSpellCastingView();
        } else {
            OpenSpellCastingView(g_status.selected_character);
        }
    }
}

// FUNCTION: WIZ8 0x005952D0
static void SubMenuButtonOpenMenu0(W8ButtonWidget* button)
{
    OpenSubMenuPanel(6);
}

// FUNCTION: WIZ8 0x00595410
static void SubMenuButtonOpenMenu1(W8ButtonWidget* button)
{
    OpenSubMenuPanel(5);
}

// FUNCTION: WIZ8 0x00595550
static void SubMenuButtonToggleCombat(W8ButtonWidget* button)
{
    ToggleCombatMode();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
    DrawSubMenuCharacterAction();
}

/* Drop the combat-end notification and tear down the panel and its rows.
   Retail expands this operation throughout the submenu callbacks; callers
   share this body and leave the inlining decision to the compiler. */
// FUNCTION: WIZ8 0x00595570
void DestroySubMenuControls(void)
{
    SetSubMenuButtonTooltips(1);
    g_level_block->combat_end_notification = -1;
    g_submenu_entry_count = 0;
    DestroySubMenuPanel();
    RequestRedraw(W8_MAIN_REDRAW_LAYOUT);
}

// FUNCTION: WIZ8 0x00595600
void ReopenSubMenuPanel(void)
{
    short notification = g_level_block->combat_end_notification;

    if (notification == -1) {
        return;
    }
    OpenSubMenuPanel(notification);
}
