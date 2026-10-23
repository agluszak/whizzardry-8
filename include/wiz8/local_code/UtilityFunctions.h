#include <cstdint>

#include "wiz8/canvas/Font.h"
#pragma once

#include <string_view>

struct W8MonsterManagerEntry;
struct W8Character;

int RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc);
unsigned int CharacterPointerToPartySlot(const W8Character* character);
bool IsPartyCharacterPointer(const W8Character* character);
