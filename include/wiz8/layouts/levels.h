#ifndef WIZ8_LAYOUTS_LEVELS_H
#define WIZ8_LAYOUTS_LEVELS_H

#include <string>

#pragma pack(push, 1)

struct W8LevelFolderRecord {
    char folder_name[50];
    char level_name[50];
    char location_code[4];
    signed char sky_index;
    signed char level_band;
};

struct W8LevelProgressRow {
    bool visited;
    /* 0x01: incremented by the combat teardown for each finished fight while
       characters are still active. */
    unsigned short combat_end_count;
    /* 0x03: counted by RecordMonsterKill for each in-combat kill credited
       while the party is on this level. */
    short monster_kill_count;
    unsigned int experience_gained; /* 0x05: accumulated when experience is awarded */
    int gold_collected;
    int sight_clock;
    unsigned char unknown_11[0x10];
};

#pragma pack(pop)

static_assert(sizeof(W8LevelProgressRow) == 0x21, "W8LevelProgressRow_must_be_0x21");

// Native asset paths, relative to the game filesystem.
struct W8LevelInfo {
    std::string level_path;
    std::string sky_path;
};

/* The forty-seven real levels. Level ids 47..56 are the test-level slots the
   LoadLevel assert (TEST_LEVEL_COUNT) still admits. */
enum { W8_LEVEL_COUNT = 47 };

#endif
