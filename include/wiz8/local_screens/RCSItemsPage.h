#pragma once

#include <string>

#include "wiz8/character_skills.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/platform/input.h"
#include "wiz8/ui_layer.h"

struct W8Character;
struct W8ItemInstance;

bool CanCharacterUseItemEntry(W8Character* character, W8ItemInstance* item);
bool CanSplitItemStack(const W8ItemInstance* item);
bool IsSpecialItemId(W8ItemInstance* item);
void SetHandCursors();

/* The twelve equip-slot label message ids indexed by region id. */
extern unsigned short g_equip_slot_label_ids[12];
