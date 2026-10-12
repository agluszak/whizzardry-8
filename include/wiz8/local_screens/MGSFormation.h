#pragma once

#include "input.h"

/* Local Screens\MGSFormation.cpp: the party-formation screen - a board of
   fifteen cells in five rows of three drawn over the main-game view, with a
   rotating compass needle tracking the party's heading. */

struct W8Region;
void ReleaseFormationBoard(void);
void EnableFormationInput();
void DestroyFormationPanel(void);
void CloseFormationPanel(void);

/* Rebuild the board, compass and overlay sprites and redraw the slot markers
   into the board image. */
void RefreshFormationBoard(void);
/* Rotate the compass needle to party_facing - party_heading. */
void UpdateFormationCompass(void);
/* Lazily create the overlay sprite above the board. */
void CreateFormationBoardOverlay(void);
/* Board callback: opens the formation panel on release, hit-tests the cell
   markers for tooltips, and switches board art on hover transitions. */
UiResult HandleFormationBoard(const UiPointerEvent& event);
/* Open the formation panel: build the controls, snapshot the live formation
   into gXStatus.edited_formation and pause the world. */
void OpenFormationPanel(void);
/* Highlight the cell holding one party slot and un-highlight the rest. */
void SelectFormationSlotCell(int party_slot);
