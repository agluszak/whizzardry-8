#pragma once

extern bool g_npc_dialogue_closed;
extern bool g_sea_caves_slope_override_enabled;

#include <cstdint>
#include <vector>

struct W8MonsterInfo;
struct W8MonsterGroup;
class stSound3D;
template <class T> class srVector3T;

/* One level-specific per-frame entry point, registered by the level's own
   master function and run every world update. */
typedef void (*W8MasterFunction)(std::int32_t);

/* The registered master functions, and the flag a callback sets to ask the
   dispatcher to drop it after this run. */
extern std::vector<W8MasterFunction> g_master_functions;
extern bool g_remove_current_master_function;
extern bool g_running_trigger_from_script;

void ClearActiveWorldCursorNode(void);
std::int32_t NormalizeMasterFunctionValue(std::int32_t value);
/* Run every registered master function once with argument zero, dropping the
   ones that set the removal flag while it runs. */
void RunMasterFunctions(void);
/* Run every registered master function once with argument -1, the persist
   command the level masters answer by writing their live state into the
   location variables. */
void SaveMasterFunctions(void);
/* Dispatch one world-cursor-node command against the nodes covering `info`'s
   ground-settled position (the camera's when info is null). */
bool DispatchWorldCursorNodeCommand(W8MonsterInfo* info, std::int32_t command, ...);
/* Format the current level's message-database path, fetch the indexed string
   and show it. Answers whether the string existed. */
bool ShowLevelMessage(std::int32_t message_id);

void InitializeLevelMasterFunctions(std::int32_t level);
W8MonsterGroup* SpawnMonsters(std::int32_t monster_id, std::int32_t count, srVector3T<float>* position, std::int32_t hostility,
                              bool settle, bool a, bool b); /* 0x004D8F00 */
stSound3D* CreateAndPlaySoundNode(const char* sound_name, srVector3T<float> position, float volume,
                                  float scale, bool play_flag); /* 0x004D8F80 */
