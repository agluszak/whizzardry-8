#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "wiz8/data/game_data.h"

/* Read the retail game data; a malformed or missing file throws. The string table
   comes from `string_data_path`, relative to the asset root. */
GameData LoadGameData(std::string_view string_data_path);

// The individual readers, for tests of their format checks.
std::vector<std::string> ReadStringTable(std::string_view path);
std::vector<W8ItemDatabaseRecord> ReadItemDatabase();
struct W8ItemTables {
    std::vector<std::string> categories;
    std::vector<W8ItemTableRecord> tables;
};
W8ItemTables ReadItemTables();
std::vector<W8NpcDatabaseRecord> ReadNpcDatabase();
std::vector<W8SpellRuntimeRecord> ReadSpellDatabase();
std::vector<W8MonsterRecord> ReadMonsterDatabase();
std::vector<W8EncounterTable> ReadEncounterTables();
// Both keyword files, or no keywords when either is missing or malformed.
W8DialogueKeywords ReadDialogueKeywords();

/* The game data the Application owns. Transitional: code that does not receive the data
   yet reads it here; `rules` and `game` may not, and this goes with its last caller.
   While nothing is installed it is empty. */
const GameData& Data();
/* Make `data`, which the caller owns and keeps alive while it is installed, what `Data()`
   reads, and return what was installed before; null uninstalls. */
const GameData* InstallGameData(const GameData* data);
