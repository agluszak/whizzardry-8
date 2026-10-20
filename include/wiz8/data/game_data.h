#pragma once

#include <string>
#include <vector>

#include "wiz8/data/item_records.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/data/records.h"

/* The retail game's content, read once at startup and never changed: the string table
   and the item, item-table, spell, NPC, fact, level and missile databases. */
struct GameData {
    std::vector<std::string> strings;
    std::vector<W8ItemDatabaseRecord> items;
    std::vector<std::string> item_table_categories;
    std::vector<W8ItemTableRecord> item_tables;
    std::vector<W8SpellRuntimeRecord> spells;
    std::vector<W8NpcDatabaseRecord> npcs;
    std::vector<W8FactDatabaseRecord> facts;
    std::vector<W8LevelDatabaseRecord> levels;
    std::vector<W8MissileTableRecord> missiles;
};
