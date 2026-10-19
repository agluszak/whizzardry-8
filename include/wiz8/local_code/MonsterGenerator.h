#ifndef WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H
#define WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H

struct W8Chunk;

#include "glm/ext/vector_float3.hpp"
#include "wiz8/filesystem.h"
#include "wiz8/geometry.h"
#include "wiz8/platform/IntervalGate.h"

struct W8EncounterTableRuntime;
struct W8EncounterEntry;
struct W8MonsterRecord;
struct MonGen {
    unsigned int flags;
    signed char custom_spawn_chance; /* MIPE edits 0..100 in steps of ten */
    short custom_interval_seconds;   /* MIPE's custom "every N s" value */
    short unknown_08;                /* persisted, initialized to -1 */
    /* World position, saved as three dwords and handed to GenerateEncounter. */
    glm::vec3 spawn_position;
    int encounter_table_index; /* index into g_encounter_tables, -1 means none */
    W8IntervalGate timer;
    char name[32];
    bool generation_enabled; /* MIPE "Toggle Active"; gates CanGenerate */

    MonGen();
    void Reset(short default_interval);
    /* Tests global encounter gates, range/LOS/occupancy constraints and this
       generator's chance. */
    bool CanGenerateEncounter(bool force);
    /* Selects and spawns the encounter table entry at `position`. */
    unsigned char GenerateEncounter(const glm::vec3* position);
    /* Choose an entry for the current rarity/time/party level, or return null. */
    const W8EncounterEntry* SelectEncounterCandidate(const W8EncounterTableRuntime& table);
    /* Difficulty-aware group-size roll for one monster database record. */
    int RollEncounterGroupSize(W8MonsterRecord* record);
    /* The save pair. Both are __thiscall in the image. */
    void Save(wiz8::File& handle);
    unsigned char Load(W8Chunk& handle, short default_interval);
    /* The MONG chunk loader. */
    /* Select one loaded encounter table and apply its HARASSMENT flag. */
    void SetEncounterTable(int index);
    MonGen(const MonGen&) = delete;
    MonGen& operator=(const MonGen&) = delete;
};

MonGen* FindMonGenByName(const char* name);

#endif
