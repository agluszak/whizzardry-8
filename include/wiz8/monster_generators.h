#pragma once
struct W8Chunk;

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

#include "wiz8/filesystem.h"
#include "wiz8/local_code/MonsterGenerator.h"

struct W8EncounterTable;
struct W8MonsterGroup;
struct MonGen;

/* The loaded encounter tables, by index and by name prefix. */
const W8EncounterTable* GetEncounterTable(int index); /* 0x0048AD00 */
int FindEncounterTableByName(const char* name);       /* 0x0048CCA0 */

void SaveEncounterState(wiz8::File& handle);
void SaveMonsterGenerators(wiz8::File& handle);
void DestroyMonsterGenerators(void);
struct World;
struct EncounterSettings {
    int saved_budget = 100;
    int random_budget = 20;
    int culling_seconds = 180;
    unsigned char save_flag = 0;
    short interval_min = 100, default_interval = 10, interval_max = -1;
};
struct W8EncounterState : EncounterSettings {
    std::vector<std::unique_ptr<MonGen>> generators;
};
struct SavedMonsterGenerator : MonsterGeneratorDefinition {
    std::optional<float> timer_scale;
};
struct SavedEncounterState : EncounterSettings {
    std::vector<SavedMonsterGenerator> generators;
};
struct SavedGeneratorState {
    std::array<char, 32> name;
    bool enabled;
    unsigned int flags;
    std::optional<GeneratorInterval> timer;
};
struct SavedGeneratorStates {
    unsigned char save_flag;
    std::vector<SavedGeneratorState> generators;
};
using SavedEncounterSection = std::variant<SavedEncounterState, SavedGeneratorStates>;
SavedEncounterState ReadEncounterState(W8Chunk& file);
SavedGeneratorStates ReadMonsterGeneratorStates(W8Chunk& file);
void ApplyEncounterSections(World& world, const std::vector<SavedEncounterSection>& sections);
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
