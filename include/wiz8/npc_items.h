#ifndef WIZ8_NPC_ITEMS_H
#define WIZ8_NPC_ITEMS_H

#include <cstdint>
#include "wiz8/npc_trade_price.h"

struct W8ItemInstance;
struct W8NpcItemEntry;
struct W8NpcState;

/* NPC stock and trade interface. The implementing source, src/wiz8/
   npc_items.cpp, sits in an unresolved-fragment gap, so original-TU ownership
   of these declarations is provisional until the fragment is attributed. */

std::int32_t AddNpcItem(W8NpcState* npc, std::int32_t item_id, std::uint32_t quantity);
std::int32_t AddNpcItemFromInstance(W8NpcState* npc, const W8ItemInstance* item, char quantity);
std::int32_t AddNpcItemWithDelay(W8NpcState* npc, std::int32_t item_id, std::uint32_t quantity, std::int32_t delay);
char RateItemIdentifyDifficulty(W8NpcState* npc, std::int32_t item_id);
std::int32_t RestockNpcItems(W8NpcState* npc);
void ClearNpcItems(W8NpcState* npc);
void SortNpcItems(W8NpcState* npc);
bool PopulateNpcStock(W8NpcState* npc);
bool MaintainNpcStock(W8NpcState* npc, bool force);
void MatureNpcDelayedItems(W8NpcState* npc);
void RestockNpcInventory(W8NpcState* npc); /* 0x0055BCC0 */
W8NpcItemEntry* GetNpcItemAt(W8NpcState* npc, std::int32_t index);
std::uint32_t GetNpcItemCount(W8NpcState* npc);
bool NpcAcceptsTradeItem(W8NpcState* npc, W8ItemInstance* item);
bool NpcAcceptsTradeItemClass(W8NpcState* npc, W8ItemInstance* item); /* 0x0055B290 */
std::int32_t CalculateNpcTradeStackPrice(W8NpcState* npc, std::int32_t item_id, W8TradePriceKind price_kind,
                                std::uint8_t stack_count, bool identified);
bool ConsumeNpcItemQuantity(W8NpcState* npc, std::int32_t index, std::uint8_t quantity);
/* 0x0055B730: move a sold/given party item into NPC stock, paying party gold
   unless suppressed (the item being a dialogue topic the NPC wants). */
bool SellItemToNpc(W8NpcState* npc, W8ItemInstance* item, std::uint8_t quantity,
                            bool suppress_payment);
/* 0x0055B7E0: the stock mutation a completed trade applies for one slot. */
void DecayNpcInventory(W8NpcState* npc);
bool CompleteNpcItemPurchase(W8NpcState* npc, std::int32_t index, std::uint8_t quantity, bool no_payment,
                             std::int32_t* remaining_out);

#endif
