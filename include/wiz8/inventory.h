#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

#include "glm/ext/vector_float2.hpp"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/inventory.h" // IWYU pragma: export - the item rules operations use.
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"

class Game;
struct W8Character;

// The item in the hand, where it was picked up from and by whom.
struct W8HeldItem {
    W8ItemInstance item;
    W8ItemLocation origin{};
    int actor = -1;
};

/* Who receives an item: a party member, or a character outside the party, such as a draft
   in party creation or an imported character. */
struct ItemRecipient {
    W8Character& character;
    // The member's slot; none for a character outside the party.
    std::optional<unsigned int> slot;
};
inline ItemRecipient Outsider(W8Character& character)
{
    return {character, std::nullopt};
}

struct W8ItemUseResult {
    // The spell engine's outcome; 1 is a success and 0 means nothing happened.
    unsigned char effect = 0;
    // The attempt's own fatigue cost; none leaves the action's ordinary cost.
    std::optional<int> fatigue_cost;
};

// Which destination a transfer tries first; the other is tried after.
enum class StoreOrder { CharacterFirst, PoolFirst };

/* The party's items. Characters own their equipment and backpack slots; the inventory owns
   the shared pool (packed, at most `kPoolLimit` entries) and the hand, and is the only
   thing that moves items between any of them. Every operation is complete when it returns:
   it checks its restrictions, moves the values, reports what was acquired, and settles the
   party (unusable equipment comes off, statistics and the shared load are recalculated,
   combat choices follow a change of weapons). A transfer that cannot place everything
   leaves the rest with its giver: an `item` parameter is what remains, emptied when all of
   it was stored. Locations are addresses for the duration of a call, not item identities;
   anything kept across operations is looked up again. */
class Inventory {
public:
    static constexpr std::size_t kPoolLimit = 500;

    // The pool keeps its capacity, so an item's address only changes when rows move.
    explicit Inventory(Game& game) : game_(game) { pool_.reserve(kPoolLimit); }
    Inventory(const Inventory&) = delete;
    Inventory& operator=(const Inventory&) = delete;

    // ---- Reading

    std::span<const W8ItemInstance> pool() const { return pool_; }
    // The item in the hand; none when the hand is empty.
    const W8HeldItem* held() const { return held_ ? &*held_ : nullptr; }
    // The held item's id; -1 when the hand is empty.
    int heldItemId() const { return held_ ? held_->item.iItemNo : -1; }
    /* The item at a location. An empty hand and a free pool row (below the limit) read as
       an empty item, as an emptied slot does; only an address no party has is none. */
    const W8ItemInstance* itemAt(W8ItemLocation location) const;
    // The first match: the hand, each member, then the pool.
    W8ItemLocation find(W8ItemMatch match, W8ItemReach reach = W8ItemReach::Party) const;
    bool has(int item_id, W8ItemReach reach = W8ItemReach::Party) const;
    // Units of one item on the whole party, a stack counting its size.
    unsigned int count(int item_id) const;
    // A use of the item at a location, expecting the item that is there now.
    W8ItemUse useAt(W8ItemLocation location) const;
    // Whether a use's location still holds the item it was chosen for.
    bool holds(const W8ItemUse& use) const;
    // Whether the held item may share a weapon set with the hand paired with `equip_slot`.
    bool heldFitsBeside(unsigned int party_slot, W8EquipSlot equip_slot) const;

    // ---- Receiving items

    ItemRecipient member(unsigned int party_slot);
    /* With a character: equipped when asked and allowed, else merged into its stacks or put
       in its first free backpack slot. A character outside the party is brought up to date
       on its own. */
    bool storeWithCharacter(ItemRecipient recipient, W8ItemInstance& item, bool equip,
                            bool announce, bool stack = true,
                            ItemReactions reactions = ItemReactions::Normal);
    // Merged into the pool's stacks in order, and the rest added at the front of the pool.
    bool storeInPool(W8ItemInstance& item, bool announce, bool stack = true,
                     ItemReactions reactions = ItemReactions::Normal);
    // The character and the pool in the order asked for.
    bool store(ItemRecipient recipient, W8ItemInstance& item, StoreOrder order, bool announce,
               bool equip, ItemReactions reactions = ItemReactions::Normal);
    // The pool, else the party's feet, else an empty hand.
    bool storeOrDrop(W8ItemInstance& item, bool announce);
    // The held item with a member: merged into its stacks or put in a free backpack slot.
    bool storeHeld(unsigned int party_slot);
    /* The held item, to a member and the pool in the order asked for. A refusal keeps the
       item held and names the slot when no destination was open. */
    std::expected<void, InventoryRefusal> giveHeld(unsigned int party_slot, StoreOrder order);
    /* Bought or stolen items, into the pool as one transfer: what the pool cannot take is
       dropped at the party's feet, or held when it may not be discarded and the hand is
       free. Nothing changes unless every item has a place; then `commit` runs (the payment)
       before the party settles and the acquisitions are reported. */
    enum class Receipt { Refused, Stored, Dropped };
    Receipt receiveAll(std::span<const W8ItemInstance> items, std::function_ref<void()> commit);
    /* A departing member's items go to the pool; equipment bound to its slot stays unless
       announced, iconless or worn by the dead. */
    void stashDeparting(unsigned int party_slot);
    // Empty a character outside the party of everything it carries.
    void emptyCharacter(W8Character& outsider, ItemReactions reactions = ItemReactions::Normal);
    // Give or raise the class item the character's current profession level earns.
    void upgradeClassItem(ItemRecipient recipient);

    // ---- Losing items

    // Remove an item id from the hand, the members and the pool; once per holder.
    bool removeById(int item_id, bool remove_all);
    // Take some units of a stack, or the whole item.
    void removeQuantity(W8ItemLocation location, unsigned int amount);
    // The item at a location, taken out; none when nothing is there.
    std::optional<W8ItemInstance> take(W8ItemLocation location);
    /* Change an item's own properties where it is: identification, charges, a one-shot
       effect. The edit may not empty or replace the item; moving items is an operation. */
    void editItem(W8ItemLocation location, std::function_ref<void(W8ItemInstance&)> edit);

    // ---- The hand

    // Put an item into the hand, replacing whatever was held.
    void replaceHeld(W8ItemInstance item);
    // The whole party tries to identify the held item.
    void identifyHeld();
    // Put an item into an empty hand; false (keeping the item) when the hand is full.
    bool hold(W8ItemInstance& item);
    // Drop the held item at the party's feet unless it may not be discarded.
    // False means geometry refused the drop; the hand keeps its item.
    std::expected<bool, InventoryRefusal> dropHeld(const glm::vec2& cursor);

    // ---- The player's moves

    /* Take some of a stack into the hand, or put the rest of the held stack back where it
       came from (else the actor's backpack, else the pool). Refusal changes nothing. */
    bool pickUp(W8ItemLocation source, unsigned int amount, unsigned int actor);
    /* Exchange a location with the hand, either of which may be empty. Equipment accepts
       only what its wearer may equip, and a bound item stays where it is. */
    bool swapWithHand(W8ItemLocation location, unsigned int actor);
    /* Put the held item into a member's equipment or backpack slot: an exchange, or for a
       hand whose partner cannot share it, an equip that takes the partner off into the
       backpack or the pool (unless the member's NPC keeps it). The receiver may remark on
       an exceptional item first. A refusal leaves the hand as it was. */
    std::expected<void, InventoryRefusal> placeHeld(unsigned int actor, W8ItemLocation location);
    /* Merge the held stack into the stack at a location. Returns the units moved; the rest
       stays held. */
    unsigned int mergeHeld(W8ItemLocation location);
    /* Put the held item into the pool at a row: merged into that row, then into every other
       stack, and the rest inserted at the row when asked. A full pool keeps the rest held;
       partial progress is distinct from refusal. */
    enum class Deposit { Refused, Partial, Complete };
    Deposit depositHeld(unsigned int row, bool insert_rest);
    /* Combine the held item with another: a recipe makes a new item and practises its
       skill; two stacks of one item merge. Returns whether anything changed. */
    std::expected<bool, InventoryRefusal> combineHeld(unsigned int actor, W8ItemLocation target);
    // Pour the held item's uses into the item at a location of the same kind.
    void pourHeldUses(unsigned int actor, W8ItemLocation destination);
    /* Bind an equipped item before it is taken off: an item that may not come off is
       refused (its binding announced); any other is marked bound. */
    std::expected<void, InventoryRefusal> claimEquipped(unsigned int actor, W8EquipSlot slot);
    // Sort the pool by class, kind, identification, value, stack and uses.
    void sortPool();

    // ---- Weapon sets and combat

    // Bind the active hands' items, announcing a binding that takes hold.
    void bindHands(unsigned int party_slot);
    void commitWeaponSwap(unsigned int party_slot, W8WeaponSwap choice);
    /* Swap a member's weapon sets now, when they can: the hands bind first, and when
       announcing, the notice says why they cannot. */
    bool swapWeaponSets(unsigned int party_slot, bool announce);
    /* The combat UI's weapon-set swap, refused outside the member's own turn; it clears a
       queued swap. */
    void requestWeaponSwap(unsigned int party_slot, bool announce);
    // Spend what one attack used and ready the weapon again; whether a thrown weapon ran out.
    bool consumeAttackItems(unsigned int actor, unsigned int hand, W8EquipSlot used,
                            W8EquipSlot paired);
    // Equip the primary weapon's off-hand partner and refill its shots before attacking.
    void readyPrimaryWeapon(unsigned int party_slot);

    // Use one item as a member's action; a use whose item has moved does nothing.
    W8ItemUseResult use(unsigned int actor, const W8ItemUse& use);

    /* Part of settling the party: equipment a member can no longer use comes off and is
       stored like a received item; what nothing takes stays equipped. Returns whether a
       weapon hand changed. */
    bool unequipUnusable(unsigned int party_slot, ItemReactions reactions);

    // ---- Saves

    // The pool and the hand a save holds, replacing the current ones.
    void restore(std::vector<W8ItemInstance> pool, std::optional<W8ItemInstance> held);

private:
    W8ItemInstance* slotAt(W8ItemLocation location);
    void remove(W8ItemLocation location);
    void exchange(W8ItemLocation first, W8ItemLocation second, int actor);
    bool spendUse(W8ItemLocation location, bool remove_exhausted);

    // Where a placement put an item, and who is said to have acquired it.
    struct Placement {
        // The item acquired; none for a placement straight into equipment.
        W8ItemInstance* received = nullptr;
        // The character who took it; none for the pool.
        W8Character* receiver = nullptr;
        // The location a new entry filled; None after merging into a stack.
        W8ItemLocation stored{};
    };
    std::optional<Placement> placeWithCharacter(ItemRecipient recipient, W8ItemInstance& item,
                                                bool equip, bool announce, bool stack);
    std::optional<Placement> placeInPool(W8ItemInstance& item, bool announce, bool stack);
    std::optional<Placement> placeEither(ItemRecipient recipient, W8ItemInstance& item,
                                         StoreOrder order, bool announce, bool equip);
    void acquire(const std::optional<Placement>& placement,
                 ItemReactions reactions = ItemReactions::Normal);
    bool dropUnstored(W8ItemInstance& item, W8ItemLocation origin = {}, int actor = -1);
    bool stowPartner(unsigned int actor, W8EquipSlot slot);
    void supersedeQueuedSwap(W8ItemLocation location);

    void bind(unsigned int party_slot, W8EquipSlot equip_slot);
    W8ItemLocation equipPartner(unsigned int party_slot, const W8ItemInstance& item, int item_id,
                                W8EquipSlot equip_slot);
    W8ItemLocation mergePartnerUses(unsigned int party_slot, W8ItemInstance& item);
    void splitThrowableStack(unsigned int party_slot, W8EquipSlot equip_slot);
    bool pourUses(W8ItemInstance& into, W8ItemLocation from);
    struct UseOutcome {
        W8ItemUseResult result;
        bool emptied = false;
    };
    UseOutcome useOnce(unsigned int party_slot, W8ItemLocation location);

    // What every operation owes after changing items.
    void settle(std::span<const W8ItemLocation> changed,
                ItemReactions reactions = ItemReactions::Normal);
    // A short fixed list, which lives through the call.
    void settle(std::initializer_list<W8ItemLocation> changed,
                ItemReactions reactions = ItemReactions::Normal)
    {
        settle(std::span<const W8ItemLocation>(changed.begin(), changed.size()), reactions);
    }
    void settleOutsider(const ItemRecipient& recipient, std::span<const W8ItemLocation> changed,
                        ItemReactions reactions = ItemReactions::Normal);
    void settleOutsider(const ItemRecipient& recipient,
                        std::initializer_list<W8ItemLocation> changed,
                        ItemReactions reactions = ItemReactions::Normal)
    {
        settleOutsider(recipient, std::span<const W8ItemLocation>(changed.begin(), changed.size()),
                       reactions);
    }

    Game& game_;
    std::vector<W8ItemInstance> pool_;
    std::optional<W8HeldItem> held_;
};

// The gold goes into the purse and the level's running tally; announcing it plays a sound.
void AddPartyGold(Game& game, int amount, bool announce);
