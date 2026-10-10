#ifndef WIZ8_ITEM_TABLES_H
#define WIZ8_ITEM_TABLES_H

#include "wiz8/layouts/item_tables.h"
#include <vector>
#include <algorithm>

struct W8WorldItem;

int FindItemTableByName(const char* name);
int GenerateItemsFromTable(std::vector<W8WorldItem*>* output_items, unsigned int table_id,
                           unsigned int maximum_items); /* 0x004F88F0 */

#endif
