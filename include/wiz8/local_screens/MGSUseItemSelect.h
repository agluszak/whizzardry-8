#pragma once

#include "input.h"
#include "wiz8/layouts/main_game_screen.h"
#include "wiz8/ui_layer.h"

struct W8ItemInstance;

extern W8MainUiMode g_use_item_select_return_mode;
extern W8ItemInstance* g_use_item_selected;
extern W8ItemInstance* g_use_item_select_override_item;

void SetUseItemSelectInputEnabled(bool enabled);
void InvalidateUseItemSelectPanel(void);
void CloseUseItemSelection(void);
/* Text-box body callback while use-item select is active. */
UiResult HandleUseItemTextBox(const UiPointerEvent& event);
/* Text-box wheel helper while use-item select is active. */
void UseItemSelectTextBoxWheelAt(short x, unsigned short y, bool flag);
void SelectCurrentUseItemLine(void);
void SetUseItemSelectOverrideItem(W8ItemInstance* value);

void CommitSelectedItemUse(void); /* FItemSelectMode per-frame commit */
void SelectUseItemLine(int line);
void OpenUseItemAssayDialog(W8ItemInstance* item);
void UpdateUseItemDetailPanel(W8ItemInstance* item);
void TakeUseItemIntoHand(void);

void CloseUseItemSelectView(void);
/* Retarget the open use-item list at party_slot (drag, recorded
   item, or plain refresh paths). */
void RefreshUseItemSelectionForSlot(int party_slot);
/* FItemSelectMode per-frame update: panel redraws plus the
   pending-use commit check. */
void UpdateUseItemSelect(bool active);
/* After dropping a cursor item during use-item select, refresh
   the selected line. */
void RefreshUseItemSelection(void);
