#ifndef WIZ8_LAYOUTS_LEVELS_H
#define WIZ8_LAYOUTS_LEVELS_H


#include <cstdint>
#pragma pack(push, 1)

struct W8LevelFolderRecord {
    char folder_name[50];
    char level_name[50];
    char location_code[4];
    std::int8_t sky_index;
    std::int8_t cd_number;
    std::int8_t level_band; /* 0x6a: returned by GetLevelBand */
};

struct W8LevelProgressRow {
    bool visited;
    /* 0x01: incremented by the combat teardown for each finished fight while
       characters are still active. */
    std::uint16_t combat_end_count;
    /* 0x03: counted by RecordMonsterKill for each in-combat kill credited
       while the party is on this level. */
    std::int16_t monster_kill_count;
    std::uint32_t experience_gained; /* 0x05: accumulated when experience is awarded */
    std::int32_t gold_collected;
    std::int32_t sight_clock;
    std::uint8_t unknown_11[0x10];
};

#pragma pack(pop)

static_assert(sizeof(W8LevelFolderRecord) == 0x6b, "W8LevelFolderRecord_must_be_0x6b");
static_assert(sizeof(W8LevelProgressRow) == 0x21, "W8LevelProgressRow_must_be_0x21");

/* LevelBuildInfoByID fills eight consecutive path buffers. LoadLevel passes
   the first three to LoadWorld as the level folder,
   mutable level filename and bitmap folder; the remaining buffers are the
   complete level path and the corresponding four sky paths. */
struct W8LevelInfo {
    char level_folder[0x64];
    char level_file_name[0x64];
    char level_bitmap_folder[0x64];
    char level_path[0x100];
    char sky_folder[0x64];
    char sky_file_name[0x64];
    char sky_bitmap_folder[0x64];
    char sky_path[0x100];
};

/* The forty-seven real levels. Level ids 47..56 are the test-level slots the
   LoadLevel assert (TEST_LEVEL_COUNT) still admits. */
enum { W8_LEVEL_COUNT = 47 };

#endif
