#include <cstdint>

#include "Font.h"
#pragma once

#include <optional>
#include <string>
#include <string_view>

struct W8MonsterManagerEntry;
struct W8Character;

std::optional<int> GetNextCharacter(int require_primary, int require_secondary,
                                    std::optional<int> previous_slot = std::nullopt);
int RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc);
void StripMonsterNameSuffix(std::string& name);
unsigned int CharacterPointerToPartySlot(const W8Character* character);
bool IsPartyCharacterPointer(const W8Character* character);
void FreeStringTable(void);
