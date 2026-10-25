#include <cstdint>

#include "wiz8/canvas/Font.h"
#pragma once

#include <string_view>

struct W8Character;

int GetNextCharacter(int require_primary, int require_secondary, int previous_slot);
unsigned int CharacterPointerToPartySlot(const W8Character* character);
bool IsPartyCharacterPointer(const W8Character* character);
