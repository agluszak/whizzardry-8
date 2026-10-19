#pragma once

#include <bitset>

#include "wiz8/layouts/party_slot.h"
#include "wiz8/party_stats.h"

struct W8Character;
struct InventoryWeightChange;
struct W8GameplayModifierBlock;

/* The active session's party rules. Calculations elsewhere take their party and
   inputs explicitly; these functions supply the live ones and apply what follows. */

/* Bring the active party's equipment, statistics and shared load in line with its
   state: unusable equipment comes off, a member whose hit points run out dies, and
   combat choices follow changed weapon hands. */
void SettleActiveParty(std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});
// An occupied member settles the active party; any other character, such as a draft
// in character creation, is calculated on its own.
void RecalculateCharacterDerivedStats(W8Character& character);
void ApplyPartyModifiers(W8GameplayModifierBlock modifiers);

// The active session's inputs to statistic calculations: combat actions and NPC story state.
CharacterStatRules CaptureLiveCharacterStatRules(const W8Character& character);
PartyStatRules CaptureLivePartyStatRules();

// A recalculated hit-point ceiling that exhausts a member's pool kills the member.
void RecalculateHitPoints(W8Character& character);
// Armor class under the active session's combat action.
void RecalculateArmorClass(W8Character& character);

// Present calculation results on the active screen.
void PresentInventoryWeightChange(W8Character& character, InventoryWeightChange change);
void PresentPartyLoadChange(bool redistributed);

// Redistribute the active party's shared load now, or after combat, and present it.
void RedistributePartyEncumbrance();
