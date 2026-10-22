#pragma once
struct W8Chunk;

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "wiz8/filesystem.h"

struct W8EncounterTable;
struct W8MonsterGroup;
struct MonGen;

/* The loaded encounter tables, by index and by name prefix. */
const W8EncounterTable* GetEncounterTable(int index); /* 0x0048AD00 */
int FindEncounterTableByName(const char* name);       /* 0x0048CCA0 */

void DestroyMonsterGenerators(void);
struct World;
struct W8EncounterState {
    int saved_budget = 100;
    int random_budget = 20;
    int culling_seconds = 180;
    unsigned char save_flag = 0;
    short interval_min = 100, default_interval = 10, interval_max = -1;
    std::vector<std::unique_ptr<MonGen>> generators;
};
W8EncounterState ReadEncounterState(W8Chunk& file);
/* The saved switches and timers of a level's generators (MGNS), matched to the generators
   by name. A timer is its progress through the interval and its scale. */
struct W8MonsterGeneratorStates {
    struct Entry {
        std::array<char, 32> name{};
        bool enabled = true;
        unsigned int flags = 0;
        std::optional<std::pair<float, float>> timer;
    };
    int version = 3;
    unsigned char save_flag = 0;
    std::vector<Entry> entries;
};
W8MonsterGeneratorStates CaptureMonsterGeneratorStates(const W8EncounterState& state);
W8MonsterGeneratorStates ReadMonsterGeneratorStates(W8Chunk& file);
void WriteMonsterGeneratorStates(wiz8::File& handle, const W8MonsterGeneratorStates& states);
void ApplyMonsterGeneratorStates(W8EncounterState& state, const W8MonsterGeneratorStates& states);
/* Rearm decoded generators in the order they were read, drawing each one's jittered
   interval from the game's stream as retail did while it read the level. */
void RearmDecodedGenerators(W8EncounterState& state);
void StartLevelEncounters(World& world);
// Roll the generators whose intervals passed in the step's `elapsed` time.
void RunMonsterGenerators(std::chrono::milliseconds elapsed);
void DespawnAllActiveMonsterGroups(void);
void ResetMonsterGeneratorTimers(void);
void SetMonsterGeneratorDurationScale(float scale); /* 0x0048CB80 */

void SpendRandomEncounterBudget();

void UpdateRandomEncounterBudget(bool reset_budget);

extern const float g_generator_jitter_fraction;
extern float g_encounter_culling_scale_fast;
extern const float g_encounter_culling_rate;
extern const double g_encounter_culling_distance;
