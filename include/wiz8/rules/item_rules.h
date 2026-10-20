#pragma once

#include <array>
#include <string>

#include "wiz8/rules/character.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"

class Rng;
struct GameData;
struct W8ItemDatabaseRecord;

unsigned int GetItemStackWeight(const GameData& data, const W8ItemInstance* item);
int WeaponHandKind(const GameData& data, const W8ItemInstance& primary,
                   const W8ItemInstance& secondary, unsigned int hand);
std::array<W8HandAttack, 2> EvaluateWeaponHands(const GameData& data, const W8ItemInstance& primary,
                                                const W8ItemInstance& secondary);

bool CompatiblePartnerItems(const GameData& data, int ranged_item_id, int other_item_id);
bool ItemHasSingledOutGenericName(const GameData& data, int item_id);
bool ItemUsesShots(const GameData& data, int item_id);

bool GetItemMergeKind(const GameData& data, int item_id, short* related_kind);

// ---- Making and naming items

/* A new item: its quantity rolled from the record's dice, or their maximum when asked,
   identified when forced or when the record identifies itself, and bound when marked.
   Placing it is a separate inventory operation. */
W8ItemInstance MakeItem(const GameData& data, Rng& rng, int item_id, bool maximum_quantity,
                        bool force_identified, bool mark_special);

enum { W8_GENERIC_ITEM_NAME_COUNT = 147 };
// The message naming each unidentified-name kind.
extern const unsigned short g_generic_item_name_notice[W8_GENERIC_ITEM_NAME_COUNT];
/* What to call an item: an identified one by its own name, an unidentified one by the
   generic name of its kind. */
std::string GetItemDisplayName(const GameData& data, const W8ItemInstance* item);
// The display name with the stack count when asked for.
std::string FormatItemDisplayName(const GameData& data, const W8ItemInstance* item,
                                  bool include_quantity);
// Whether an item has anything left to identify beyond its name.
bool ItemHasHiddenProperties(const GameData& data, int item_id);

// ---- Equipment slots

// The hand opposite the one given; anything that is not a hand has none.
W8EquipSlot GetPairedEquipSlot(W8EquipSlot equip_slot);
/* Whether a slot has a paper-doll position. The alternate hands have none, so a binding
   never holds an item there. */
bool HasPaperDollPosition(W8EquipSlot equip_slot);
/* Where an item of this kind wants to go. Once play has started, ranged weapons and
   ammunition go to the alternate pair of hands, which party creation does not fill. */
W8EquipSlot GetItemDefaultEquipSlot(const GameData& data, int item_id, bool game_started);
/* Every slot this item could be placed in, a bit per slot, given which of the four hands
   are free. */
unsigned short GetItemEquipSlotMask(const GameData& data, int item_id, bool primary_off_hand_free,
                                    bool alternate_off_hand_free, bool primary_main_hand_free,
                                    bool alternate_main_hand_free);
/* Whether the character could put this item in one particular slot; ignoring what is
   worn answers for an empty character instead. */
bool CanEquipItemInSlot(const GameData& data, const W8Character* character, int item_id,
                        unsigned char equip_slot, bool ignore_worn_items);
// Whether two items may share the two hands of one weapon set.
bool CanHoldItemsTogether(const GameData& data, int first_item_id, int second_item_id);
/* Whether the item worn in one slot may be taken off: an unannounced binding holds it,
   except in the alternate hands, which have no paper-doll position, or on the dead. */
bool CanUnequipSlotItem(const GameData& data, const W8Character* character, W8EquipSlot equip_slot);

// ---- Using items

/* The first attribute or skill requirement the character misses, with the message
   naming it. */
bool FindUnmetItemRequirement(const W8Character* character, const W8ItemDatabaseRecord* record,
                              unsigned short* message_id);
/* Whether a character may use an item at all: profession, race and gender admit them,
   every requirement is met, and a spell-bearing item is one they have not learned or are
   strong enough to trigger. */
bool CanCharacterUseItem(const GameData& data, const W8Character* character, int item_id);
// The lowest caster level that may cast a spell of this level.
int MinimumCasterLevelForSpellLevel(int spell_level);
