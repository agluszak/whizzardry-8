#pragma once

#include "glm/ext/vector_float3.hpp"

// Worn or held by the party, this item ignores the Sea Caves' explicit slopes.
inline constexpr int kSlopeOverrideItem = 0x254;

struct W8MonsterInfo;
struct W8MonsterGroup;
struct World;
class Sound3D;

/* Dispatch one world-cursor-node command through the loaded level's script
   (LevelScript::dispatchCursorNodeCommand); false while no level is loaded. */
bool DispatchWorldCursorNodeCommand(W8MonsterInfo* info, int command, int argument = 0);
/* Format the current level's message-database path, fetch the indexed string
   and show it. Answers whether the string existed. */
bool ShowLevelMessage(int message_id);

W8MonsterGroup* SpawnMonsters(int monster_id, int count, glm::vec3* position, int hostility,
                              bool settle, bool use_alternate_name); /* 0x004D8F00 */
/* A one-shot positional sound owned by `world`'s script sounds; null when the
   loudness fraction is negative. */
Sound3D* CreateAndPlaySoundNode(World& world, const char* sound_name, glm::vec3 position,
                                float volume, float scale, bool play_flag); /* 0x004D8F80 */
