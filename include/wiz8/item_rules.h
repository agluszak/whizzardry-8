#pragma once

#include <array>

#include "wiz8/layouts/character.h"
#include "wiz8/layouts/item_instance.h"

unsigned int GetItemStackWeight(const W8ItemInstance* item);
int WeaponHandKind(const W8ItemInstance& primary, const W8ItemInstance& secondary,
                   unsigned int hand);
std::array<W8HandAttack, 2> EvaluateWeaponHands(const W8ItemInstance& primary,
                                                const W8ItemInstance& secondary);

bool CompatiblePartnerItems(int ranged_item_id, int other_item_id);
bool ItemHasSingledOutGenericName(int item_id);
bool ItemUsesShots(int item_id);

bool GetItemMergeKind(int item_id, short* related_kind);
