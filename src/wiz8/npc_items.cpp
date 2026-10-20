#include "wiz8/npc_items.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "character_skills.h"
#include "local_code/ConditionsAndEnchantments.h"
#include "npc_trade_price.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/cursor.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/skills.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/npc_state.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

static W8NpcItemEntry MakeNpcItemEntry(int item_id)
{
    W8NpcItemEntry entry{};
    entry.item = MakeItem(Data(), CurrentGame().rng, item_id, true, true, false);
    entry.item.stack_count = 0;
    return entry;
}

static int FindNpcStockItem(W8NpcState* npc, int item_id)
{
    const auto count = npc->items.size();
    for (unsigned int index = 0; index < count; ++index) {
        if (npc->items[index].item.iItemNo == item_id) {
            return index;
        }
    }
    return -1;
}

static unsigned char GetNpcStockQuantity(W8NpcState* npc, int item_id)
{
    const int index = FindNpcStockItem(npc, item_id);
    return index >= 0 ? npc->items[index].quantity : 0;
}

static unsigned char GetNpcConfiguredStockQuantity(W8NpcState* npc, int item_id)
{
    const auto count = npc->record->item_stock_rules.size();
    for (unsigned int index = 0; index < count; ++index) {
        const W8NpcItemStockRule* rule = &npc->record->item_stock_rules[index];
        if (rule->item_id == item_id) {
            return rule->quantity;
        }
    }
    return 0;
}

static char NpcStockChanceForTier(W8NpcStockChanceTier tier)
{
    switch (tier) {
    case W8NpcStockChanceTier::never:
        return 0;
    case W8NpcStockChanceTier::quarter:
        return 25;
    case W8NpcStockChanceTier::half:
        return 50;
    case W8NpcStockChanceTier::three_quarters:
        return 75;
    case W8NpcStockChanceTier::always:
        return 100;
    default:
        return 0;
    }
}

static unsigned char JitterNpcStockQuantity(unsigned char amount)
{
    int roll = CurrentGame().rng.below(3);
    if (roll == 0) {
        unsigned char jitter = static_cast<unsigned char>(amount >> 1);
        amount += jitter;
    } else if (roll == 1) {
        unsigned char jitter = static_cast<unsigned char>(-(amount >> 1));
        amount += jitter;
    }
    return amount;
}

static void RemoveDepletedNpcStock(W8NpcState* npc)
{
    std::erase_if(npc->items, [](const W8NpcItemEntry& entry) { return entry.quantity == 0; });
}

int AddNpcItem(W8NpcState* npc, int item_id, unsigned int quantity)
{
    const W8ItemDatabaseRecord* record;
    W8NpcItemEntry* entry;
    unsigned int repeats;
    unsigned int added;
    int index;

    if (npc == 0) {
        return -1;
    }
    record = &Data().items[item_id];
    if (record->equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        repeats = quantity;
    } else {
        repeats = 1;
    }
    if (repeats == 0) {
        return item_id;
    }
    added = 0;
    for (; added < repeats; ++added) {
        index = -1;
        if (record->equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            index = FindNpcStockItem(npc, item_id);
        }
        if (index == -1) {
            index = static_cast<int>(npc->items.size());
            npc->items.push_back(MakeNpcItemEntry(item_id));
        }
        entry = &npc->items[index];
        if (record->equip_class == W8_ITEM_EQUIP_CLASS_AMMUNITION) {
            entry->item.stack_count = 0x19;
            entry->quantity = 1;
        } else if (record->quantity_kind == W8_ITEM_QUANTITY_STACK) {
            entry->item.stack_count += quantity;
            entry->quantity = 1;
        } else {
            entry->item.stack_count = 1;
            entry->quantity += quantity;
        }
    }
    return index;
}

bool MaintainNpcStock(W8NpcState* npc, bool force)
{
    W8NpcItemEntry* entry;
    const W8NpcItemStockRule* rule;
    std::size_t count;
    unsigned int index;
    unsigned int rule_index;
    int item_id;
    unsigned char configured;
    unsigned char held;

    count = npc->items.size();
    for (index = 0; index < count; ++index) {
        entry = &npc->items[index];
        if (((entry->quantity != 0) &&
             (NormalizeItemQuantityKind(&entry->item),
              Data().items[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK)) &&
            entry->quantity > entry->item.stack_count) {
            entry->item.stack_count = entry->quantity;
            entry->quantity = 1;
        }
    }

    if (static_cast<unsigned int>(CurrentGame().status.world_clock - npc->restock_clock) > 0xa8c0 ||
        force) {
        npc->restock_clock = CurrentGame().status.world_clock;
        count = npc->record->item_stock_rules.size();
        for (rule_index = 0; rule_index < count; ++rule_index) {
            rule = &npc->record->item_stock_rules[rule_index];
            if (rule->persistent != 0) {
                item_id = rule->item_id;
                configured = GetNpcConfiguredStockQuantity(npc, item_id);
                if (configured != 0) {
                    item_id = rule->item_id;
                    held = GetNpcStockQuantity(npc, item_id);
                    if (held <= configured / 2) {
                        configured -= held;
                        configured = JitterNpcStockQuantity(configured);
                        if (configured != 0) {
                            AddNpcItem(npc, rule->item_id, configured);
                        }
                    }
                }
            }
        }
    }

    if (static_cast<unsigned int>(CurrentGame().status.world_clock - npc->maintenance_clock) <
            0x15180 &&
        !force) {
        return false;
    }
    DecayNpcInventory(npc);
    RestockNpcItems(npc);
    RemoveDepletedNpcStock(npc);
    npc->maintenance_clock = CurrentGame().status.world_clock;
    npc->restock_clock = CurrentGame().status.world_clock;
    return true;
}

bool PopulateNpcStock(W8NpcState* npc)
{
    const W8NpcItemStockRule* rule;
    std::size_t rule_count;
    unsigned int rule_index;
    unsigned int remaining;
    int item_id;
    char persistent;
    char added;
    char chance;
    W8NpcStockChanceTier tier;

    rule_count = npc->record->item_stock_rules.size();
    if (rule_count == 0) {
        npc->items.clear();
        return true;
    }
    for (rule_index = 0; rule_index < rule_count; ++rule_index) {
        rule = &npc->record->item_stock_rules[rule_index];
        item_id = rule->item_id;
        if (item_id < static_cast<int>(Data().items.size())) {
            persistent = rule->persistent;
            added = 0;
            if (rule->quantity != 0) {
                remaining = rule->quantity;
                do {
                    if (persistent == 0) {
                        tier = RateItemIdentifyDifficulty(npc, item_id);
                    } else {
                        tier = W8NpcStockChanceTier::always;
                    }
                    chance = NpcStockChanceForTier(tier);
                    if (CurrentGame().rng.below(100) < static_cast<unsigned int>(chance)) {
                        ++added;
                    }
                    --remaining;
                } while (remaining != 0);
            }
            if (added != 0) {
                AddNpcItem(npc, rule->item_id, added);
            }
        }
    }
    SortNpcItems(npc);
    npc->maintenance_clock = CurrentGame().status.world_clock;
    npc->restock_clock = CurrentGame().status.world_clock;
    return true;
}

/* Order the stock as the item pool orders items; equal entries keep their order. */
void SortNpcItems(W8NpcState* npc)
{
    std::ranges::stable_sort(npc->items,
                             [](const W8NpcItemEntry& first, const W8NpcItemEntry& second) {
                                 return ItemPrecedesInPool(Data(), first.item, second.item);
                             });
}

/* Add stock described by an item instance. Only the instance's item id is used;
   the entry receives a freshly built item rather than a copy of the argument.
   Equipment, equip_class four, always takes a new entry instead of merging. The
   quantity then lands in whichever of the two counts the item's quantity kind
   uses, and the other count is raised to one while it is still clear. */
int AddNpcItemFromInstance(W8NpcState* npc, const W8ItemInstance* item, char quantity)
{
    W8NpcItemEntry* entry;
    int item_id;
    int index;

    if (npc == 0) {
        return -1;
    }
    if (item->iItemNo < 0) {
        return -1;
    }
    item_id = item->iItemNo;
    index = -1;
    if (Data().items[item_id].equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        index = FindNpcStockItem(npc, item_id);
    }
    if (index == -1) {
        item_id = item->iItemNo;
        index = static_cast<int>(npc->items.size());
        npc->items.push_back(MakeNpcItemEntry(item_id));
    }
    entry = &npc->items[index];
    if (Data().items[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (entry->quantity == 0) {
            entry->quantity = 1;
        }
        entry->item.stack_count += quantity;
        return index;
    }
    if (entry->item.stack_count == 0) {
        entry->item.stack_count = 1;
    }
    entry->quantity += quantity;
    return index;
}

/* Top up an NPC's stock from its database rules. A rule restocks only while it
   is not the persistent kind, while any fact gating its item is set, and while
   the NPC holds no more than half the configured quantity. The difficulty tier
   then becomes a percentage chance, and the shortfall is jittered by half up or
   down before being added. Item 0x1fc is gated behind fact 0x15f; neither has a
   recovered symbolic name. */
int RestockNpcItems(W8NpcState* npc)
{
    const W8NpcItemStockRule* rule;
    std::size_t rule_count;
    unsigned int rule_index;
    int item_id;
    int configured;
    unsigned char held;
    unsigned char amount;
    W8NpcStockChanceTier tier;
    char chance;

    rule_count = npc->record->item_stock_rules.size();
    for (rule_index = 0; rule_index < rule_count; ++rule_index) {
        rule = &npc->record->item_stock_rules[rule_index];
        if (rule->persistent != 0 || (rule->item_id == 0x1fc && GetFact(W8_FACT_TEMPLAR) == 0)) {
            continue;
        }

        item_id = rule->item_id;
        configured = GetNpcConfiguredStockQuantity(npc, item_id);
        if (configured <= 0) {
            continue;
        }

        item_id = rule->item_id;
        held = GetNpcStockQuantity(npc, item_id);
        if (held > configured / 2) {
            continue;
        }

        if (rule->persistent == 0) {
            tier = RateItemIdentifyDifficulty(npc, rule->item_id);
        } else {
            tier = W8NpcStockChanceTier::always;
        }
        chance = NpcStockChanceForTier(tier);
        if (static_cast<unsigned int>(chance) <= CurrentGame().rng.below(100)) {
            continue;
        }

        amount = configured - held;
        amount = JitterNpcStockQuantity(amount);
        if (amount != 0) {
            AddNpcItem(npc, rule->item_id, amount);
        }
    }
    return 1;
}

/* Rate an item's identify difficulty against the band the party's average level
   supports. Three means the difficulty sits inside the band, zero that it is
   past the upper bound, and one or two grade how far below the lower bound it
   falls. Every comparison here is signed, so the difficulty byte is read into a
   char, and the tier is returned byte-sized. The NPC argument is unused by the
   original. */
W8NpcStockChanceTier RateItemIdentifyDifficulty(W8NpcState* npc, int item_id)
{
    char difficulty;
    char level;
    char base;
    char raw_upper;
    char lower;
    char upper;

    difficulty = static_cast<char>(Data().items[item_id].identify_difficulty);
    level = static_cast<char>(GetAveragePartyLevel());
    base = static_cast<char>(level * 100 / 30) / 5;
    raw_upper = base + 4;
    if (base <= 16) {
        lower = base < 1 ? 1 : base;
    } else {
        lower = 16;
    }
    if (raw_upper <= 20) {
        upper = raw_upper < 4 ? 4 : raw_upper;
    } else {
        upper = 20;
    }
    if (difficulty > upper) {
        return W8NpcStockChanceTier::never;
    }
    if (difficulty < lower) {
        return static_cast<W8NpcStockChanceTier>((difficulty + 4 >= lower) + 1);
    }
    return W8NpcStockChanceTier::three_quarters;
}

/* Add stock that only becomes ordinary trade stock once the world clock passes
   the given delay. This is what establishes the leading field as a clock stamp
   rather than a state enum. */
int AddNpcItemWithDelay(W8NpcState* npc, int item_id, unsigned int quantity, int delay)
{
    int index;

    if (npc == 0) {
        return -1;
    }
    index = AddNpcItem(npc, item_id, quantity);
    if (index == -1) {
        return -1;
    }
    npc->items[index].available_at = CurrentGame().status.world_clock + delay;
    return index;
}

W8NpcItemEntry* GetNpcItemAt(W8NpcState* npc, int index)
{
    if (index < 0 || std::cmp_greater_equal(index, npc->items.size())) {
        return nullptr;
    }
    return &npc->items[index];
}

std::size_t GetNpcItemCount(W8NpcState* npc)
{
    return npc->items.size();
}

static char g_sound_cash_transaction[] = "Data\\Sound\\misc\\Cash Transaction.wav";

static bool SellItemToNpc(W8NpcState* npc, W8ItemInstance* item, unsigned char quantity,
                          bool suppress_payment)
{
    W8ItemInstance stack;
    int amount;

    if ((Data().items[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (NpcAcceptsTradeItemClass(npc, item)) {
            stack = *item;
            stack.stack_count = quantity;
            amount = CalculateTradeStackPrice(npc, &stack, W8_TRADE_PRICE_PARTY_SELLS);
            if (!suppress_payment) {
                AddPartyGold(CurrentGame(), amount, false);
            }
            if (item->stack_count == 0) {
                item->stack_count = 1;
            }
            AddNpcItemFromInstance(npc, item, quantity);
            return true;
        }
    }
    return false;
}

W8ItemInstance* ResolveTradeSelection(W8NpcState* npc, const W8TradeSelection& selection)
{
    W8ItemInstance* item = nullptr;
    switch (selection.source) {
    case W8TradeSource::Backpack:
        if (selection.character < 0 || selection.character >= 8 ||
            !CurrentGame().party.rows[selection.character].fOccupied || selection.slot < 0 ||
            selection.slot >= 8)
            return nullptr;
        item = &CurrentGame().party.characters[selection.character].backpack[selection.slot];
        break;
    case W8TradeSource::PartyPool:
        if (selection.slot < 0 ||
            std::cmp_greater_equal(selection.slot, CurrentGame().party.item_count))
            return nullptr;
        item = &CurrentGame().party.item_pool[selection.slot];
        break;
    case W8TradeSource::NpcStock: {
        auto* entry = npc ? GetNpcItemAt(npc, selection.slot) : nullptr;
        if (!entry || entry->available_at != 0 || entry->quantity != selection.stock_quantity)
            return nullptr;
        item = &entry->item;
        break;
    }
    case W8TradeSource::Gold:
        return nullptr;
    }
    return item->iItemNo != -1 && *item == selection.expected ? item : nullptr;
}

W8TradeResult CompleteNpcTrade(W8NpcState* npc, const W8TradeSelection& selection,
                               unsigned char quantity)
{
    auto* item = ResolveTradeSelection(npc, selection);
    if (!item || !npc || quantity == 0) return {W8TradeOutcome::InvalidSelection};
    if (selection.source == W8TradeSource::NpcStock) {
        return CompleteNpcItemPurchase(npc, selection.slot, quantity, false);
    }
    const auto available = item->stack_count ? item->stack_count : 1;
    if (quantity > available) return {W8TradeOutcome::InvalidSelection};
    if (selection.source == W8TradeSource::Backpack) {
        const int owner = CurrentGame().party.rows[selection.character].npc_index;
        auto* owner_npc = owner == -1 ? nullptr : GetNpcState(owner);
        if (owner_npc && NpcWantsItem(owner_npc, item)) return {W8TradeOutcome::WantedByOwner};
    }
    if (!NpcAcceptsTradeItem(npc, item)) return {W8TradeOutcome::Refused};
    const bool wanted = NpcHasTopic(npc, item->iItemNo);
    if (!SellItemToNpc(npc, item, quantity, wanted)) return {W8TradeOutcome::Refused};
    const W8ItemLocation sold =
        selection.source == W8TradeSource::Backpack
            ? W8ItemLocation{W8ItemStorage::Backpack, static_cast<unsigned>(selection.character),
                             static_cast<unsigned>(selection.slot)}
            : W8ItemLocation{W8ItemStorage::Pool, 0, static_cast<unsigned>(selection.slot)};
    RemoveInventoryQuantity(CurrentGame(), sold, quantity);
    wiz8::sound::play(g_sound_cash_transaction);
    return {W8TradeOutcome::Complete, wanted};
}

bool NpcAcceptsTradeItemClass(const W8NpcState* npc, const W8ItemInstance* item)
{
    const auto cls = Data().items[item->iItemNo].equip_class;
    return cls <= W8_ITEM_EQUIP_CLASS_OTHER &&
           (npc->record->trade_item_class_mask & (std::uint32_t{1} << cls)) != 0;
}

bool NpcAcceptsTradeItem(W8NpcState* npc, W8ItemInstance* item)
{
    if ((Data().items[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
        return false;
    }
    return NpcAcceptsTradeItemClass(npc, item);
}

int CalculateNpcTradeStackPrice(W8NpcState* npc, int item_id, W8TradePriceKind price_kind,
                                unsigned char stack_count, bool identified)
{
    // A price query observes contents; constructing random gameplay items here
    // made merely drawing a trade row advance the shared random sequence.
    W8ItemInstance item{.iItemNo = item_id,
                        .stack_count = stack_count,
                        .identified = identified || (Data().items[item_id].flags &
                                                     W8_ITEM_FLAG_AUTO_IDENTIFY) != 0};
    return CalculateTradeStackPrice(npc, &item, price_kind);
}

/* The bargained price of one offered stack: the party's best communication
   skill (0x16) builds a 0.2%-per-point adjustment, halved once it would move
   the price further than half the record's buy/sell spread, then applied to
   the low factor when the NPC buys (mode 0) or against the high factor when
   the party buys (mode 1). Stackable non-armour bundles price per unit times
   the count, everything else prices the whole stack value, unidentified
   merchandise goes at quarter price, and no stack settles below 1. */
int CalculateTradeStackPrice(W8NpcState* npc, W8ItemInstance* item, W8TradePriceKind price_kind)
{
    int stack_value = GetItemStackValue(item);
    unsigned int skill = GetBestPartySkillLevel(W8_SKILL_COMMUNICATION, 0);
    const W8NpcDatabaseRecord* record = npc->record;
    float scale = skill * 0.002f;
    if (scale > fabs(record->sell_price_factor - record->buy_price_factor) * 0.5) {
        scale *= 0.5f;
    }
    float price;
    if (price_kind != W8_TRADE_PRICE_PARTY_SELLS) {
        price = record->sell_price_factor - scale;
    } else {
        price = record->buy_price_factor + scale;
    }
    if (price <= 0.0f) {
        price = 0.1f;
    }
    const W8ItemDatabaseRecord* info = &Data().items[item->iItemNo];
    int amount;
    if (info->quantity_kind == W8_ITEM_QUANTITY_STACK && item->stack_count > 1 &&
        info->equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        amount = static_cast<int>(price * info->value) * item->stack_count;
    } else {
        amount = static_cast<int>(price * stack_value);
    }
    if (!item->identified) {
        amount /= 4;
    }
    return amount != 0 ? amount : 1;
}

/* Take quantity units of one stock slot off the NPC and into the party. Each
   pass hands over at most one maximum-quantity stack (or one unit for the
   non-stack quantity kinds) until the request is filled; the price of the
   accumulated stack then leaves the party purse and the stock entry shrinks.
   The trailing pass drops entries whose remaining count hit zero. */
W8TradeResult CompleteNpcItemPurchase(W8NpcState* npc, int index, unsigned char quantity,
                                      bool no_payment)
{
    auto* entry = GetNpcItemAt(npc, index);
    if (!entry || quantity == 0 || entry->available_at != 0)
        return {W8TradeOutcome::InvalidSelection};
    const auto stock = entry->item;
    const auto& record = Data().items[stock.iItemNo];
    const auto available =
        record.quantity_kind == W8_ITEM_QUANTITY_STACK ? stock.stack_count : entry->quantity;
    if (quantity > available) return {W8TradeOutcome::InvalidSelection};
    const auto price =
        CalculateNpcTradeStackPrice(npc, stock.iItemNo, W8_TRADE_PRICE_PARTY_BUYS, quantity, true);
    if (!no_payment && std::cmp_greater(price, CurrentGame().party.gold))
        return {W8TradeOutcome::InsufficientGold};

    // Plan the destination too. No acquisition callback can observe paid-for
    // stock that has not yet reached its final pool, cursor or world location.
    std::vector<W8ItemInstance> pool(CurrentGame().party.item_pool,
                                     CurrentGame().party.item_pool +
                                         CurrentGame().party.item_count);
    std::vector<std::size_t> acquired;
    std::vector<W8ItemInstance> overflow;
    unsigned int moved = 0;
    while (moved < quantity) {
        W8ItemInstance hand;
        hand = MakeItem(Data(), CurrentGame().rng, stock.iItemNo, true, true, false);
        unsigned int unit = 1;
        if (record.quantity_kind == W8_ITEM_QUANTITY_STACK) {
            if (record.maximum_quantity == 0) return {W8TradeOutcome::Refused};
            unit = std::min<unsigned int>(quantity - moved, record.maximum_quantity);
            hand.stack_count = unit;
            for (std::size_t slot = 0; slot < pool.size() && hand.stack_count; ++slot) {
                auto& destination = pool[slot];
                if (destination.iItemNo != hand.iItemNo ||
                    destination.identified != hand.identified)
                    continue;
                const auto count =
                    destination.stack_count ? destination.stack_count : destination.uses_or_charges;
                if (count > record.maximum_quantity) return {W8TradeOutcome::Refused};
                const auto merged =
                    std::min<unsigned int>(hand.stack_count, record.maximum_quantity - count);
                if (!merged) continue;
                destination.stack_count = count + merged;
                destination.uses_or_charges = 0;
                hand.stack_count -= merged;
                acquired.push_back(slot);
            }
        } else if (record.quantity_kind != W8_ITEM_QUANTITY_NONE) {
            hand.uses_or_charges = stock.uses_or_charges;
        }
        if (record.quantity_kind != W8_ITEM_QUANTITY_STACK || hand.stack_count) {
            if (pool.size() < 500) {
                for (auto& slot : acquired) ++slot;
                pool.insert(pool.begin(), hand);
                acquired.push_back(0);
            } else
                overflow.push_back(hand);
        }
        moved += unit;
    }
    std::optional<W8ItemInstance> held;
    std::vector<std::unique_ptr<W8WorldItem>> drops;
    if (!overflow.empty()) {
        if ((record.flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
            if ((CurrentGame().party.held.item.iItemNo != -1) || overflow.size() != 1)
                return {W8TradeOutcome::Refused};
            held = overflow.front();
            overflow.clear();
        } else {
            const auto drop_position = FindItemDropPosition();
            if (!drop_position) return {W8TradeOutcome::Refused};
            for (auto& item : overflow)
                drops.push_back(CreateWorldItem(&item, &*drop_position,
                                                W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
        }
    }
    if (!drops.empty()) {
        auto& items = CurrentLevelPopulation().world_items;
        items.reserve(items.size() + drops.size());
    }
    // Publish the complete transfer before encumbrance, reactions or presentation.
    std::ranges::copy(pool, CurrentGame().party.item_pool);
    CurrentGame().party.item_count = static_cast<unsigned int>(pool.size());
    ConsumeNpcItemQuantity(npc, index, quantity);
    RemoveDepletedNpcStock(npc);
    if (!no_payment) SpendPartyGold(CurrentGame().party, price);
    for (auto& drop : drops) AddWorldItem(std::move(drop));
    if (held) {
        CurrentGame().party.held.item = *held;

        CurrentGame().party.held.actor = -1;
        CurrentGame().party.held.origin = {};
        SetItemCursor(0);
    }
    SettleActiveParty(CurrentGame());
    for (std::size_t slot : acquired) {
        auto* item = &CurrentGame().party.item_pool[slot];
        UpdateFactsAfterAcquiringItem(item);
        DeliverExceptionalItemReaction(item, true, nullptr);
    }
    wiz8::sound::play(g_sound_cash_transaction);
    return {W8TradeOutcome::Complete, false, !drops.empty()};
}

bool ConsumeNpcItemQuantity(W8NpcState* npc, int index, unsigned char quantity)
{
    W8NpcItemEntry* entry;

    if (quantity == 0) {
        return false;
    }
    entry = GetNpcItemAt(npc, index);
    if (entry == 0) {
        return false;
    }
    if (Data().items[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
        if (entry->item.stack_count < quantity) {
            entry->item.stack_count = quantity;
        }
        entry->item.stack_count -= quantity;
        if (entry->item.stack_count == 0) {
            entry->quantity = 0;
            return true;
        }
    } else {
        if (entry->quantity < quantity) {
            quantity = entry->quantity;
        }
        entry->quantity -= quantity;
    }
    return true;
}

/* Remove ordinary stock that is neither protected by its item flags nor kept
   by the NPC database's persistent stock rule. */
void DecayNpcInventory(W8NpcState* npc)
{
    int item_id;
    std::size_t item_count;
    W8NpcItemEntry* entry;
    std::size_t rule_count;
    const W8NpcItemStockRule* rule;
    char quantity;
    unsigned int rule_index;
    unsigned int item_index;
    unsigned char remaining;

    item_count = npc->items.size();
    item_index = 0;
    if (item_count > 0) {
        do {
            entry = &npc->items[item_index];
            if (((entry->quantity != 0) &&
                 (item_id = entry->item.iItemNo,
                  (Data().items[item_id].flags &
                   (W8_ITEM_FLAG_NO_DISCARD | W8_ITEM_FLAG_MERCHANT_CANNOT_SELL)) == 0))) {
                if (entry->available_at > 0) {
                    goto next_item;
                }
                rule_count = npc->record->item_stock_rules.size();
                for (rule_index = 0; rule_index < rule_count; ++rule_index) {
                    rule = &npc->record->item_stock_rules[rule_index];
                    if (rule->item_id == item_id) {
                        if (rule->persistent == 1) {
                            goto next_item;
                        }
                        break;
                    }
                }

                remaining = entry->quantity;
                quantity = 0;
                if (remaining > 0) {
                    rule_count = remaining;
                    do {
                        if (CurrentGame().rng.below(100) < 100) {
                            ++quantity;
                        }
                        --rule_count;
                    } while (rule_count != 0);
                    if (quantity != 0) {
                        ConsumeNpcItemQuantity(npc, item_index, quantity);
                    }
                }
            }

        next_item:
            ++item_index;
        } while (item_index < item_count);
    }
}

void MatureNpcDelayedItems(W8NpcState* npc)
{
    for (auto& stock : npc->items) {
        W8NpcItemEntry* entry = &stock;
        if (entry->available_at == 0 ||
            entry->available_at >= static_cast<unsigned int>(CurrentGame().status.world_clock)) {
            continue;
        }
        entry->available_at = 0;
        if (npc->record->kind == 3) {
            switch (entry->item.iItemNo) {
            case 0x28a:
                SetFact(W8_FACT_ZYNARYX_PLATE_COMPLETE, 1, false);
                break;
            case 0x28b:
                SetFact(W8_FACT_STEELHIDE_COMPLETE, 1, false);
                break;
            case 0x28c:
                SetFact(W8_FACT_FEATHERWEIGHT_COMPLETE, 1, false);
                break;
            case 0x28d:
                SetFact(W8_FACT_BEASTSLAYER_COMPLETE, 1, false);
                break;
            case 0x28e:
                SetFact(W8_FACT_EBON_STAFF_COMPLETE, 1, false);
                break;
            default:
                break;
            }
        } else if (npc->record->kind == 0x49) {
            if (entry->item.iItemNo == 0x1b0) {
                SetFact(W8_FACT_TRYNNIE_FUZZFAS_POTION_DONE, 1, false);
                SetFact(W8_FACT_TRYNNIE_FUZZFAS_POTION, 0, false);
                return;
            }
        } else if (npc->record->kind == 0x39) {
            if (entry->item.iItemNo == 500) {
                SetFact(W8_FACT_FERRO_MIRROR_ARMOR_ALL_INGREDIENTS, 0, false);
                SetFact(W8_FACT_MIRROR_ARMOR_COMPLETE, 1, false);
                return;
            }
            if (entry->item.iItemNo == 0x1f5) {
                SetFact(W8_FACT_FERRO_IVORY_BLADE_ALL_INGREDIENTS, 0, false);
                SetFact(W8_FACT_IVORY_BLADE_COMPLETE, 1, false);
                return;
            }
            if (entry->item.iItemNo == 0x1f8) {
                SetFact(W8_FACT_FERRO_VAMPIRE_CHAIN_ALL_INGREDIENTS, 0, false);
                SetFact(W8_FACT_VAMPIRE_CHAIN_COMPLETE, 1, false);
                return;
            }
        }
    }
}

/* Restock the NPC's trade inventory, then refresh the derived stock state. */
void RestockNpcInventory(W8NpcState* npc)
{
    MaintainNpcStock(npc, false);
    MatureNpcDelayedItems(npc);
}
