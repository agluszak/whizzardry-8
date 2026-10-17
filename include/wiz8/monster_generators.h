#pragma once
struct W8Chunk;

#include <algorithm>
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
int FindEncounterTableByName(const char* name);        /* 0x0048CCA0 */

void SaveEncounterState(wiz8::File& handle);
void SaveMonsterGenerators(wiz8::File& handle);
void DestroyMonsterGenerators(void);
void LoadMonsterGenerators(W8Chunk& handle); /* 0x0048C470 */
void RunMonsterGenerators(void);
void DespawnAllActiveMonsterGroups(void);
void ResetMonsterGeneratorTimers(void);
void SetMonsterGeneratorDurationScale(float scale); /* 0x0048CB80 */

/* The encounter-budget registry used by random groups. */
void UnregisterActiveEncounterGroup(W8MonsterGroup* group); /* 0x0048C670 */
void RegisterActiveEncounterGroup(W8MonsterGroup* group);   /* 0x0048C750 */

void UpdateRandomEncounterBudget(bool reset_budget);

extern int g_random_encounter_budget;
extern int g_random_encounter_limit;
extern std::vector<W8MonsterGroup*> g_active_groups;
extern unsigned char g_generator_save_flag;
extern short g_generator_default_interval;
extern short g_generator_interval_min;
extern short g_generator_interval_max;
extern int g_encounter_culling_time_seconds;
extern const float g_generator_jitter_fraction;
extern float g_encounter_culling_scale_fast;
extern const float g_encounter_culling_rate;
extern const double g_encounter_culling_distance;
