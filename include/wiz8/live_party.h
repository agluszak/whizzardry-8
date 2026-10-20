#pragma once

#include <bitset>

#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/party_stats.h"

class Game;
struct W8Character;
struct InventoryWeightChange;
struct W8GameplayModifierBlock;

/* A game's party rules. Calculations elsewhere take their party and inputs
   explicitly; these functions supply the game's own and apply what follows. */

/* Bring the party's equipment, statistics and shared load in line with its state:
   unusable equipment comes off, a member whose hit points run out dies, and combat
   choices follow changed weapon hands. */
void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});
// An occupied member settles the party; any other character, such as a draft in
// character creation, is calculated on its own.
void RecalculateCharacterDerivedStats(Game& game, W8Character& character);
void ApplyPartyModifiers(Game& game, W8GameplayModifierBlock modifiers);

// A recalculated hit-point ceiling that exhausts a member's pool kills the member.
void RecalculateHitPoints(Game& game, W8Character& character);
// Armor class under the game's combat action.
void RecalculateArmorClass(const Game& game, W8Character& character);

// A member who became overloaded says so.
void PresentInventoryWeightChange(W8Character& character, InventoryWeightChange change);

// Redistribute the party's shared load now, or after combat.
void RedistributePartyEncumbrance(Game& game);
