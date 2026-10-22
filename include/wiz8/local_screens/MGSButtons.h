#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <SDL3/SDL_rect.h>

#include "wiz8/character_skills.h"
#include "wiz8/platform/input.h"

/* Local Screens\MGSButtons.cpp. The combat sub-menu's panel, its five text
   rows and the two scroll-arrow buttons: construction, teardown and the
   per-frame name/action caption draw. The shared W8SubMenu enums and the
   portrait-combat unit's const tables live in MGSPortraitCombat.h. */

#include "wiz8/local_code/ButtonWidget.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/ui_layer.h"

// Owned by MainGameScreen while the screen is active. The layer precedes
// the buttons so input registrations are removed before the layer is destroyed.
// Modal submenu controls retain their independently activated layer.
struct MainGameHudButtons {
    // Screen state constructs HUD before world input at the same order.
    // The submenu (0x27) stays above both.
    UiLayer input{UiLayerOrder::GameplayOverlays};
    std::array<W8ButtonWidget, 2> submenu_scroll{W8ButtonWidget{input}, W8ButtonWidget{input}};
    std::array<W8ButtonWidget, 2> submenu_panel{W8ButtonWidget{input}, W8ButtonWidget{input}};
    std::array<W8ButtonWidget, 6> layout_arrows{W8ButtonWidget{input}, W8ButtonWidget{input},
                                                W8ButtonWidget{input}, W8ButtonWidget{input},
                                                W8ButtonWidget{input}, W8ButtonWidget{input}};
    std::array<W8ButtonWidget, 5> combat_stance{W8ButtonWidget{input}, W8ButtonWidget{input},
                                                W8ButtonWidget{input}, W8ButtonWidget{input},
                                                W8ButtonWidget{input}};
    std::array<W8ButtonWidget, 9> submenu{
        W8ButtonWidget{input}, W8ButtonWidget{input}, W8ButtonWidget{input},
        W8ButtonWidget{input}, W8ButtonWidget{input}, W8ButtonWidget{input},
        W8ButtonWidget{input}, W8ButtonWidget{input}, W8ButtonWidget{input}};
    std::array<W8ButtonWidget, 3> roof{W8ButtonWidget{input}, W8ButtonWidget{input},
                                       W8ButtonWidget{input}};
    W8ButtonWidget options_disk{input};
};

// An active submenu owns its independently enabled input and button rows.
struct MainGameSubMenu {
    UiLayer input{UiLayerOrder::Submenu};
    SDL_Rect bounds{};
    int background = 0;
    std::optional<uint64_t> close_deadline;
    std::vector<std::unique_ptr<W8ButtonWidget>> buttons;
};

/* The action-kind message indexes the caption draw maps through. */
extern unsigned short g_action_kind_message_ids[12];

/* Redraw the sub-menu panel when its repaint was requested; otherwise only its
   changed buttons. */
void RefreshSubMenuPanel(bool invalidate);
/* Close and rebuild the sub menu around the saved combat-end notification. */
void ResetSubMenuPanel(void);
/* Draw roof chrome and the three buttons; the same repaint draws the combat
   progress gauge. */
void RedrawRoofButtons(void);
// Sync roof-button visibility/enabled/pressed state.
void UpdateRoofButtons(void);
/* Press the roof button that matches g_settings.main_ui_mode. */
void SyncRoofButtonPressedState(void);
// Draw layout-arrow chrome and the visible raise/lower arrows.
void RedrawLayoutArrowButtons(void);
/* Sync the six raise/lower arrows against the live action-panel /
   formation-board / radar-map visibility flags. */
void UpdateLayoutArrowButtons(void);
/* MainGameScreenEnter's interface-button bank: submenu, scroll, panel,
   options disk, combat stance, roof, and layout arrows. */
void ConfigureMainGameHudButtons(void);
/* Draw the selected character's name/profession line and the caption for
   their queued action. */
void DrawSubMenuCharacterAction(void);
// Sync enable/press of the two sub-menu panel buttons.
void UpdateSubMenuPanelButtons(void);
/* Redraw the bottom sub-menu chrome, bank buttons, panel buttons, character
   action caption, and scroll arrows. */
void RedrawSubMenuButtons(void);
// Redraw the options-disk button.
void RedrawOptionsDiskButton(void);
// Draw the combat-stance chrome and the live stance button.
void RedrawCombatStanceButtons(void);
/* Show the active continuous/manual combat-stance button for the live mode. */
void UpdateCombatStanceButtons(void);
/* The presentation update's button sync: bank buttons, scroll arrows,
   panel/options/stance/roof buttons and the layout arrows. */
void UpdateMainGameButtons(void);
/* The draw's per-frame button refresh: every changed button except the layout
   arrows, which the layout repaint draws. */
void RefreshMainGameButtons(void);
/* While the combat-end submenu is up and the cursor has left its row band,
   run a 500 ms countdown, then tear the panel and rows down. */
void UpdateSubMenuAutoClose(void);

/* Move selected_character to the next eligible party slot (0 steps toward
   slot 0, 1 toward slot 7), wrapping, and repaint the portraits involved. */
void ScrollSubMenuCharacter(signed char direction);

/* The per-row select callbacks AssignSubMenuCallback installs. */
void SubMenuSelectAttack(void);
void SubMenuSelectBerserk(void);
void SubMenuSelectBreathe(void);
void SubMenuSelectTurnUndead(void);
void SubMenuSelectPray(void);
void SubMenuSelectDefend(void);
void SubMenuSelectProtect(void);
void SubMenuSelectEquip(void);
void SubMenuOpenUseItemView(void);
void SubMenuUseRecordedItem(void);
void SubMenuOpenSpellView(void);
void SubMenuCastRecordedSpell(void);
void SubMenuSelectRun(void);
void SubMenuSelectWalk(void);

/* Applies SetTooltipEnabled to all nine bank buttons, both scroll arrows and
   both panel buttons; the submenu rebuild paths call it around teardown. */
void SetSubMenuButtonTooltips(int enabled);
/* Region callback for the sub-menu background: right-up tears the panel down. */
/* Enable/disable the scroll, roof and layout button
   banks around the surprise/camp sequence. */
void EnableMenuButtonBanks(void);
void DisableMenuButtonBanks(void);
/* Region callback the five sub-menu rows share. */

/* Enable the panel region set and one input region per live row. */
void EnableSubMenuRegions(void);
/* Descriptive name for the complete submenu opening operation expanded in
   the button callbacks and reopen/reset paths. */
void OpenSubMenuPanel(short notification);
/* Build the panel and one row per available entry of the notification's
   menu. */
bool BuildSubMenuPanel(short notification);
/* Install the row's primary callback for its (W8SubMenuPage, entry) pair. */
/* The row availability states the refresh maps icon frames through. */
W8SubMenuEntryState GetSubMenuEntryState(short menu, short item, int party_slot);

int GetAttackMenuWeaponOffset(W8Skill skill);
