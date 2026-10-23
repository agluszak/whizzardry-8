#pragma once

#include <functional>
#include <memory>
#include <string>

struct SpriteSheet;
/* The item's inventory artwork, shared through the image asset cache. */
std::shared_ptr<const SpriteSheet> ItemSprites(int item_id);

#include "wiz8/character_skills.h"
#include "wiz8/dialog_code/DialogBase.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/npc_trade_price.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"

class Game;
struct W8ItemDatabaseRecord;
struct W8NpcState;
struct W8CombatSlot;
struct W8Action;

#include "wiz8/rules/equipment_slots.h"

bool CanCharacterActivateItem(W8Character* character, const W8ItemInstance* item);

extern W8Skill g_item_spell_presentation[11];

unsigned int GetEquipmentBindingDifficulty(int character_index);

/* The message ids for what an item use did, read at every other entry; the
   last six are the item property labels. */
enum { W8_ITEM_PROPERTY_MESSAGE_FIRST = 19 };
extern unsigned short g_item_use_messages[25];

bool TryIdentifyItemFor(W8Character* character, W8ItemInstance* item);
int GetItemSpell(const W8ItemInstance* item);
W8RangeCategory GetItemSpellRange(const W8ItemInstance* item);
void ApplyIdentifyAttempt(W8ItemInstance* item, unsigned int strength, unsigned int percent);
/* Reveal one character's worn bindings; 0 none bound, 1 some
   still hidden, 2 all revealed. */
int RevealCharacterItemBindings(unsigned int party_slot, int strength, unsigned int percent);
unsigned int GetItemStackValue(const W8ItemInstance* item);
unsigned int GetItemUnitWeight(const W8ItemInstance* item);
unsigned char GetItemEquipClass(const W8ItemInstance* item);
W8Skill GetItemSpellPresentation(const W8ItemDatabaseRecord* record);
void UpdateFactsAfterAcquiringItem(const W8ItemInstance* item);
void DeliverExceptionalItemReaction(Game& game, W8ItemInstance* item, bool choose_character,
                                    W8Character* character);
void StagePartySlotItemUse(int party_slot, W8ItemLocation location, const W8CombatSlot* target);
bool PartyAttemptsToIdentifyItem(W8ItemInstance* item, int);

bool CanItemLeaveItsSlot(const W8ItemInstance* item);
bool IsItemWornByCharacter(W8Character* character, const W8ItemInstance* item);
bool IsItemCarriedByCharacter(W8Character* character, const W8ItemInstance* item);

bool AreAllHandSlotsEmpty(const W8Character* character);

/* Same equipment class, and same unidentified display name. */
bool ItemsShareEquipClass(const W8ItemInstance* first, const W8ItemInstance* second);
bool ItemsShareUnidentifiedName(const W8ItemInstance* first, const W8ItemInstance* second);

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

unsigned int CountIdentifyAttemptsNeeded(const W8ItemInstance* item, unsigned int percent);

bool ItemClassNormalizesTarget(const W8ItemDatabaseRecord* record);

/* Per-item-class notice index into Data().strings used for the
   unidentified ("Uncursed item" style) display name. */
unsigned short GetItemUnidentifiedNameIndex(const W8ItemInstance* item);

/* Gold price of a stack in the active trade context. price_kind uses
   W8TradePriceKind values. */
int CalculateTradeStackPrice(W8NpcState* npc, W8ItemInstance* item, W8TradePriceKind price_kind);
/* Whether any occupied, conscious party member can use the item. */
bool AnyPartyMemberCanUseItem(int item_id);

/* Whether an item is bound to whoever is wearing it, which is what stops it
   being taken off or swapped away. */
bool IsItemBoundToWearer(const W8ItemInstance* item);

bool CanUseItemForAction(int party_slot, const W8ItemInstance* item);

/* Whether the held item can share a hand pair with what the other hand holds. */

/* How hard one attempt at an item's spell is for a character of this skill
   level. */
unsigned int GetItemUseDifficulty(const W8Character* character, W8Skill skill,
                                  unsigned int skill_level, unsigned int spell_id,
                                  unsigned int power);
int CastItemSpell(W8Character* character, W8ItemInstance* item, unsigned int power);

void SetCharacterItemUse(int party_slot, const W8Action& action);
void BindEveryPartyItem(void);
/* Whether the item's equip class is directly usable (0x17/0x19). */
bool IsUsableItemClass(const W8ItemInstance* item);
/* Validate an item's embedded spell for use now; nonzero reports
   use blocked with the reason notice queued through the callback. A use that
   aims at once (the use-item picker's) also needs a valid target. */
bool ValidateItemSpellUse(int character_index, const W8ItemInstance* item,
                          std::function<void(bool)> callback, bool needs_target);
bool CharacterHasServiceItem(W8Character* character);
int CountUsableCharacterItems(W8Character* character);
