#pragma once

#include <bitset>

#include "wiz8/rules/conditions.h"
#include "wiz8/rules/party_conditions.h"
#include "wiz8/rules/party_slot.h"

class Game;
struct W8Character;

/* A game's party rules. The rules in `party_conditions.h` take their party and inputs
   explicitly; these commands apply them to the game's party with the game as their host,
   which supplies its inputs and applies their combat, story, world and speech
   consequences. Every change to what a member's statistics depend on settles the party. */

/* Bring the party's equipment, statistics and shared load in line with its state:
   unusable equipment comes off, pools follow changed ceilings, a member whose hit points
   run out dies, and combat choices follow changed weapon hands. */
void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});
bool InflictCondition(Game& game, unsigned int slot, const ConditionInfliction& infliction);
void LiftCondition(Game& game, unsigned int slot, W8Condition condition, bool announce);
void AgeCondition(Game& game, unsigned int slot, W8Condition condition, unsigned int minutes);
void LiftConditionFromParty(Game& game, W8Condition condition);
void LiftAllConditionsFromParty(Game& game);
void ReviveByCheatingDeath(Game& game, unsigned int slot);
void Enchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment, int power,
             unsigned int turns, unsigned int percent);
void Disenchant(Game& game, unsigned int slot, W8EnchantmentSlot enchantment);
void AgeEnchantment(Game& game, unsigned int slot, W8EnchantmentSlot enchantment,
                    unsigned int turns);
/* Member `slot`'s condition block follows a change outside its conditions (a bound NPC,
   a dependence), and the party settles. */
void RefreshConditionModifiers(Game& game, unsigned int slot);

/* A member of the party (its own `fInParty`) settles the party; any other character, such as
   a draft in character creation, is calculated on its own. */
void RecalculateCharacterDerivedStats(Game& game, W8Character& character);
/* The calculation for a draft outside the party from the effective attributes and skill
   levels it holds, which the creation editor edits directly. */
void RecalculateEditedCharacterStats(W8Character& draft);
