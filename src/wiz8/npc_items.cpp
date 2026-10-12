#include <cstdint>
#include <utility>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "wiz8/layouts/npc_state.h"
#include "wiz8/npc_items.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/gameplay_databases.h"
#include "wiz8/fact_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_tables.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/MonsterManager.h"
#include "random.h"
#include "soundman.h"
#include "character_skills.h"
#include "local_code/ConditionsAndEnchantments.h"
#include "npc_trade_price.h"
#include "xstatus.h"

/* Original translation-unit ownership is unknown; surrounding anchors do not resolve it. */

static W8NpcItemEntry* CreateNpcItemEntry(std::int32_t item_id)
{
    W8NpcItemEntry* entry = new W8NpcItemEntry;
    if (entry != 0) {
        memset(entry, 0, sizeof(*entry));
        ReplaceOrCreateItem(&entry->item, item_id, true, true, false);
    }
    entry->item.stack_count = 0;
    return entry;
}

static std::int32_t FindNpcStockItem(W8NpcState* npc, std::int32_t item_id)
{
    std::uint32_t count = (npc->items ? npc->items->size() : 0);
    for (std::uint32_t index = 0; index < count; ++index) {
        W8NpcItemEntry* entry = (*npc->items)[index];
        if (entry != 0 && entry->item.iItemNo == item_id) {
            return index;
        }
    }
    return -1;
}

static std::uint8_t GetNpcStockQuantity(W8NpcState* npc, std::int32_t item_id)
{
    const std::int32_t index = FindNpcStockItem(npc, item_id);
    return index >= 0 ? (*npc->items)[index]->quantity : 0;
}

static std::uint8_t GetNpcConfiguredStockQuantity(W8NpcState* npc, std::int32_t item_id)
{
    std::uint32_t count =
        (npc->record->item_stock_rules ? npc->record->item_stock_rules->size() : 0);
    for (std::uint32_t index = 0; index < count; ++index) {
        W8NpcItemStockRule* rule = (*npc->record->item_stock_rules)[index];
        if (rule->item_id == item_id) {
            return rule->quantity;
        }
    }
    return 0;
}

static char NpcStockChanceForTier(char tier)
{
    switch (tier) {
    case 0:
        return 0;
    case 1:
        return 25;
    case 2:
        return 50;
    case 3:
        return 75;
    case 4:
        return 100;
    default:
        return 0;
    }
}

static std::uint8_t JitterNpcStockQuantity(std::uint8_t amount)
{
    std::int32_t roll = Random(3);
    if (roll == 0) {
        std::uint8_t jitter = static_cast<std::uint8_t>(amount >> 1);
        amount += jitter;
    } else if (roll == 1) {
        std::uint8_t jitter = static_cast<std::uint8_t>(-(amount >> 1));
        amount += jitter;
    }
    return amount;
}

static void RemoveDepletedNpcStock(W8NpcState* npc)
{
    std::uint32_t count = (npc->items ? npc->items->size() : 0);
    for (std::uint32_t index = 0; index < count; ++index) {
        W8NpcItemEntry* entry = (*npc->items)[index];
        if (entry != 0 && entry->quantity == 0) {
            auto* entry = (*npc->items)[index];
            npc->items->erase(npc->items->begin() + index);
            delete entry;
            if (index != 0) {
                --index;
            }
            count = (npc->items ? npc->items->size() : 0);
        }
    }
}

// FUNCTION: WIZ8 0x0055a7b0
std::int32_t AddNpcItem(W8NpcState* npc, std::int32_t item_id, std::uint32_t quantity)
{
    W8ItemDatabaseRecord* record;
    W8NpcItemEntry* entry;
    std::uint32_t repeats;
    std::uint32_t added;
    std::int32_t index;

    if (npc == 0) {
        return -1;
    }
    if (npc->items == 0) {
        npc->items = new std::vector<W8NpcItemEntry*>;
    }
    record = &g_item_records[item_id];
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
            entry = CreateNpcItemEntry(item_id);
            index = static_cast<std::int32_t>(npc->items->size());
            npc->items->push_back(entry);
        } else {
            entry = (*npc->items)[index];
        }
        if (entry == 0) {
            return -1;
        }
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

// FUNCTION: WIZ8 0x0055afa0
bool MaintainNpcStock(W8NpcState* npc, bool force)
{
    W8NpcItemEntry* entry;
    W8NpcItemStockRule* rule;
    std::uint32_t count;
    std::uint32_t index;
    std::uint32_t rule_index;
    std::int32_t item_id;
    std::uint8_t configured;
    std::uint8_t held;

    count = (npc->items ? npc->items->size() : 0);
    for (index = 0; index < count; ++index) {
        entry = (*npc->items)[index];
        if (((entry != 0 && entry->quantity != 0) &&
             (NormalizeItemQuantityKind(&entry->item),
              g_item_records[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK)) &&
            entry->quantity > entry->item.stack_count) {
            entry->item.stack_count = entry->quantity;
            entry->quantity = 1;
        }
    }

    if (static_cast<std::uint32_t>(g_status.world_clock - npc->restock_clock) > 0xa8c0 || force) {
        npc->restock_clock = g_status.world_clock;
        count = (npc->record->item_stock_rules ? npc->record->item_stock_rules->size() : 0);
        for (rule_index = 0; rule_index < count; ++rule_index) {
            rule = (*npc->record->item_stock_rules)[rule_index];
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

    if (static_cast<std::uint32_t>(g_status.world_clock - npc->maintenance_clock) < 0x15180 &&
        !force) {
        return false;
    }
    DecayNpcInventory(npc);
    RestockNpcItems(npc);
    RemoveDepletedNpcStock(npc);
    npc->maintenance_clock = g_status.world_clock;
    npc->restock_clock = g_status.world_clock;
    return true;
}

// FUNCTION: WIZ8 0x0055a630
bool PopulateNpcStock(W8NpcState* npc)
{
    std::vector<W8NpcItemStockRule*>* rules;
    W8NpcItemStockRule* rule;
    std::uint32_t rule_count;
    std::uint32_t rule_index;
    std::uint32_t remaining;
    std::int32_t item_id;
    char persistent;
    char added;
    char chance;
    char tier;

    rules = npc->record->item_stock_rules;
    if (rules == 0) {
        npc->items = 0;
        return true;
    }
    rule_count = (rules ? rules->size() : 0);
    if (rule_count == 0) {
        npc->items = 0;
        return true;
    }
    if (npc->items == 0) {
        npc->items = new std::vector<W8NpcItemEntry*>;
    }
    rule_count = (npc->record->item_stock_rules ? npc->record->item_stock_rules->size() : 0);
    for (rule_index = 0; rule_index < rule_count; ++rule_index) {
        rule = (*npc->record->item_stock_rules)[rule_index];
        item_id = rule->item_id;
        if (item_id < static_cast<std::int32_t>(gXStatus.uiItemsInDatabase)) {
            persistent = rule->persistent;
            added = 0;
            if (rule->quantity != 0) {
                remaining = rule->quantity;
                do {
                    if (persistent == 0) {
                        tier = RateItemIdentifyDifficulty(npc, item_id);
                    } else {
                        tier = 4;
                    }
                    chance = NpcStockChanceForTier(tier);
                    if (Random(100) < static_cast<std::uint32_t>(chance)) {
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
    npc->maintenance_clock = g_status.world_clock;
    npc->restock_clock = g_status.world_clock;
    return true;
}

/* Keep equally ordered stock entries in their current order and preserve the
   addresses of the owned records while reordering their pointer slots. */
// FUNCTION: WIZ8 0x0055b9c0
void SortNpcItems(W8NpcState* npc)
{
    if (!npc->items) {
        return;
    }
    std::vector<W8NpcItemEntry*>& items = *npc->items;
    std::ranges::stable_sort(items, [](const W8NpcItemEntry* first, const W8NpcItemEntry* second) {
        return CompareItemsForPool(&first->item, &second->item) < 0;
    });
}

/* Release an NPC's owned item list, but only for the database records that ask
   for it. */
// FUNCTION: WIZ8 0x0055a5d0
void ClearNpcItems(W8NpcState* npc)
{
    std::vector<W8NpcItemEntry*>* items;

    if (npc->record->owns_stock != 0 && (items = npc->items) != 0) {
        for (auto* entry : *items) {
            delete entry;
        }
        items->clear();
        delete items;

        npc->items = 0;
    }
}

/* Add stock described by an item instance. Only the instance's item id is used;
   the entry receives a freshly built item rather than a copy of the argument.
   Equipment, equip_class four, always takes a new entry instead of merging. The
   quantity then lands in whichever of the two counts the item's quantity kind
   uses, and the other count is raised to one while it is still clear. */
// FUNCTION: WIZ8 0x0055a930
std::int32_t AddNpcItemFromInstance(W8NpcState* npc, const W8ItemInstance* item, char quantity)
{
    W8NpcItemEntry* entry;
    std::int32_t item_id;
    std::int32_t index;

    if (npc == 0) {
        return -1;
    }
    if (item->iItemNo < 0) {
        return -1;
    }
    if (npc->items == 0) {
        npc->items = new std::vector<W8NpcItemEntry*>;
    }
    item_id = item->iItemNo;
    index = -1;
    if (g_item_records[item_id].equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        index = FindNpcStockItem(npc, item_id);
    }
    if (index == -1) {
        item_id = item->iItemNo;
        entry = CreateNpcItemEntry(item_id);
        index = static_cast<std::int32_t>(npc->items->size());
        npc->items->push_back(entry);
    } else {
        entry = (*npc->items)[index];
    }
    if (entry == 0) {
        return -1;
    }
    if (g_item_records[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
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
// FUNCTION: WIZ8 0x0055ab80
std::int32_t RestockNpcItems(W8NpcState* npc)
{
    W8NpcItemStockRule* rule;
    std::uint32_t rule_count;
    std::uint32_t rule_index;
    std::int32_t item_id;
    std::int32_t configured;
    std::uint8_t held;
    std::uint8_t amount;
    char tier;
    char chance;

    rule_count = (npc->record->item_stock_rules ? npc->record->item_stock_rules->size() : 0);
    for (rule_index = 0; rule_index < rule_count; ++rule_index) {
        rule = (*npc->record->item_stock_rules)[rule_index];
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
            tier = 4;
        }
        chance = NpcStockChanceForTier(tier);
        if (static_cast<std::uint32_t>(chance) <= Random(100)) {
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
// FUNCTION: WIZ8 0x0055aad0
char RateItemIdentifyDifficulty(W8NpcState* npc, std::int32_t item_id)
{
    char difficulty;
    char level;
    char base;
    char raw_upper;
    char lower;
    char upper;

    difficulty = static_cast<char>(g_item_records[item_id].identify_difficulty);
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
        return 0;
    }
    if (difficulty < lower) {
        return static_cast<char>((difficulty + 4 >= lower) + 1);
    }
    return 3;
}

/* Add stock that only becomes ordinary trade stock once the world clock passes
   the given delay. This is what establishes the leading field as a clock stamp
   rather than a state enum. */
// FUNCTION: WIZ8 0x0055aa80
std::int32_t AddNpcItemWithDelay(W8NpcState* npc, std::int32_t item_id, std::uint32_t quantity, std::int32_t delay)
{
    std::int32_t index;
    W8NpcItemEntry* entry;

    if (npc == 0) {
        return -1;
    }
    index = AddNpcItem(npc, item_id, quantity);
    if (index == -1) {
        return -1;
    }
    entry = (*npc->items)[index];
    entry->available_at = g_status.world_clock + delay;
    return index;
}

// FUNCTION: WIZ8 0x0055ade0
W8NpcItemEntry* GetNpcItemAt(W8NpcState* npc, std::int32_t index)
{
    if (!npc->items || index < 0 || std::cmp_greater_equal(index, npc->items->size())) {
        return nullptr;
    }
    return (*npc->items)[index];
}

// FUNCTION: WIZ8 0x0055b710
std::uint32_t GetNpcItemCount(W8NpcState* npc) { return (npc->items ? npc->items->size() : 0); }

// GLOBAL: WIZ8 0x0062A80C
static char g_sound_cash_transaction[] = "Data\\Sound\\misc\\Cash Transaction.wav";

// FUNCTION: WIZ8 0x0055B730
bool SellItemToNpc(W8NpcState* npc, W8ItemInstance* item, std::uint8_t quantity,
                            bool suppress_payment)
{
    W8ItemInstance stack;
    std::int32_t amount;

    if ((g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) == 0) {
        if (NpcAcceptsTradeItemClass(npc, item)) {
            ReplaceOrCreateItem(&stack, item->iItemNo, false, item->identified, false);
            stack.stack_count = quantity;
            amount = CalculateTradeStackPrice(npc, &stack, W8_TRADE_PRICE_PARTY_SELLS);
            if (!suppress_payment) {
                AddPartyGold(amount, false);
            }
            wiz8::sound::play(g_sound_cash_transaction);
            if (item->stack_count == 0) {
                item->stack_count = 1;
            }
            AddNpcItemFromInstance(npc, item, quantity);
            return true;
        }
    }
    return false;
}

// FUNCTION: WIZ8 0x0055b290
bool NpcAcceptsTradeItemClass(W8NpcState* npc, W8ItemInstance* item)
{
    switch (g_item_records[item->iItemNo].equip_class) {
    case W8_ITEM_EQUIP_CLASS_SHORT_WEAPON:
        if ((npc->record->trade_item_class_mask & 0x1u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_EXTENDED_WEAPON:
        if ((npc->record->trade_item_class_mask & 0x2u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_THROWN_WEAPON:
        if ((npc->record->trade_item_class_mask & 0x4u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_RANGED_WEAPON:
        if ((npc->record->trade_item_class_mask & 0x8u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_AMMUNITION:
        if ((npc->record->trade_item_class_mask & 0x10u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_SHIELD:
        if ((npc->record->trade_item_class_mask & 0x20u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_TORSO:
        if ((npc->record->trade_item_class_mask & 0x40u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_LEGS:
        if ((npc->record->trade_item_class_mask & 0x80u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_HEAD:
        if ((npc->record->trade_item_class_mask & 0x100u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_HANDS:
        if ((npc->record->trade_item_class_mask & 0x200u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_FEET:
        if ((npc->record->trade_item_class_mask & 0x400u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_MISC:
        if ((npc->record->trade_item_class_mask & 0x800u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_CLOAK:
        if ((npc->record->trade_item_class_mask & 0x1000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_INSTRUMENT:
        if ((npc->record->trade_item_class_mask & 0x2000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_GADGET:
        if ((npc->record->trade_item_class_mask & 0x4000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_MISC_MAGIC:
        if ((npc->record->trade_item_class_mask & 0x8000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_POTION:
        if ((npc->record->trade_item_class_mask & 0x10000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_BOMB:
        if ((npc->record->trade_item_class_mask & 0x20000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_POWDER:
        if ((npc->record->trade_item_class_mask & 0x40000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_SPELLBOOK:
        if ((npc->record->trade_item_class_mask & 0x80000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_SCROLL:
        if ((npc->record->trade_item_class_mask & 0x100000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_FOOD:
        if ((npc->record->trade_item_class_mask & 0x200000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_DRINK:
        if ((npc->record->trade_item_class_mask & 0x400000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_KEY:
        if ((npc->record->trade_item_class_mask & 0x800000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_WRITING:
        if ((npc->record->trade_item_class_mask & 0x1000000u) != 0) {
            return true;
        }
        break;
    case W8_ITEM_EQUIP_CLASS_OTHER:
        if ((npc->record->trade_item_class_mask & 0x2000000u) != 0) {
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// FUNCTION: WIZ8 0x0055b250
bool NpcAcceptsTradeItem(W8NpcState* npc, W8ItemInstance* item)
{
    if ((g_item_records[item->iItemNo].flags & W8_ITEM_FLAG_NO_DISCARD) != 0) {
        return false;
    }
    return NpcAcceptsTradeItemClass(npc, item);
}

// FUNCTION: WIZ8 0x0055b5a0
std::int32_t CalculateNpcTradeStackPrice(W8NpcState* npc, std::int32_t item_id, W8TradePriceKind price_kind,
                                std::uint8_t stack_count, bool identified)
{
    W8ItemInstance item;
    ReplaceOrCreateItem(&item, item_id, false, identified, false);
    item.stack_count = stack_count;
    return CalculateTradeStackPrice(npc, &item, price_kind);
}

/* The bargained price of one offered stack: the party's best communication
   skill (0x16) builds a 0.2%-per-point adjustment, halved once it would move
   the price further than half the record's buy/sell spread, then applied to
   the low factor when the NPC buys (mode 0) or against the high factor when
   the party buys (mode 1). Stackable non-armour bundles price per unit times
   the count, everything else prices the whole stack value, unidentified
   merchandise goes at quarter price, and no stack settles below 1. */
// FUNCTION: WIZ8 0x0055b5e0
std::int32_t CalculateTradeStackPrice(W8NpcState* npc, W8ItemInstance* item, char price_kind)
{
    std::int32_t stack_value = GetItemStackValue(item);
    std::uint32_t skill = GetBestPartySkillLevel(W8_SKILL_COMMUNICATION, 0);
    W8NpcDatabaseRecord* record = npc->record;
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
    W8ItemDatabaseRecord* info = &g_item_records[item->iItemNo];
    std::int32_t amount;
    if (info->quantity_kind == W8_ITEM_QUANTITY_STACK && item->stack_count > 1 &&
        info->equip_class != W8_ITEM_EQUIP_CLASS_AMMUNITION) {
        amount = static_cast<std::int32_t>(price * info->value) * item->stack_count;
    } else {
        amount = static_cast<std::int32_t>(price * stack_value);
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
// FUNCTION: WIZ8 0x0055B7E0
bool CompleteNpcItemPurchase(W8NpcState* npc, std::int32_t index, std::uint8_t quantity, bool no_payment,
                             std::int32_t* remaining_out)
{
    std::uint8_t available;
    std::uint8_t moved;
    std::uint8_t unit;
    std::uint32_t price;
    W8ItemInstance hand;
    W8ItemInstance stack;
    W8NpcItemEntry* entry;

    moved = 0;
    entry = GetNpcItemAt(npc, index);
    if (entry == 0) {
        return false;
    }
    available = entry->item.stack_count;
    for (;;) {
        ReplaceOrCreateItem(&hand, entry->item.iItemNo, true, true, false);
        if (g_item_records[hand.iItemNo].quantity_kind == W8_ITEM_QUANTITY_NONE) {
            unit = 1;
        } else if (g_item_records[hand.iItemNo].quantity_kind != W8_ITEM_QUANTITY_STACK) {
            hand.uses_or_charges = entry->item.uses_or_charges;
            unit = 1;
        } else {
            unit = quantity - moved;
            if (g_item_records[hand.iItemNo].maximum_quantity < unit) {
                unit = g_item_records[hand.iItemNo].maximum_quantity;
            }
            hand.stack_count = unit;
            if (unit == 0) {
                unit = 1;
                hand.stack_count = unit;
            }
        }
        if (!AddItemToPartyOrDrop(&hand, false) && !g_status.item_in_cursor) {
            DisplayNpcQuote(g_localized_strings[0x6b1], false);
        }
        moved += unit;
        if (quantity <= moved) {
            if (moved == 0) {
                return false;
            }
            ReplaceOrCreateItem(&stack, entry->item.iItemNo, false, true, false);
            stack.stack_count = moved;
            price = CalculateTradeStackPrice(npc, &stack, W8_TRADE_PRICE_PARTY_BUYS);
            wiz8::sound::play(g_sound_cash_transaction);
            if (ConsumeNpcItemQuantity(npc, index, moved) == 0) {
                return false;
            }
            if (!no_payment) {
                SpendPartyGold(price);
            }
            if (remaining_out != 0) {
                *remaining_out = available - moved;
            }
            RemoveDepletedNpcStock(npc);
            return true;
        }
    }
}

// FUNCTION: WIZ8 0x0055ae00
bool ConsumeNpcItemQuantity(W8NpcState* npc, std::int32_t index, std::uint8_t quantity)
{
    W8NpcItemEntry* entry;

    if (quantity == 0) {
        return false;
    }
    entry = GetNpcItemAt(npc, index);
    if (entry == 0) {
        return false;
    }
    if (g_item_records[entry->item.iItemNo].quantity_kind == W8_ITEM_QUANTITY_STACK) {
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
// FUNCTION: WIZ8 0x0055ae70
void DecayNpcInventory(W8NpcState* npc)
{
    std::int32_t item_id;
    std::uint32_t item_count;
    W8NpcItemEntry* entry;
    std::uint32_t rule_count;
    W8NpcItemStockRule* rule;
    char quantity;
    std::uint32_t rule_index;
    std::uint32_t item_index;
    std::uint8_t remaining;

    item_count = (npc->items ? npc->items->size() : 0);
    item_index = 0;
    if (item_count > 0) {
        do {
            entry = (*npc->items)[item_index];
            if (((entry != 0 && entry->quantity != 0) &&
                 (item_id = entry->item.iItemNo,
                  (g_item_records[item_id].flags &
                   (W8_ITEM_FLAG_NO_DISCARD | W8_ITEM_FLAG_MERCHANT_CANNOT_SELL)) == 0))) {
                if (entry->available_at > 0) {
                    goto next_item;
                }
                rule_count =
                    (npc->record->item_stock_rules ? npc->record->item_stock_rules->size() : 0);
                for (rule_index = 0; rule_index < rule_count; ++rule_index) {
                    rule = static_cast<W8NpcItemStockRule*>(
                        (*npc->record->item_stock_rules)[rule_index]);
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
                        if (Random(100) < 100) {
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

// FUNCTION: WIZ8 0x0055BB10
void MatureNpcDelayedItems(W8NpcState* npc)
{
    if (npc->items == 0) {
        return;
    }
    std::uint32_t count = (npc->items ? npc->items->size() : 0);
    for (std::uint32_t index = 0; index < count; ++index) {
        W8NpcItemEntry* entry = (*npc->items)[index];
        if (entry->available_at == 0 ||
            entry->available_at >= static_cast<std::uint32_t>(g_status.world_clock)) {
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
// FUNCTION: WIZ8 0x0055BCC0
void RestockNpcInventory(W8NpcState* npc)
{
    MaintainNpcStock(npc, false);
    MatureNpcDelayedItems(npc);
}
