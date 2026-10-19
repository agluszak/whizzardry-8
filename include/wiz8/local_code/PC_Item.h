#pragma once

#include <bitset>
#include <functional>
#include <memory>
#include <optional>
#include <string>

void ReleaseGenericItemNames(void);
struct SpriteSheet;
/* The item's inventory artwork, shared through the image asset cache. */
std::shared_ptr<const SpriteSheet> ItemSprites(int item_id);

#include "wiz8/character_skills.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/item_location.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/npc_trade_price.h"

struct W8ItemDatabaseRecord;
struct W8NpcState;
struct W8CombatSlot;
struct W8Action;

#include "wiz8/equipment_slots.h"

bool CanCharacterActivateItem(W8Character* character, const W8ItemInstance* item);

extern W8Skill g_item_spell_presentation[11];
/* The paper-doll icon of each of the twelve equipment slots. The
   two alternate-set hand slots have none, which is exactly the value the
   bound-item predicates refuse to hold a binding behind. */
extern int g_equip_slot_icons[12];
int GetItemInHand(void);

struct W8WeaponSwap {
    bool primary;
    bool secondary;
};
std::optional<W8WeaponSwap> PlanWeaponSetSwap(const W8Character& character);
void CommitWeaponSetSwap(int party_slot, W8WeaponSwap choice);
void SetHandType(W8Character* character, W8EquipSlot equip_slot);
unsigned int GetEquipmentBindingDifficulty(int character_index);
W8EquipSlot GetPairedEquipSlot(W8EquipSlot equip_slot);
std::string GetItemDisplayName(const W8ItemInstance* item);

enum { W8_GENERIC_ITEM_NAME_COUNT = 147 };
/* The per-item-class notice index. */
extern unsigned short g_generic_item_name_notice[W8_GENERIC_ITEM_NAME_COUNT];
/* One lazily built generic name per unidentified-name index. */
extern std::string g_generic_item_names[W8_GENERIC_ITEM_NAME_COUNT];
/* The message ids for what an item use did, read at every other entry; the
   last six are the item property labels. */
enum { W8_ITEM_PROPERTY_MESSAGE_FIRST = 19 };
extern unsigned short g_item_use_messages[25];

bool AddItemToParty(W8ItemInstance* item, bool announce, bool skip_stacking);
bool AddItemToPartyOrDrop(W8ItemInstance* item, bool announce);
bool AddItemToCharacter(W8Character* character, W8ItemInstance* item, bool equip_if_possible,
                        bool announce, bool skip_stacking);

void EmptyAllCarriedItems(W8Character* character);
bool TryIdentifyItemFor(W8Character* character, W8ItemInstance* item);
int GetItemSpell(const W8ItemInstance* item);
W8RangeCategory GetItemSpellRange(const W8ItemInstance* item);
void ApplyIdentifyAttempt(W8ItemInstance* item, unsigned int strength, unsigned int percent);
/* Reveal one character's worn bindings; 0 none bound, 1 some
   still hidden, 2 all revealed. */
int RevealCharacterItemBindings(unsigned int party_slot, int strength, unsigned int percent);
std::string FormatItemDisplayName(const W8ItemInstance* item, bool include_quantity);
unsigned int GetItemStackValue(const W8ItemInstance* item);
/* What an item search covers: worn equipment, also the backpack, and for the party
   also the shared pool. */
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
// Units of one item on the active party, a stack counting its size.
unsigned int CountPartyItem(int item_id);
// Whether every occupied member has one item.
bool EveryCharacterHasItem(int item_id, W8ItemReach reach);
unsigned int GetItemUnitWeight(const W8ItemInstance* item);
unsigned char GetItemEquipClass(const W8ItemInstance* item);
W8EquipSlot GetItemDefaultEquipSlot(int item_id);
W8Skill GetItemSpellPresentation(const W8ItemDatabaseRecord* record);
unsigned short GetItemEquipSlotMask(int item_id, bool primary_off_hand_free,
                                    bool alternate_off_hand_free, bool primary_main_hand_free,
                                    bool alternate_main_hand_free);
void CreateItemIntoHandOrPool(int item_id, bool quality);
void AddPartyGold(int amount, bool announce);

void SortPartyItemPool(void);
W8ItemInstance MakeItem(int item_id, bool maximum_quantity, bool force_identified,
                        bool mark_special);
void ReplaceHeldItem(W8ItemInstance item);
bool PlaceItemOnCursor(int item_id, bool maximum_quantity, bool force_identified,
                       bool mark_special);
bool EquipHeldItemAndStowPartner(unsigned actor, W8EquipSlot slot);
struct W8GameplayModifierBlock;
void ApplyPartyModifiers(W8GameplayModifierBlock modifiers);
void MergeItemUses(unsigned actor, W8ItemLocation destination, W8ItemLocation source);
void UpdateFactsAfterAcquiringItem(const W8ItemInstance* item);
void DeliverExceptionalItemReaction(W8ItemInstance* item, bool choose_character,
                                    W8Character* character);
/* How many of one item a character holds, counting a stack as its count and
   anything else as one. */
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach);
/* Move the departing character's soul-bound equipment to the
   party pool or the held-item display before the slot is released. */
void StashDepartingCharacterItems(W8Character* character);
void StagePartySlotItemUse(int party_slot, W8ItemInstance* item, const W8CombatSlot* target);
bool PartyAttemptsToIdentifyItem(W8ItemInstance* item, int);

bool CanItemLeaveItsSlot(const W8ItemInstance* item);
bool IsItemWornByCharacter(W8Character* character, const W8ItemInstance* item);
bool IsItemCarriedByCharacter(W8Character* character, const W8ItemInstance* item);
bool DropItemInHand(int unused);
void BindEquippedItem(W8Character* character, W8EquipSlot equip_slot);
bool CanUnequipSlotItem(const W8Character* character, W8EquipSlot equip_slot);
bool AreAllHandSlotsEmpty(const W8Character* character);
bool CanEquipItemInSlot(W8Character* character, int item_id, unsigned char equip_slot,
                        bool ignore_worn_items);
/* Merges the held stack into one existing stack, announcing on
   refusal. */
bool MergeItems(unsigned actor, W8ItemLocation target);

/* Same equipment class, and same unidentified display name. */
bool ItemsShareEquipClass(const W8ItemInstance* first, const W8ItemInstance* second);
bool ItemsShareUnidentifiedName(const W8ItemInstance* first, const W8ItemInstance* second);

bool CanCharacterUseItem(const W8Character* character, int item_id);

/* Equip-slot bucket used by the items-page realm filters: body locations are
   Head/Torso/Feet/Legs/Hands, accessory slots are Misc #1/#2 and Cloak, and
   the four weapon slots form the hand bucket. */
enum W8ItemEquipSlotGroup {
    W8_ITEM_EQUIP_GROUP_HAND = 2,
    W8_ITEM_EQUIP_GROUP_BODY = 3,
    W8_ITEM_EQUIP_GROUP_ACCESSORY = 4,
    W8_ITEM_EQUIP_GROUP_OTHER = 5
};
W8ItemEquipSlotGroup GetItemEquipSlotGroup(int item_id);

unsigned int CountIdentifyAttemptsNeeded(W8ItemInstance* item, unsigned int percent);

bool ItemClassNormalizesTarget(const W8ItemDatabaseRecord* record);

bool StoreItemWithCharacterOrParty(W8Character* character, W8ItemInstance* item, bool party_first,
                                   bool announce, bool equip_if_possible);
bool ItemHasHiddenProperties(int item_id);

/* Per-item-class notice index into g_localized_strings used for the
   unidentified ("Uncursed item" style) display name. */
unsigned short GetItemUnidentifiedNameIndex(const W8ItemInstance* item);

/* Gold price of a stack in the active trade context. price_kind uses
   W8TradePriceKind values. */
int CalculateTradeStackPrice(W8NpcState* npc, W8ItemInstance* item, W8TradePriceKind price_kind);
/* Whether any occupied, conscious party member can use the item. */
bool AnyPartyMemberCanUseItem(int item_id);

void BindCharacterItems(int party_slot, bool announce);
/* Whether an item is bound to whoever is wearing it, which is what stops it
   being taken off or swapped away. */
bool IsItemBoundToWearer(const W8ItemInstance* item);
bool RemovePartyItemByID(int item_id, bool remove_all);

bool CanUseItemForAction(int party_slot, const W8ItemInstance* item);

/* Whether the held item can share a hand pair with what the other hand holds. */
bool HeldItemFitsPairedSlot(int party_slot, W8EquipSlot equip_slot);

/* Hand one item to a party member, preferring the character or the
   party pool according to the flag exactly as StoreItemWithCharacterOrParty
   does, and then consume the source record. */
bool GiveItemToCharacterOrParty(int uiChar, W8ItemInstance* item, bool party_first);

/* The item-in-hand form of the same store, reached from the
   portrait screen, after the whole party has had its identification attempt. */
bool GiveHeldItemToCharacterOrParty(int uiChar, bool party_first);

/* Use one item as a character's action. `out_uses` receives the
   fatigue cost of the attempt, or -1 when nothing was attempted. */
unsigned char UseItem(unsigned actor, W8ItemLocation item, int* out_uses);

/* How hard one attempt at an item's spell is for a character of this skill
   level. */
unsigned int GetItemUseDifficulty(const W8Character* character, W8Skill skill,
                                  unsigned int skill_level, unsigned int spell_id,
                                  unsigned int power);
int CastItemSpell(W8Character* character, W8ItemInstance* item, unsigned int power);

void SetCharacterItemUse(int party_slot, const W8Action& action);
bool SwapWeaponSetSlots(int party_slot, bool announce);
void UpgradeProfessionClassItem(W8Character* character);

bool ItemPrecedesInPool(const W8ItemInstance& first, const W8ItemInstance& second);
void BindEveryPartyItem(void);
/* Whether the item's equip class is directly usable (0x17/0x19). */
bool IsUsableItemClass(W8ItemInstance* item);
/* Validate an item's embedded spell for use now; nonzero reports
   use blocked with the reason notice queued through the callback. */
bool ValidateItemSpellUse(int character_index, W8ItemInstance* item,
                          std::function<void(bool)> callback);
bool CharacterHasServiceItem(W8Character* character);
int CountUsableCharacterItems(W8Character* character);
/* Take gold from the party, clamping at zero. */
void SpendPartyGold(unsigned int amount);
/* Add recharge uses to a stackable item (hourly tick, item 0x266). */
void AddItemUses(W8ItemInstance* item, char uses);

/* Live inventory commands on the active party. Each one finishes its own
   consequences: settled statistics and equipment, combat choices that follow a
   change of weapons, and the held-item cursor. */

// Split or pick up a stack. Refusal leaves inventory unchanged.
bool SplitItemStack(W8ItemLocation source, unsigned amount, unsigned actor);

bool ExchangeInventoryItems(W8ItemLocation first, W8ItemLocation second, unsigned actor);
W8ItemInstance TakeInventoryItem(W8ItemLocation location);
bool ReceiveItemInHand(W8ItemInstance& item);

// Spend what one attack used and ready the weapon again.
bool ConsumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used, W8EquipSlot paired);
// Equip the primary weapon's off-hand partner and refill its shots before attacking.
void ReadyPrimaryWeapon(unsigned party_slot);

struct StackMergeResult {
    bool complete = false;
    bool changed = false;
};
StackMergeResult MergeInventoryStacks(W8ItemLocation destination, W8ItemLocation source);
bool InsertHeldItemInPool(unsigned index);

void RemoveInventoryQuantity(W8ItemLocation location, unsigned amount);

// Bring the active party's equipment, statistics and shared load in line with its
// current state, and apply what follows: unusable equipment comes off, a member
// whose hit points run out dies, and combat choices follow changed weapon hands.
void SettleActiveParty(std::bitset<W8_PARTY_SLOT_COUNT> changed_hands = {});
