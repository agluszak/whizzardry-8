#pragma once
struct W8Chunk;
#include <memory>
#include <vector>

#include "wiz8/filesystem.h"
#include "wiz8/item_spawning.h"
#include "wiz8/retail_save_records.h"

/* A world item as a save stores it: the container's retail record, then one per content,
   each marking whether another follows. I/O and malformed records throw. */
using SavedWorldItem = std::vector<wiz8::retail::WorldItem>;
// The records of a live item, with an active item's position and flags from its entity.
SavedWorldItem CaptureWorldItem(const W8WorldItem& item);
// Read a flat sequence; the level's own status clears the radar-seen flag.
SavedWorldItem ReadSavedWorldItem(W8Chunk& handle, bool from_assets = false);
void WriteSavedWorldItem(wiz8::File& handle, const SavedWorldItem& item);
// The container and its owned contents, inactive and placed in no sector.
std::unique_ptr<W8WorldItem> BuildWorldItem(const SavedWorldItem& item);
bool LoadSavedLevelItems(int level, std::vector<std::unique_ptr<W8WorldItem>>& items);
