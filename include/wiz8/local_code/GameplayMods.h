#pragma once

#include "wiz8/rules/gameplay_modifiers.h"

/* Local Code\Gameplay Mods.cpp. */

struct W8MonsterInfo;
struct W8Character;
struct W8Enchantment;

void RebuildPartyEffectBlock(void);

/* The monster-side rebuild - refill the monster's modifier block
   from its conditions, enchantments and effect slots, then refresh its
   derived attributes and regeneration rates. */
void RebuildMonsterDerivedStats(int location_id);
void RebuildMonsterDerivedStats(W8MonsterInfo& monster);

/* The unit's block-fold helpers. Each folds its source into the shared
   modifier block; the party-wide block is their usual target. */
void ApplyPartyEffectSlots(const W8EffectSlot* source, W8GameplayModifierBlock* target);
void ApplyCombatEffectSlots(const W8EffectSlot* source, W8GameplayModifierBlock* target);
