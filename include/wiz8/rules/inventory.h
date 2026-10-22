#pragma once

#include <bitset>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/data/skills.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/targeting.h"

struct GameData;
class Events;
class Rng;
struct W8Character;
struct W8Party;

/* Party storage: searching, placing and editing the items a party carries in its members'
   equipment and backpacks, the shared pool and the hand. PartyStorage applies the rules to
   one party and reports notices and speech as events. Its edits return what they changed:
   the game's inventory commands (inventory.h) apply what acquiring an item sets off and
   settle the party after every edit. */

// Why an inventory edit was turned down, worded for the player.
struct InventoryRefusal {
    std::string notice;
};

/* What an edit leaves for the party's settlement: the locations it replaced or emptied (a
   change of quantity alone is not a replacement), and whether the party settles at all; a
   request accepted without changing anything does not. */
struct InventoryEdit {
    std::vector<W8ItemLocation> replaced;
    bool settle = true;
};

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
bool ItemIsOfKind(const GameData& data, const W8ItemInstance& item, short item_kind);
inline auto ItemOfKind(const GameData& data, short item_kind)
{
    return [&data, item_kind](const W8ItemInstance& item) {
        return ItemIsOfKind(data, item, item_kind);
    };
}
// The first match in equipment slot order, then in backpack order.
W8ItemInstance* FindCharacterItem(W8Character& character, W8ItemMatch match, W8ItemReach reach);
const W8ItemInstance* FindCharacterItem(const W8Character& character, W8ItemMatch match,
                                        W8ItemReach reach);
// The first match on the party: the hand, each occupied member, then the pool.
W8ItemLocation FindPartyItem(const W8Party& party, W8ItemMatch match, W8ItemReach reach);
inline bool PartyHasItem(const W8Party& party, int item_id, W8ItemReach reach = W8ItemReach::Party)
{
    return FindPartyItem(party, ItemWithId(item_id), reach).storage != W8ItemStorage::None;
}
// Units of one item, a stack counting its size.
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach);
unsigned int CountPartyItem(const W8Party& party, int item_id);
bool EveryCharacterHasItem(const W8Party& party, int item_id, W8ItemReach reach);
// The held item's id, or -1.
int GetItemInHand(const W8Party& party);
// A use of the item at a location, expecting the item that is there now.
W8ItemUse ItemUseAt(const W8Party& party, W8ItemLocation location);
// The item a use names, while its location still holds the item the use was chosen for.
W8ItemInstance* ResolveItemUse(W8Party& party, const W8ItemUse& use);

// ---- Placing items

/* Who receives an item: a party member, or a character outside the party, such as a draft
   in party creation. */
struct ItemRecipient {
    W8Character& character;
    // The member's slot; none for a character outside the party.
    std::optional<unsigned int> slot;
};
/* An item to place: at a location in the party, or a value the caller owns. A stored item
   leaves its source; a refused one stays there, and a stack merged in part keeps the rest. */
struct ItemSource {
    W8ItemInstance& item;
    // Where the item is in the party; none for a value the caller owns.
    std::optional<W8ItemLocation> location;
};
ItemSource PartyItem(W8Party& party, W8ItemLocation location);
inline ItemSource OwnedItem(W8ItemInstance& item)
{
    return {item, std::nullopt};
}
/* Where a placement put an item. `stored` is the location a new entry filled; it is None
   after merging into a stack and for a recipient outside the party. `received` is the item
   the acquisition is reported for, valid until the next edit: retail reports the backpack
   cell at the stored index even after merging into the equipment slot of that index, and
   the pool entry that took the item. */
struct ItemPlacement {
    W8ItemLocation stored;
    W8ItemInstance* received = nullptr;
};
/* The first piece of equipment a member can no longer use, taken off and announced once
   play has started; none when everything is usable. Taking one item off can remove a bonus
   another one needs, so the member's inputs are evaluated again each time. */
struct UnusableEquipment {
    W8ItemInstance item;
    bool weapon_hand = false;
    unsigned int equip_slot = 0;
    std::string notice;
};

// ---- Losing items and gold

// Take gold from the party, clamping at zero.
void SpendPartyGold(W8Party& party, unsigned int amount);

// ---- Player edits

/* Equip the held item in a hand whose partner it cannot share, taking the partner off. With
   a full backpack the partner goes to the pool, unless the member's NPC wants to keep it. */
struct StowedPartner {
    InventoryEdit edit;
    // Where the partner was stored, which acquired it; none when it went into the hand.
    std::optional<ItemPlacement> stored;
};
struct StackMerge {
    bool complete = false;
    InventoryEdit edit;
};
/* Combining the held item with another: a recipe makes a new item, and practice of the
   recipe's skill follows; otherwise two stacks of one item merge. */
struct ItemCombination {
    // A recipe was made or units were stacked.
    bool merged = false;
    InventoryEdit edit;
    // The skill a recipe practises and by how much.
    std::optional<W8Skill> practised_skill;
    unsigned int practice = 0;
};
// Add recharge uses to an item, never past what it can hold.
void AddItemUses(const GameData& data, W8ItemInstance& item, signed char uses);
// Pool order: by class, kind, identification, value, stack and uses, all descending.
bool ItemPrecedesInPool(const GameData& data, const W8ItemInstance& first,
                        const W8ItemInstance& second);

// ---- Weapon sets and ammunition

struct W8WeaponSwap {
    bool primary;
    bool secondary;
};
std::optional<W8WeaponSwap> PlanWeaponSetSwap(const GameData& data, const W8Character& character);
/* Spend what one attack used and ready the weapon again. `thrown_spent` says the thrown
   weapon ran out. */
struct AttackItemsSpent {
    bool thrown_spent = false;
    InventoryEdit edit;
};

// ---- The rules over one party

/* A party's storage, with the data it reads, the random stream it draws from and the log it
   reports to. Before play has started, party creation fills hands its own way and nothing
   is announced. It borrows all of them and lives no longer than they do. */
class PartyStorage {
public:
    PartyStorage(const GameData& data, W8Party& party, Rng& rng, Events& events,
                 const bool& game_started)
        : data_(data), party_(party), rng_(rng), events_(events), game_started_(game_started)
    {
    }

    // ---- Placing items

    /* Place an item with a character: equipped when asked and allowed, else merged into a
       stack or put in the first free backpack slot. Announcing names it in a notice. */
    std::optional<ItemPlacement> placeWithCharacter(ItemRecipient recipient, ItemSource source,
                                                    bool equip_if_possible, bool announce,
                                                    bool skip_stacking);
    // Merge into a pool stack, else add a new entry at the front of the pool.
    std::optional<ItemPlacement> placeInPool(ItemSource source, bool announce, bool skip_stacking);
    // Put an item into an empty hand; false when the hand holds something.
    bool placeInEmptyHand(ItemSource source);
    /* Take one use off an item, a stack's unit or a charge, and empty it after the last one.
       An exhausted charged item is emptied only when asked, and one with hidden properties
       is kept. Returns whether the location was emptied. */
    bool consumeUse(W8ItemLocation location, bool remove_exhausted);
    // See UnusableEquipment.
    std::optional<UnusableEquipment>
    takeUnusableEquipment(unsigned int party_slot, std::bitset<W8_EQUIP_SLOT_COUNT> kept = {});

    // ---- Losing items

    // Remove an item id from the hand, the members and the pool; once per holder.
    InventoryEdit removeItemById(int item_id, bool remove_all);
    // Take some units of a stack; none when nothing is there.
    std::optional<InventoryEdit> removeQuantity(W8ItemLocation location, unsigned amount);
    // The item at a location, taken out; none when nothing is there.
    std::optional<W8ItemInstance> take(W8ItemLocation location);

    // ---- Player edits

    // Split or pick up a stack; none when refused, which leaves inventory unchanged.
    std::optional<InventoryEdit> splitStack(W8ItemLocation source, unsigned amount, unsigned actor);
    // Exchange two locations; equipment slots accept only what their wearer may equip.
    std::optional<InventoryEdit> exchange(W8ItemLocation first, W8ItemLocation second,
                                          unsigned actor);
    // See StowedPartner.
    std::optional<StowedPartner>
    equipHeldAndStowPartner(unsigned actor, W8EquipSlot slot,
                            std::function_ref<bool(const W8ItemInstance&)> npc_keeps);
    // Fold as much of one stack onto another as fits; none when nothing moved.
    std::optional<StackMerge> mergeStacks(W8ItemLocation destination, W8ItemLocation source);
    std::optional<InventoryEdit> insertHeldInPool(unsigned index);
    /* Combine the held item with another. None when either item is missing; the refusal of
       an unidentified item, a recipe beyond the character's skill or two items that do not
       combine. */
    std::expected<std::optional<ItemCombination>, InventoryRefusal> combine(unsigned actor,
                                                                            W8ItemLocation target);
    // Pour the source's uses into the destination; none when they do not share an item.
    std::optional<InventoryEdit> mergeUses(unsigned actor, W8ItemLocation destination,
                                           W8ItemLocation source);
    // Sort the pool by ItemPrecedesInPool; only once play has started.
    void sortPool();

    // ---- Weapon sets and ammunition

    /* Bind one worn item to its wearer; an unannounced binding is announced as it takes
       hold. The alternate hands bind nothing. */
    void bind(W8Character& character, W8EquipSlot equip_slot);
    InventoryEdit commitWeaponSetSwap(unsigned int party_slot, W8WeaponSwap choice);
    /* Whether a member's weapon sets can swap now: the hands' items bind first, and the
       notice says why when they cannot (to the given text box context). None when nothing
       would swap. */
    std::optional<W8WeaponSwap> prepareWeaponSetSwap(unsigned int party_slot, bool announce,
                                                     short notice_context);
    // See AttackItemsSpent.
    AttackItemsSpent consumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used,
                                        W8EquipSlot paired);
    // Equip the primary weapon's off-hand partner and refill its shots before attacking.
    InventoryEdit readyPrimaryWeapon(unsigned party_slot);

private:
    W8EquipSlot chooseEquipSlot(W8Character& character, int item_id);
    W8ItemLocation equipPartner(unsigned int party_slot, const W8ItemInstance& item, int item_id,
                                W8EquipSlot equip_slot);
    W8ItemLocation mergePartnerUses(unsigned int party_slot, W8ItemInstance& item);
    void splitThrowableStack(unsigned int party_slot, W8EquipSlot equip_slot);

    const GameData& data_;
    W8Party& party_;
    Rng& rng_;
    Events& events_;
    const bool& game_started_;
};
