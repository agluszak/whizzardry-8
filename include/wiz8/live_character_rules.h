#pragma once

#include "wiz8/character_stat_rules.h"
#include "wiz8/party_stat_rules.h"

struct W8Character;
// Application boundary: snapshot the live combat and NPC story inputs.
CharacterStatRules CaptureLiveCharacterStatRules(const W8Character& character);
PartyStatRules CaptureLivePartyStatRules();
