#pragma once

#include <expected>
#include <optional>

#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/inventory.h" // IWYU pragma: export - the storage rules the commands apply.
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"

class Game;
struct W8Character;

/* A game's inventory commands. The storage rules (rules/inventory.h) place and edit items;
   these apply what they set off in parts of the game that are not rules yet: acquiring an
   item updates the story's facts and may draw a member's remark, what no store takes is
   dropped into the world, and every edit settles the party: unusable equipment comes off,
   statistics and shared load are recalculated, combat choices follow a change of weapons,
   and the hand's change is reported. A command the player is refused returns the refusal
   for the screen to show. */

// ---- Receiving, giving and losing items

// A party member as the receiver of an item.
ItemRecipient PartyMember(Game& game, unsigned int party_slot);
// A character outside the party, such as a draft in party creation.
inline ItemRecipient Outsider(W8Character& character)
{
    return {character, std::nullopt};
}

/* Place an item with a character: equipped when asked and allowed, else merged into a stack
   or put in the first free backpack slot. A character outside the party is also brought up
   to date on its own. */
bool AddItemToCharacter(Game& game, ItemRecipient recipient, ItemSource source,
                        bool equip_if_possible, bool announce, bool skip_stacking);
// Merge into a pool stack, else add a new entry at the front of the pool.
bool AddItemToParty(Game& game, ItemSource source, bool announce, bool skip_stacking);
// The pool, else the party's feet, else an empty hand.
bool AddItemToPartyOrDrop(Game& game, ItemSource source, bool announce);
// The character and the pool, in the order the flag asks for.
bool StoreItemWithCharacterOrParty(Game& game, ItemRecipient recipient, ItemSource source,
                                   bool party_first, bool announce, bool equip_if_possible);
/* Hand one item to a party member, preferring the member or the party pool according to the
   flag; a member who cannot take items leaves only the pool. */
bool GiveItemToCharacterOrParty(Game& game, unsigned int party_slot, ItemSource source,
                                bool party_first);
/* The held-item form, after the whole party has had its identification attempt. A failed
   party-first attempt still reports success and empties the hand, as retail did. */
std::expected<void, InventoryRefusal>
GiveHeldItemToCharacterOrParty(Game& game, unsigned int party_slot, bool party_first);
/* A departing member's items go to the pool; equipment bound to its slot stays unless
   announced, iconless or worn by the dead. */
void StashDepartingCharacterItems(Game& game, unsigned int party_slot);
// Empty a character outside the party, such as a draft, of everything it carries.
void EmptyAllCarriedItems(Game& game, W8Character& outsider);
// Give or raise the class item the character's current profession level earns.
void UpgradeProfessionClassItem(Game& game, ItemRecipient recipient);
// Remove an item id from the hand, the members and the pool; once per holder.
bool RemovePartyItemByID(Game& game, int item_id, bool remove_all);
void RemoveInventoryQuantity(Game& game, W8ItemLocation location, unsigned amount);
W8ItemInstance TakeInventoryItem(Game& game, W8ItemLocation location);
void AddPartyGold(Game& game, int amount, bool announce);

// ---- The hand

void ReplaceHeldItem(Game& game, W8ItemInstance item);
// A new item in the hand when it is free; false when it already holds something.
bool PlaceItemOnCursor(Game& game, int item_id, bool maximum_quantity, bool force_identified,
                       bool mark_special);
// Conjure an item into the hand, replacing whatever was held.
void ReplaceHeldItemWithNew(Game& game, int item_id, bool quality);
bool ReceiveItemInHand(Game& game, W8ItemInstance& item);
// Drop the held item unless it may not be discarded.
std::expected<void, InventoryRefusal> DropItemInHand(Game& game, int unused);

// ---- Player edits

// Split or pick up a stack. Refusal leaves inventory unchanged.
bool SplitItemStack(Game& game, W8ItemLocation source, unsigned amount, unsigned actor);
// Exchange two locations; equipment slots accept only what their wearer may equip.
bool ExchangeInventoryItems(Game& game, W8ItemLocation first, W8ItemLocation second,
                            unsigned actor);
// Equip the held item in a hand whose partner it cannot share, taking the partner off.
bool EquipHeldItemAndStowPartner(Game& game, unsigned actor, W8EquipSlot slot);
struct StackMergeResult {
    bool complete = false;
    bool changed = false;
};
StackMergeResult MergeInventoryStacks(Game& game, W8ItemLocation destination,
                                      W8ItemLocation source);
bool InsertHeldItemInPool(Game& game, unsigned index);
// Combine the held item with another by recipe, or stack them; whether anything merged.
std::expected<bool, InventoryRefusal> MergeItems(Game& game, unsigned actor, W8ItemLocation target);
// Pour the source's uses into the destination.
void MergeItemUses(Game& game, unsigned actor, W8ItemLocation destination, W8ItemLocation source);

// ---- Weapon sets and ammunition

void CommitWeaponSetSwap(Game& game, unsigned int party_slot, W8WeaponSwap choice);
bool SwapWeaponSetSlots(Game& game, unsigned int party_slot, bool announce);
// The combat UI's weapon-set swap, refused outside the member's own turn.
void BindCharacterItems(Game& game, unsigned int party_slot, bool announce);
// Spend what one attack used and ready the weapon again; whether the thrown weapon ran out.
bool ConsumeAttackItems(Game& game, unsigned actor, unsigned hand, W8EquipSlot used,
                        W8EquipSlot paired);
// Equip the primary weapon's off-hand partner and refill its shots before attacking.
void ReadyPrimaryWeapon(Game& game, unsigned party_slot);

// ---- Using items

struct W8ItemUseResult {
    // The spell engine's outcome; 1 is a success and 0 means nothing happened.
    unsigned char effect = 0;
    // The attempt's own fatigue cost; none leaves the action's ordinary cost.
    std::optional<int> fatigue_cost;
};
/* Use one item as a character's action; a use whose location no longer holds its item does
   nothing. */
W8ItemUseResult UseItem(Game& game, unsigned actor, const W8ItemUse& use);

// ---- Equipment rule

/* Equipment a member can no longer use is announced and stored like a received item; one
   that nothing can take stays equipped. Returns whether a weapon hand changed. Part of
   settling the party. */
bool UnequipUnusableItems(Game& game, unsigned int party_slot);
