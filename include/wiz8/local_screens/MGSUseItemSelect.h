#pragma once

#include "input.h"
#include "wiz8/layouts/main_game_screen.h"

struct W8Region;
class W8DialogBase;
struct W8ItemInstance;

extern W8MainUiMode g_use_item_select_return_mode;
extern W8ItemInstance* g_use_item_selected;
extern W8ItemInstance* g_use_item_select_override_item;

/* GXStatus.iCurrentCursor saved while an item/spell info dialog
   is open; RestoreTargetCursor puts it back on dialog destroy. */
extern int g_saved_target_cursor;

void SetUseItemSelectReturnMode(W8MainUiMode value);
void EnableUseItemSelectInput();
void InvalidateUseItemSelectPanel(void);
void CloseUseItemSelection(void);
/* Text-box body callback while use-item select is active. */
unsigned char UseItemSelectTextBoxRegionEvent(const InputEvent* event, W8Region* region);
/* Text-box wheel helper while use-item select is active. */
void UseItemSelectTextBoxWheelAt(short x, unsigned short y, bool flag);
W8ItemInstance* GetSelectedOrFallbackValue(void);
void SelectCurrentUseItemLine(void);
void SetUseItemSelectOverrideItem(W8ItemInstance* value);

void CommitSelectedItemUse(void); /* FItemSelectMode per-frame commit */
void SelectUseItemLine(int line);
void OpenUseItemAssayDialog(W8ItemInstance* item);
void RestoreTargetCursor(W8DialogBase* dialog);
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
