#include <memory>
#include <vector>
#pragma once

#include "wiz8/filesystem.h"
#include "wiz8/item_spawning.h"

bool SaveItemFile(wiz8::File& handle, W8WorldItem* item);
std::unique_ptr<W8WorldItem> LoadItem(wiz8::File& handle);
bool LoadSavedLevelItems(int level, std::vector<std::unique_ptr<W8WorldItem>>& items);
