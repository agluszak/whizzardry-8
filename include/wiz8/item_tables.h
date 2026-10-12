#ifndef WIZ8_ITEM_TABLES_H
#define WIZ8_ITEM_TABLES_H

#include <cstdint>
#include "wiz8/layouts/item_tables.h"
#include <vector>
#include <algorithm>

struct W8WorldItem;

std::int32_t FindItemTableByName(const char* name);
std::int32_t GenerateItemsFromTable(std::vector<W8WorldItem*>& output_items, std::uint32_t table_id,
                           std::uint32_t maximum_items); /* 0x004F88F0 */

#endif
