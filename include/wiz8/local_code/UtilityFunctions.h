#include "Font.h"
#pragma once

#include <string_view>
#include <string>

#include "Button System.h"
#include "Types.h"

struct W8MonsterManagerEntry;
struct W8Character;

bool CreateMessageBox(std::string_view text, Font& font, unsigned int shade, bool has_accept, bool has_cancel,
                      void (*callback)(void));
void CloseMessageBox(void);
void MessageBoxAcceptMoveCallback(GUI_BUTTON* button, INT32 reason);
void MessageBoxAcceptClickCallback(GUI_BUTTON* button, INT32 reason);
void MessageBoxCancelMoveCallback(GUI_BUTTON* button, INT32 reason);
void MessageBoxCancelClickCallback(GUI_BUTTON* button, INT32 reason);
int GetNextCharacter(int require_primary, int require_secondary, int previous_slot);
int RPCPtrToPCSlot(const W8MonsterManagerEntry* rpc);
void StripMonsterNameSuffix(std::string& name);
unsigned int CharacterPointerToPartySlot(const W8Character* character);
bool IsPartyCharacterPointer(const W8Character* character);
void FreeStringTable(void);
