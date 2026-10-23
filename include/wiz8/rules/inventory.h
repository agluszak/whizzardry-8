#pragma once

#include <bitset>
#include <functional>
#include <optional>
#include <string>

#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"

struct GameData;
struct W8Character;
struct W8GameplayModifierBlock;
struct W8Party;

/* Item rules over values and characters: what stacks with what, where an item goes on a
   character, how a use is spent. They never touch the party's shared pool or the hand,
   which the game's `Inventory` owns (wiz8/inventory.h), and report nothing: the
   inventory decides what an operation announces and what it sets off. */

// Why an inventory operation was turned down, worded for the player.
struct InventoryRefusal {
    std::string notice;
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
// Where a party member carries the first match; None when it carries none.
W8ItemLocation FindMemberItem(const W8Party& party, unsigned int slot, W8ItemMatch match,
                              W8ItemReach reach);
// Units of one item, a stack counting its size.
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach);
bool EveryCharacterHasItem(const W8Party& party, int item_id, W8ItemReach reach);

// ---- Quantities

/* Move as many units of a stack onto another as it has room for, and return how many
   moved. Only identified-alike stacks of one stacking item merge, and a stack already
   over its record's maximum (a legacy save) takes nothing more. Emptying the source is
   its owner's decision. */
unsigned int MergeStack(const GameData& data, W8ItemInstance& destination, W8ItemInstance& source);
/* Spend one use of an item: a stack's unit or a charge. Returns whether the item is used
   up and should go. An exhausted charged item goes only when asked, and one whose kind
   has hidden properties stays even then. */
bool SpendItemUse(const GameData& data, W8ItemInstance& item, bool remove_exhausted);
// Add recharge uses to an item, never past what it can hold.
void AddItemUses(const GameData& data, W8ItemInstance& item, signed char uses);
// Pool order: by class, kind, identification, value, stack and uses, all descending.
bool ItemPrecedesInPool(const GameData& data, const W8ItemInstance& first,
                        const W8ItemInstance& second);

// ---- Placing an item with a character

/* Where the last of an item went on a character. `equipped` says it went straight into
   the equipment slot it was offered for, which acquires nothing; `merged` says it joined a
   stack already there rather than filling an empty slot. */
struct CharacterPlacement {
    W8ItemStorage storage = W8ItemStorage::None;
    unsigned int slot = 0;
    bool equipped = false;
    bool merged = false;
};
/* Place an item with a character: equipped when asked and allowed, else merged into the
   character's stacks, else into the first free backpack slot. `item` keeps whatever was
   not placed. Before play, party creation fills the hands its own way. Returns where the
   last of the item went; none when some of it is left. */
std::optional<CharacterPlacement> PlaceWithCharacter(const GameData& data, W8Character& character,
                                                     W8ItemInstance& item, bool equip, bool stack,
                                                     bool game_started);

// ---- Equipment

/* The first equipped slot whose item the character can no longer use, skipping `kept`.
   The character's inputs are evaluated first, since taking one item off can remove a
   bonus another one needs. */
std::optional<unsigned int> FindUnusableEquipment(const GameData& data, W8Character& character,
                                                  const W8GameplayModifierBlock* modifiers,
                                                  std::bitset<W8_EQUIP_SLOT_COUNT> kept);
// The notice announcing that a character took off an item it can no longer use.
std::string UnusableEquipmentNotice(const GameData& data, const W8Character& character,
                                    const W8ItemInstance& item);
/* Bind one worn item to its wearer. Returns the announcement of a binding that has just
   taken hold. The alternate hands bind nothing. */
std::optional<std::string> BindEquippedItem(const GameData& data, W8Character& character,
                                            W8EquipSlot equip_slot);
struct W8WeaponSwap {
    bool primary;
    bool secondary;
};
std::optional<W8WeaponSwap> PlanWeaponSetSwap(const GameData& data, const W8Character& character);
