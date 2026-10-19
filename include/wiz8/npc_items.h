#ifndef WIZ8_NPC_ITEMS_H
#define WIZ8_NPC_ITEMS_H

#include <cstddef>

#include "wiz8/layouts/item_instance.h"

enum class W8NpcStockChanceTier : signed char {
    never = 0,
    quarter = 1,
    half = 2,
    three_quarters = 3,
    always = 4,
};
#include "wiz8/npc_trade_price.h"

struct W8ItemInstance;
struct W8NpcItemEntry;
struct W8NpcState;

enum class W8TradeSource : unsigned char { Backpack, PartyPool, NpcStock, Gold };
// A selection names a source location with expected contents. Replacing or
// compacting its contents invalidates the choice instead of redirecting it.
// Identical item stacks are fungible; no persistent item identity is invented.
struct W8TradeSelection {
    W8TradeSource source;
    int character = -1;
    int slot = -1;
    W8ItemInstance expected{};
    unsigned char stock_quantity = 0;
};
W8ItemInstance* ResolveTradeSelection(W8NpcState* npc, const W8TradeSelection& selection);
enum class W8TradeOutcome : unsigned char {
    Complete,
    InvalidSelection,
    Refused,
    WantedByOwner,
    InsufficientGold
};
struct W8TradeResult {
    W8TradeOutcome outcome;
    bool accepted_topic = false;
    bool dropped_overflow = false;
};
W8TradeResult CompleteNpcTrade(W8NpcState* npc, const W8TradeSelection& selection,
                               unsigned char quantity);

/* NPC stock and trade interface. The implementing source, src/wiz8/
   npc_items.cpp, sits in an unresolved-fragment gap, so original-TU ownership
   of these declarations is provisional until the fragment is attributed. */

int AddNpcItem(W8NpcState* npc, int item_id, unsigned int quantity);
int AddNpcItemFromInstance(W8NpcState* npc, const W8ItemInstance* item, char quantity);
int AddNpcItemWithDelay(W8NpcState* npc, int item_id, unsigned int quantity, int delay);
W8NpcStockChanceTier RateItemIdentifyDifficulty(W8NpcState* npc, int item_id);
int RestockNpcItems(W8NpcState* npc);
void SortNpcItems(W8NpcState* npc);
unsigned char PopulateNpcStock(W8NpcState* npc);
unsigned char MaintainNpcStock(W8NpcState* npc, bool force);
void MatureNpcDelayedItems(W8NpcState* npc);
void RestockNpcInventory(W8NpcState* npc); /* 0x0055BCC0 */
W8NpcItemEntry* GetNpcItemAt(W8NpcState* npc, int index);
std::size_t GetNpcItemCount(W8NpcState* npc);
bool NpcAcceptsTradeItem(W8NpcState* npc, W8ItemInstance* item);
bool NpcAcceptsTradeItemClass(const W8NpcState* npc, const W8ItemInstance* item); /* 0x0055B290 */
int CalculateNpcTradeStackPrice(W8NpcState* npc, int item_id, W8TradePriceKind price_kind,
                                unsigned char stack_count, bool identified);
unsigned char ConsumeNpcItemQuantity(W8NpcState* npc, int index, unsigned char quantity);
/* 0x0055B730: move a sold/given party item into NPC stock, paying party gold
   unless suppressed (the item being a dialogue topic the NPC wants). */
/* 0x0055B7E0: the stock mutation a completed trade applies for one slot. */
void DecayNpcInventory(W8NpcState* npc);
W8TradeResult CompleteNpcItemPurchase(W8NpcState* npc, int index, unsigned char quantity,
                                      bool no_payment);

#endif
