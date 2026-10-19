#pragma once

extern bool g_npc_dialogue_closed;
extern bool g_sea_caves_slope_override_enabled;
// Worn or held by the party, this item ignores the Sea Caves' explicit slopes.
inline constexpr int kSlopeOverrideItem = 0x254;

#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/location_variables.h"

struct W8MonsterInfo;
struct W8MonsterGroup;
class Sound3D;

/* One level-specific per-frame entry point, registered by the level's own
   master function and run every world update. */
using W8MasterFunction = bool (*)(int);

// An update returns true when complete. Capture never advances gameplay.
struct W8LevelMaster {
    W8MasterFunction update;
    void (*capture)(W8SavedLocationState&) = nullptr;
    bool operator==(const W8LevelMaster&) const = default;
};
extern std::vector<W8LevelMaster> g_master_functions;
extern bool g_running_trigger_from_script;

void ClearActiveWorldCursorNode(void);
int NormalizeMasterFunctionValue(int value);
// Run the current batch once; completed entries leave and new entries wait until next frame.
void RunMasterFunctions(void);
// Capture only registered persistence callbacks; never step gameplay while saving.
W8SavedLocationState CaptureMasterFunctionState();
/* Dispatch one world-cursor-node command against the nodes covering `info`'s
   ground-settled position (the camera's when info is null). */
unsigned char DispatchWorldCursorNodeCommand(W8MonsterInfo* info, int command, ...);
/* Format the current level's message-database path, fetch the indexed string
   and show it. Answers whether the string existed. */
unsigned char ShowLevelMessage(int message_id);

void InitializeLevelMasterFunctions(int level);
W8MonsterGroup* SpawnMonsters(int monster_id, int count, glm::vec3* position, int hostility,
                              bool settle, bool a, bool b); /* 0x004D8F00 */
Sound3D* CreateAndPlaySoundNode(const char* sound_name, glm::vec3 position, float volume,
                                float scale, bool play_flag); /* 0x004D8F80 */
