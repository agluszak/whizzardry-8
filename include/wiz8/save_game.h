#pragma once
struct W8Chunk;
#include <memory>
#include <vector>

#include "wiz8/filesystem.h"
#include "wiz8/item_spawning.h"

// Flat retail item sequences; I/O and malformed records throw.
void SaveItemFile(wiz8::File& handle, const W8WorldItem& item);
std::unique_ptr<W8WorldItem> LoadItem(W8Chunk& handle, bool from_assets = false);
bool LoadSavedLevelItems(int level, std::vector<std::unique_ptr<W8WorldItem>>& items);
