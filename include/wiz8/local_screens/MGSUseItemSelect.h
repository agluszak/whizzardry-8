#pragma once

#include <bitset>

#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"

struct W8ItemInstance;

struct W8Action;

// The draft exists exactly while the item picker is open.
W8Action* ItemUseDraft();

void SetUseItemSelectInputEnabled(bool enabled);
void CloseUseItemSelection(void);
/* Text-box body callback while use-item select is active. */
UiResult HandleUseItemTextBox(const UiPointerEvent& event);
/* Text-box wheel helper while use-item select is active. */
void UseItemSelectTextBoxWheelAt(short x, unsigned short y, bool flag);
void SelectCurrentUseItemLine(void);

void CommitSelectedItemUse(void); /* FItemSelectMode per-frame commit */
void SelectUseItemLine(int line);
void OpenUseItemAssayDialog(W8ItemInstance* item);
void UpdateUseItemDetailPanel(W8ItemInstance* item);
void TakeUseItemIntoHand(void);

void CloseUseItemSelectView(void);
/* Retarget the open use-item list at party_slot (drag, recorded
   item, or plain refresh paths). */
void RefreshUseItemSelectionForSlot(int party_slot);
/* Before the draw: keep the source buttons in step with the picker (combat
   moves a party list back to the character), then add the panels whose widgets
   changed to `panels`. The draw paints the requested panels. */
void UpdateUseItemScrollButtons(void);
void UpdateUseItemPanels(std::bitset<7>& panels);
void PaintUseItemPanels(std::bitset<7> panels);
/* After dropping a cursor item during use-item select, refresh
   the selected line. */
void RefreshUseItemSelection(void);
