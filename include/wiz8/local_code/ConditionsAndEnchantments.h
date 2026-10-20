#pragma once
#include "wiz8/rules/conditions.h"

struct W8ItemInstance;
struct W8TargetSource;
struct W8Character;
struct W8MonsterInfo;

void CopyCharacterConditionsToTarget(const W8Character& character, const int* target);
void CopyMonsterConditionsToCharacter(W8Character* character, const W8MonsterInfo* monster_info);

#pragma pack(push, 2)
struct W8ConditionImmunity {
    unsigned char kind;
    /* The catch-all flag the monster-info immunity list prints as its
       final "all" entry. */
    unsigned char immune_all;
    W8Condition conditions[20];
};
#pragma pack(pop)
static_assert(sizeof(W8ConditionImmunity) == 0x52, "W8ConditionImmunity_size");

extern W8ConditionImmunity g_condition_immunities[3];
extern unsigned short g_enchantment_notices[16];
extern unsigned short g_condition_interrupt_notices[12];

void SetMonsterCondition(int location_id, W8Condition condition, int duration, int poison_strength,
                         W8TargetSource* target, bool announce);
void ClearMonsterCondition(int location_id, W8Condition condition);
void ClearMonsterEnchantmentSlot(int location_id, W8EnchantmentSlot slot);
void TickMonsterEnchantmentSlot(int location_id, W8EnchantmentSlot slot, unsigned int turns);
/* The per-condition aging tick the sight producer runs while a
   condition's countdown is live. */
void TickMonsterCondition(int location_id, W8Condition condition, unsigned int minutes);
/* Settle an enchantment on a character or monster with its argument, rolled
   duration and definition percentage. */
void ApplyMonsterCondition(int location_id, W8EnchantmentSlot slot, int argument,
                           unsigned int duration, unsigned int percent);
bool GetConditionRecordFlag(int party_slot, W8CharacterDependence dependence);
/* Bind a monster into one of a character's two dependence
   condition records; a slot-one binding also retires the monster's group. */
void BindMonsterToCharacterDependence(unsigned int party_slot, unsigned int dependence_slot,
                                      int monster_id);

/* Clear the character condition records a dead monster sourced,
   over both dependence slots and the per-character tables; RecordMonsterKill
   runs it before the faction fallout. */
void ReleaseMonsterConditionBindings(W8MonsterInfo* monster_info);

void RemoveConditionFromEveryone(W8Condition condition);
void RemoveAllEnchantments(void);

void NormalizeItemQuantityKind(W8ItemInstance* item);
/* The post-load repair LoadGame runs - unequip unusable items on
   every character and normalize quantity kinds on carried and pooled items. */
void SanitizeLoadedItems(void);
