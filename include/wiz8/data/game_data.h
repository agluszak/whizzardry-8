#pragma once

#include <string>
#include <vector>

#include "wiz8/data/encounter_records.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/missile_records.h"
#include "wiz8/data/records.h"

/* The dialogue keyword lists: one row per line and one word per '/'-separated field.
   A keyword typed in the translated language is replaced by the English word at the
   same row and field. */
struct W8DialogueKeywords {
    std::vector<std::vector<std::string>> english;
    std::vector<std::vector<std::string>> translated;
};

/* The retail game's content, read once at startup and never changed: the string table,
   the item, item-table, spell, NPC, fact, level, monster and missile databases, the
   encounter tables and the dialogue keyword lists. */
struct GameData {
    std::vector<std::string> strings;
    std::vector<W8ItemDatabaseRecord> items;
    std::vector<std::string> item_table_categories;
    std::vector<W8ItemTableRecord> item_tables;
    std::vector<W8SpellRuntimeRecord> spells;
    std::vector<W8NpcDatabaseRecord> npcs;
    std::vector<W8FactDatabaseRecord> facts;
    std::vector<W8LevelDatabaseRecord> levels;
    std::vector<W8MonsterRecord> monsters;
    std::vector<W8MissileTableRecord> missiles;
    std::vector<W8EncounterTable> encounter_tables;
    W8DialogueKeywords keywords;
};
