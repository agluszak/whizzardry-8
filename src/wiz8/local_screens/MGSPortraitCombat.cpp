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
#include "wiz8/local_screens/MGSButtons.h"
#include "wiz8/local_screens/MGSSpellCasting.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/npc_interaction_state.h"
#include "wiz8/rules/action_rules.h"
#include "wiz8/screen.h"
#include "wiz8/ui_input.h"

const char g_submenu_icons_path[] = "Data\\Main Interface\\icons_standard.sti";
const char g_submenu_combat_icons_path[] = "Data\\Main Interface\\icon_combat_toggle.sti";
/* The (x, y) of the nine bank buttons. */
const glm::ivec2 g_submenu_button_positions[9] = {
    {40, 452},  {70, 452},  {100, 452}, {130, 452}, {160, 452},
    {232, 452}, {262, 452}, {199, 452}, {200, 452},
};
/* The (x, y) of the two scroll arrows. */
const glm::ivec2 g_scroll_button_positions[2] = {{300, 456}, {323, 456}};
/* The (x, y) of the two panel buttons (close and formation). */
const glm::ivec2 g_submenu_panel_button_positions[2] = {{541, 452}, {571, 452}};
const char g_options_disk_path[] = "Data\\Main Interface\\options_disk.sti";
const glm::ivec2 g_options_disk_position = {0, 450};
const char g_attack_confirm_path[] = "Data\\Main Interface\\attack_confirm.sti";
const char g_combat_stop_path[] = "Data\\Main Interface\\combat_stop.sti";
const char g_cont_start_path[] = "Data\\Main Interface\\cont_start.sti";
const char g_cont_toggle_path[] = "Data\\Main Interface\\cont_toggle.sti";
const char g_cont_pending_path[] = "Data\\Main Interface\\cont_pending.sti";
/* All five combat-stance buttons share the same screen origin. */
const glm::ivec2 g_combat_stance_positions[5] = {
    {610, 450}, {610, 450}, {610, 450}, {610, 450}, {610, 450},
};
const char g_roof_buttons_path[] = "Data\\Main Interface\\main_roof_buttons.sti";
const glm::ivec2 g_roof_button_positions[3] = {{10, 1}, {39, 1}, {68, 1}};
const char g_layout_arrows_path[] = "Data\\Main Interface\\main_layout_arrows.sti";
/* Left column then right column; both columns share the same x in retail. */
const glm::ivec2 g_layout_arrow_positions[6] = {
    {0, 371}, {0, 394}, {0, 417}, {0, 371}, {0, 394}, {0, 417},
};
/* The (menu, item) keyed message indexes both menus build rows from. */
const short g_submenu_entry_message_ids[25] = {
    0,  63, 70,  77,  84, 91, 98, -1,  -1,  -1, 154, 161, 168,
    -1, -1, 105, 112, -1, -1, -1, 175, 182, -1, -1,  -1,
};
/* The matching help indexes. */
const int g_submenu_entry_help_ids[25] = {
    82, 83, 84, 85, 86, 87, 88, -1, -1, -1, 91, 92, -1,
    -1, -1, 89, -1, -1, -1, -1, 95, 94, -1, -1, -1,
};

void MainGameScreen::createSubMenuButtons()
{
    int index;

    hud_buttons.submenu[0].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x2b, 0x28, 0x29, 0x2a, 0x2c),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 button->SetPressed(false);
                 button->m_dirty = true;
                 if (game.activity.conversation) {
                     game.activity.conversation_aside = true;
                 }
                 RequestScreen({.id = W8_SCREEN_JOURNAL});
             },
         .tooltip = 0x42});
    hud_buttons.submenu[1].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x3, 0x0, 0x1, 0x2, 0x4),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 RequestCamp();
                 hud->repaint.layout = true;
                 DrawSubMenuCharacterAction();
             },
         .tooltip = 0x43});
    hud_buttons.submenu[2].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x8, 0x5, 0x6, 0x7, 0x9),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 game.search.toggle();
                 button->SetPressed(game.search.active());
                 hud->repaint.layout = true;
                 DrawSubMenuCharacterAction();
             },
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x44});
    hud_buttons.submenu[3].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x17, 0x14, 0x15, 0x16, 0x18),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 if (item_picker.get()) {
                     closeItemPicker();
                     return;
                 }
                 if (game.combat.has_value()) {
                     openSubMenu(3);
                     ReleaseUiCapture();
                     return;
                 }
                 openItemPicker(game.status.selected_character);
             },
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x45});
    hud_buttons.submenu[4].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x1c, 0x19, 0x1a, 0x1b, 0x1d),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 if (CharacterHasCastableSpell(
                         game.data, &game.party.characters[game.status.selected_character])) {
                     if (spell_picker.get()) {
                         closeSpellPicker();
                     } else {
                         openSpellPicker(game.status.selected_character);
                     }
                 }
             },
         .policy = {.primary_toggle = ButtonToggle::Press},
         .tooltip = 0x46});
    hud_buttons.submenu[5].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0x12, 0xf, 0x10, 0x11, 0x13),
        {.on_click = [this](W8ButtonWidget* button) { openSubMenu(5); }, .tooltip = 0x4a});
    hud_buttons.submenu[6].Configure(
        ButtonGraphics::Load(g_submenu_icons_path, 0xd, 0xa, 0xb, 0xc, 0xe),
        {.on_click = [this](W8ButtonWidget* button) { openSubMenu(6); }, .tooltip = 0x49});
    hud_buttons.submenu[7].Configure(
        ButtonGraphics::Load(g_submenu_combat_icons_path, 0x3, 0x0, 0x1, 0x2, 0x2),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 ToggleCombatMode();
                 hud->repaint.layout = true;
                 DrawSubMenuCharacterAction();
             },
         .tooltip = 0x47});
    hud_buttons.submenu[8].Configure(
        ButtonGraphics::Load(g_submenu_combat_icons_path, 0x7, 0x4, 0x5, 0x6, 0x6),
        {.on_click =
             [this](W8ButtonWidget* button) {
                 ToggleCombatMode();
                 hud->repaint.layout = true;
                 DrawSubMenuCharacterAction();
             },
         .tooltip = 0x48});
    for (index = 0; index < 9; ++index) {
        hud_buttons.submenu[index].SetPosition(g_submenu_button_positions[index].x,
                                               g_submenu_button_positions[index].y);
    }
}

void MainGameScreen::updateSubMenuButton(int index)
{
    auto& button = hud_buttons.submenu[index];
    const bool in_combat = game.combat.has_value();
    if (index == 5 || index == 6 || index == 8) button.SetVisible(in_combat);
    if (index == 7) button.SetVisible(!in_combat);
    if ((game.activity.conversation &&
         (index != 0 || IsNpcDialogueCursorActive() || game.npc_interaction.scripted_dialogue)) ||
        lock_active || trap_active || formation_panel_open || (item_picker && index != 3) ||
        (spell_picker && index != 4) || game.surprise_possible ||
        game.activity.conversation_aside || (in_combat && !game.combat->round_active) || submenu) {
        button.SetEnabled(false);
        return;
    }
    const int slot = game.status.selected_character;
    switch (index) {
    case 0:
    case 1:
    case 7:
        button.SetEnabled(!in_combat);
        break;
    case 2:
        button.SetEnabled(!in_combat);
        button.SetPressed(game.search.active());
        break;
    case 3:
        if (!IsPartySlotEligible(slot)) {
            button.SetEnabled(false);
            break;
        }
        button.SetEnabled(true);
        button.SetPressed(bool(item_picker));
        break;
    case 4:
        button.SetEnabled(IsPartySlotEligible(slot) &&
                          CharacterHasCastableSpell(game.data, &game.party.characters[slot]));
        button.SetPressed(bool(spell_picker));
        break;
    case 5:
    case 6:
        button.SetEnabled(in_combat && IsPartySlotEligible(slot));
        break;
    case 8:
        button.SetEnabled(in_combat);
        break;
    }
}
