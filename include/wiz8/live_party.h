#pragma once

#include <bitset>

#include "wiz8/rules/party_slot.h"

class Game;
struct W8Character;

/* A game's party rules. Calculations elsewhere take their party and inputs
   explicitly; these functions supply the game's own and apply what follows. Every change
   to what a member's statistics depend on settles the party. */

/* Bring the party's equipment, statistics and shared load in line with its state:
   unusable equipment comes off, pools follow changed ceilings, a member whose hit points
   run out dies, and combat choices follow changed weapon hands. */
void SettleActiveParty(Game& game, std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});
// An occupied member settles the party; any other character, such as a draft in
// character creation, is calculated on its own.
void RecalculateCharacterDerivedStats(Game& game, W8Character& character);
/* The same calculation from the effective attributes and skill levels the character holds,
   which the creation editor edits directly. */
void RecalculateEditedCharacterStats(Game& game, W8Character& character);
