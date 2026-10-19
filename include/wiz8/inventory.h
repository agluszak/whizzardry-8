#pragma once

#include <functional>
#include <optional>

#include "wiz8/equipment_slots.h"
#include "wiz8/item_location.h"
#include "wiz8/layouts/item_instance.h"

struct W8Character;

/* The active party's inventory: its members' equipment and backpacks, the shared
   pool and the hand. Every command here acts on the active party and finishes its
   own consequences: unusable equipment comes off, statistics and shared load are
   recalculated, combat choices follow a change of weapons, a committed item use
   loses a replaced item, and the cursor shows the hand. */

// ---- Searching

/* What a search covers: worn equipment, also the backpack, and for the party also the
   shared pool. */
enum class W8ItemReach { Worn, Carried, Party };
using W8ItemMatch = std::function_ref<bool(const W8ItemInstance&)>;
inline auto ItemWithId(int item_id)
{
    return [item_id](const W8ItemInstance& item) { return item.iItemNo == item_id; };
}
// Items whose records share an unidentified name kind, such as every class item tier.
bool ItemIsOfKind(const W8ItemInstance& item, short item_kind);
inline auto ItemOfKind(short item_kind)
{
    return [item_kind](const W8ItemInstance& item) { return ItemIsOfKind(item, item_kind); };
}
// The first match in equipment slot order, then in backpack order.
W8ItemInstance* FindCharacterItem(W8Character& character, W8ItemMatch match, W8ItemReach reach);
// The first match on the active party: the hand, each occupied member, then the pool.
W8ItemLocation FindPartyItem(W8ItemMatch match, W8ItemReach reach);
inline bool PartyHasItem(int item_id, W8ItemReach reach = W8ItemReach::Party)
{
    return FindPartyItem(ItemWithId(item_id), reach).storage != W8ItemStorage::None;
}
// Units of one item, a stack counting its size.
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach);
unsigned int CountPartyItem(int item_id);
bool EveryCharacterHasItem(int item_id, W8ItemReach reach);
// The held item's id, or -1.
int GetItemInHand();

// ---- Receiving, giving and losing items

/* Place an item with a character: equipped when asked and allowed, else merged into
   a stack or put in the first free backpack slot. A stored item leaves its source;
   a refused one stays there. The character may be a draft outside the party. */
bool AddItemToCharacter(W8Character* character, W8ItemInstance* item, bool equip_if_possible,
                        bool announce, bool skip_stacking);
// Merge into a pool stack, else add a new entry at the front of the pool.
bool AddItemToParty(W8ItemInstance* item, bool announce, bool skip_stacking);
// The pool, else the party's feet, else an empty hand.
bool AddItemToPartyOrDrop(W8ItemInstance* item, bool announce);
// The character and the pool, in the order the flag asks for.
bool StoreItemWithCharacterOrParty(W8Character* character, W8ItemInstance* item, bool party_first,
                                   bool announce, bool equip_if_possible);
/* Hand one item to a party member, preferring the character or the party pool
   according to the flag. */
bool GiveItemToCharacterOrParty(int uiChar, W8ItemInstance* item, bool party_first);
/* The held-item form, after the whole party has had its identification attempt. A
   failed party-first attempt still reports success and empties the hand, as retail
   did. */
bool GiveHeldItemToCharacterOrParty(int uiChar, bool party_first);
/* A departing character's items go to the pool; equipment bound to its slot stays
   unless announced, iconless or worn by the dead. */
void StashDepartingCharacterItems(W8Character* character);
void EmptyAllCarriedItems(W8Character* character);
// Give or raise the class item the character's current profession level earns.
void UpgradeProfessionClassItem(W8Character* character);
// Remove an item id from the hand, the members and the pool; once per holder.
bool RemovePartyItemByID(int item_id, bool remove_all);
void RemoveInventoryQuantity(W8ItemLocation location, unsigned amount);
W8ItemInstance TakeInventoryItem(W8ItemLocation location);
void AddPartyGold(int amount, bool announce);
// Take gold from the party, clamping at zero.
void SpendPartyGold(unsigned int amount);

// ---- The hand

void ReplaceHeldItem(W8ItemInstance item);
// A new item in the hand when it is free; false when it already holds something.
bool PlaceItemOnCursor(int item_id, bool maximum_quantity, bool force_identified,
                       bool mark_special);
// Conjure an item into the hand, replacing whatever was held.
void CreateItemIntoHandOrPool(int item_id, bool quality);
bool ReceiveItemInHand(W8ItemInstance& item);
// Drop the held item unless it may not be discarded.
bool DropItemInHand(int unused);

// ---- Player edits

// Split or pick up a stack. Refusal leaves inventory unchanged.
bool SplitItemStack(W8ItemLocation source, unsigned amount, unsigned actor);
// Exchange two locations; equipment slots accept only what their wearer may equip.
bool ExchangeInventoryItems(W8ItemLocation first, W8ItemLocation second, unsigned actor);
// Equip the held item in a hand whose partner it cannot share, taking the partner off.
bool EquipHeldItemAndStowPartner(unsigned actor, W8EquipSlot slot);
struct StackMergeResult {
    bool complete = false;
    bool changed = false;
};
StackMergeResult MergeInventoryStacks(W8ItemLocation destination, W8ItemLocation source);
bool InsertHeldItemInPool(unsigned index);
// Combine the held item with another by recipe, or stack them; announces refusal.
bool MergeItems(unsigned actor, W8ItemLocation target);
// Pour the source's uses into the destination.
void MergeItemUses(unsigned actor, W8ItemLocation destination, W8ItemLocation source);
// Add recharge uses to an item, never past what it can hold.
void AddItemUses(W8ItemInstance* item, char uses);
void SortPartyItemPool();
bool ItemPrecedesInPool(const W8ItemInstance& first, const W8ItemInstance& second);

// ---- Weapon sets and ammunition

struct W8WeaponSwap {
    bool primary;
    bool secondary;
};
std::optional<W8WeaponSwap> PlanWeaponSetSwap(const W8Character& character);
void CommitWeaponSetSwap(int party_slot, W8WeaponSwap choice);
bool SwapWeaponSetSlots(int party_slot, bool announce);
// The combat UI's weapon-set swap, refused outside the member's own turn.
void BindCharacterItems(int party_slot, bool announce);
// Spend what one attack used and ready the weapon again.
bool ConsumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used, W8EquipSlot paired);
// Equip the primary weapon's off-hand partner and refill its shots before attacking.
void ReadyPrimaryWeapon(unsigned party_slot);

// ---- Using items

struct W8ItemUseResult {
    // The spell engine's outcome; 1 is a success and 0 means nothing happened.
    unsigned char effect = 0;
    // The attempt's own fatigue cost; none leaves the action's ordinary cost.
    std::optional<int> fatigue_cost;
};
// Use one item as a character's action.
W8ItemUseResult UseItem(unsigned actor, W8ItemLocation item);

// ---- Equipment rule

/* Equipment a member can no longer use is announced and stored like a received item.
   Returns whether a weapon hand changed. Part of settling the active party. */
bool UnequipUnusableItems(unsigned int party_slot);
