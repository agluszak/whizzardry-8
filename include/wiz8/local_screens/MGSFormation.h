#pragma once

#include "input.h"

/* Local Screens\MGSFormation.cpp: the party-formation screen - a board of
   fifteen cells in five rows of three drawn over the main-game view, with a
   rotating compass needle tracking the party's heading. */

struct W8Region;
struct Controls;
class W8TextControl;
class ScreenRegionSet;

extern std::unique_ptr<Controls> g_formation_panel;
extern W8TextControl* g_formation_cell_controls[15];
extern W8TextControl* g_formation_cell_overlays[15];
extern W8TextControl* g_formation_action_buttons[3];

void ReleaseFormationBoard(void);
extern ScreenRegionSet g_formation_regions;
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
unsigned char FormationBoardRegionEvent(const InputEvent* event, W8Region* region, unsigned short);
/* Open the formation panel: build the controls, snapshot the live formation
   into gXStatus.edited_formation and pause the world. */
void OpenFormationPanel(void);
/* Region callback the fifteen formation cells share. */
unsigned char FormationCellRegionEvent(const InputEvent* event, W8Region* region, unsigned short);
unsigned char FormationBackgroundRegionEvent(const InputEvent* event, W8Region* region, unsigned short);
/* Highlight the cell holding one party slot and un-highlight the rest. */
void SelectFormationSlotCell(int party_slot);
