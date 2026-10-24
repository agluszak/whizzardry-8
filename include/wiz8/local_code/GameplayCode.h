#pragma once

struct W8Character;

void AdvanceCharacterToLevel(W8Character* character, unsigned int level);
bool AnyCharacterActive(void);
void CalcCharacterLevelBand(W8Character* character);

void CalcCharacterTableValue(W8Character* character);
int CountActiveCharacters(void);
unsigned int FindFreePartySlot(unsigned int first, unsigned int last);

void RefreshLevelUpReadyNotices(void);
int AddCharacterToParty(W8Character& character, int slot);
/* Remove a slot's character from the party; fSaveCharData
   persists it back to its NPC record first. */
void RemoveCharacterFromParty(int party_slot);

void AwardPartyExperience(int value, bool alternate_message);
bool IsCharacterReadyToAdvance(int party_slot);
void CalcXPGoal(W8Character* character);
void DeriveCharacterPersonality(W8Character* character);
/* Rerolls personality/voice until no party member shares the
   character's gender/personality/voice pick. */
void EnsureUniquePartyVoice(W8Character* character);
/* Knock-knock style spell committed against the active lock or
   trap interaction; with no interaction open it just prints the refusal
   notice. The lock-interaction owner lives past MainGameScreen.h, which this
   task does not expand. */
void CastSpellAtLockInteraction(unsigned int level, int flag, int backfire);
unsigned char RecruitCharacterIntoParty(W8Character* character, W8Character* record,
                                        bool buy_equipment);
bool AnyMonsterEngaged(void);
/* On a level-motion override, roll `pow(8.0, fall + 0.7)` d6 of
   fall damage against the party with a notice and the fall-impact sound. */
void HandleLevelOverride(float fall);
