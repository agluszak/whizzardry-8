#pragma once

#include <cstdint>
#include "wiz8/filesystem.h"

#include "wiz8/layouts/encounter_tables.h"
#include <vector>
#include <string>
#include <algorithm>

// Runtime records own their storage; the database layout stays in layouts/.
struct W8EncounterEntry {
    std::uint16_t species;
    std::uint8_t rarity_class;
    std::uint8_t time_condition;
    std::uint8_t challenge_level;
    W8EncounterScriptName script;
};

struct W8EncounterTableRuntime {
    std::vector<W8EncounterEntry> entries;
    char name[256];
    std::uint32_t category;
    std::uint8_t version_two_flags;
};

std::uint32_t InitializeEncounterTables(void);
void UnloadEncounterTables(void);

extern std::vector<W8EncounterTableRuntime> g_encounter_tables;
extern std::vector<std::string> g_encounter_names;
extern std::int32_t g_encounter_tables_level;

struct W8MonsterGroup;
struct MonGen;

/* MonGen.cpp's generator registry accessors. */
std::int32_t GetMonsterGeneratorCount(void);                    /* 0x0048BD80 */
MonGen* GetMonsterGenerator(std::int32_t index);                /* 0x0048BD90 */
void AddMonsterGenerator(MonGen* generator);           /* 0x0048BE30 */
void RemoveMonsterGenerator(MonGen* generator);        /* 0x0048BEB0 */
W8EncounterTableRuntime* GetEncounterTable(std::int32_t index); /* 0x0048AD00 */
std::int32_t FindEncounterTableByName(const char* name);        /* 0x0048CCA0 */

void SaveEncounterState(wiz8::File& handle);
void SaveMonsterGenerators(wiz8::File& handle);
void DestroyMonsterGenerators(void);
void LoadMonsterGenerators(wiz8::File& handle); /* 0x0048C470 */
void RunMonsterGenerators(void);
void DespawnAllActiveMonsterGroups(void);
void ResetMonsterGeneratorTimers(void);
void SetMonsterGeneratorDurationScale(float scale); /* 0x0048CB80 */

/* The encounter-budget registry used by random groups. */
void UnregisterActiveEncounterGroup(W8MonsterGroup* group); /* 0x0048C670 */
void RegisterActiveEncounterGroup(W8MonsterGroup* group);   /* 0x0048C750 */

void UpdateRandomEncounterBudget(bool reset_budget);

extern std::int32_t g_random_encounter_budget;
extern std::int32_t g_random_encounter_limit;
extern std::vector<W8MonsterGroup*> g_active_groups;
extern std::uint8_t g_generator_save_flag;
extern std::int16_t g_generator_default_interval;
extern std::int16_t g_generator_interval_min;
extern std::int16_t g_generator_interval_max;
extern std::int32_t g_encounter_culling_time_seconds;
extern const float g_generator_jitter_fraction;
extern float g_encounter_culling_scale_fast;
extern const float g_encounter_culling_rate;
extern const double g_encounter_culling_distance;
