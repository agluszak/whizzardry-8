#pragma once

#include "wiz8/character_stat_rules.h"
#include "wiz8/party_stat_rules.h"

struct W8Character;
struct InventoryWeightChange;

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
