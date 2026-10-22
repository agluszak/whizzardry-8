#ifndef WIZ8_ITEM_TABLES_H
#define WIZ8_ITEM_TABLES_H

#include <algorithm>
#include <memory>
#include <vector>

#include <glm/ext/vector_float3.hpp>

#include "wiz8/data/item_records.h"

struct W8WorldItem;

int FindItemTableByName(const char* name);
int GenerateItemsFromTable(std::vector<std::unique_ptr<W8WorldItem>>& output_items,
                           unsigned int table_id, unsigned int maximum_items,
                           const glm::vec3& position);

#endif
