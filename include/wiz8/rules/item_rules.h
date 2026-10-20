#pragma once

#include <array>

#include "wiz8/rules/character.h"
#include "wiz8/rules/item_instance.h"

struct GameData;

unsigned int GetItemStackWeight(const GameData& data, const W8ItemInstance* item);
int WeaponHandKind(const GameData& data, const W8ItemInstance& primary,
                   const W8ItemInstance& secondary, unsigned int hand);
std::array<W8HandAttack, 2> EvaluateWeaponHands(const GameData& data, const W8ItemInstance& primary,
                                                const W8ItemInstance& secondary);

bool CompatiblePartnerItems(const GameData& data, int ranged_item_id, int other_item_id);
bool ItemHasSingledOutGenericName(const GameData& data, int item_id);
bool ItemUsesShots(const GameData& data, int item_id);

bool GetItemMergeKind(const GameData& data, int item_id, short* related_kind);
