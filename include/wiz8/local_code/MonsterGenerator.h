#ifndef WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H
#define WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H

#include <cstdint>
#include "wiz8/filesystem.h"

#include "wiz8/geometry.h"
#include "wiz8/engine_code/IntervalGate.h"

struct W8EncounterTableRuntime;
struct W8EncounterEntry;
struct W8Item;
struct W8MonsterRecord;
struct MonGen {
    std::uint32_t flags;
    std::int8_t custom_spawn_chance; /* MIPE edits 0..100 in steps of ten */
    std::int16_t custom_interval_seconds;   /* MIPE's custom "every N s" value */
    std::int16_t unknown_08;                /* persisted, initialized to -1 */
    /* World position, saved as three dwords and handed to GenerateEncounter. */
    srVector3T<float> spawn_position;
    W8Item* marker_item;       /* loaded Data\Items3D\Bitmaps\mongen.itm marker */
    std::int32_t encounter_table_index; /* index into g_encounter_tables, -1 means none */
    W8IntervalGate* m_pTimer;
    char name[32];
    std::uint8_t generation_enabled; /* MIPE "Toggle Active"; gates CanGenerate */

    MonGen();
    void Reset();
    /* Tests global encounter gates, range/LOS/occupancy constraints and this
       generator's chance. */
    bool CanGenerateEncounter(bool force);
    /* Selects and spawns the encounter table entry at `position`. */
    std::uint8_t GenerateEncounter(const srVector3T<float>* position);
    /* Choose an entry for the current rarity/time/party level, or return null. */
    const W8EncounterEntry* SelectEncounterCandidate(const W8EncounterTableRuntime& table);
    /* Difficulty-aware group-size roll for one monster database record. */
    std::int32_t RollEncounterGroupSize(W8MonsterRecord* record);
    /* Arms or disarms the generator, loading its marker on the way in. */
    void SetActive(std::uint8_t active, W8Item* node);
    /* The save pair. Both are __thiscall in the image. */
    void Save(wiz8::File& handle);
    std::uint8_t Load(wiz8::File& handle);
    /* The MONG chunk loader. */
    static std::uint8_t LoadAll(wiz8::File& save_handle);
    /* Moves the generator, notifying the scene when the generator has a marker. */
    void SetState(const srVector3T<float>* state);
    /* Loads the marker unconditionally, then applies the armed state. */
    void Reload(std::int32_t, bool active);
    /* Strncpy into the fixed 32-byte name member. */
    void SetName(const char* name);
    /* Select one loaded encounter table and apply its HARASSMENT flag. */
    void SetEncounterTable(std::int32_t index);
    ~MonGen();
};

MonGen* FindMonGenByName(const char* name);

#endif
