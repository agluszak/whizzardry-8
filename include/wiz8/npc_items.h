#ifndef WIZ8_NPC_ITEMS_H
#define WIZ8_NPC_ITEMS_H

#include <cstddef>

#include "wiz8/npc_trade_price.h"
#include "wiz8/rules/item_instance.h"

enum class W8NpcStockChanceTier : signed char {
    never = 0,
    quarter = 1,
    half = 2,
    three_quarters = 3,
    always = 4,
};

class Game;
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
W8ItemInstance* ResolveTradeSelection(Game& game, W8NpcState* npc,
                                      const W8TradeSelection& selection);
enum class W8TradeOutcome : unsigned char {
    Complete,
    InvalidSelection,
    Refused,
    WantedByOwner,
    InsufficientGold,
    Offer,
    TheftFailed,
    TheftCaught
};
enum class W8TradeAction : unsigned char { Exchange, Steal, Offer };
struct W8TradeResult {
    W8TradeOutcome outcome;
    bool accepted_topic = false;
    bool dropped_overflow = false;
    bool theft_succeeded = false;
};
W8TradeResult CompleteNpcTrade(Game& game, W8NpcState* npc, int actor,
                               const W8TradeSelection& selection, int quantity,
                               W8TradeAction action);

int AddNpcItem(W8NpcState* npc, int item_id, unsigned int quantity);
int AddNpcItemFromInstance(W8NpcState* npc, const W8ItemInstance* item, signed char quantity);
int AddNpcItemWithDelay(W8NpcState* npc, int item_id, unsigned int quantity, int delay);
W8NpcStockChanceTier RateItemIdentifyDifficulty(W8NpcState* npc, int item_id);
int RestockNpcItems(W8NpcState* npc);
void SortNpcItems(W8NpcState* npc);
bool PopulateNpcStock(W8NpcState* npc);
bool MaintainNpcStock(W8NpcState* npc, bool force);
void MatureNpcDelayedItems(W8NpcState* npc);
void RestockNpcInventory(W8NpcState* npc); /* 0x0055BCC0 */
W8NpcItemEntry* GetNpcItemAt(W8NpcState* npc, int index);
std::size_t GetNpcItemCount(W8NpcState* npc);
bool NpcAcceptsTradeItem(W8NpcState* npc, W8ItemInstance* item);
bool NpcAcceptsTradeItemClass(const W8NpcState* npc, const W8ItemInstance* item); /* 0x0055B290 */
int CalculateNpcTradeStackPrice(W8NpcState* npc, int item_id, W8TradePriceKind price_kind,
                                unsigned char stack_count, bool identified);
bool ConsumeNpcItemQuantity(W8NpcState* npc, int index, unsigned char quantity);
void DecayNpcInventory(W8NpcState* npc);

#endif
