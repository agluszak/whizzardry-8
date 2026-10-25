#pragma once

#include "wiz8/rules/gameplay_modifiers.h"

/* Local Code\Gameplay Mods.cpp. */

struct W8MonsterInfo;
struct W8MonsterRecord;
struct W8Character;
struct W8Enchantment;

/* The monster-side rebuild - refill the monster's modifier block
   from its conditions, enchantments and effect slots, then refresh its
   derived attributes and regeneration rates. */
void RebuildMonsterDerivedStats(int location_id);
void RebuildMonsterDerivedStats(W8MonsterInfo& monster, const W8MonsterRecord& record);
