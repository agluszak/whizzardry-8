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
    void setSubMenuTooltips(bool enabled);
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
    explicit MainGameSubMenu(short bank) : bank(bank) {}
    void draw(bool invalidate);
    const short bank;
    UiLayer input{UiLayerOrder::Submenu};
    SDL_Rect bounds{};
    int background = 0;
    std::optional<uint64_t> close_deadline;
    std::vector<std::unique_ptr<W8ButtonWidget>> buttons;
};

/* The action-kind message indexes the caption draw maps through. */
extern const unsigned short g_action_kind_message_ids[12];

/* Draw roof chrome and the three buttons; the same repaint draws the combat
   progress gauge. */
void RedrawRoofButtons(void);
// Draw layout-arrow chrome and the visible raise/lower arrows.
void RedrawLayoutArrowButtons(void);
/* Draw the selected character's name/profession line and the caption for
   their queued action. */
void DrawSubMenuCharacterAction(void);
/* Redraw the bottom sub-menu chrome, bank buttons, panel buttons, character
   action caption, and scroll arrows. */
void RedrawSubMenuButtons(void);
// Redraw the options-disk button.
void RedrawOptionsDiskButton(void);
// Draw the combat-stance chrome and the live stance button.
void RedrawCombatStanceButtons(void);

/* Enable/disable the scroll, roof and layout button
   banks around the surprise/camp sequence. */
void EnableMenuButtonBanks(void);
void DisableMenuButtonBanks(void);
/* The row availability states the refresh maps icon frames through. */
W8SubMenuEntryState GetSubMenuEntryState(short menu, short item, int party_slot);

int GetAttackMenuWeaponOffset(W8Skill skill);
