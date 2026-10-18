#pragma once
struct W8Chunk;

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "wiz8/filesystem.h"
#include "wiz8/layouts/encounter_tables.h"

// Runtime records own their storage; the database layout stays in layouts/.
struct W8EncounterEntry {
    unsigned short species;
    unsigned char rarity_class;
    unsigned char time_condition;
    unsigned char challenge_level;
    W8EncounterScriptName script;
};

struct W8EncounterTableRuntime {
    std::vector<W8EncounterEntry> entries;
    char name[256];
    unsigned int category;
    unsigned char version_two_flags;
};

unsigned int InitializeEncounterTables(void);
void UnloadEncounterTables(void);

extern std::vector<W8EncounterTableRuntime> g_encounter_tables;
extern std::vector<std::string> g_encounter_names;
extern int g_encounter_tables_level;

struct W8MonsterGroup;
struct MonGen;

/* MonGen.cpp's generator registry accessors. */
W8EncounterTableRuntime* GetEncounterTable(int index); /* 0x0048AD00 */
std::optional<int> FindEncounterTableByName(const char* name); /* 0x0048CCA0 */

void SaveEncounterState(wiz8::File& handle);
void SaveMonsterGenerators(wiz8::File& handle);
void DestroyMonsterGenerators(void);
struct W8World;
struct W8EncounterState {
    int saved_budget = 100;
    int random_budget = 20;
    int culling_seconds = 180;
    unsigned char save_flag = 0;
    short interval_min = 100, default_interval = 10, interval_max = -1;
    std::vector<std::unique_ptr<MonGen>> generators;
};
W8EncounterState ReadEncounterState(W8Chunk& file);
void ReadMonsterGeneratorStates(W8Chunk& file, W8EncounterState& state);
void StartLevelEncounters(W8World& world);
void RunMonsterGenerators(void);
void DespawnAllActiveMonsterGroups(void);
void ResetMonsterGeneratorTimers(void);
void SetMonsterGeneratorDurationScale(float scale); /* 0x0048CB80 */

void SpendRandomEncounterBudget();

void UpdateRandomEncounterBudget(bool reset_budget);

extern const float g_generator_jitter_fraction;
extern float g_encounter_culling_scale_fast;
extern const float g_encounter_culling_rate;
extern const double g_encounter_culling_distance;
