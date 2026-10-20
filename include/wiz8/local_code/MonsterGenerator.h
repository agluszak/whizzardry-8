#ifndef WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H
#define WIZ8_LOCAL_CODE_MONSTER_GENERATOR_H

struct W8Chunk;

#include <chrono>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/filesystem.h"
#include "wiz8/geometry.h"

struct W8EncounterTable;
struct W8EncounterEntry;
struct W8MonsterRecord;

/* A generator's roll interval on the step's time: an interval in seconds, scaled by the
   encounter-culling scale, and the step time since it began. A save stores the progress
   through it and the scale. */
class GeneratorInterval {
public:
    // Set the unscaled interval; a length that is not positive keeps the previous one.
    void setSeconds(float seconds);
    /* Change the scale, keeping the progress through the interval; while the shared
       timers are held the progress restarts, as it did on the retail clock. */
    void setScale(float scale);
    // Begin a new interval.
    void arm() { elapsed_ = {}; }
    /* Take `elapsed` step time and return how many whole intervals have passed; those
       are consumed and the remainder carries over. */
    unsigned int advance(std::chrono::milliseconds elapsed);
    [[nodiscard]] float progress() const { return elapsed_ / length(); }
    [[nodiscard]] float scale() const { return scale_; }
    // The retail pair: progress, then scale.
    void save(wiz8::File& handle) const;
    void load(W8Chunk& handle);

private:
    [[nodiscard]] std::chrono::duration<float> length() const
    {
        return std::chrono::duration<float>(seconds_ * scale_);
    }

    float seconds_ = 1.0f;
    float scale_ = 1.0f;
    std::chrono::duration<float> elapsed_{};
};

struct MonGen {
    unsigned int flags;
    signed char custom_spawn_chance; /* MIPE edits 0..100 in steps of ten */
    short custom_interval_seconds;   /* MIPE's custom "every N s" value */
    short unknown_08;                /* persisted, initialized to -1 */
    /* World position, saved as three dwords and handed to GenerateEncounter. */
    glm::vec3 spawn_position;
    int encounter_table_index; /* index into GameData::encounter_tables, -1 means none */
    GeneratorInterval timer;
    char name[32]{};
    bool generation_enabled; /* MIPE "Toggle Active"; gates CanGenerate */

    MonGen();
    void Reset(short default_interval);
    /* Tests global encounter gates, range/LOS/occupancy constraints and this
       generator's chance. */
    bool CanGenerateEncounter(bool force);
    /* Selects and spawns the encounter table entry at `position`. */
    bool GenerateEncounter(const glm::vec3* position);
    /* Choose an entry for the current rarity/time/party level, or return null. */
    const W8EncounterEntry* SelectEncounterCandidate(const W8EncounterTable& table);
    /* Difficulty-aware group-size roll for one monster database record. */
    int RollEncounterGroupSize(const W8MonsterRecord* record);
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
