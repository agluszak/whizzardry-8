#include "wiz8/inventory.h"

#include <algorithm>
#include <array>
#include <bitset>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <fmt/printf.h>

#include "layouts/levels.h"
#include "layouts/main_game_screen.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/character_skills.h"
#include "wiz8/character_stats.h"
#include "wiz8/cursor.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/fonts.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/item_rules.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatAttack.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayDatabase.h"
#include "wiz8/local_code/GameplayMods.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/UtilityFunctions.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_screens/CharacterScreen.h"
#include "wiz8/local_screens/MGSPortraitCombat.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/local_screens/MGSUseItemSelect.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/local_screens/ReviewCharacterScreen.h"
#include "wiz8/local_screens/Screens.h"
#include "wiz8/notices.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/party_stats.h"
#include "wiz8/platform/assertion.h"
#include "wiz8/platform/random.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/equipment_slots.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/screen.h"
#include "wiz8/world/level_session.h"
#include "xstatus.h"

enum { W8_ITEM_USE_TYPE_COUNT = 10 };

static W8EquipSlot ChooseCharacterEquipSlot(W8Character* character, int item_id);
static bool MergeStack(W8ItemInstance* destination, W8ItemInstance* source, bool* partially_merged);
static std::optional<W8ItemLocation> StoreWithCharacter(W8Character* character,
                                                        W8ItemInstance* item,
                                                        bool equip_if_possible, bool announce,
                                                        bool skip_stacking);
static std::optional<W8ItemLocation> StoreInPool(W8ItemInstance* item, bool announce,
                                                 bool skip_stacking);
static bool ConsumeItemUse(W8ItemInstance* item, bool remove_exhausted);
static void DropUnstoredCharacterItem(W8ItemInstance* item);

// ---- Storage helpers

static bool IsHandSlot(unsigned int slot)
{
    return slot == W8_EQUIP_SLOT_PRIMARY_WEAPON || slot == W8_EQUIP_SLOT_SECONDARY_WEAPON;
}

/* Empty an item wherever the active party stores it. */
static void DiscardItem(W8ItemInstance* item)
{
    if (const auto location = g_status.party.locateItem(item);
        location.storage != W8ItemStorage::None)
        g_status.party.take(location);
    else
        *item = {};
}

/* Move a complete item into an empty location and empty its source. Either side may be
   a value outside the party; moving into the hand records where the item came from. */
static void MoveItem(W8ItemInstance* destination, W8ItemInstance* source)
{
    auto& party = g_status.party;
    const auto to = party.locateItem(destination);
    const auto from = party.locateItem(source);
    if (to.storage != W8ItemStorage::None && from.storage != W8ItemStorage::None) {
        const bool carried =
            from.storage == W8ItemStorage::Equipment || from.storage == W8ItemStorage::Backpack;
        party.exchange(to, from, carried ? static_cast<int>(from.character) : -1);
        return;
    }
    *destination = from.storage != W8ItemStorage::None ? party.take(from)
                                                       : std::exchange(*source, W8ItemInstance{});
    destination->bind_announced = false;
    if (to.storage == W8ItemStorage::Hand) party.held = {*destination, {}, -1};
}

/* The most of one item a character can hold at once: the record's own quantity
   dice taken at their maximum. */
static int MaximumQuantity(int item_id)
{
    return g_item_records[item_id].initial_quantity.Maximum();
}

/* Normalize a stack and split every full overflow stack into the party pool.
   Quantity kinds two through four live in uses_or_charges instead. */
static void NormalizeItemStack(W8ItemInstance* item)
{
    if (item->iItemNo == -1) {
        return;
    }

    const W8ItemDatabaseRecord* record = &g_item_records[item->iItemNo];
    if (record->quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (item->uses_or_charges != 0) {
            if (item->stack_count == 0) {
                item->stack_count = item->uses_or_charges;
            }
            item->uses_or_charges = 0;
        }
    } else if (record->quantity_kind >= W8_ITEM_QUANTITY_CHARGES &&
               record->quantity_kind <= W8_ITEM_QUANTITY_SHOTS && item->stack_count != 0) {
        if (item->uses_or_charges == 0) {
            item->uses_or_charges = item->stack_count;
        }
        item->stack_count = 0;
    }

    if (record->quantity_kind != W8_ITEM_QUANTITY_STACK) {
        return;
    }
    if (record->maximum_quantity == 0) {
        item->stack_count = 0;
        return;
    }
    while (item->stack_count > record->maximum_quantity) {
        W8ItemInstance split = *item;
        unsigned char quantity = item->stack_count - record->maximum_quantity;
        if (quantity > record->maximum_quantity) {
            quantity = record->maximum_quantity;
        }
        split.stack_count = quantity;

        if (!g_status.party.insertIntoPool(g_status.party.item_count, split)) {
            item->stack_count = record->maximum_quantity;
            return;
        }

        item->stack_count -= quantity;
        record = &g_item_records[item->iItemNo];
    }
}

/* Merge as much of one stack as fits in another.  A completely consumed
   source is removed from its owner; a partial merge is reported separately so
   the caller can refresh carrying capacity before placing the remainder. */
static bool MergeStack(W8ItemInstance* destination, W8ItemInstance* source, bool* partially_merged)
{
    if (destination->iItemNo == -1) {
        return false;
    }

    NormalizeItemStack(destination);
    const W8ItemDatabaseRecord* record = &g_item_records[destination->iItemNo];
    if (record->quantity_kind != W8_ITEM_QUANTITY_STACK) {
        return false;
    }
    if (destination->stack_count > record->maximum_quantity) {
        Fail("FALSE",
             fmt::format(
                 "StackItemsIfPossible: ERROR - slot holds {:d} when max is {:d}, item {:d}({})",
                 destination->stack_count, record->maximum_quantity, destination->iItemNo,
                 FormatItemDisplayName(destination, true)));
        destination->stack_count = record->maximum_quantity;
    }
    if (destination->iItemNo != source->iItemNo || destination->identified != source->identified) {
        return false;
    }

    unsigned char room = record->maximum_quantity - destination->stack_count;
    if (room == 0) {
        return false;
    }
    unsigned char moved = source->stack_count < room ? source->stack_count : room;
    destination->stack_count += moved;
    source->stack_count -= moved;
    if (source->stack_count == 0) {
        DiscardItem(source);
        return true;
    }
    if (partially_merged != 0) {
        *partially_merged = true;
    }
    return false;
}

/* Take one of an item's uses away, and when that was the last one, empty the
   record. A stack counts down its count and a charged item its charges; an
   already-exhausted charged item can only be cleared when the caller allows
   it, and one whose kind has hidden properties is kept even then. Either way
   the emptied record leaves the packed party pool behind it. Returns whether
   the record was emptied. */
static bool ConsumeItemUse(W8ItemInstance* item, bool remove_exhausted)
{
    int item_id = item->iItemNo;

    if (item_id == -1) {
        return false;
    }
    if (g_item_records[item_id].quantity_kind == W8_ITEM_QUANTITY_NONE &&
        g_item_records[item_id].equip_class != W8_ITEM_EQUIP_CLASS_DRINK) {
        return false;
    }

    if (g_item_records[item_id].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (item->stack_count != 0) {
            --item->stack_count;
        }
        if (item->stack_count != 0) {
            return false;
        }
    } else if (item->uses_or_charges == 0) {
        if (!remove_exhausted) {
            return false;
        }
    } else {
        --item->uses_or_charges;
        if (item->uses_or_charges != 0) {
            return false;
        }
        if (ItemHasHiddenProperties(item_id)) {
            return false;
        }
    }

    DiscardItem(item);
    return true;
}

// ---- Consequences of an edit

/* The consequences of one live inventory edit. The edit names the locations whose item
   it replaced or emptied; a change of quantity alone is not a replacement. A committed
   item use loses its item, a weapon hand change reconciles combat choices, and the
   cursor shows what the hand now holds. */
static void FinishInventoryEdit(std::span<const W8ItemLocation> replaced)
{
    auto& party = g_status.party;
    std::bitset<W8_PARTY_SLOT_COUNT> changed_hands;
    bool hand = false;
    bool any = false;
    for (const auto location : replaced) {
        if (location.storage == W8ItemStorage::None) continue;
        any = true;
        hand |= location.storage == W8ItemStorage::Hand;
        if (location.storage == W8ItemStorage::Equipment && IsHandSlot(location.slot))
            changed_hands.set(location.character);
        for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot) {
            const auto& action = party.rows[slot].committed_action;
            if (g_status.game_started && party.rows[slot].fOccupied &&
                action.kind == W8_ACTION_USE_ITEM && action.detail_block.item_use.item == location)
                DropCharacterFromRound(slot);
        }
    }
    SettleActiveParty(changed_hands);
    if (hand) UpdateHeldItemCursor();
    if (any && CurrentScreenState().id == W8_SCREEN_MAIN_GAME && g_level_block) {
        if (g_level_block->combat_end_notification != -1) {
            ReopenSubMenuPanel();
        }
        RefreshFlaggedMainGameState();
    }
}

static void FinishInventoryEdit(std::initializer_list<W8ItemLocation> replaced)
{
    FinishInventoryEdit(std::span{replaced.begin(), replaced.size()});
}

/* An edit of a character who may be outside the active party, such as a draft in
   character creation, also brings that character's own statistics up to date. */
static void FinishCharacterInventoryEdit(W8Character& character,
                                         std::initializer_list<W8ItemLocation> replaced)
{
    FinishInventoryEdit(replaced);
    const auto slot = g_status.party.slotOf(character);
    if (!slot || !g_status.party.rows[*slot].fOccupied)
        UpdateStandaloneCharacterStats(character, {});
}

/* Equipment a member can no longer use is announced and stored like a received item:
   on the member, then in the pool, and otherwise at the party's feet. Taking one item
   off can remove a bonus another one needs, so each removal is evaluated again.
   Returns whether a weapon hand changed. */
bool UnequipUnusableItems(unsigned int party_slot)
{
    auto& party = g_status.party;
    W8Character* character = &party.characters[party_slot];
    bool hands_changed = false;
    for (;;) {
        EvaluateCharacterInputs(*character, character->fInParty ? &party.modifiers : nullptr);
        auto* item = std::ranges::find_if(character->EquippedItem, [&](const auto& candidate) {
            return candidate.iItemNo != -1 && !CanCharacterUseItem(character, candidate.iItemNo);
        });
        if (item == std::end(character->EquippedItem)) return hands_changed;
        const auto equip_slot = static_cast<unsigned int>(item - character->EquippedItem);
        hands_changed |= IsHandSlot(equip_slot);
        if (g_status.game_started) {
            unsigned short message_id = 0;
            const auto& pronoun =
                g_localized_strings[g_gender_name_message_rows[character->gender][2]];
            PostCharacterNotice(
                party_slot,
                FindUnmetItemRequirement(character, &g_item_records[item->iItemNo], &message_id)
                    ? fmt::sprintf(g_localized_strings[0x1ef], pronoun,
                                   FormatItemDisplayName(item, true), pronoun,
                                   g_localized_strings[message_id])
                    : fmt::sprintf(g_localized_strings[0x1f0], pronoun,
                                   FormatItemDisplayName(item, true)));
        }
        auto removed = party.take({W8ItemStorage::Equipment, party_slot, equip_slot});
        if (!StoreWithCharacter(character, &removed, false, false, false) &&
            !StoreInPool(&removed, false, false))
            DropUnstoredCharacterItem(&removed);
    }
}

// ---- Searching

bool ItemIsOfKind(const W8ItemInstance& item, short item_kind)
{
    return item.iItemNo != -1 && g_item_records[item.iItemNo].unidentified_name_index == item_kind;
}

W8ItemInstance* FindCharacterItem(W8Character& character, W8ItemMatch match, W8ItemReach reach)
{
    if (auto* item = std::ranges::find_if(character.EquippedItem, match);
        item != std::end(character.EquippedItem))
        return item;
    if (reach == W8ItemReach::Worn) return nullptr;
    auto* item = std::ranges::find_if(character.backpack, match);
    return item != std::end(character.backpack) ? item : nullptr;
}

W8ItemLocation FindPartyItem(W8ItemMatch match, W8ItemReach reach)
{
    auto& party = g_status.party;
    if (party.held.item.iItemNo != -1 && match(party.held.item)) return {W8ItemStorage::Hand};
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.rows[slot].fOccupied)
            if (const auto* item = FindCharacterItem(party.characters[slot], match, reach))
                return party.locateItem(item);
    if (reach != W8ItemReach::Party) return {};
    const auto pool = std::span{party.item_pool}.first(party.item_count);
    const auto found = std::ranges::find_if(pool, match);
    if (found == pool.end()) return {};
    return {W8ItemStorage::Pool, 0, static_cast<unsigned int>(found - pool.begin())};
}

/* How many of one item a character holds, counting a stack as its count and
   anything else as one. */
unsigned int CountCharacterItem(const W8Character& character, int item_id, W8ItemReach reach)
{
    const auto units = [](const W8ItemInstance& item) {
        return item.stack_count == 0 ? 1u : item.stack_count;
    };
    unsigned int total = 0;
    for (const auto& item : character.EquippedItem)
        if (item.iItemNo == item_id) total += units(item);
    if (reach == W8ItemReach::Worn) return total;
    for (unsigned int slot = 0; slot < 8; ++slot)
        if (character.backpack[slot].iItemNo == item_id)
            /* Bug: this counts the equipped slot with the same index. */
            total += units(character.EquippedItem[slot]);
    return total;
}

/* The whole-party counterpart: the item in hand, every occupied slot's carried
   items, and the party item pool. The held stack counts its size as stored. */
unsigned int CountPartyItem(int item_id)
{
    const auto& party = g_status.party;
    unsigned int total = 0;
    if (party.held.item.iItemNo != -1 && party.held.item.iItemNo == item_id)
        total = party.held.item.stack_count;
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (party.rows[slot].fOccupied)
            total += CountCharacterItem(party.characters[slot], item_id, W8ItemReach::Carried);
    for (const auto& item : std::span{party.item_pool}.first(party.item_count))
        if (item.iItemNo == item_id) total += item.stack_count == 0 ? 1 : item.stack_count;
    return total;
}

bool EveryCharacterHasItem(int item_id, W8ItemReach reach)
{
    for (unsigned int slot = 0; slot < W8_PARTY_SLOT_COUNT; ++slot)
        if (g_status.party.rows[slot].fOccupied &&
            !FindCharacterItem(g_status.party.characters[slot], ItemWithId(item_id), reach))
            return false;
    return true;
}

int GetItemInHand(void)
{
    return g_status.party.held.item.iItemNo;
}

// ---- Receiving, giving and losing items

/* Pick the equipment slot a newly acquired item goes into. Before the game
   has started the two hand pairs are special-cased: a dual-wield-capable
   item can displace the right hand's item into the left hand, class-three
   gear fills a free pair before anything else, and class-four gear looks for
   a two-handed holder and otherwise fails. */
static W8EquipSlot ChooseCharacterEquipSlot(W8Character* character, int item_id)
{
    W8EquipSlot slot = GetItemDefaultEquipSlot(item_id);
    if (g_status.game_started ||
        (slot != W8_EQUIP_SLOT_PRIMARY_WEAPON && slot != W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        return slot;
    }

    int primary_right = character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    int alternate_right = character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo;
    int primary_left = character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo;
    int alternate_left = character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo;
    switch (g_item_records[item_id].equip_class) {
    case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
    case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
        if (character->skills[W8_SKILL_DUAL_WEAPONS].points != 0) {
            if (CanEquipItemInSlot(character, item_id, 7, false)) {
                if (primary_right != -1 && primary_left == -1 &&
                    (g_item_records[primary_right].equip_class ==
                         W8_ITEM_EQUIP_CLASS_SHORT_WEAPON ||
                     g_item_records[primary_right].equip_class ==
                         W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON) &&
                    (g_item_records[primary_right].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
                    slot = W8_EQUIP_SLOT_SECONDARY_WEAPON;
                }
                if (alternate_right != -1 && alternate_left == -1 &&
                    (g_item_records[alternate_right].equip_class ==
                         W8_ITEM_EQUIP_CLASS_SHORT_WEAPON ||
                     g_item_records[alternate_right].equip_class ==
                         W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON) &&
                    (g_item_records[alternate_right].flags & W8_ITEM_FLAG_TWO_HANDED) == 0) {
                    return W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
                }
            } else {
                if (primary_right != -1 && primary_left == -1 &&
                    CanEquipItemInSlot(character, primary_right, 7, false)) {
                    MoveItem(&character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON],
                             &character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON]);
                    return W8_EQUIP_SLOT_PRIMARY_WEAPON;
                }
                if (alternate_right != -1 && alternate_left == -1 &&
                    CanEquipItemInSlot(character, alternate_right, 9, false)) {
                    MoveItem(&character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON],
                             &character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON]);
                    return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
                }
            }
        }
        break;
    case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
        if (primary_right == -1 && primary_left == -1) {
            return W8_EQUIP_SLOT_PRIMARY_WEAPON;
        }
        if (alternate_right == -1 && alternate_left == -1) {
            return W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_AMMUNITION:
        if (primary_right != -1 &&
            g_item_records[primary_right].equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON) {
            return W8_EQUIP_SLOT_SECONDARY_WEAPON;
        }
        if (alternate_right != -1 &&
            g_item_records[alternate_right].equip_class == W8_ITEM_EQUIP_CLASS_RANGED_WEAPON) {
            return W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON;
        }
        return W8_EQUIP_SLOT_NONE;
    default:
        break;
    }
    return slot;
}

/* Place an item with one character. Equipment is tried first when requested;
   otherwise compatible stacks are coalesced before the first empty backpack
   slot is used. The source item is consumed only after a destination accepts
   it, so the caller can still fall back to the party pool on failure. Returns
   the location a new placement filled, or None after merging into a stack. */
static std::optional<W8ItemLocation> StoreWithCharacter(W8Character* character,
                                                        W8ItemInstance* item,
                                                        bool equip_if_possible, bool announce,
                                                        bool skip_stacking)
{
    auto& party = g_status.party;
    W8ItemInstance* stored_item = 0;
    unsigned int stored_index = 0;

    if (equip_if_possible && CanCharacterUseItem(character, item->iItemNo)) {
        W8EquipSlot equip_slot = ChooseCharacterEquipSlot(character, item->iItemNo);
        if (equip_slot != W8_EQUIP_SLOT_NONE) {
            W8ItemInstance* destination = &character->EquippedItem[equip_slot];
            /* A refused merge leaves the equipment untouched; retail skips
               the stats rebuild for it (0x0051c379). */
            if (destination->iItemNo == -1) {
                MoveItem(destination, item);
                return party.locateItem(destination);
            }
            if (MergeStack(destination, item, 0)) return W8ItemLocation{};
        }
    }

    // Named before a merge can empty the source.
    const std::string display_name = announce ? FormatItemDisplayName(item, true) : "";
    bool placed = false;
    if (g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK && !skip_stacking) {
        unsigned int index;
        for (index = 0; index < 12; ++index) {
            if (MergeStack(&character->EquippedItem[index], item, 0)) {
                stored_item = &character->EquippedItem[index];
                stored_index = index;
                break;
            }
        }
        if (stored_item == 0) {
            for (index = 0; index < 8; ++index) {
                if (MergeStack(&character->backpack[index], item, 0)) {
                    stored_item = &character->backpack[index];
                    stored_index = index;
                    break;
                }
            }
        }
    }

    if (stored_item == 0) {
        while (stored_index < 8 && character->backpack[stored_index].iItemNo != -1) {
            ++stored_index;
        }
        if (stored_index == 8) {
            return {};
        }
        stored_item = &character->backpack[stored_index];
        MoveItem(stored_item, item);
        placed = true;
    }

    if (announce) {
        PostCharacterNotice(CharacterPointerToPartySlot(character),
                            fmt::sprintf(g_localized_strings[0x1e8], display_name));
    }
    /* Retail always reports the backpack cell at the stored index, even when
       the stack was merged into the equipped slot with that index. */
    W8ItemInstance* reported_item = &character->backpack[stored_index];
    UpdateFactsAfterAcquiringItem(reported_item);
    DeliverExceptionalItemReaction(reported_item, false, character);
    return placed ? party.locateItem(stored_item) : W8ItemLocation{};
}

/* Put an item into the party pool, first coalescing compatible stacks unless
   requested otherwise; a new entry goes to the front. Returns the location a new
   entry filled, or None after merging into a stack. */
static std::optional<W8ItemLocation> StoreInPool(W8ItemInstance* item, bool announce,
                                                 bool skip_stacking)
{
    auto& party = g_status.party;
    // Named before a merge can empty the source.
    const std::string display_name = announce ? FormatItemDisplayName(item, true) : "";
    bool partially_merged = false;
    unsigned int index = 0;
    bool merged = false;

    if (g_item_records[item->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK && !skip_stacking) {
        while (index < party.item_count) {
            if (MergeStack(&party.item_pool[index], item, &partially_merged)) {
                merged = true;
                break;
            }
            ++index;
        }
    }

    if (!merged) {
        if (party.item_count >= std::size(party.item_pool)) {
            return {};
        }
        index = 0;
        W8ItemInstance entry{};
        MoveItem(&entry, item);
        party.insertIntoPool(0, entry);
    }

    if (announce) {
        ShowNotice(W8_FONT_PALETTE_WHITE, fmt::sprintf(g_localized_strings[0x1e9], display_name));
    }
    W8ItemInstance* stored_item = &party.item_pool[index];
    UpdateFactsAfterAcquiringItem(stored_item);
    DeliverExceptionalItemReaction(stored_item, true, 0);
    return merged ? W8ItemLocation{} : W8ItemLocation{W8ItemStorage::Pool, 0, 0};
}

/* Put an item somewhere it will fit. The flag decides which of the character
   and the party pool is tried first; the other is tried after. */
static std::optional<W8ItemLocation> StoreWithCharacterOrPool(W8Character* character,
                                                              W8ItemInstance* item,
                                                              bool party_first, bool announce,
                                                              bool equip_if_possible)
{
    std::optional<W8ItemLocation> stored;
    if (!party_first)
        stored = StoreWithCharacter(character, item, equip_if_possible, announce, false);
    if (!stored) stored = StoreInPool(item, announce, false);
    if (!stored && party_first)
        stored = StoreWithCharacter(character, item, equip_if_possible, announce, false);
    return stored;
}

bool AddItemToCharacter(W8Character* character, W8ItemInstance* item, bool equip_if_possible,
                        bool announce, bool skip_stacking)
{
    const auto source = g_status.party.locateItem(item);
    const auto stored =
        StoreWithCharacter(character, item, equip_if_possible, announce, skip_stacking);
    FinishCharacterInventoryEdit(
        *character, {stored ? source : W8ItemLocation{}, stored.value_or(W8ItemLocation{})});
    return stored.has_value();
}

bool AddItemToParty(W8ItemInstance* item, bool announce, bool skip_stacking)
{
    const auto source = g_status.party.locateItem(item);
    const auto stored = StoreInPool(item, announce, skip_stacking);
    FinishInventoryEdit({stored ? source : W8ItemLocation{}, stored.value_or(W8ItemLocation{})});
    return stored.has_value();
}

static void DropUnstoredCharacterItem(W8ItemInstance* item)
{
    if ((g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (const auto position = FindItemDropPosition()) {
            AddWorldItem(
                CreateWorldItem(item, &*position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            return;
        }
    }
    if (g_status.party.held.item.iItemNo == -1) MoveItem(&g_status.party.held.item, item);
}

/* Put an item in the party pool, and when the pool will not take it, drop it at the
   party's feet or into an empty hand. */
bool AddItemToPartyOrDrop(W8ItemInstance* item, bool announce)
{
    const auto source = g_status.party.locateItem(item);
    const auto stored = StoreInPool(item, announce, false);
    if (!stored) DropUnstoredCharacterItem(item);
    FinishInventoryEdit({source, stored.value_or(W8ItemLocation{W8ItemStorage::Hand})});
    return stored.has_value();
}

bool StoreItemWithCharacterOrParty(W8Character* character, W8ItemInstance* item, bool party_first,
                                   bool announce, bool equip_if_possible)
{
    const auto source = g_status.party.locateItem(item);
    const auto stored =
        StoreWithCharacterOrPool(character, item, party_first, announce, equip_if_possible);
    FinishCharacterInventoryEdit(
        *character, {stored ? source : W8ItemLocation{}, stored.value_or(W8ItemLocation{})});
    return stored.has_value();
}

/* Hand one item to a party member. The character and the party pool are tried
   in the order the flag asks for, with the other tried after - and when the
   slot has nobody in it, only the pool. */
bool GiveItemToCharacterOrParty(int uiChar, W8ItemInstance* item, bool party_first)
{
    if (!g_status.game_started) {
        Fail("gStatus.fGameStarted");
    }
    if (!g_status.party.rows[uiChar].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }

    const auto source = g_status.party.locateItem(item);
    std::optional<W8ItemLocation> destination;
    if (!IsPartySlotEligible(uiChar)) {
        if (party_first) destination = StoreInPool(item, true, false);
    } else {
        destination = StoreWithCharacterOrPool(&g_status.party.characters[uiChar], item,
                                               party_first, true, false);
    }
    // A stored item has left its source; a refused one is untouched.
    FinishInventoryEdit(
        {destination ? source : W8ItemLocation{}, destination.value_or(W8ItemLocation{})});
    return destination.has_value();
}

/* Hand the item in hand to a party member. It is first offered to everybody who
   could identify it, and then stored with the character or the party pool in
   the order the flag asks for. The item-in-hand state is cleared before the
   outcome is tested, and a store that fails is announced: the notice names the
   slot when neither destination was open, and the item otherwise. The flag
   doubles as the result, so a failed party-first attempt still reports
   success. */
bool GiveHeldItemToCharacterOrParty(int uiChar, bool party_first)
{
    W8ItemInstance* item = &g_status.party.held.item;
    bool stored = party_first;
    if (!(g_status.party.held.item.iItemNo != -1)) {
        Fail("gStatus.fItemInCursor");
    }

    if (!item->identified) PartyAttemptsToIdentifyItem(item, 0);

    if (!g_status.game_started) {
        Fail("gStatus.fGameStarted");
    }
    if (!g_status.party.rows[uiChar].fOccupied) {
        Fail("fCHAR_OCCUPIED(uiChar)");
    }

    std::optional<W8ItemLocation> destination;
    if (IsPartySlotEligible(uiChar)) {
        W8Character* character = &g_status.party.characters[uiChar];
        destination = StoreWithCharacterOrPool(character, item, party_first, true, false);
        if (destination) stored = true;
    } else if (stored) {
        destination = StoreInPool(item, true, false);
        stored = destination.has_value();
    }

    /* Only a stored item releases the cursor; a refused one stays held. */
    if (stored) {
        DiscardItem(&g_status.party.held.item);
        FinishInventoryEdit({{W8ItemStorage::Hand}, destination.value_or(W8ItemLocation{})});
        return stored;
    }

    FinishInventoryEdit({});
    if (!IsPartySlotEligible(uiChar) && !party_first) {
        ShowNoticeLine(g_localized_strings[0x1f5], 0, true, false);
    } else {
        ShowNoticeLine(g_localized_strings[0x90c], 0, true, false);
    }
    return stored;
}

/* Empty a departing character into the party pool. Equipment that is bound
   to its slot stays with the body unless the binding was announced, the slot
   has no cursor icon, or the character is dead. What the pool cannot take is
   dropped directly into the world or held when the hand is empty. */
void StashDepartingCharacterItems(W8Character* character)
{
    int item_id;

    for (int equip_slot = 0; equip_slot < W8_EQUIP_SLOT_COUNT; ++equip_slot) {
        W8ItemInstance* item = &character->EquippedItem[equip_slot];
        item_id = item->iItemNo;
        if (item_id != -1 &&
            (g_item_records[item_id].binds_on_equip == 0 || item->bind_announced ||
             g_equip_slot_icons[equip_slot] == -1 ||
             character->uiCondition[W8_CONDITION_DEAD] != 0) &&
            !StoreInPool(item, false, false)) {
            DropUnstoredCharacterItem(item);
            ShowNotice(W8_FONT_PALETTE_RED, fmt::sprintf(g_localized_strings[0x7d3],
                                                         g_item_records[item_id].display_name));
        }
    }

    for (int slot = 0; slot < 8; ++slot) {
        W8ItemInstance* item = &character->backpack[slot];
        item_id = item->iItemNo;
        if (item_id != -1 && !StoreInPool(item, false, false)) {
            DropUnstoredCharacterItem(item);
            ShowNotice(W8_FONT_PALETTE_RED, fmt::sprintf(g_localized_strings[0x7d3],
                                                         g_item_records[item_id].display_name));
        }
    }
    FinishCharacterInventoryEdit(*character, {});
}

/* Empty every equipped and backpack item record a character carries. */
void EmptyAllCarriedItems(W8Character* character)
{
    for (auto& item : character->EquippedItem) item = {};
    for (auto& item : character->backpack) item = {};
    FinishCharacterInventoryEdit(*character, {});
}

/* Give the character the class item their current profession level earns. The
   ladder answers with the item id and the notice to show; the first tier hands
   over the welcome item outright, the top tier replaces nothing, and an item
   they already hold only gets its id raised when it is below the tier's. */
static W8ItemLocation UpgradeClassItem(W8Character* character)
{
    W8ItemInstance* found = 0;
    W8ItemInstance created;
    int item_id;
    int message_id;

    switch (character->profession_levels[W8_PROFESSION_GADGETEER]) {
    case 0:
    case 2:
        return {};
    case 1: {
        if (FindCharacterItem(*character, ItemOfKind(0x83), W8ItemReach::Carried)) {
            return {};
        }
        created = MakeItem(599, false, true, true);
        auto stored = StoreWithCharacter(character, &created, false, true, false);
        if (!stored) stored = StoreInPool(&created, true, false);
        if (!stored) {
            return {};
        }
        PostCharacterNotice(
            CharacterPointerToPartySlot(character),
            fmt::sprintf(g_localized_strings[0x167],
                         g_localized_strings[g_gender_name_message_rows[character->gender][3]]));
        return *stored;
    }
    case 3:
    case 4:
        item_id = 600;
        message_id = 0x168;
        break;
    case 5:
    case 6:
        item_id = 0x259;
        message_id = 0x169;
        break;
    case 7:
    case 8:
        item_id = 0x25a;
        message_id = 0x16a;
        break;
    case 9:
    case 10:
        item_id = 0x25b;
        message_id = 0x16b;
        break;
    case 0xb:
    case 0xc:
        item_id = 0x25c;
        message_id = 0x16c;
        break;
    case 0xd:
    case 0xe:
    case 0xf:
        item_id = 0x25d;
        message_id = 0x16d;
        break;
    case 0x10:
    case 0x11:
        item_id = 0x25e;
        message_id = 0x16e;
        break;
    case 0x12:
    case 0x13:
    case 0x14:
        item_id = 0x25f;
        message_id = 0x16f;
        break;
    case 0x15:
    case 0x16:
        item_id = 0x260;
        message_id = 0x170;
        break;
    case 0x17:
    case 0x18:
    case 0x19:
        item_id = 0x261;
        message_id = 0x171;
        break;
    default:
        item_id = 0x262;
        message_id = 0x172;
        break;
    }

    found = FindCharacterItem(*character, ItemOfKind(0x83), W8ItemReach::Carried);
    if (!found) found = g_status.party.itemAt(FindPartyItem(ItemOfKind(0x83), W8ItemReach::Party));
    if (found && found->iItemNo < item_id) {
        found->iItemNo = item_id;
        PostCharacterNotice(
            CharacterPointerToPartySlot(character),
            fmt::sprintf(g_localized_strings[message_id],
                         g_localized_strings[g_gender_name_message_rows[character->gender][2]]));
        return g_status.party.locateItem(found);
    }
    return {};
}

void UpgradeProfessionClassItem(W8Character* character)
{
    FinishCharacterInventoryEdit(*character, {UpgradeClassItem(character)});
}

/* Take every instance of one item id out of the party: the hand first, then
   every member's slots, then the pool. When remove_all is clear the search
   stops at the first hit. Even when it is set, only one copy per party member
   is cleared; the pool walk does compact every matching pool entry. */
bool RemovePartyItemByID(int item_id, bool remove_all)
{
    auto& party = g_status.party;
    std::vector<W8ItemLocation> removed;
    const auto finish = [&] {
        FinishInventoryEdit(removed);
        return !removed.empty();
    };

    if (party.held.item.iItemNo != -1 && party.held.item.iItemNo == item_id) {
        party.take({W8ItemStorage::Hand});
        removed.push_back({W8ItemStorage::Hand});
        if (!remove_all) return finish();
    }

    for (unsigned int slot = 0; slot < 8; ++slot) {
        if (!party.rows[slot].fOccupied) {
            continue;
        }
        const auto* found =
            FindCharacterItem(party.characters[slot], ItemWithId(item_id), W8ItemReach::Carried);
        if (!found) {
            continue;
        }
        removed.push_back(party.locateItem(found));
        party.take(removed.back());
        if (!remove_all) return finish();
    }

    for (unsigned int index = 0; index < party.item_count; ++index) {
        if (party.item_pool[index].iItemNo != item_id) {
            continue;
        }
        removed.push_back({W8ItemStorage::Pool, 0, index});
        party.take(removed.back());
        if (!remove_all) return finish();
        /* The tail moved down into this index, so it is visited again. */
        --index;
    }
    return finish();
}

void RemoveInventoryQuantity(W8ItemLocation location, unsigned amount)
{
    auto* item = g_status.party.itemAt(location);
    if (!item || item->iItemNo == -1 || amount == 0) return;
    if (amount >= std::max<unsigned>(1, item->stack_count)) {
        (void)TakeInventoryItem(location);
        return;
    }
    item->stack_count -= static_cast<unsigned char>(amount);
    FinishInventoryEdit({});
}

W8ItemInstance TakeInventoryItem(W8ItemLocation location)
{
    auto& party = g_status.party;
    if (location.storage == W8ItemStorage::Pool && location.slot >= party.item_count) return {};
    if (!party.itemAt(location)) return {};
    auto result = party.take(location);
    FinishInventoryEdit({location});
    return result;
}

/* Give gold to the party. It goes into the purse and into whichever running
   tally is open, and announcing it plays the coin sound - copied to the stack
   first because the sound call takes a writable path. */
void AddPartyGold(int amount, bool announce)
{
    std::string sound_path;

    sound_path = "Data\\Sound\\Misc\\ChaChing.wav";

    g_status.party.gold += amount;
    if (GetLoadedLevelID() >= 0 && GetLoadedLevelID() < W8_LEVEL_COUNT) {
        g_status.level_progress[GetLoadedLevelID()].gold_collected += amount;
    }

    if (announce) {
        ShowNotice(8, fmt::format("{} {:d} {}.", g_localized_strings[0x15f], amount,
                                  g_localized_strings[0x160]));
        if (!wiz8::sound::is_file_playing(sound_path)) {
            wiz8::sound::play(sound_path);
        }
    }
}

/* Take gold from the party. Asking for more than it has empties the purse
   rather than wrapping it around. */
void SpendPartyGold(unsigned int amount)
{
    if (amount > g_status.party.gold) {
        g_status.party.gold = 0;
    } else {
        g_status.party.gold -= amount;
    }
}

// ---- The hand

void ReplaceHeldItem(W8ItemInstance item)
{
    g_status.party.held = {.item = item};
    FinishInventoryEdit({{W8ItemStorage::Hand}});
}

/* Puts a new item in the hand when the cursor is free; false when it already holds something. */
bool PlaceItemOnCursor(int item_id, bool maximum_quantity, bool force_identified, bool mark_special)
{
    if ((g_status.party.held.item.iItemNo != -1)) {
        return false;
    }
    ReplaceHeldItem(MakeItem(item_id, maximum_quantity, force_identified, mark_special));

    return true;
}

/* Conjure one item into the hand, replacing whatever was held. */
void CreateItemIntoHandOrPool(int item_id, bool quality)
{
    DiscardItem(&g_status.party.held.item);
    auto created = MakeItem(item_id, true, quality, false);
    MoveItem(&g_status.party.held.item, &created);
    FinishInventoryEdit({{W8ItemStorage::Hand}});
}

bool ReceiveItemInHand(W8ItemInstance& item)
{
    auto& party = g_status.party;
    if (party.held.item.iItemNo != -1 || item.iItemNo == -1) return false;
    party.held = {std::exchange(item, W8ItemInstance{}), {}, -1};
    FinishInventoryEdit({{W8ItemStorage::Hand}});
    return true;
}

/* Drop whatever is in hand, unless it is one of the items that may not be
   discarded - in which case say so instead. */
bool DropItemInHand(int unused)
{
    if ((g_item_records[g_status.party.held.item.iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
        ShowNoticeLine(g_localized_strings[0x4ef], 0, true, false);
        return false;
    }
    DropHeldItem(unused);
    FinishInventoryEdit({{W8ItemStorage::Hand}});
    return true;
}

// ---- Player edits

/* Split a stack into the hand, or put part of the held stack back. Taking from a
   location starts or grows the held stack; splitting the held stack returns the
   remainder to where it came from when that still fits, and otherwise to the
   actor's backpack or the pool. Refusal leaves inventory unchanged. */
bool SplitItemStack(W8ItemLocation source_location, unsigned amount, unsigned actor)
{
    auto& party = g_status.party;
    if (source_location.storage == W8ItemStorage::Pool && source_location.slot >= party.item_count)
        return false;
    auto* source = party.itemAt(source_location);
    if (!source || source->iItemNo < 0 || actor >= W8_PARTY_SLOT_COUNT || amount == 0 ||
        amount > source->stack_count ||
        g_item_records[source->iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK ||
        (g_item_records[source->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD))
        return false;
    if (source_location.storage == W8ItemStorage::Equipment && amount == source->stack_count &&
        !CanUnequipSlotItem(&party.characters[source_location.character],
                            static_cast<W8EquipSlot>(source_location.slot)))
        return false;
    const bool from_hand = source_location.storage == W8ItemStorage::Hand;
    const bool append = !from_hand && party.held.item.iItemNo != -1;
    if (append &&
        (party.held.item.iItemNo != source->iItemNo ||
         party.held.item.stack_count + amount > g_item_records[source->iItemNo].maximum_quantity))
        return false;
    if (from_hand && amount == source->stack_count) return true;

    W8ItemLocation destination_location{W8ItemStorage::Hand};
    if (from_hand) {
        destination_location = party.held.origin;
        auto* original = party.itemAt(destination_location);
        const bool equipment = destination_location.storage == W8ItemStorage::Equipment;
        if (!original || original->iItemNo != -1 ||
            (destination_location.storage != W8ItemStorage::Backpack && !equipment) ||
            (equipment &&
             (!CanCharacterUseItem(&party.characters[destination_location.character],
                                   source->iItemNo) ||
              !CanEquipItemInSlot(&party.characters[destination_location.character],
                                  source->iItemNo, destination_location.slot, false)))) {
            destination_location = {};
            for (unsigned slot = 0; slot < 8; ++slot) {
                if (party.characters[actor].backpack[slot].iItemNo == -1) {
                    destination_location = {W8ItemStorage::Backpack, actor, slot};
                    break;
                }
            }
            if (destination_location.storage == W8ItemStorage::None) {
                if (party.item_count == std::size(party.item_pool)) return false;
                destination_location = {W8ItemStorage::Pool, 0, party.item_count};
            }
        }
    }
    const unsigned transferred = from_hand ? source->stack_count - amount : amount;
    auto part = *source;
    part.stack_count = static_cast<unsigned char>(transferred);
    source->stack_count -= static_cast<unsigned char>(transferred);
    if (append) {
        party.held.item.stack_count += static_cast<unsigned char>(transferred);
    } else if (destination_location.storage == W8ItemStorage::Hand) {
        party.held = {part, source_location, static_cast<int>(actor)};
    } else if (destination_location.storage == W8ItemStorage::Pool) {
        party.insertIntoPool(destination_location.slot, part);
    } else {
        *party.itemAt(destination_location) = part;
    }
    const bool emptied = source->stack_count == 0;
    if (emptied) party.take(source_location);
    FinishInventoryEdit({emptied ? source_location : W8ItemLocation{},
                         append ? W8ItemLocation{} : destination_location});
    return true;
}

/* Exchange two locations, either of which may be empty, as the player does by
   moving an item. Equipment slots accept only what their wearer may equip, and a
   bound item stays where it is. */
bool ExchangeInventoryItems(W8ItemLocation first_location, W8ItemLocation second_location,
                            unsigned actor)
{
    auto& party = g_status.party;
    if (second_location.storage == W8ItemStorage::Pool && second_location.slot >= party.item_count)
        return false;
    if (first_location.storage == W8ItemStorage::Pool && first_location.slot >= party.item_count)
        return false;
    auto* first = party.itemAt(first_location);
    auto* second = party.itemAt(second_location);
    if (!first || !second || first == second || actor >= W8_PARTY_SLOT_COUNT) return false;
    for (auto [location, incoming] :
         {std::pair{first_location, second}, std::pair{second_location, first}}) {
        if (location.storage != W8ItemStorage::Equipment) continue;
        auto* character = &party.characters[location.character];
        if (!CanUnequipSlotItem(character, static_cast<W8EquipSlot>(location.slot))) return false;
        if (incoming->iItemNo != -1 &&
            (!CanCharacterUseItem(character, incoming->iItemNo) ||
             !CanEquipItemInSlot(character, incoming->iItemNo, location.slot, false)))
            return false;
    }
    party.exchange(first_location, second_location, static_cast<int>(actor));
    FinishInventoryEdit({first_location, second_location});
    return true;
}

/* Equip the held item in a hand whose partner it cannot share, taking the partner off.
   An occupied target goes back into the hand and the partner into the backpack or the
   pool; an empty target puts the partner itself into the hand. */
bool EquipHeldItemAndStowPartner(unsigned actor, W8EquipSlot slot)
{
    auto& party = g_status.party;
    if (actor >= W8_PARTY_SLOT_COUNT) return false;
    auto& character = party.characters[actor];
    const auto partner_slot = GetPairedEquipSlot(slot);
    if (partner_slot == W8_EQUIP_SLOT_NONE || party.held.item.iItemNo == -1 ||
        !CanUnequipSlotItem(&character, slot) || !CanUnequipSlotItem(&character, partner_slot))
        return false;
    auto proposed = character;
    proposed.EquippedItem[partner_slot].iItemNo = -1;
    if (!CanCharacterUseItem(&character, party.held.item.iItemNo) ||
        !CanEquipItemInSlot(&proposed, party.held.item.iItemNo, slot, false))
        return false;
    auto& destination = character.EquippedItem[slot];
    auto& partner = character.EquippedItem[partner_slot];
    const bool replace = destination.iItemNo != -1;
    W8ItemLocation storage{};
    if (replace) {
        for (unsigned index = 0; index < 8; ++index) {
            if (character.backpack[index].iItemNo == -1) {
                storage = {W8ItemStorage::Backpack, actor, index};
                break;
            }
        }
        if (storage.storage == W8ItemStorage::None) {
            auto* npc = actor < 2 ? GetNpcState(party.rows[actor].npc_index) : nullptr;
            if ((npc && NpcWantsItem(npc, &partner)) || party.item_count >= 500) return false;
            storage = {W8ItemStorage::Pool, 0, 0};
        }
    }
    // Every destination and gameplay restriction is known before the first write.
    auto displaced = partner;
    displaced.bound = true;
    displaced.bind_announced = false;
    const auto held_origin = replace ? slot : partner_slot;
    const auto new_held = replace ? destination : displaced;
    destination = party.held.item;
    destination.bind_announced = false;
    partner = {};
    party.held = {new_held,
                  {W8ItemStorage::Equipment, actor, static_cast<unsigned>(held_origin)},
                  static_cast<int>(actor)};
    party.held.item.bind_announced = false;
    if (storage.storage == W8ItemStorage::Pool)
        party.insertIntoPool(0, displaced);
    else if (storage.storage == W8ItemStorage::Backpack)
        *party.itemAt(storage) = displaced;

    if (storage.storage != W8ItemStorage::None) {
        auto* stored = party.itemAt(storage);
        const bool pool = storage.storage == W8ItemStorage::Pool;
        UpdateFactsAfterAcquiringItem(stored);
        DeliverExceptionalItemReaction(stored, pool, pool ? nullptr : &character);
    }
    FinishInventoryEdit({{W8ItemStorage::Hand},
                         {W8ItemStorage::Equipment, actor, static_cast<unsigned>(slot)},
                         {W8ItemStorage::Equipment, actor, static_cast<unsigned>(partner_slot)},
                         storage});
    return true;
}

/* Fold as much of one stack onto another as fits. */
StackMergeResult MergeInventoryStacks(W8ItemLocation destination_location,
                                      W8ItemLocation source_location)
{
    auto& party = g_status.party;
    if (source_location.storage == W8ItemStorage::Pool && source_location.slot >= party.item_count)
        return {};
    if (destination_location.storage == W8ItemStorage::Pool &&
        destination_location.slot >= party.item_count)
        return {};
    auto* destination = party.itemAt(destination_location);
    auto* source = party.itemAt(source_location);
    if (!destination || !source || destination == source || destination->iItemNo == -1 ||
        source->iItemNo != destination->iItemNo || source->identified != destination->identified ||
        g_item_records[source->iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK)
        return {};
    const unsigned maximum = g_item_records[source->iItemNo].maximum_quantity;
    if (destination->stack_count >= maximum) return {};
    const unsigned amount =
        std::min<unsigned>(source->stack_count, maximum - destination->stack_count);
    if (!amount) return {};
    destination->stack_count += static_cast<unsigned char>(amount);
    source->stack_count -= static_cast<unsigned char>(amount);
    const bool complete = source->stack_count == 0;
    if (complete) party.take(source_location);
    FinishInventoryEdit({complete ? source_location : W8ItemLocation{}});
    return {complete, true};
}

bool InsertHeldItemInPool(unsigned index)
{
    auto& party = g_status.party;
    if (party.held.item.iItemNo == -1 || index > party.item_count ||
        party.item_count >= std::size(party.item_pool))
        return false;
    party.insertIntoPool(index, party.take({W8ItemStorage::Hand}));
    FinishInventoryEdit({{W8ItemStorage::Hand}, {W8ItemStorage::Pool, 0, index}});
    return true;
}

/* Combine the held item with another one. Two identified items that some
   database entry names as its components (in either order) are consumed and
   that entry is created in their place, provided the character has the skill
   it asks for; stacks give up as many units as the smaller one holds. Failing
   that, two items of one kind are stacked together. Returns whether a recipe
   merge (or, for stacking, a merge of any units) happened. */
bool MergeItems(unsigned actor, W8ItemLocation target)
{
    auto& party = g_status.party;
    auto* destination = party.itemAt(target);
    auto& held = party.held.item;
    if (actor >= W8_PARTY_SLOT_COUNT || !destination || held.iItemNo == -1 ||
        destination->iItemNo == -1)
        return false;
    W8Character* character = &party.characters[actor];
    if (!held.identified || !destination->identified) {
        ShowCampNoticeLine(g_localized_strings[0x163], 0, true, false);
        return false;
    }
    for (unsigned id = 0; id < g_item_records.size(); ++id) {
        const auto& recipe = g_item_records[id];
        if (!((recipe.merge_component_a == held.iItemNo &&
               recipe.merge_component_b == destination->iItemNo) ||
              (recipe.merge_component_b == held.iItemNo &&
               recipe.merge_component_a == destination->iItemNo)))
            continue;
        if (recipe.merge_skill != -1 &&
            character->skills[recipe.merge_skill].level < recipe.merge_skill_level) {
            ShowCampNoticeLine(fmt::sprintf(g_localized_strings[0x162], character->name), 0, true,
                               false);
            return false;
        }
        const bool held_stack =
            g_item_records[held.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK;
        const bool target_stack =
            g_item_records[destination->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK;
        const unsigned quantity =
            held_stack && target_stack ? std::min(held.stack_count, destination->stack_count) : 1;
        auto remaining_held = held;
        auto remaining_target = *destination;
        auto consume = [quantity](W8ItemInstance& item, bool stack) {
            if (stack && item.stack_count > quantity)
                item.stack_count -= static_cast<unsigned char>(quantity);
            else
                item = W8ItemInstance{};
        };
        consume(remaining_held, held_stack);
        consume(remaining_target, target_stack);
        auto product = MakeItem(id, false, false, false);
        if (recipe.quantity_kind == W8_ITEM_QUANTITY_STACK)
            product.stack_count = static_cast<unsigned char>(quantity);
        if (remaining_target.iItemNo == -1)
            remaining_target = product;
        else
            remaining_held = product;
        held = remaining_held;
        *destination = remaining_target;
        if (held.iItemNo == -1) party.held = {};
        FinishInventoryEdit({{W8ItemStorage::Hand}, target});
        // Practice follows the completed craft as a phase of its own.
        if (recipe.merge_skill != -1) {
            QueueCharacterEvent(character, g_learn_sound, 0, kCharacterEventFullVolume);
            PracticeCharacterSkill(character, static_cast<W8Skill>(recipe.merge_skill),
                                   recipe.merge_skill_level / 10, false);
        }
        return true;
    }
    if (held.iItemNo == destination->iItemNo) {
        bool partial = false;
        const bool merged = MergeStack(destination, &held, &partial);
        FinishInventoryEdit({merged ? W8ItemLocation{W8ItemStorage::Hand} : W8ItemLocation{}});
        return merged || partial;
    }
    ShowCampNoticeLine(g_localized_strings[0x161], 0, true, false);
    return false;
}

/* Pour one item's uses into another and take that many off the source, one at
   a time - which is what makes the source disappear when it is emptied. Each
   side counts its quantity the way its own record says to. Returns whether the
   source was used up. */
static bool MergeUses(W8ItemInstance* into, W8ItemInstance* from)
{
    unsigned char available;
    unsigned char held;
    unsigned int moved;

    available = g_item_records[from->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
                    ? from->stack_count
                    : from->uses_or_charges;
    held = g_item_records[into->iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK
               ? into->stack_count
               : into->uses_or_charges;

    moved = MaximumQuantity(into->iItemNo) - held;
    if (available <= moved) {
        moved = available;
    }
    into->uses_or_charges += static_cast<char>(moved);
    bool emptied = false;
    for (; moved != 0 && !emptied; --moved) {
        emptied = ConsumeItemUse(from, false);
    }
    return emptied;
}

void MergeItemUses(unsigned actor, W8ItemLocation destination, W8ItemLocation source)
{
    auto* into = g_status.party.itemAt(destination);
    auto* from = g_status.party.itemAt(source);
    if (actor >= W8_PARTY_SLOT_COUNT || !into || !from || into == from || into->iItemNo == -1 ||
        into->iItemNo != from->iItemNo)
        return;
    FinishInventoryEdit({MergeUses(into, from) ? source : W8ItemLocation{}});
}

/* Add uses to one item, never past what it can hold. */
void AddItemUses(W8ItemInstance* item, char uses)
{
    unsigned char total = item->uses_or_charges + uses;
    int maximum = MaximumQuantity(item->iItemNo);

    item->uses_or_charges = total;
    if (static_cast<int>(static_cast<unsigned int>(total)) < maximum) {
        item->uses_or_charges = total;
        return;
    }
    item->uses_or_charges = static_cast<unsigned char>(maximum);
}

/* Order two pool entries. Both have to hold something, and they are compared
   by equipment class, then by generic name,
   then unidentified before identified, then by value, then by stack count and
   finally by uses. Every comparison is the reverse of the usual sense, so the
   pool ends up in descending order. */
bool ItemPrecedesInPool(const W8ItemInstance& first, const W8ItemInstance& second)
{
    const auto* a = &first;
    const auto* b = &second;
    const W8ItemDatabaseRecord* ra;
    const W8ItemDatabaseRecord* rb;

    if (a->iItemNo == -1) {
        Fail("pPCItem1->iItemNo != BAD_INDEX");
    }
    if (b->iItemNo == -1) {
        Fail("pPCItem2->iItemNo != BAD_INDEX");
    }
    ra = &g_item_records[a->iItemNo];
    rb = &g_item_records[b->iItemNo];

    if (rb->equip_class < ra->equip_class) {
        return false;
    }
    if (ra->equip_class < rb->equip_class) {
        return true;
    }
    if (rb->unidentified_name_index < ra->unidentified_name_index) {
        return false;
    }
    if (ra->unidentified_name_index < rb->unidentified_name_index) {
        return true;
    }
    if (!a->identified) {
        if (!b->identified) {
            return false;
        }
        return false;
    }
    if (!b->identified) {
        return true;
    }
    if (ra->value < rb->value) {
        return false;
    }
    if (rb->value < ra->value) {
        return true;
    }
    if (a->stack_count < b->stack_count) {
        return false;
    }
    if (b->stack_count < a->stack_count) {
        return true;
    }
    if (a->uses_or_charges < b->uses_or_charges) {
        return false;
    }
    return b->uses_or_charges < a->uses_or_charges;
}

/* Put the party pool back in order; a pool of one is left alone. */
void SortPartyItemPool(void)
{
    if (!g_status.game_started) {
        Fail("gStatus.fGameStarted");
    }
    if (g_status.party.item_count > 1) {
        std::sort(g_status.party.item_pool, g_status.party.item_pool + g_status.party.item_count,
                  ItemPrecedesInPool);
    }
}

// ---- Weapon sets and ammunition

std::optional<W8WeaponSwap> PlanWeaponSetSwap(const W8Character& value)
{
    const auto* character = &value;
    if (!CanUnequipSlotItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON))
        return {};
    if (std::ranges::all_of(std::array{W8_EQUIP_SLOT_PRIMARY_WEAPON, W8_EQUIP_SLOT_SECONDARY_WEAPON,
                                       W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON,
                                       W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON},
                            [&](int slot) { return character->EquippedItem[slot].iItemNo == -1; }))
        return {};
    bool blocked_primary = false;
    bool blocked_alternate = false;
    int primary_right_item;
    int alternate_right_item;
    primary_right_item = character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo;
    alternate_right_item = character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo;
    if (alternate_right_item == -1 &&
        character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo != -1) {
        blocked_primary = CanHoldItemsTogether(
            primary_right_item,
            character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo);
    }
    if (character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == -1 &&
        alternate_right_item != -1) {
        blocked_alternate = CanHoldItemsTogether(
            character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo, alternate_right_item);
    }

    return W8WeaponSwap{
        (primary_right_item != -1 || alternate_right_item != -1) && !blocked_primary,
        (character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo != -1 ||
         character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo != -1) &&
            !blocked_alternate};
}

/* Swap each hand's weapon set for the other pair, and say why when it cannot
   happen. A character who is holding nothing at all has nothing to swap and is
   told so; a weapon bound to its wearer blocks the whole swap until the binding
   has been announced; and an item that cannot be held together with what is
   already there is left where it is, with the pair swapping around it. */
void CommitWeaponSetSwap(int party_slot, W8WeaponSwap choice)
{
    auto* character = &g_status.party.characters[party_slot];
    BindEquippedItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    for (auto [swap, active, alternate] :
         {std::tuple{choice.primary, W8_EQUIP_SLOT_PRIMARY_WEAPON,
                     W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON},
          std::tuple{choice.secondary, W8_EQUIP_SLOT_SECONDARY_WEAPON,
                     W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON}}) {
        if (!swap) continue;
        auto& first = character->EquippedItem[active];
        auto& second = character->EquippedItem[alternate];
        std::swap(first, second);
        first.bind_announced = second.bind_announced = false;
    }
    const auto slot = static_cast<unsigned int>(party_slot);
    const auto hand = [&](bool swapped, W8EquipSlot equip_slot) {
        return swapped ? W8ItemLocation{W8ItemStorage::Equipment, slot,
                                        static_cast<unsigned int>(equip_slot)}
                       : W8ItemLocation{};
    };
    FinishInventoryEdit({hand(choice.primary, W8_EQUIP_SLOT_PRIMARY_WEAPON),
                         hand(choice.primary, W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON),
                         hand(choice.secondary, W8_EQUIP_SLOT_SECONDARY_WEAPON),
                         hand(choice.secondary, W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON)});
}

bool SwapWeaponSetSlots(int party_slot, bool announce)
{
    W8Character* character = &g_status.party.characters[party_slot];
    int notice_context = gXStatus.fNpcDialogueMode ? 0 : -1;

    if (character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON].iItemNo == -1 &&
        character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1 &&
        character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_PRIMARY_WEAPON].iItemNo == -1 &&
        character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo == -1) {
        if (announce) {
            PostCharacterNotice(party_slot, g_localized_strings[0x1eb], notice_context);
        }
        return false;
    }

    BindEquippedItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON);
    BindEquippedItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    if (!CanUnequipSlotItem(character, W8_EQUIP_SLOT_PRIMARY_WEAPON) ||
        !CanUnequipSlotItem(character, W8_EQUIP_SLOT_SECONDARY_WEAPON)) {
        if (announce) {
            PostCharacterNotice(party_slot, g_localized_strings[0x1ee], notice_context);
        }
        return false;
    }

    const auto plan = PlanWeaponSetSwap(*character);
    if (!plan) return false;
    CommitWeaponSetSwap(party_slot, *plan);

    if (announce) {
        PostCharacterNotice(party_slot, g_localized_strings[0x1ec], notice_context);
    }
    return true;
}

/* Bind a party member's carried items, which the combat UI only allows once the
   turn is really theirs: a free-turn phase or a party movement mode refuses the
   whole request with a notice, and so does the slot whose action is still being
   chosen. Otherwise the weapon sets swap, the row's one-shot flag clears, and
   the portraits redraw. */
void BindCharacterItems(int party_slot, bool announce)
{
    if (g_combat.active()) {
        if (!g_combat.state()->round_active && !gXStatus.fPartyMovementMode) {
            ShowNotice(W8_FONT_PALETTE_BEIGE,
                       g_localized_strings[W8_NOTICE_WEAPON_SWAP_BLOCKED_COMBAT]);
            return;
        }
        if (g_combat.state()->iActionChar == party_slot &&
            g_combat.state()->action_status == W8CombatActionStatus::resolving &&
            (g_status.party.rows[party_slot].pending_action == W8_ACTION_ATTACK ||
             g_status.party.rows[party_slot].pending_action == W8_ACTION_BERSERK)) {
            ShowNotice(W8_FONT_PALETTE_WHITE,
                       fmt::sprintf(g_localized_strings[0x1f7],
                                    g_status.party.characters[party_slot].name));
            return;
        }
    }

    if (IsPartySlotEligible(party_slot)) {
        if (SwapWeaponSetSlots(party_slot, announce)) {
            g_status.party.rows[party_slot].weapon_swap_pending = false;
        }
    }
    RequestRedraw(W8_MAIN_REDRAW_CHARACTER_ACTION);
}

/* Fill one equipment slot with the item that pairs with the given one. Only
   the five generic name kinds the merge path singles out pair at all. The
   alternate left hand is the pair to take when it holds something compatible;
   otherwise the caller may name an item that has to be found on the character.
   Neither being available queues the character's event instead of swapping.
   Returns where the partner came from, or None when nothing moved. */
static W8ItemLocation EquipPartner(W8Character* character, W8ItemInstance* item, int item_id,
                                   W8EquipSlot equip_slot)
{
    W8ItemInstance* pair = nullptr;

    if (!ItemHasSingledOutGenericName(item->iItemNo)) {
        return {};
    }
    if (item->iItemNo == -1) {
        Fail("pPCItem->iItemNo != -1");
    }

    if (character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo != -1 &&
        CompatiblePartnerItems(
            item->iItemNo,
            character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON].iItemNo)) {
        pair = &character->EquippedItem[W8_EQUIP_SLOT_ALTERNATE_SECONDARY_WEAPON];
    } else if (item_id != -1) {
        pair = FindCharacterItem(*character, ItemWithId(item_id), W8ItemReach::Carried);
    }
    if (!pair) {
        QueueCharacterEvent(character, 39, 0, kCharacterEventFullVolume);
        return {};
    }

    auto& party = g_status.party;
    const auto from = party.locateItem(pair);
    party.exchange(from, party.locateItem(&character->EquippedItem[equip_slot]), -1);
    PostCharacterNotice(CharacterPointerToPartySlot(character), g_localized_strings[0x1d4]);
    return from;
}

/* The same pairing as EquipPartner, but for the items that carry a number of uses
   rather than being worn: the compatible-partner table names the kind to look for,
   and the first one found anywhere on the character takes the uses. With no
   partner the character only voices the attempt. Returns the partner's location
   when its uses ran out. */
static W8ItemLocation MergePartnerUses(W8Character* character, W8ItemInstance* item)
{
    int item_id = item->iItemNo;
    short related_kind;

    if (item_id == -1 || !ItemUsesShots(item_id)) {
        return {};
    }

    if (!GetItemMergeKind(item_id, &related_kind)) return {};

    if (auto* partner =
            FindCharacterItem(*character, ItemOfKind(related_kind), W8ItemReach::Carried)) {
        const auto location = g_status.party.locateItem(partner);
        const bool emptied = MergeUses(item, partner);
        PostCharacterNotice(CharacterPointerToPartySlot(character), g_localized_strings[0x1d4]);
        return emptied ? location : W8ItemLocation{};
    }

    QueueCharacterEvent(character, 39, 0, kCharacterEventFullVolume);
    return {};
}

/* Split a stack of thrown weapons across the two hands. The source is the hand
   opposite the requested one and the destination is the requested hand, so the
   primary hand takes the larger half of the stack and the off hand the smaller;
   a lone item moves across as itself rather than being split, but only when the
   primary hand is the destination. */
static void SplitThrowableStack(W8Character* character, W8EquipSlot equip_slot)
{
    W8ItemInstance created;
    W8EquipSlot source_slot;
    W8ItemInstance* source;
    int item_id;
    unsigned char moved;

    if (equip_slot != W8_EQUIP_SLOT_PRIMARY_WEAPON &&
        equip_slot != W8_EQUIP_SLOT_SECONDARY_WEAPON) {
        return;
    }
    source_slot = GetPairedEquipSlot(equip_slot);
    source = &character->EquippedItem[source_slot];
    item_id = source->iItemNo;
    /* Equipment class two is the thrown weapon, which is the only kind that
       stacks across the two hands. */
    if (item_id == -1 || g_item_records[item_id].equip_class != W8_ITEM_EQUIP_CLASS_THROWN_WEAPON) {
        return;
    }

    moved = source->stack_count;
    if (equip_slot == W8_EQUIP_SLOT_PRIMARY_WEAPON) {
        if (moved == 1) {
            g_status.party.exchange(g_status.party.locateItem(&character->EquippedItem[equip_slot]),
                                    g_status.party.locateItem(source), -1);
            return;
        }
        moved -= (moved >> 1);
    } else {
        if (moved == 1) {
            return;
        }
        moved >>= 1;
    }

    created = MakeItem(item_id, false, source->identified, source->bound);
    source->stack_count -= moved;
    created.stack_count = moved;
    MoveItem(&character->EquippedItem[equip_slot], &created);
}

/* Spend what one attack used and ready the weapon again: thrown weapons come off
   their stack and the other hand's stack is shared out, ammunition is taken from
   the paired hand and its partner equipped when it runs out, and an emptied
   shot weapon draws uses from its partner. Returns whether the thrown weapon ran
   out. */
bool ConsumeAttackItems(unsigned actor, unsigned hand, W8EquipSlot used, W8EquipSlot paired)
{
    auto* character = &g_status.party.characters[actor];
    const W8ItemLocation used_location{W8ItemStorage::Equipment, actor,
                                       static_cast<unsigned>(used)};
    if (paired != W8_EQUIP_SLOT_NONE) {
        const W8ItemLocation paired_location{W8ItemStorage::Equipment, actor,
                                             static_cast<unsigned>(paired)};
        const int id = character->EquippedItem[paired].iItemNo;
        const bool emptied = ConsumeItemUse(&character->EquippedItem[paired], true);
        W8ItemLocation partner{};
        if (character->EquippedItem[paired].stack_count == 0)
            partner = EquipPartner(character, &character->EquippedItem[used], id, paired);
        FinishInventoryEdit(
            {emptied ? paired_location : W8ItemLocation{},
             partner.storage != W8ItemStorage::None ? paired_location : W8ItemLocation{}, partner});
        return false;
    }
    auto* item = &character->EquippedItem[used];
    if (item->iItemNo == -1 || character->Hand[hand].combat_skill != W8_SKILL_RANGED_COMBAT ||
        g_item_records[item->iItemNo].spell_id != W8_SPELL_NONE)
        return false;
    const auto& record = g_item_records[item->iItemNo];
    if (record.equip_class == W8_ITEM_EQUIP_CLASS_THROWN_WEAPON) {
        if (record.flags & W8_ITEM_FLAG_NEVER_DEPLETES) return false;
        const bool emptied = ConsumeItemUse(item, true);
        const bool split = emptied || record.quantity_kind != W8_ITEM_QUANTITY_STACK;
        if (split) SplitThrowableStack(character, used);
        const W8ItemLocation other_hand{W8ItemStorage::Equipment, actor,
                                        static_cast<unsigned>(GetPairedEquipSlot(used))};
        FinishInventoryEdit(
            {split ? used_location : W8ItemLocation{}, split ? other_hand : W8ItemLocation{}});
        return item->iItemNo == -1 || item->stack_count == 0;
    }
    ConsumeItemUse(item, false);
    W8ItemLocation partner{};
    const bool exhausted = item->uses_or_charges == 0;
    if (exhausted) partner = MergePartnerUses(character, item);
    // A shot weapon that runs out counts as a change of the hand that holds it.
    FinishInventoryEdit({exhausted ? used_location : W8ItemLocation{}, partner});
    return false;
}

/* Ready the primary weapon before an attack: equip the off-hand partner it needs
   and draw uses for an empty shot weapon. */
void ReadyPrimaryWeapon(unsigned party_slot)
{
    auto* character = &g_status.party.characters[party_slot];
    auto* weapon = &character->EquippedItem[W8_EQUIP_SLOT_PRIMARY_WEAPON];
    if (weapon->iItemNo == -1) return;
    W8ItemLocation partner{};
    if (character->EquippedItem[W8_EQUIP_SLOT_SECONDARY_WEAPON].iItemNo == -1)
        partner = EquipPartner(character, weapon, -1, W8_EQUIP_SLOT_SECONDARY_WEAPON);
    W8ItemLocation spent{};
    if (weapon->uses_or_charges == 0) spent = MergePartnerUses(character, weapon);
    const W8ItemLocation off_hand{W8ItemStorage::Equipment, party_slot,
                                  W8_EQUIP_SLOT_SECONDARY_WEAPON};
    FinishInventoryEdit(
        {partner.storage != W8ItemStorage::None ? off_hand : W8ItemLocation{}, partner, spent});
}

// ---- Using items

/* Use one item as a character's action. The attempts that cannot happen at all
   come first, each with its own notice and character event: a spent charge
   stack, a spent dose, a class-0x0d item whose influence the character is still
   under, and the one item that resolves to an NPC fact instead of a spell.
   Everything else aims the item and casts the spell it carries, which is where
   the spell's presentation skill and the character's own level in it decide how
   hard the attempt is. `out_uses` receives the fatigue cost of the attempt, and
   stays -1 when nothing was attempted; a casting aid's attempt costs eight per
   level of its spell plus twenty, even when it fails. */
namespace {
struct ItemUseOutcome {
    W8ItemUseResult result;
    bool emptied = false;
};
}

static ItemUseOutcome UseItemOnce(W8Character* character, W8ItemInstance* item)
{
    const W8ItemDatabaseRecord* record = &g_item_records[item->iItemNo];
    unsigned int party_slot = CharacterPointerToPartySlot(character);
    unsigned int power;
    unsigned int chance;
    W8Skill skill;
    int event_type;
    int fatigue_cost = -1;
    bool emptied = false;
    const auto own_cost = [&] {
        return fatigue_cost < 0 ? std::nullopt : std::optional<int>{fatigue_cost};
    };
    W8TargetSource target;
    // Retail returned an unset byte for a spent item; nothing was used.
    unsigned char used = 0;

    if (!CanCharacterUseItem(character, item->iItemNo)) {
        PostCharacterNotice(party_slot,
                            fmt::sprintf(g_localized_strings[0x164], GetItemDisplayName(item)));
        return {};
    }
    if (!CanCharacterActivateItem(character, item)) {
        PostCharacterNotice(party_slot,
                            fmt::sprintf(g_localized_strings[0x165], GetItemDisplayName(item)));
        return {};
    }

    if (record->quantity_kind == W8_ITEM_QUANTITY_CHARGES && item->uses_or_charges == 0) {
        PostCharacterNotice(party_slot,
                            fmt::sprintf(g_localized_strings[0x1f1], GetItemDisplayName(item)));
        event_type = g_special_event13;
    } else if (record->quantity_kind == W8_ITEM_QUANTITY_SHOTS && item->uses_or_charges == 0) {
        PostCharacterNotice(party_slot,
                            fmt::sprintf(g_localized_strings[0x1f2], GetItemDisplayName(item)));
        event_type = 39;
    } else {
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            character->uiCondition[W8_CONDITION_SILENCED] != 0) {
            PostCharacterNotice(party_slot, g_localized_strings[0x166]);
            return {};
        }

        /* The one item that is not used on anybody: it hands the level's NPC
           over to the fact that says the party has met them. */
        if (item->iItemNo == 0x29f) {
            W8NpcState* npc =
                GetNpcState(g_status.party.rows[CharacterPointerToPartySlot(character)].npc_index);
            if (npc == 0) {
                Fail("pNPC");
            }
            if (npc->name_style != W8_NPC_RFS81_A) {
                Fail("pNPC->ubNPCDBaseID == NPC_RFS81A");
            }
            SetFact(W8_FACT_RFS81_HAS_BEEN_FIXED, 1, false);
            SetFact(W8_FACT_QUEST_ANDROID_BROKEN, 0, false);
            return {{1, std::nullopt}, ConsumeItemUse(item, true)};
        }

        if (record->equip_class != W8_ITEM_EQUIP_CLASS_INSTRUMENT &&
            record->equip_class != W8_ITEM_EQUIP_CLASS_GADGET) {
            /* An ordinary item carries the spell's own power, and the one use
               kind whose presentation skill is nine costs a flat ten. */
            if (GetItemSpellPresentation(record) == 9) {
                fatigue_cost = 10;
            }
            power = record->spell_power;
        } else {
            /* A casting aid has to beat the difficulty of the character's own
               level in the skill that presents the spell. Each attempt that
               fails is retried one power lower, and the first roll that
               succeeds ends the search. Bug: spells 0x58 and 0x74 have no
               presentation skill, and still index skills[-1] here. */
            skill = GetItemSpellPresentation(record);
            /* The attempt costs fatigue by the carried spell's level, whether
               or not it succeeds. */
            fatigue_cost = g_spell_records[record->spell_id].spell_level * 8 + 0x14;
            power = 7;
            do {
                if (GetItemUseDifficulty(character, skill, character->skills[skill].level,
                                         record->spell_id, power) > 0xf) {
                    --power;
                    continue;
                }
                chance = power * 7;
                while (power != 0 && chance > Random(100)) {
                    --power;
                    chance -= 7;
                }
                break;
            } while (power != 0);

            if (power == 0) {
                PostCharacterNotice(
                    party_slot, fmt::sprintf(g_localized_strings[0x1f3], GetItemDisplayName(item)));
                PracticeCharacterSkill(character, skill, 1, false);
                return {{0, own_cost()}};
            }
        }

        /* The targeting classes aim through the slot's own out-of-combat
           target; everything else is used on the character acting. */
        SetTargetSourceToCharacter(party_slot, &target);
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_POTION ||
            (record->equip_class > W8_ITEM_EQUIP_CLASS_SCROLL &&
             record->equip_class < W8_ITEM_EQUIP_CLASS_KEY)) {
            if (!g_combat.active()) {
                W8CombatSlot* aimed = &g_status.party.rows[party_slot].target_out_of_combat;
                if (aimed->iType == W8_TARGET_KIND_CHARACTER) {
                    SetTargetSourceToCharacter(aimed->iChar, &target);
                } else if (aimed->iType == W8_TARGET_KIND_MONSTER) {
                    SetTargetSourceToMonster(FindMonster(aimed->iMonsterID), &target);
                }
            } else {
                AimAtCharacter(party_slot, party_slot, W8_TARGETING_CONTEXT_OUT_OF_COMBAT);
            }
        }
        target.name_known = true;

        if (static_cast<int>(record->category) >= W8_ITEM_USE_TYPE_COUNT) {
            Fail("pItemDB->ubUseType < USE_TYPE_COUNT");
        }
        /* A spell-carrying item the world-cursor handler refuses cannot be used
           at all, and the attempt is over before anything is announced. */
        if (record->spell_id != W8_SPELL_NONE &&
            g_item_records[item->iItemNo].spell_id != W8_SPELL_NONE &&
            g_item_records[item->iItemNo].equip_class != W8_ITEM_EQUIP_CLASS_GADGET &&
            (g_item_records[item->iItemNo].equip_class < W8_ITEM_EQUIP_CLASS_FOOD ||
             g_item_records[item->iItemNo].equip_class > W8_ITEM_EQUIP_CLASS_DRINK) &&
            DispatchWorldCursorNodeCommand(0, 4, 1)) {
            return {{0, 0}};
        }

        if (TargetSourceIsCharacter(&target, 0)) {
            PostCharacterNotice(
                target.iChar,
                fmt::format("{} {}", g_localized_strings[g_item_use_messages[record->category * 2]],
                            GetItemDisplayName(item)));
        } else if (TargetSourceIsMonster(&target, 0)) {
            ShowNotice(9,
                       fmt::format("{} {} {}", GetMonsterName(FindMonster(target.iMonsterID), 0, 0),
                                   g_localized_strings[g_item_use_messages[record->category * 2]],
                                   GetItemDisplayName(item)));
        } else {
            Fail("FALSE");
        }

        if (g_settings.verbose_combat_messages == 0) {
            SetTextBoxMode(W8_TEXT_BOX_MODE_APPEND, -1);
        }
        if (record->spell_id == W8_SPELL_NONE) {
            std::string message = fmt::format(
                "UseItem: ERROR - Usable item {:d} doesn't do anything!", item->iItemNo);
            Fail("FALSE", message);
            used = 0;
            fatigue_cost = 0;
        } else {
            used = CastItemSpell(character, item, power);
            if (!used) {
                fatigue_cost = 0;
            } else {
                emptied = ConsumeItemUse(item, true);
            }
        }
        return {{used, own_cost()}, emptied};
    }

    QueueCharacterEvent(&g_status.party.characters[party_slot], event_type, 0,
                        kCharacterEventFullVolume);
    return {{used, own_cost()}, emptied};
}

W8ItemUseResult UseItem(unsigned actor, W8ItemLocation location)
{
    auto* item = g_status.party.itemAt(location);
    if (actor >= W8_PARTY_SLOT_COUNT || !item || item->iItemNo == -1) return {};
    const auto outcome = UseItemOnce(&g_status.party.characters[actor], item);
    FinishInventoryEdit({outcome.emptied ? location : W8ItemLocation{}});
    return outcome.result;
}
