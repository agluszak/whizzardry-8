#include <cstdint>
#include <vector>
#pragma once

#include "wiz8/filesystem.h"

#include "wiz8/item_spawning.h"

bool SaveItemFile(wiz8::File& handle, W8WorldItem* item);
W8WorldItem* LoadItem(wiz8::File& handle, bool add_to_list);
bool LoadSavedLevelItems(std::int32_t level, std::vector<W8WorldItem*>& items);
