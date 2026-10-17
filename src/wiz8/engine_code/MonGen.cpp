#include "wiz8/engine_code/MonGen.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <fmt/format.h>
#include <glm/geometric.hpp>

#include "IntervalGate.h"
#include "Monster.h"
#include "Octree.h"
#include "asset_paths.h"
#include "dice.h"
#include "difficulty.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/encounter_tables.h"
#include "layouts/gameplay_databases.h"
#include "layouts/levels.h"
#include "layouts/screen_state.h"
#include "layouts/world.h"
#include "local_code/Factions.h"
#include "random.h"
#include "surrender/srDebug.h"
#include "wiz8/chunk.h"
#include "wiz8/containers.h"
#include "wiz8/engine_code/GameData.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/fonts.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Gameloop.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/monster_generators.h"
#include "wiz8/ui_input.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

/* These are recovered in their owning Local Code units. Retail MonGen calls
   them out of line, so keep the cross-TU seams rather than cloning their logic. */
unsigned char LinkMonsterGroupToLeader(W8MonsterGroup* leader,
                                       W8MonsterGroup* monster_group); /* 0x0050FC20 */

// FUNCTION: WIZ8 0x0048A680
MonGen::MonGen()
{
    flags = 0;
    custom_spawn_chance = 100;
    custom_interval_seconds = 100;
    unknown_08 = 0xffff;
    encounter_table_index = -1;
    timer.m_flags.one_shot = false;
    generation_enabled = 1;
    memset(name, 0, sizeof(name));
}

// FUNCTION: WIZ8 0x0048bdc0
MonGen* FindMonGenByName(const char* name)
{
    for (const auto& generator : g_world->monster_generators) {
        if (strncmp(name, generator->name, sizeof(generator->name)) == 0) {
            return generator.get();
        }
    }
    return 0;
}

// GLOBAL: WIZ8 0x0060a6c4
int g_random_encounter_budget = 20;

// GLOBAL: WIZ8 0x0060a6c0
int g_random_encounter_limit = 10;

// GLOBAL: WIZ8 0x0065ba10
std::vector<W8MonsterGroup*> g_active_groups;

// GLOBAL: WIZ8 0x0065ba48
unsigned char g_generator_save_flag;

// GLOBAL: WIZ8 0x0060a6b6
short g_generator_default_interval = 10;

// GLOBAL: WIZ8 0x0060a6b4
short g_generator_interval_min = 100;

// GLOBAL: WIZ8 0x0060a6b8
short g_generator_interval_max = -1;

// GLOBAL: WIZ8 0x0060a6c8
int g_encounter_culling_time_seconds = 180;

// GLOBAL: WIZ8 0x005ec040
const float g_generator_jitter_fraction = 0.20000000298023224f;

// GLOBAL: WIZ8 0x0065ba20
std::vector<W8EncounterTableRuntime> g_encounter_tables;
// GLOBAL: WIZ8 0x0065ba38
std::vector<std::string> g_encounter_names;
// GLOBAL: WIZ8 0x0060a6bc
int g_encounter_tables_level = -1;

// FUNCTION: WIZ8 0x0048a7a0
unsigned int InitializeEncounterTables(void)
try {
    auto handle = wiz8::open_file(wiz8::asset_path("Data\\Databases\\EncounterTables.dbs"));
    UnloadEncounterTables();
    std::vector<std::string> names;
    std::vector<W8EncounterTableRuntime> tables;
    int name_count;
    int table_count;
    handle->read_exact(&name_count, sizeof(name_count));
    if (name_count < 0) return 0;
    for (int index = 0; index < name_count; ++index) {
        char name[256];
        handle->read_exact(name, sizeof(name));
        names.emplace_back(name, std::find(std::begin(name), std::end(name), '\0'));
    }
    handle->read_exact(&table_count, sizeof(table_count));
    if (table_count < 0) return 0;
    for (int index = 0; index < table_count; ++index) {
        W8EncounterTableDiskHeader header;
        handle->read_exact(&header, sizeof(header));
        auto& table = tables.emplace_back();
        memcpy(table.name, header.name, sizeof(table.name));
        table.name[sizeof(table.name) - 1] = '\0';
        table.category = header.unknown_101;
        table.entries.resize(header.entry_count);
        // The file stores columns; combine them into records at the boundary.
        for (auto& entry : table.entries) handle->read_exact(&entry.species, sizeof(entry.species));
        for (auto& entry : table.entries) handle->read_exact(&entry.rarity_class, 1);
        for (auto& entry : table.entries) handle->read_exact(&entry.time_condition, 1);
        for (auto& entry : table.entries) handle->read_exact(&entry.challenge_level, 1);
        for (auto& entry : table.entries) {
            handle->read_exact(&entry.script, sizeof(entry.script));
            entry.script.value[sizeof(entry.script.value) - 1] = '\0';
        }
        if (header.version != 1) handle->read_exact(&table.version_two_flags, 1);
    }
    g_encounter_names = std::move(names);
    g_encounter_tables = std::move(tables);
    g_encounter_tables_level = g_status.current_level;
    return 1;
} catch (const std::exception&) {
    return 0;
}

// STRING: WIZ8 0x0060A6E0
#define MON_GEN_CPP "C:\\Projects\\Wizardry 8\\Engine Code\\MonGen.cpp"

/* Flag bits on a generator: bit 0 suppresses spawning, bit 2 is armed, bit 3
   selects the shared default chance and interval, and bit 5 adds the
   story/faction gate. */
enum {
    W8_MONGEN_DISABLED = 1,
    W8_MONGEN_ARMED = 4,
    W8_MONGEN_USE_DEFAULT_SETTINGS = 8,
    W8_MONGEN_STORY_GATED = 0x20
};

/* Ten hours of game time. Past that the elapsed span is not distributed over
   the live groups at all - a fresh roll replaces them instead. */
enum { W8_ENCOUNTER_STALE_SECONDS = 36000 };

/* Pick the encounter entries compatible with this moment. The rarity draw uses
   the four stored rarity classes (3/7/20/70). The first pass also requires the
   entry's challenge to stay within half the party's rounded average level and
   scales rarity down as that mismatch grows; when that leaves no candidate,
   retail falls back to raw rarity plus the day/night condition. A random
   candidate is returned to GenerateEncounter. */
// FUNCTION: WIZ8 0x0048B9A0
const W8EncounterEntry* MonGen::SelectEncounterCandidate(const W8EncounterTableRuntime& table)
{
    int rarity_roll;
    int rarity_class;
    int night;
    float party_level;

    std::vector<const W8EncounterEntry*> candidates;
    night = !(g_status.game_time_ms > 18000000 && g_status.game_time_ms <= 79200000);

    rarity_roll = Random(100);
    if (rarity_roll <= 3) {
        rarity_class = 3;
    } else if (rarity_roll <= 10) {
        rarity_class = 7;
    } else if (rarity_roll <= 30) {
        rarity_class = 20;
    } else {
        rarity_class = 70;
    }

    party_level = static_cast<float>(floor(GetAveragePartyMemberLevel() + 0.49f));
    for (const auto& entry : table.entries) {
        float challenge = static_cast<float>(entry.challenge_level);
        float difference = static_cast<float>(fabs(1.0f - challenge / party_level));

        if (difference < 0.5f) {
            float adjusted = (entry.rarity_class) * (1.0f - difference * 1.8f);
            int adjusted_class;

            if (adjusted <= 3.0f) {
                adjusted_class = 3;
            } else if (adjusted <= 10.0f) {
                adjusted_class = 7;
            } else if (adjusted <= 30.0f) {
                adjusted_class = 20;
            } else {
                adjusted_class = 70;
            }

            unsigned char time = entry.time_condition;
            if (adjusted_class == rarity_class && (time == 2 || time == night)) {
                candidates.push_back(&entry);
            }
        }
    }

    if (candidates.empty()) {
        for (const auto& entry : table.entries) {
            unsigned char time = entry.time_condition;
            if (entry.rarity_class == rarity_class && (time == 2 || time == night)) {
                candidates.push_back(&entry);
            }
        }
    }

    if (candidates.empty()) return nullptr;
    return candidates[candidates.size() == 1 ? 0 : Random(static_cast<int>(candidates.size()))];
}

/* Bias the database's group-size dice by party-relative monster level and the
   configured difficulty. Strong encounters are pushed toward the lower half on
   normal difficulty and weak tiny encounters toward the upper half; novice and
   expert take the corresponding fixed extremes in those branches. */
// FUNCTION: WIZ8 0x0048BC30
int MonGen::RollEncounterGroupSize(W8MonsterRecord* record)
{
    W8Dice* dice = &record->group_size_dice;
    float relative_level = record->display_level / GetAveragePartyMemberLevel();
    int minimum = dice->Minimum();
    int maximum = dice->Maximum();
    float midpoint = (minimum + maximum) * 0.5f;
    int rolled = RollDice(dice);

    if (relative_level >= 1.2f) {
        if (g_settings.difficulty == W8_DIFFICULTY_NOVICE) {
            return minimum;
        }
        if (g_settings.difficulty == W8_DIFFICULTY_EXPERT) {
            return rolled;
        }
        while (rolled > midpoint) {
            rolled = RollDice(dice);
        }
        return rolled;
    }

    /* Retail really compares the midpoint here, not relative_level. */
    if (midpoint > 0.8f) {
        return rolled;
    }
    if (g_settings.difficulty == W8_DIFFICULTY_EXPERT) {
        return maximum;
    }
    if (g_settings.difficulty == W8_DIFFICULTY_NOVICE) {
        return rolled;
    }
    while (rolled < midpoint) {
        rolled = RollDice(dice);
    }
    return rolled;
}

/* Select one table entry, spawn its main group and up to two database-defined
   companion groups, attach scripts and formation state, and register the main
   group as a live random encounter. The bit-5 form is only valid for hostile
   factions while one of the three story facts is set. */
// FUNCTION: WIZ8 0x0048AD20
unsigned char MonGen::GenerateEncounter(const glm::vec3* position)
{
    W8EncounterCompanionRecord companion_records[2];
    bool companion_active[2] = {false, false};
    glm::vec3 spawn_position;
    W8EncounterTableRuntime* table;
    W8MonsterRecord* record;
    W8MonsterGroup* group;
    unsigned int species;
    const char* script;
    int encounter_weight;
    int companion_count = 0;
    int index;

    if ((flags & W8_MONGEN_DISABLED) != 0 || encounter_table_index == -1) {
        return 0;
    }

    table = GetEncounterTable(encounter_table_index);
    if (!table) return 0;
    const auto* selected = SelectEncounterCandidate(*table);
    if (!selected) {
        return 0;
    }

    species = selected->species;
    script = selected->script.value;
    spawn_position = *position;
    record = MonsterDBFromSpecies(species);
    if (record == 0) {
        srAssertFail("pMonsterDB", MON_GEN_CPP, 0x146,
                     fmt::format("MonsterDBFromSpecies failed for species {:d}", species));
    }
    if (record->deleted != 0) {
        DebugMessage(1, fmt::format("MonGen::GenerateEncounter: WARNING - DBS record for monster "
                                    "species {:d} has been DELETED!",
                                    species));
        return 0;
    }

    /* Retail computes this total even though the surviving release path never
       reads it afterwards; the external calls can still populate cycle data. */
    encounter_weight = GetMonsterCycleFallbackValue(species);
    for (index = 0; index < 2; ++index) {
        companion_records[index] = record->companions[index];
        if (companion_records[index].species > 0 && Chance(companion_records[index].chance)) {
            companion_active[index] = true;
            encounter_weight += GetMonsterCycleFallbackValue(companion_records[index].species);
        }
    }
    static_cast<void>(encounter_weight);

    if ((flags & W8_MONGEN_STORY_GATED) != 0) {
        if (GetFactionDisposition(record->faction_id) != W8_FACTION_HOSTILE ||
            (!GetFact(W8_FACT_DESTINAE_POSSESS) && !GetFact(W8_FACT_ASTRAL_POSSESS) &&
             !GetFact(W8_FACT_CHAOS_POSSESS))) {
            return 0;
        }
    }

    int count = RollEncounterGroupSize(record);
    if (count == 0) {
        srAssertFail("ulNumMonsters", MON_GEN_CPP, 0x17b,
                     fmt::format("Error in Monster DB: zero group size ({})", record->name0));
    }

    group = CreateGroup(species, count, &spawn_position, false, false, true);
    group->encounter_registered = true;
    SetMonsterGroupFormation(group, &this->spawn_position);
    if (group != 0 && group->encounter_registered &&
        !std::ranges::contains(g_active_groups, group)) {
        g_active_groups.push_back(group);
    }

    W8Monster* monster = GetMonsterByLocationID(group->leader_location_id);
    if (monster != 0) {
        monster->SetScript(script != 0 && script[0] != '\0' ? script : "Default.MSF", true);
    }

    for (index = 0; index < 2; ++index) {
        if (!companion_active[index]) {
            continue;
        }

        unsigned int companion_species =
            static_cast<unsigned short>(companion_records[index].species);
        W8MonsterRecord* companion_record = MonsterDBFromSpecies(companion_species);
        if (companion_record == 0) {
            srAssertFail(
                "pMonsterDB", MON_GEN_CPP, 0x193,
                fmt::format("MonsterDBFromSpecies failed for species {:d}", companion_species));
        }
        if (companion_record->deleted != 0) {
            DebugMessage(1,
                         fmt::format("MonGen::GenerateEncounter: WARNING - DBS record for monster "
                                     "species {:d} has been DELETED, chum of species {:d}!",
                                     companion_species, species));
            return 0;
        }

        int companion_group_count = RollDice(&companion_record->group_size_dice);
        companion_count += companion_group_count;
        W8MonsterGroup* companion_group = CreateGroup(companion_species, companion_group_count,
                                                      &spawn_position, false, false, true);
        companion_group->encounter_registered = true;
        SetMonsterGroupFormation(companion_group, &this->spawn_position);
        LinkMonsterGroupToLeader(group, companion_group);
    }

    if (g_dev_mode && g_current_screen_state.id != W8_SCREEN_PLEASE_WAIT) {
        W8MonsterRecord* group_record = MonsterGroupGetRecord(group);
        std::string group_name =
            group->member_count == 1 ? group_record->name0 : group_record->name1;
        const char* companion_word = companion_count == 1 ? "chum" : "chums";
        ShowNotice(W8_FONT_PALETTE_BROWN,
                   fmt::format("MonGen ({}): spawned {:d} {} & {:d} {} (lvl {:d})", name,
                               group->member_count, group_name, companion_count, companion_word,
                               selected->challenge_level));
    }
    return 1;
}

/* Refuse generation while global gameplay modes block it, while the generator
   is disabled, or while its candidate point is too close/far, visible, occupied
   or over the active-encounter budget. A forced roll bypasses the spatial and
   chance tests but not the global-mode and budget gates. */
// FUNCTION: WIZ8 0x0048B200
bool MonGen::CanGenerateEncounter(bool force)
{
    glm::vec3 camera;
    float distance;

    if (g_generator_save_flag != 0 || gXStatus.world_update_blocked || gXStatus.fCombatMode ||
        gXStatus.fNpcDialogueMode || generation_enabled == 0) {
        return false;
    }

    camera = cameraLocation();
    glm::vec3 delta = spawn_position - camera;
    distance = glm::length(delta);

    if (!force && !g_status.world_suspended) {
        if (distance > 200000.0f || distance < 35000.0f) {
            return false;
        }
        if (g_octree != 0 && g_octree->HasLineOfSight(&camera, &spawn_position, true)) {
            return false;
        }
    }

    if (static_cast<int>(g_active_groups.size()) >= g_random_encounter_limit) {
        CullExpiredEncounters();
        if (static_cast<int>(g_active_groups.size()) >= g_random_encounter_limit) {
            return false;
        }
    }

    if (force) {
        return true;
    }

    if (g_encounter_culling_scale_fast == 1.0f) {
        glm::vec3 lower(spawn_position.x - 5000.0f, spawn_position.y - 5000.0f,
                        spawn_position.z - 5000.0f);
        glm::vec3 upper(spawn_position.x + 5000.0f, spawn_position.y + 5000.0f,
                        spawn_position.z + 5000.0f);
        const auto locations = g_octree->locationsInBox({lower, upper}, 0);
        if (!locations.empty()) {
            return false;
        }
    }

    int chance = (flags & W8_MONGEN_USE_DEFAULT_SETTINGS) != 0 ? g_generator_interval_min
                                                               : custom_spawn_chance;
    return Chance(chance) != 0;
}

/* Restore the level's encounter budget and population bounds. */
static void ClampRandomEncounterLimits(const W8LevelDatabaseRecord& level)
{
    ClampInteger(&g_random_encounter_budget, level.minimum_encounter_budget,
                 level.maximum_encounter_budget);
    g_random_encounter_limit = g_random_encounter_budget;
    ClampInteger(&g_random_encounter_limit, level.minimum_random_encounters,
                 level.maximum_random_encounters);
}

/* Advances the random-encounter budget for the current level.

   Normally the budget grows by the time elapsed since the level was last
   budgeted, divided by that level's period, and both the budget and the derived
   limit are then clamped into their own per-level ranges. A reset instead sets
   the budget straight to the level maximum and declares the elapsed span stale,
   which is what makes the reset path take the reroll branch below.

   The elapsed span is finally added to every live group's own timestamp, so a
   group that existed through the gap ages by exactly as much as the level did.
   Past ten hours that is not worth doing and the encounters are rerolled. */
// FUNCTION: WIZ8 0x0048c810
void UpdateRandomEncounterBudget(bool reset_budget)
{
    W8LevelDatabaseRecord* level;
    int elapsed;
    int index;

    if (!reset_budget) {
        elapsed =
            g_status.world_clock - g_status.level_progress[g_status.current_level].sight_clock;
        g_random_encounter_budget +=
            elapsed / g_level_records[g_status.current_level].encounter_budget_period;
    } else {
        g_random_encounter_budget =
            g_level_records[g_status.current_level].maximum_encounter_budget;
        elapsed = W8_ENCOUNTER_STALE_SECONDS + 1;
    }
    level = &g_level_records[g_status.current_level];
    ClampRandomEncounterLimits(*level);
    if (elapsed > W8_ENCOUNTER_STALE_SECONDS) {
        RollRandomEncounters();
        return;
    }
    for (index = 0; index < static_cast<int>(g_active_groups.size()); ++index) {
        W8MonsterGroup* group = g_active_groups[index];

        group->spawn_time += elapsed;
    }
}

// FUNCTION: WIZ8 0x0048c9f0
void DespawnAllActiveMonsterGroups(void)
{
    while (!g_active_groups.empty()) {
        DespawnMonsterGroup(g_active_groups.back());
    }
}

/* Put the encounter-culling scale back to its fast default and rearm every
   loaded generator's interval timer. */
// FUNCTION: WIZ8 0x0048cbe0
void ResetMonsterGeneratorTimers(void)
{
    g_encounter_culling_scale_fast = 1.0f;
    auto* generators = &g_world->monster_generators;

    for (const auto& generator : *generators) {

        generator->timer.ResetDurationScale();
    }
}

// GLOBAL: WIZ8 0x0060a6cc
float g_encounter_culling_scale_fast = 1.0f;

// GLOBAL: WIZ8 0x005ec918
const float g_encounter_culling_rate = 2880.0f;

// GLOBAL: WIZ8 0x005eca90
const double g_encounter_culling_distance = 50000.0;

/* Retires random encounters that have outlived their welcome.

   A group is a candidate once more than the level's culling span has passed
   since it was budgeted, where the span is scaled one way normally and another
   when 0x00504910 reports the faster clock. A candidate is only actually
   despawned when the party is far enough away from its lead member - or when
   the override at 0x00687500 says to drop it regardless of distance.

   The elapsed span is compared as unsigned, so a group whose timestamp is ahead
   of the clock reads as very old rather than as not yet due. Preserved as
   found. */
// FUNCTION: WIZ8 0x0048c8e0
void CullExpiredEncounters(void)
{
    glm::vec3 party;
    glm::vec3 position;
    float span;
    int index;

    party = cameraLocation();
    if (!IsSightRangeOverridden()) {
        span = g_encounter_culling_time_seconds * g_sight_default;
    } else {
        span = g_encounter_culling_time_seconds * g_encounter_culling_scale_fast *
               g_encounter_culling_rate;
    }
    for (index = 0; index < static_cast<int>(g_active_groups.size()); ++index) {
        W8MonsterGroup* group = g_active_groups[index];

        if (span < (static_cast<unsigned int>(g_status.world_clock - group->spawn_time))) {
            W8Monster* monster = GetMonsterByLocationID(group->leader_location_id);

            position = monster->GetPosition();
            glm::vec3 delta = position - party;

            if (g_encounter_culling_distance < glm::length(delta) || g_status.world_suspended) {
                DespawnMonsterGroup(group);
            }
        }
    }
}

/* One loaded encounter table by index. */
// FUNCTION: WIZ8 0x0048ad00
W8EncounterTableRuntime* GetEncounterTable(int index)
{
    if (index >= 0 && index < static_cast<int>(g_encounter_tables.size())) {
        return &g_encounter_tables[index];
    }
    return 0;
}

/* Destroy every generator and empty the list. */
// FUNCTION: WIZ8 0x0048bf70
void DestroyMonsterGenerators(void)
{
    g_world->monster_generators.clear();
}

/* Restore the full MONG encounter state. Older save versions omit later global
   fields; version four and below also have no saved encounter budget. The
   random-encounter budget is always clamped to the current level after loading,
   and the culling span is finally reset from that level's database row. */
// FUNCTION: WIZ8 0x0048c110
unsigned char MonGen::LoadAll(W8Chunk& chunk)
try {
    DestroyMonsterGenerators();
    const auto version = chunk.ReadValue<int>();
    if (version > 4)
        chunk.ReadValue(gXStatus.saved_encounter_budget);
    else
        gXStatus.saved_encounter_budget = 100;
    chunk.ReadValue(g_random_encounter_budget);
    if (version > 1) chunk.ReadValue(g_encounter_culling_time_seconds);
    if (version > 2) {
        chunk.ReadValue(g_generator_save_flag);
        chunk.ReadValue(g_generator_interval_min);
        chunk.ReadValue(g_generator_default_interval);
        chunk.ReadValue(g_generator_interval_max);
    }
    const auto count = chunk.ReadValue<int>();
    if (count < 0 || count > chunk.RemainingBytes() / 26) return false;
    for (int index = 0; index < count; ++index) {
        auto generator = std::make_unique<MonGen>();
        if (!generator->Load(chunk)) return false;
        g_world->monster_generators.push_back(std::move(generator));
    }
    auto& level = g_level_records[g_status.current_level];
    ClampRandomEncounterLimits(level);
    g_encounter_culling_time_seconds = level.encounter_culling_seconds;
    return true;
} catch (const std::exception&) {
    return false;
}

/* Writes one generator-independent save record back onto an already loaded
   world. A record whose name no longer exists is still consumed completely: a
   temporary interval gate reads and discards the saved timer payload. */
// FUNCTION: WIZ8 0x0048c470
void LoadMonsterGenerators(W8Chunk& chunk)
{
    const auto version = chunk.ReadValue<int>();
    chunk.ReadValue(g_generator_save_flag);
    const auto count = chunk.ReadValue<int>();
    if (count < 0 || count > chunk.RemainingBytes() / 37)
        throw std::runtime_error("invalid monster generator count");
    for (int index = 0; index < count; ++index) {
        char name[32];
        chunk.ReadBytes(name, sizeof(name));
        const auto enabled = chunk.ReadValue<unsigned char>();
        const auto flags = chunk.ReadValue<unsigned int>();

        MonGen* generator = nullptr;
        for (const auto& candidate : g_world->monster_generators) {
            if (strncmp(name, candidate->name, sizeof(name)) == 0) {
                generator = candidate.get();
                break;
            }
        }
        if (generator) {
            generator->generation_enabled = enabled;
            generator->flags = flags;
        }
        if (version > 1) {
            if (generator) {
                if (!generator->timer.Load(chunk))
                    throw std::runtime_error("invalid monster generator timer");
                generator->timer.Arm();
            } else {
                W8IntervalGate scratch;
                scratch.m_flags.one_shot = false;
                if (!scratch.Load(chunk))
                    throw std::runtime_error("invalid missing-generator timer");
            }
        }
    }
}

/* Runs one encounter roll for every generator that is armed and whose own
   precondition passes. The count is taken once, but the vector is re-read for
   each element, which is what the repeated bounds test in the original is. */
// FUNCTION: WIZ8 0x0048c600
void RunMonsterGenerators(void)
{
    int count = static_cast<int>(g_world->monster_generators.size());
    MonGen* generator;
    int index;

    for (index = 0; index < count; ++index) {
        generator = g_world->monster_generators[index].get();
        if (generator->timer.PollElapsedIntervals() != 0) {
            if (generator->CanGenerateEncounter(false)) {
                generator->GenerateEncounter(&generator->spawn_position);
            }
            generator->Reset();
        }
    }
}

/* Remove one random-encounter leader from the live registry. During combat,
   removing an actually registered group also spends one unit of the encounter
   budget and recomputes the level-clamped active limit. */
// FUNCTION: WIZ8 0x0048c670
void UnregisterActiveEncounterGroup(W8MonsterGroup* group)
{
    if (group == 0 || !group->encounter_registered) {
        return;
    }
    const bool was_removed = wiz8::erase_first(g_active_groups, group);
    unsigned char removed = was_removed;
    if (!gXStatus.fCombatMode || removed == 0) {
        return;
    }

    W8LevelDatabaseRecord* level = &g_level_records[g_status.current_level];
    --g_random_encounter_budget;
    ClampRandomEncounterLimits(*level);
}

/* Register a live random-encounter leader exactly once. */
// FUNCTION: WIZ8 0x0048c750
void RegisterActiveEncounterGroup(W8MonsterGroup* group)
{
    if (group == 0 || !group->encounter_registered ||
        std::ranges::contains(g_active_groups, group)) {
        return;
    }
    g_active_groups.push_back(group);
}

/* Release all encounter tables and names, and mark no level loaded. */
// FUNCTION: WIZ8 0x0048a710
void UnloadEncounterTables(void)
{
    g_encounter_tables.clear();
    g_encounter_names.clear();
    g_encounter_tables_level = -1;
}

/* Writes the world's generators to a save. The record version goes out first,
   then a shared flag byte, then the count, and then each generator as its name,
   its trailing flag, its own flag word and whatever 0x0043A770 appends. */
// FUNCTION: WIZ8 0x0048c3b0
void SaveMonsterGenerators(wiz8::File& handle)
{
    MonGen* generator;
    int count;
    int version;
    int index;

    version = 3;
    count = static_cast<int>(g_world->monster_generators.size());
    handle.write(&version, 4);
    handle.write(&g_generator_save_flag, 1);
    handle.write(&count, 4);
    for (index = 0; index < count; ++index) {
        generator = g_world->monster_generators[index].get();
        handle.write(generator->name, 0x20);
        handle.write(&generator->generation_enabled, 1);
        handle.write(&generator->flags, 4);
        generator->timer.Save(handle);
    }
}

/* Writes one generator to a save, field by field rather than as a block: the
   record on disk is narrower than the structure and skips +0x05, +0x0A and
   +0x18. The leading byte is written uninitialised - a one-byte local the
   original never assigns. Preserved as found. */
// FUNCTION: WIZ8 0x0048b520
void MonGen::Save(wiz8::File& handle)
{
    unsigned char version = 3;

    handle.write(&version, 1);
    handle.write(name, 0x20);
    handle.write(&generation_enabled, 1);
    handle.write(&flags, 4);
    handle.write(&custom_spawn_chance, 1);
    handle.write(&custom_interval_seconds, 2);
    handle.write(&unknown_08, 2);
    handle.write(&spawn_position.x, 4);
    handle.write(&spawn_position.y, 4);
    handle.write(&spawn_position.z, 4);
    handle.write(&encounter_table_index, 4);
    timer.Save(handle);
}

/* Reads one generator back. The leading byte is a record version: from 3 the
   name and its trailing flag are stored too, below that they are not, and the
   two paths converge on the same eight common fields. Every read is chained
   through the same conjunction, so the first failure abandons the rest and the
   record is reported bad; the timer is rearmed either way, and the armed bit is
   always cleared on the way out so a loaded generator starts disarmed. */
// FUNCTION: WIZ8 0x0048b5e0
unsigned char MonGen::Load(W8Chunk& chunk)
try {
    const auto version = chunk.ReadValue<unsigned char>();
    if (version >= 3) {
        chunk.ReadBytes(name, sizeof(name));
        chunk.ReadValue(generation_enabled);
    }
    chunk.ReadValue(flags);
    chunk.ReadValue(custom_spawn_chance);
    chunk.ReadValue(custom_interval_seconds);
    chunk.ReadValue(unknown_08);
    chunk.ReadValue(spawn_position.x);
    chunk.ReadValue(spawn_position.y);
    chunk.ReadValue(spawn_position.z);
    chunk.ReadValue(encounter_table_index);
    Reset();
    if (version > 1) {
        if (!timer.Load(chunk)) return false;
        timer.Arm();
    }
    flags &= ~static_cast<unsigned int>(W8_MONGEN_ARMED);
    return true;
} catch (const std::exception&) {
    return false;
}

/* Rearms the generator's timer. The delay is the
   configured interval jittered by a uniform draw over twice its jitter
   fraction, so the mean is the interval itself. Bit 3 selects the shared
   default interval; a clear bit uses the generator's custom +0x06 interval. */
// FUNCTION: WIZ8 0x0048b420
void MonGen::Reset()
{
    short interval;
    float jitter;

    interval = (flags & W8_MONGEN_USE_DEFAULT_SETTINGS) != 0 ? g_generator_default_interval
                                                             : custom_interval_seconds;
    jitter = interval * g_generator_jitter_fraction;
    timer.SetDuration(static_cast<float>(Random(static_cast<int>(jitter) * 2 + 1)) + interval -
                      jitter);
    timer.Arm();
}

/* Writes the encounter subsystem's own state to a save, ahead of the generator
   records themselves: the record version, the level's saved budget, the running
   budget and culling span, the shared flag byte, three shared interval words,
   and then the count followed by that many generator records. */
// FUNCTION: WIZ8 0x0048c020
void SaveEncounterState(wiz8::File& handle)
{
    int count;
    int version;
    int index;

    count = static_cast<int>(g_world->monster_generators.size());
    version = 5;
    handle.write(&version, 4);
    handle.write(&gXStatus.saved_encounter_budget, 4);
    handle.write(&g_random_encounter_budget, 4);
    handle.write(&g_encounter_culling_time_seconds, 4);
    handle.write(&g_generator_save_flag, 1);
    handle.write(&g_generator_interval_min, 2);
    handle.write(&g_generator_default_interval, 2);
    handle.write(&g_generator_interval_max, 2);
    handle.write(&count, 4);
    for (index = 0; index < count; ++index) {
        g_world->monster_generators[index]->Save(handle);
    }
}

/* Destroy every current random group, make an exact-count copy of the world's
   generator vector, randomize it with count independent pair swaps, then force
   one generation attempt from every copied entry. */
// FUNCTION: WIZ8 0x0048ca20
void RollRandomEncounters(void)
{
    while (!g_active_groups.empty()) {
        DespawnMonsterGroup(g_active_groups.back());
    }

    std::vector<MonGen*> generators;
    for (const auto& generator : g_world->monster_generators) generators.push_back(generator.get());
    int count = static_cast<int>(generators.size());
    if (count > 1) {
        for (int remaining = count; remaining > 0; --remaining) {
            int first = Random(count);
            int second = Random(count);
            if (first != second) {
                MonGen* swap = generators[first];
                generators[first] = generators[second];
                generators[second] = swap;
            }
        }
    }

    for (int index = 0; index < count; ++index) {
        MonGen* generator = generators[index];
        if (generator->CanGenerateEncounter(true)) {
            generator->GenerateEncounter(&generator->spawn_position);
        }
    }
}

/* Apply one duration scale to the encounter culling clock and every loaded
   generator timer. */
// FUNCTION: WIZ8 0x0048cb80
void SetMonsterGeneratorDurationScale(float scale)
{
    g_encounter_culling_scale_fast = scale;
    auto* generators = &g_world->monster_generators;

    for (auto& generator : *generators) {
        generator->timer.SetDurationScale(scale);
    }
}

/* Store the table index and mark HARASSMENT tables with bit 5. Retail never
   clears that bit here when a later table is not HARASSMENT. */
// FUNCTION: WIZ8 0x0048cc50
void MonGen::SetEncounterTable(int index)
{
    encounter_table_index = index;
    if (auto* table = GetEncounterTable(index);
        table && SDL_strncasecmp(table->name, "HARASSMENT", 10) == 0) {
        flags |= W8_MONGEN_STORY_GATED;
    }
}

/* Return the first table whose name starts with the complete caller string,
   ignoring case as the retail _strnicmp import does. */
// FUNCTION: WIZ8 0x0048cca0
int FindEncounterTableByName(const char* name)
{
    for (int index = 0; index < static_cast<int>(g_encounter_tables.size()); ++index) {
        W8EncounterTableRuntime* table = &g_encounter_tables[index];
        if (SDL_strncasecmp(table->name, name, strlen(name)) == 0) {
            return index;
        }
    }
    return -1;
}
