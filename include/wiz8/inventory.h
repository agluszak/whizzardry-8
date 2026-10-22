#pragma once

#include <bitset>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>

#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/inventory.h" // IWYU pragma: export - the storage rules the commands apply.
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"

class Game;
struct W8Character;

// A character outside the party, such as a draft in party creation.
inline ItemRecipient Outsider(W8Character& character)
{
    return {character, std::nullopt};
}

struct StackMergeResult {
    bool complete = false;
    bool changed = false;
};

struct W8ItemUseResult {
    // The spell engine's outcome; 1 is a success and 0 means nothing happened.
    unsigned char effect = 0;
    // The attempt's own fatigue cost; none leaves the action's ordinary cost.
    std::optional<int> fatigue_cost;
};

/* A game's inventory: the commands that change what the party carries. The storage rules
   (`PartyStorage`, which only the inventory holds) place and edit items; the inventory
   applies what they set off in parts of the game that are not rules yet: acquiring an item
   updates the story's facts and may draw a member's remark, what no store takes is dropped
   into the world, and every edit settles the party: unusable equipment comes off,
   statistics and shared load are recalculated, combat choices follow a change of weapons,
   and the hand's change is reported. A command the player is refused returns the refusal
   for the screen to show. Each game owns one. */
class Inventory {
public:
    explicit Inventory(Game& game);
    Inventory(const Inventory&) = delete;
    Inventory& operator=(const Inventory&) = delete;

    // ---- Receiving, giving and losing items

    // A party member as the receiver of an item.
    ItemRecipient member(unsigned int party_slot);
    /* Place an item with a character: equipped when asked and allowed, else merged into a
       stack or put in the first free backpack slot. A character outside the party is also
       brought up to date on its own. */
    bool addToCharacter(ItemRecipient recipient, ItemSource source, bool equip_if_possible,
                        bool announce, bool skip_stacking);
    // Merge into a pool stack, else add a new entry at the front of the pool.
    bool addToPool(ItemSource source, bool announce, bool skip_stacking);
    // The pool, else the party's feet, else an empty hand.
    bool addToPoolOrDrop(ItemSource source, bool announce);
    // The character and the pool, in the order the flag asks for.
    bool storeWithCharacterOrPool(ItemRecipient recipient, ItemSource source, bool party_first,
                                  bool announce, bool equip_if_possible);
    /* Hand one item to a party member, preferring the member or the party pool according to
       the flag; a member who cannot take items leaves only the pool. */
    bool give(unsigned int party_slot, ItemSource source, bool party_first);
    /* The held-item form, after the whole party has had its identification attempt. A failed
       party-first attempt still reports success and empties the hand, as retail did. */
    std::expected<void, InventoryRefusal> giveHeld(unsigned int party_slot, bool party_first);
    /* A departing member's items go to the pool; equipment bound to its slot stays unless
       announced, iconless or worn by the dead. */
    void stashDepartingMember(unsigned int party_slot);
    // Empty a character outside the party, such as a draft, of everything it carries.
    void emptyOutsider(W8Character& outsider);
    // Give or raise the class item the character's current profession level earns.
    void upgradeClassItem(ItemRecipient recipient);
    // Remove an item id from the hand, the members and the pool; once per holder.
    bool removeItemById(int item_id, bool remove_all);
    void removeQuantity(W8ItemLocation location, unsigned amount);
    W8ItemInstance take(W8ItemLocation location);
    void addGold(int amount, bool announce);
    /* What acquiring an item sets off: a handful of plot items update their facts, and an
       exceptional one draws a remark (reactToExceptionalItem). */
    void acquire(W8ItemInstance& item, W8Character* receiver);
    /* An exceptional item's one-time remark, from the receiver, or from a random member when
       there is none. */
    void reactToExceptionalItem(W8ItemInstance& item, W8Character* receiver);

    // ---- The hand

    void replaceHeld(W8ItemInstance item);
    // A new item in the hand when it is free; false when it already holds something.
    bool placeInHand(int item_id, bool maximum_quantity, bool force_identified, bool mark_special);
    // Conjure an item into the hand, replacing whatever was held.
    void replaceHeldWithNew(int item_id, bool quality);
    bool receiveInHand(W8ItemInstance& item);
    // Drop the held item unless it may not be discarded.
    std::expected<void, InventoryRefusal> dropHeld(int unused);

    // ---- Player edits

    // Split or pick up a stack. Refusal leaves inventory unchanged.
    bool splitStack(W8ItemLocation source, unsigned amount, unsigned actor);
    // Exchange two locations; equipment slots accept only what their wearer may equip.
    bool exchange(W8ItemLocation first, W8ItemLocation second, unsigned actor);
    // Equip the held item in a hand whose partner it cannot share, taking the partner off.
    bool equipHeldAndStowPartner(unsigned actor, W8EquipSlot slot);
    StackMergeResult mergeStacks(W8ItemLocation destination, W8ItemLocation source);
    bool insertHeldInPool(unsigned index);
    // Combine the held item with another by recipe, or stack them; whether anything merged.
    std::expected<bool, InventoryRefusal> combine(unsigned actor, W8ItemLocation target);
    // Pour the source's uses into the destination.
    void mergeUses(unsigned actor, W8ItemLocation destination, W8ItemLocation source);
    // Sort the pool for the player; only once play has started.
    void sortPool();

    // ---- Weapon sets and ammunition

    // Bind one worn item to its wearer (PartyStorage::bind).
    void bind(W8Character& character, W8EquipSlot equip_slot);
    void commitWeaponSetSwap(unsigned int party_slot, W8WeaponSwap choice);
    bool swapWeaponSets(unsigned int party_slot, bool announce);
    // The combat UI's weapon-set swap, refused outside the member's own turn.
    void requestWeaponSetSwap(unsigned int party_slot, bool announce);
    // Spend what one attack used and ready the weapon again; whether the thrown weapon ran out.
    bool consumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used, W8EquipSlot paired);
    // Equip the primary weapon's off-hand partner and refill its shots before attacking.
    void readyPrimaryWeapon(unsigned party_slot);

    // ---- Using items

    /* Use one item as a character's action; a use whose location no longer holds its item
       does nothing. */
    W8ItemUseResult use(unsigned actor, const W8ItemUse& item_use);

    // ---- Equipment rule

    /* Equipment a member can no longer use is announced and stored like a received item; one
       that nothing can take stays equipped. Returns whether a weapon hand changed. Part of
       settling the party. */
    bool unequipUnusable(unsigned int party_slot);

private:
    struct ItemUseOutcome {
        W8ItemUseResult result;
        bool emptied = false;
    };

    PlayPhase phase() const;
    void finishEdit(std::span<const W8ItemLocation> replaced);
    void finishEdit(std::initializer_list<W8ItemLocation> replaced);
    void finish(const InventoryEdit& edit);
    void finishCharacterEdit(ItemRecipient recipient,
                             std::initializer_list<W8ItemLocation> replaced);
    void acquire(const std::optional<ItemPlacement>& placement, W8Character* receiver);
    std::optional<ItemPlacement> storeWithCharacter(ItemRecipient recipient, ItemSource source,
                                                    bool equip_if_possible, bool announce,
                                                    bool skip_stacking);
    std::optional<ItemPlacement> storeInPool(ItemSource source, bool announce, bool skip_stacking);
    std::optional<ItemPlacement> storeWithCharacterOrPoolFirst(ItemRecipient recipient,
                                                               ItemSource source, bool party_first,
                                                               bool announce,
                                                               bool equip_if_possible);
    bool dropUnstored(ItemSource source);
    W8ItemLocation upgradeClassItemIn(ItemRecipient recipient);
    ItemUseOutcome useOnce(unsigned int party_slot, W8ItemLocation location);

    Game& game_;
    PartyStorage storage_;
};
