#include "wiz8/world/encounters.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "layouts/levels.h"
#include "local_code/Factions.h"
#include "wiz8/chunk.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/encounter_records.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/difficulty.h"
#include "wiz8/entities/monster.h"
#include "wiz8/fact_state.h"
#include "wiz8/filesystem.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_population.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/monster_generators.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

MonGen::MonGen()
{
    flags = 0;
    custom_spawn_chance = 100;
    custom_interval_seconds = 100;
    unknown_08 = 0xffff;
    encounter_table_index = -1;
    generation_enabled = true;
}

void GeneratorInterval::setSeconds(float seconds)
{
    if (seconds > 0.0f) seconds_ = seconds;
}

void GeneratorInterval::setScale(float scale)
{
    const float kept = SharedGameTimersHeld() ? 0.0f : progress();
    scale_ = scale;
    elapsed_ = kept * length();
}

unsigned int GeneratorInterval::advance(std::chrono::milliseconds elapsed)
{
    elapsed_ += elapsed;
    const auto intervals = static_cast<unsigned int>(progress());
    elapsed_ -= static_cast<float>(intervals) * length();
    return intervals;
}

void GeneratorInterval::save(wiz8::File& handle) const
{
    const float saved_progress = progress();
    handle.write(&saved_progress, sizeof(saved_progress));
    handle.write(&scale_, sizeof(scale_));
}

void GeneratorInterval::load(W8Chunk& handle)
{
    const auto saved_progress = handle.ReadValue<float>();
    scale_ = handle.ReadValue<float>();
    elapsed_ = saved_progress * length();
}

MonGen* FindMonGenByName(const char* name)
{
    for (const auto& generator : GetWorld()->encounters.generators) {
        if (strncmp(name, generator->name, sizeof(generator->name)) == 0) {
            return generator.get();
        }
    }
    return 0;
}

const float g_generator_jitter_fraction = 0.20000000298023224f;

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
const W8EncounterEntry* MonGen::SelectEncounterCandidate(const W8EncounterTable& table)
{
    int rarity_roll;
    int rarity_class;
    int night;
    float party_level;

    std::vector<const W8EncounterEntry*> candidates;
    night = !(CurrentGame().status.game_time_ms > 18000000 &&
              CurrentGame().status.game_time_ms <= 79200000);

    rarity_roll = CurrentGame().rng.below(100);
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
    return candidates[candidates.size() == 1
                          ? 0
                          : CurrentGame().rng.below(static_cast<int>(candidates.size()))];
}

/* Bias the database's group-size dice by party-relative monster level and the
   configured difficulty. Strong encounters are pushed toward the lower half on
   normal difficulty and weak tiny encounters toward the upper half; novice and
   expert take the corresponding fixed extremes in those branches. */
int MonGen::RollEncounterGroupSize(const W8MonsterRecord* record)
{
    const W8Dice* dice = &record->group_size_dice;
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
bool MonGen::GenerateEncounter(const glm::vec3* position)
{
    W8EncounterCompanionRecord companion_records[2];
    bool companion_active[2] = {false, false};
    glm::vec3 spawn_position;
    const W8EncounterTable* table;
    const W8MonsterRecord* record;
    W8MonsterGroup* group;
    unsigned int species;
    const char* script;
    int encounter_weight;
    int index;

    if ((flags & W8_MONGEN_DISABLED) != 0 || encounter_table_index == -1) {
        return false;
    }

    table = GetEncounterTable(encounter_table_index);
    if (!table) return false;
    const auto* selected = SelectEncounterCandidate(*table);
    if (!selected) {
        return false;
    }

    species = selected->species;
    script = selected->script.c_str();
    spawn_position = *position;
    record = MonsterDBFromSpecies(species);
    if (record == 0) throw std::runtime_error("encounter names an unknown monster species");
    if (record->deleted != 0) {
        return false;
    }

    /* Retail computes this total even though the surviving release path never
       reads it afterwards; the external calls can still populate cycle data. */
    encounter_weight = GetMonsterCycleFallbackValue(species);
    for (index = 0; index < 2; ++index) {
        companion_records[index] = record->companions[index];
        if (companion_records[index].species > 0 &&
            CurrentGame().rng.chance(companion_records[index].chance)) {
            companion_active[index] = true;
            encounter_weight += GetMonsterCycleFallbackValue(companion_records[index].species);
        }
    }
    static_cast<void>(encounter_weight);

    if ((flags & W8_MONGEN_STORY_GATED) != 0) {
        if (GetFactionDisposition(record->faction_id) != W8_FACTION_HOSTILE ||
            (!GetFact(W8_FACT_DESTINAE_POSSESS) && !GetFact(W8_FACT_ASTRAL_POSSESS) &&
             !GetFact(W8_FACT_CHAOS_POSSESS))) {
            return false;
        }
    }

    int count = RollEncounterGroupSize(record);
    if (count == 0) throw std::runtime_error("monster database gives an empty encounter group");

    group = CreateGroup(species, count, &spawn_position, false, true);
    group->encounter_registered = true;
    SetMonsterGroupFormation(group, &this->spawn_position);

    Monster* monster = GetMonsterByLocationID(group->leader_location_id);
    if (monster != 0) {
        monster->SetScript(script != 0 && script[0] != '\0' ? script : "Default.MSF", true);
    }

    for (index = 0; index < 2; ++index) {
        if (!companion_active[index]) {
            continue;
        }

        unsigned int companion_species =
            static_cast<unsigned short>(companion_records[index].species);
        const W8MonsterRecord* companion_record = MonsterDBFromSpecies(companion_species);
        if (companion_record == 0)
            throw std::runtime_error("encounter names an unknown companion species");
        if (companion_record->deleted != 0) {
            return false;
        }

        int companion_group_count = RollDice(&companion_record->group_size_dice);
        W8MonsterGroup* companion_group =
            CreateGroup(companion_species, companion_group_count, &spawn_position, false, true);
        companion_group->encounter_registered = true;
        SetMonsterGroupFormation(companion_group, &this->spawn_position);
        LinkMonsterGroupToLeader(group, companion_group);
    }

    return true;
}

/* Refuse generation while global gameplay modes block it, while the generator
   is disabled, or while its candidate point is too close/far, visible, occupied
   or over the active-encounter budget. A forced roll bypasses the spatial and
   chance tests but not the global-mode and budget gates. */
bool MonGen::CanGenerateEncounter(bool force)
{
    glm::vec3 camera;
    float distance;

    if (GetWorld()->encounters.save_flag != 0 || CurrentGame().advancement.held ||
        CurrentGame().combat.has_value() || CurrentGame().activity.conversation ||
        !generation_enabled) {
        return false;
    }

    camera = cameraLocation();
    glm::vec3 delta = spawn_position - camera;
    distance = glm::length(delta);

    if (!force && !CurrentGame().status.world_suspended) {
        if (distance > 200000.0f || distance < 35000.0f) {
            return false;
        }
        if (GetWorldSpatialIndex() != 0 &&
            GetWorldSpatialIndex()->hasLineOfSight(camera, spawn_position)) {
            return false;
        }
    }

    const auto& level = Data().levels[GetLoadedLevelID()];
    const int limit = std::clamp(GetWorld()->encounters.random_budget,
                                 level.minimum_random_encounters, level.maximum_random_encounters);
    if (static_cast<int>(CurrentLevelPopulation().activeEncounterLeaders().size()) >= limit) {
        CullExpiredEncounters();
        if (static_cast<int>(CurrentLevelPopulation().activeEncounterLeaders().size()) >= limit) {
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
        const auto locations = GetWorldSpatialIndex()->locationsInBox({lower, upper}, 0);
        if (!locations.empty()) {
            return false;
        }
    }

    int chance = (flags & W8_MONGEN_USE_DEFAULT_SETTINGS) != 0 ? GetWorld()->encounters.interval_min
                                                               : custom_spawn_chance;
    return CurrentGame().rng.chance(chance) != 0;
}

/* Restore the level's encounter budget and population bounds. */
static void ClampEncounterBudget(World& world)
{
    const auto& level = Data().levels[world.level_id];
    world.encounters.random_budget =
        std::clamp(world.encounters.random_budget, level.minimum_encounter_budget,
                   level.maximum_encounter_budget);
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
void UpdateRandomEncounterBudget(bool reset_budget)
{
    int elapsed;

    if (!reset_budget) {
        elapsed = CurrentGame().status.world_clock -
                  CurrentGame().status.level_progress[GetLoadedLevelID()].sight_clock;
        GetWorld()->encounters.random_budget +=
            elapsed / Data().levels[GetLoadedLevelID()].encounter_budget_period;
    } else {
        GetWorld()->encounters.random_budget =
            Data().levels[GetLoadedLevelID()].maximum_encounter_budget;
        elapsed = W8_ENCOUNTER_STALE_SECONDS + 1;
    }
    ClampEncounterBudget(*GetWorld());
    if (elapsed > W8_ENCOUNTER_STALE_SECONDS) {
        RollRandomEncounters();
        return;
    }
    for (auto* group : CurrentLevelPopulation().activeEncounterLeaders()) {

        group->spawn_time += elapsed;
    }
}

void DespawnAllActiveMonsterGroups(void)
{
    for (;;) {
        const auto groups = CurrentLevelPopulation().activeEncounterLeaders();
        if (groups.empty()) return;
        DespawnMonsterGroup(groups.back());
    }
}

/* Put the encounter-culling scale back to its fast default and rearm every
   loaded generator's interval timer. */
void ResetMonsterGeneratorTimers(void)
{
    g_encounter_culling_scale_fast = 1.0f;
    auto* generators = &GetWorld()->encounters.generators;

    for (const auto& generator : *generators) {

        generator->timer.setScale(1.0f);
    }
}

float g_encounter_culling_scale_fast = 1.0f;

const float g_encounter_culling_rate = 2880.0f;

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
void CullExpiredEncounters(void)
{
    glm::vec3 party;
    glm::vec3 position;
    float span;

    party = cameraLocation();
    if (!IsSightRangeOverridden()) {
        span = GetWorld()->encounters.culling_seconds * g_sight_default;
    } else {
        span = GetWorld()->encounters.culling_seconds * g_encounter_culling_scale_fast *
               g_encounter_culling_rate;
    }
    std::vector<int> expired;
    for (auto* group : CurrentLevelPopulation().activeEncounterLeaders()) {

        if (span <
            (static_cast<unsigned int>(CurrentGame().status.world_clock - group->spawn_time))) {
            Monster* monster = GetMonsterByLocationID(group->leader_location_id);

            position = monster ? monster->nav.GetPosition() : group->centre;
            glm::vec3 delta = position - party;

            if (g_encounter_culling_distance < glm::length(delta) ||
                CurrentGame().status.world_suspended) {
                expired.push_back(group->group_id);
            }
        }
    }
    // Removing one formation can retire related groups from a restored save.
    // Keep ids across removals, never pointers into another operation's lifetime.
    for (int id : expired)
        if (auto* group = CurrentLevelPopulation().findGroup(id)) DespawnMonsterGroup(group);
}

/* One encounter table by index, or null. */
const W8EncounterTable* GetEncounterTable(int index)
{
    const auto& tables = Data().encounter_tables;
    return index >= 0 && index < static_cast<int>(tables.size()) ? &tables[index] : nullptr;
}

/* Destroy every generator and empty the list. */
void DestroyMonsterGenerators(void)
{
    GetWorld()->encounters.generators.clear();
}

/* Restore the full MONG encounter state. Older save versions omit later global
   fields; version four and below also have no saved encounter budget. The
   random-encounter budget is always clamped to the current level after loading,
   and the culling span is finally reset from that level's database row. */
W8EncounterState ReadEncounterState(W8Chunk& file)
{
    W8EncounterState state;
    int version, count;
    file.ReadBytes(&version, sizeof(version));
    if (version > 4) file.ReadBytes(&state.saved_budget, sizeof(state.saved_budget));
    file.ReadBytes(&state.random_budget, sizeof(state.random_budget));
    if (version > 1) file.ReadBytes(&state.culling_seconds, sizeof(state.culling_seconds));
    if (version > 2) {
        file.ReadBytes(&state.save_flag, sizeof(state.save_flag));
        file.ReadBytes(&state.interval_min, sizeof(state.interval_min));
        file.ReadBytes(&state.default_interval, sizeof(state.default_interval));
        file.ReadBytes(&state.interval_max, sizeof(state.interval_max));
    }
    file.ReadBytes(&count, sizeof(count));
    if (count < 0 || count > file.RemainingBytes() / 26)
        throw std::runtime_error("invalid encounter generator count");
    state.generators.reserve(count);
    for (int index = 0; index < count; ++index) {
        auto generator = std::make_unique<MonGen>();
        if (!generator->Load(file, state.default_interval))
            throw std::runtime_error("invalid encounter generator");
        state.generators.push_back(std::move(generator));
    }
    return state;
}

void ReadMonsterGeneratorStates(W8Chunk& file, W8EncounterState& state)
{
    int version, count;
    file.ReadBytes(&version, sizeof(version));
    file.ReadBytes(&state.save_flag, sizeof(state.save_flag));
    file.ReadBytes(&count, sizeof(count));
    if (count < 0 || count > file.RemainingBytes() / 37)
        throw std::runtime_error("invalid generator state count");
    for (int index = 0; index < count; ++index) {
        char name[32];
        unsigned char enabled;
        unsigned int flags;
        file.ReadBytes(name, sizeof(name));
        file.ReadBytes(&enabled, sizeof(enabled));
        file.ReadBytes(&flags, sizeof(flags));
        MonGen* generator = nullptr;
        for (const auto& candidate : state.generators)
            if (std::strncmp(name, candidate->name, sizeof(name)) == 0) {
                generator = candidate.get();
                break;
            }
        if (generator) {
            generator->generation_enabled = enabled != 0;
            generator->flags = flags;
        }
        if (version > 1) {
            // The record restores a one-second interval until the generator rearms.
            GeneratorInterval timer;
            timer.load(file);
            if (generator) generator->timer = timer;
        }
    }
}

void StartLevelEncounters(World& world)
{
    for (const auto& generator : world.encounters.generators) generator->timer.arm();
    if (world.level_id >= 0 && world.level_id < W8_LEVEL_COUNT) {
        auto& level = Data().levels[world.level_id];
        ClampEncounterBudget(world);
        world.encounters.culling_seconds = level.encounter_culling_seconds;
    }
}

/* Runs one encounter roll for every generator whose interval passed in the step's
   `elapsed` time (none passes while the shared timers are held) and whose own
   precondition passes. The count is taken once, but the vector is re-read for
   each element, which is what the repeated bounds test in the original is. */
void RunMonsterGenerators(std::chrono::milliseconds elapsed)
{
    if (SharedGameTimersHeld()) elapsed = {};
    int count = static_cast<int>(GetWorld()->encounters.generators.size());
    MonGen* generator;
    int index;

    for (index = 0; index < count; ++index) {
        generator = GetWorld()->encounters.generators[index].get();
        if (generator->timer.advance(elapsed) != 0) {
            if (generator->CanGenerateEncounter(false)) {
                generator->GenerateEncounter(&generator->spawn_position);
            }
            generator->Reset(GetWorld()->encounters.default_interval);
        }
    }
}

/* Completing a random encounter during combat spends one budget unit.
   Retirement and final leader removal own this event, exactly once. */
void SpendRandomEncounterBudget()
{
    if (!CurrentGame().combat.has_value()) return;
    --GetWorld()->encounters.random_budget;
    ClampEncounterBudget(*GetWorld());
}

/* Writes the world's generators to a save. The record version goes out first,
   then a shared flag byte, then the count, and then each generator as its name,
   its trailing flag, its own flag word and whatever 0x0043A770 appends. */
void SaveMonsterGenerators(wiz8::File& handle)
{
    MonGen* generator;
    int count;
    int version;
    int index;

    version = 3;
    count = static_cast<int>(GetWorld()->encounters.generators.size());
    handle.write(&version, 4);
    handle.write(&GetWorld()->encounters.save_flag, 1);
    handle.write(&count, 4);
    for (index = 0; index < count; ++index) {
        generator = GetWorld()->encounters.generators[index].get();
        handle.write(generator->name, 0x20);
        const unsigned char enabled = generator->generation_enabled;
        handle.write(&enabled, 1);
        handle.write(&generator->flags, 4);
        generator->timer.save(handle);
    }
}

/* Writes one generator to a save, field by field rather than as a block: the
   record on disk is narrower than the structure and skips +0x05, +0x0A and
   +0x18. The leading byte is written uninitialised - a one-byte local the
   original never assigns. Preserved as found. */
void MonGen::Save(wiz8::File& handle)
{
    unsigned char version = 3;

    handle.write(&version, 1);
    handle.write(name, 0x20);
    const unsigned char enabled = generation_enabled;
    handle.write(&enabled, 1);
    handle.write(&flags, 4);
    handle.write(&custom_spawn_chance, 1);
    handle.write(&custom_interval_seconds, 2);
    handle.write(&unknown_08, 2);
    handle.write(&spawn_position.x, 4);
    handle.write(&spawn_position.y, 4);
    handle.write(&spawn_position.z, 4);
    handle.write(&encounter_table_index, 4);
    timer.save(handle);
}

/* Reads one generator back. The leading byte is a record version: from 3 the
   name and its trailing flag are stored too, below that they are not, and the
   two paths converge on the same eight common fields. Every read is chained
   through the same conjunction, so the first failure abandons the rest and the
   record is reported bad; the timer is rearmed either way, and the armed bit is
   always cleared on the way out so a loaded generator starts disarmed. */
unsigned char MonGen::Load(W8Chunk& handle, short default_interval)
try {
    unsigned char version{};

    handle.ReadBytes(&version, 1);
    if (static_cast<signed char>(version) >= 3) {
        handle.ReadBytes(name, 0x20);
        unsigned char enabled;
        handle.ReadBytes(&enabled, 1);
        generation_enabled = enabled != 0;
    }
    handle.ReadBytes(&flags, 4);
    handle.ReadBytes(&custom_spawn_chance, 1);
    handle.ReadBytes(&custom_interval_seconds, 2);
    handle.ReadBytes(&unknown_08, 2);
    handle.ReadBytes(&spawn_position.x, 4);
    handle.ReadBytes(&spawn_position.y, 4);
    handle.ReadBytes(&spawn_position.z, 4);
    handle.ReadBytes(&encounter_table_index, 4);
    Reset(default_interval);
    if (static_cast<signed char>(version) > 1) {
        timer.load(handle);
        timer.arm();
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
void MonGen::Reset(short default_interval)
{
    short interval;
    float jitter;

    interval =
        (flags & W8_MONGEN_USE_DEFAULT_SETTINGS) != 0 ? default_interval : custom_interval_seconds;
    jitter = interval * g_generator_jitter_fraction;
    timer.setSeconds(static_cast<float>(CurrentGame().rng.below(static_cast<int>(jitter) * 2 + 1)) +
                     interval - jitter);
    timer.arm();
}

/* Writes the encounter subsystem's own state to a save, ahead of the generator
   records themselves: the record version, the level's saved budget, the running
   budget and culling span, the shared flag byte, three shared interval words,
   and then the count followed by that many generator records. */
void SaveEncounterState(wiz8::File& handle)
{
    int count;
    int version;
    int index;

    count = static_cast<int>(GetWorld()->encounters.generators.size());
    version = 5;
    handle.write(&version, 4);
    handle.write(&GetWorld()->encounters.saved_budget, 4);
    handle.write(&GetWorld()->encounters.random_budget, 4);
    handle.write(&GetWorld()->encounters.culling_seconds, 4);
    handle.write(&GetWorld()->encounters.save_flag, 1);
    handle.write(&GetWorld()->encounters.interval_min, 2);
    handle.write(&GetWorld()->encounters.default_interval, 2);
    handle.write(&GetWorld()->encounters.interval_max, 2);
    handle.write(&count, 4);
    for (index = 0; index < count; ++index) {
        GetWorld()->encounters.generators[index]->Save(handle);
    }
}

/* Destroy every current random group, make an exact-count copy of the world's
   generator vector, randomize it with count independent pair swaps, then force
   one generation attempt from every copied entry. */
void RollRandomEncounters(void)
{
    DespawnAllActiveMonsterGroups();

    std::vector<MonGen*> generators;
    for (const auto& generator : GetWorld()->encounters.generators)
        generators.push_back(generator.get());
    int count = static_cast<int>(generators.size());
    if (count > 1) {
        for (int remaining = count; remaining > 0; --remaining) {
            int first = CurrentGame().rng.below(count);
            int second = CurrentGame().rng.below(count);
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
void SetMonsterGeneratorDurationScale(float scale)
{
    g_encounter_culling_scale_fast = scale;
    auto* generators = &GetWorld()->encounters.generators;

    for (auto& generator : *generators) {
        generator->timer.setScale(scale);
    }
}

/* Store the table index and mark HARASSMENT tables with bit 5. Retail never
   clears that bit here when a later table is not HARASSMENT. */
void MonGen::SetEncounterTable(int index)
{
    encounter_table_index = index;
    if (auto* table = GetEncounterTable(index);
        table && SDL_strncasecmp(table->name.c_str(), "HARASSMENT", 10) == 0) {
        flags |= W8_MONGEN_STORY_GATED;
    }
}

/* Return the first table whose name starts with the complete caller string,
   ignoring case as the retail _strnicmp import does. */
int FindEncounterTableByName(const char* name)
{
    const auto& tables = Data().encounter_tables;
    for (int index = 0; index < static_cast<int>(tables.size()); ++index) {
        if (SDL_strncasecmp(tables[index].name.c_str(), name, strlen(name)) == 0) {
            return index;
        }
    }
    return -1;
}
