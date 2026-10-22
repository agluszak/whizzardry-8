#include "wiz8/local_screens/MGSPortraitCombat.h"

#include "glm/ext/vector_int2.hpp"
#include "layouts/main_game_screen.h"
#include "wiz8/button_interaction.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/fight.h"
#include "wiz8/dialog_code/ButtonAppearance.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

char g_submenu_icons_path[] = "Data\\Main Interface\\icons_standard.sti";
char g_submenu_combat_icons_path[] = "Data\\Main Interface\\icon_combat_toggle.sti";
/* The (x, y) of the nine bank buttons. */
glm::ivec2 g_submenu_button_positions[9] = {
    {40, 452},  {70, 452},  {100, 452}, {130, 452}, {160, 452},
    {232, 452}, {262, 452}, {199, 452}, {200, 452},
};
/* The (x, y) of the two scroll arrows. */
glm::ivec2 g_scroll_button_positions[2] = {{300, 456}, {323, 456}};
/* The (x, y) of the two panel buttons (close and formation). */
glm::ivec2 g_submenu_panel_button_positions[2] = {{541, 452}, {571, 452}};
char g_options_disk_path[] = "Data\\Main Interface\\options_disk.sti";
glm::ivec2 g_options_disk_position = {0, 450};
char g_attack_confirm_path[] = "Data\\Main Interface\\attack_confirm.sti";
char g_combat_stop_path[] = "Data\\Main Interface\\combat_stop.sti";
char g_cont_start_path[] = "Data\\Main Interface\\cont_start.sti";
char g_cont_toggle_path[] = "Data\\Main Interface\\cont_toggle.sti";
char g_cont_pending_path[] = "Data\\Main Interface\\cont_pending.sti";
/* All five combat-stance buttons share the same screen origin. */
glm::ivec2 g_combat_stance_positions[5] = {
    {610, 450}, {610, 450}, {610, 450}, {610, 450}, {610, 450},
};
char g_roof_buttons_path[] = "Data\\Main Interface\\main_roof_buttons.sti";
glm::ivec2 g_roof_button_positions[3] = {{10, 1}, {39, 1}, {68, 1}};
char g_layout_arrows_path[] = "Data\\Main Interface\\main_layout_arrows.sti";
/* Left column then right column; both columns share the same x in retail. */
glm::ivec2 g_layout_arrow_positions[6] = {
    {0, 371}, {0, 394}, {0, 417}, {0, 371}, {0, 394}, {0, 417},
};
/* The (menu, item) keyed message indexes both menus build rows from. */
short g_submenu_entry_message_ids[25] = {
    0,  63, 70,  77,  84, 91, 98, -1,  -1,  -1, 154, 161, 168,
    -1, -1, 105, 112, -1, -1, -1, 175, 182, -1, -1,  -1,
};
/* The matching help indexes. */
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

void CreateSubMenuButtons()
{
    int index;

    MainGame()->hud_buttons.submenu[0].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x2b, 0x28, 0x29, 0x2a, 0x2c),
        {.on_click = SubMenuButtonPendingScreen, .tooltip = 0x42});
    MainGame()->hud_buttons.submenu[1].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x3, 0x0, 0x1, 0x2, 0x4),
        {.on_click = SubMenuButtonSurprise, .tooltip = 0x43});
    MainGame()->hud_buttons.submenu[2].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x8, 0x5, 0x6, 0x7, 0x9),
        {.on_click = SubMenuButtonToggleFlag,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x44});
    MainGame()->hud_buttons.submenu[3].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x17, 0x14, 0x15, 0x16, 0x18),
        {.on_click = SubMenuButtonUseItem,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x45});
    MainGame()->hud_buttons.submenu[4].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x1c, 0x19, 0x1a, 0x1b, 0x1d),
        {.on_click = SubMenuButtonSpellView,
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x46});
    MainGame()->hud_buttons.submenu[5].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x12, 0xf, 0x10, 0x11, 0x13),
        {.on_click = SubMenuButtonOpenMenu1, .tooltip = 0x4a});
    MainGame()->hud_buttons.submenu[6].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0xd, 0xa, 0xb, 0xc, 0xe),
        {.on_click = SubMenuButtonOpenMenu0, .tooltip = 0x49});
    MainGame()->hud_buttons.submenu[7].Configure(
        ButtonGraphics::Load(g_submenu_combat_icons_path, 0x3, 0x0, 0x1, 0x2, 0x2),
        {.on_click = SubMenuButtonToggleCombat, .tooltip = 0x47});
    MainGame()->hud_buttons.submenu[8].Configure(
        ButtonGraphics::Load(g_submenu_combat_icons_path, 0x7, 0x4, 0x5, 0x6, 0x6),
        {.on_click = SubMenuButtonToggleCombat, .tooltip = 0x48});
    for (index = 0; index < 9; ++index) {
        MainGame()->hud_buttons.submenu[index].SetPosition(g_submenu_button_positions[index].x,
                                                           g_submenu_button_positions[index].y);
    }
}

void UpdateSubMenuButton(int index)
{
    switch (index) {
    case 5:
    case 6:
    case 8:
        MainGame()->hud_buttons.submenu[index].SetVisible(CurrentGame().combat.has_value());
        break;
    case 7:
        MainGame()->hud_buttons.submenu[index].SetVisible(!CurrentGame().combat.has_value());
        break;
    }
    if (CurrentGame().activity.conversation &&
        (index != 0 || IsNpcDialogueCursorActive() != 0 ||
         (CurrentGame().activity.conversation &&
          CurrentGame().npc_interaction.scripted_dialogue))) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (IsLockInteractionActive() || IsTrapInteractionActive() || FormationPanelOpen()) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (UseItemPicker() && index != 3) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (SpellPicker() && index != 4) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (CurrentGame().surprise_possible || CurrentGame().activity.conversation_aside) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (CurrentGame().combat.has_value() && !CurrentGame().combat->round_active) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    if (MainGameHud()->open_submenu) {
        MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        return;
    }
    switch (index) {
    case 0:
        if (CurrentGame().combat.has_value()) {
            MainGame()->hud_buttons.submenu[0].SetEnabled(false);
        } else {
            MainGame()->hud_buttons.submenu[0].SetEnabled(true);
        }
        break;
    case 1:
        if (CurrentGame().combat.has_value()) {
            MainGame()->hud_buttons.submenu[1].SetEnabled(false);
        } else {
            MainGame()->hud_buttons.submenu[1].SetEnabled(true);
        }
        break;
    case 2:
        if (CurrentGame().combat.has_value()) {
            MainGame()->hud_buttons.submenu[2].SetEnabled(false);
        } else {
            MainGame()->hud_buttons.submenu[2].SetEnabled(true);
        }
        /* Two armed, not one `IsPressed() != (mode != 0)`. The retail tests the
           mode first and then branches on IsPressed in each arm, giving two
           IsPressed and two SetPressed calls for this case alone
           (0x00594E92/0x00594EA7 under search_mode, 0x00594EAE/0x00594EC3
           under its negation). */
        if (CurrentGame().status.search_mode) {
            if (MainGame()->hud_buttons.submenu[2].IsPressed() == 0) {
                MainGame()->hud_buttons.submenu[2].SetPressed(true);
            }
        } else {
            if (MainGame()->hud_buttons.submenu[2].IsPressed()) {
                MainGame()->hud_buttons.submenu[2].SetPressed(false);
            }
        }
        break;
    case 3:
        if (!IsPartySlotEligible(CurrentGame().status.selected_character)) {
            MainGame()->hud_buttons.submenu[3].SetEnabled(false);
            break;
        }
        MainGame()->hud_buttons.submenu[3].SetEnabled(true);
        /* As case 2, with an item draft: 0x00594F07/0x00594F1C then
           0x00594F23/0x00594F38. */
        if (UseItemPicker()) {
            if (MainGame()->hud_buttons.submenu[3].IsPressed() == 0) {
                MainGame()->hud_buttons.submenu[3].SetPressed(true);
            }
        } else {
            if (MainGame()->hud_buttons.submenu[3].IsPressed()) {
                MainGame()->hud_buttons.submenu[3].SetPressed(false);
            }
        }
        break;
    case 4:
        MainGame()->hud_buttons.submenu[4].SetEnabled(
            IsPartySlotEligible(CurrentGame().status.selected_character) &&
            CharacterHasCastableSpell(
                &CurrentGame().party.characters[CurrentGame().status.selected_character]));
        /* As case 2, with the spell picker open: 0x00594FA0/0x00594FB5 then
           0x00594FBC/0x00594FCD. This case's SetEnabled stays a single call -
           the retail's `? :` collapses into one at 0x00594F8C. */
        if (SpellPicker()) {
            if (MainGame()->hud_buttons.submenu[4].IsPressed() == 0) {
                MainGame()->hud_buttons.submenu[4].SetPressed(true);
            }
        } else {
            if (MainGame()->hud_buttons.submenu[4].IsPressed()) {
                MainGame()->hud_buttons.submenu[4].SetPressed(false);
            }
        }
        break;
    case 5:
    case 6:
        MainGame()->hud_buttons.submenu[index].SetEnabled(CurrentGame().combat.has_value());
        if (!IsPartySlotEligible(CurrentGame().status.selected_character)) {
            MainGame()->hud_buttons.submenu[index].SetEnabled(false);
        }
        break;
    case 7:
        if (CurrentGame().combat.has_value()) {
            MainGame()->hud_buttons.submenu[7].SetEnabled(false);
        } else {
            MainGame()->hud_buttons.submenu[7].SetEnabled(true);
        }
        break;
    case 8:
        if (CurrentGame().combat.has_value()) {
            MainGame()->hud_buttons.submenu[8].SetEnabled(true);
        } else {
            MainGame()->hud_buttons.submenu[8].SetEnabled(false);
        }
        break;
    }
}

static void SubMenuButtonPendingScreen(W8ButtonWidget* button)
{
    button->SetPressed(false);
    button->m_dirty = true;
    if (CurrentGame().activity.conversation) {
        CurrentGame().activity.conversation_aside = true;
    }
    RequestScreen({.id = W8_SCREEN_JOURNAL});
}

static void SubMenuButtonSurprise(W8ButtonWidget* button)
{
    RequestCamp();
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    DrawSubMenuCharacterAction();
}

static void SubMenuButtonToggleFlag(W8ButtonWidget* button)
{
    ToggleSearchMode();
    button->SetPressed(CurrentGame().status.search_mode ? 1 : 0);
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    DrawSubMenuCharacterAction();
}

static void SubMenuButtonUseItem(W8ButtonWidget* button)
{
    if (UseItemPicker()) {
        MainGame()->closeItemPicker();
        return;
    }
    if (CurrentGame().combat.has_value()) {
        OpenSubMenuPanel(3);
        ReleaseUiCapture();
        return;
    }
    MainGame()->openItemPicker(CurrentGame().status.selected_character);
}

static void SubMenuButtonSpellView(W8ButtonWidget* button)
{
    if (CharacterHasCastableSpell(
            &CurrentGame().party.characters[CurrentGame().status.selected_character])) {
        if (SpellPicker()) {
            MainGame()->closeSpellPicker();
        } else {
            MainGame()->openSpellPicker(CurrentGame().status.selected_character);
        }
    }
}

static void SubMenuButtonOpenMenu0(W8ButtonWidget* button)
{
    OpenSubMenuPanel(6);
}

static void SubMenuButtonOpenMenu1(W8ButtonWidget* button)
{
    OpenSubMenuPanel(5);
}

static void SubMenuButtonToggleCombat(W8ButtonWidget* button)
{
    ToggleCombatMode();
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
    DrawSubMenuCharacterAction();
}

/* Drop the combat-end notification and tear down the panel and its rows.
   Retail expands this operation throughout the submenu callbacks; callers
   share this body and leave the inlining decision to the compiler. */
void DestroySubMenuControls(void)
{
    SetSubMenuButtonTooltips(1);
    MainGameHud()->open_submenu.reset();
    DestroySubMenuPanel();
    if (auto* hud = MainGameHud()) hud->repaint.layout = true;
}

void ReopenSubMenuPanel(void)
{
    if (const auto open = MainGameHud()->open_submenu) OpenSubMenuPanel(*open);
}
