#pragma once

#include <string>
#include <vector>

/* When an encounter entry may be rolled. */
enum W8EncounterTimeCondition {
    W8_ENCOUNTER_DAY = 0,
    W8_ENCOUNTER_NIGHT = 1,
    W8_ENCOUNTER_ANY_TIME = 2
};

/* One monster an encounter table can roll. */
struct W8EncounterEntry {
    unsigned short species;
    unsigned char rarity_class;
    unsigned char time_condition;
    unsigned char challenge_level;
    std::string script;
};

/* One EncounterTables.dbs table, named for the generators that select it. */
struct W8EncounterTable {
    std::string name;
    std::vector<W8EncounterEntry> entries;
};
