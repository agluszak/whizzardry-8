#ifndef WIZ8_ITEM_TABLES_H
#define WIZ8_ITEM_TABLES_H

#include <algorithm>
#include <vector>

#include "wiz8/layouts/item_tables.h"

struct W8WorldItem;

int FindItemTableByName(const char* name);
int GenerateItemsFromTable(std::vector<W8WorldItem*>& output_items, unsigned int table_id,
                           unsigned int maximum_items); /* 0x004F88F0 */

#endif
