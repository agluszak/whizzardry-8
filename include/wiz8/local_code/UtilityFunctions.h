#include <cstdint>
#include "Font.h"
#pragma once

#include <string_view>
#include <string>

struct W8MonsterManagerEntry;
struct W8Character;

bool CreateMessageBox(std::string_view text, Font& font, std::uint32_t shade, bool has_accept, bool has_cancel,
                      void (*callback)(void));
void CloseMessageBox(void);
std::int32_t GetNextCharacter(std::int32_t require_primary, std::int32_t require_secondary, std::int32_t previous_slot);
std::int32_t RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc);
void StripMonsterNameSuffix(std::string& name);
std::uint32_t CharacterPointerToPartySlot(const W8Character* character);
bool IsPartyCharacterPointer(const W8Character* character);
void FreeStringTable(void);
