#pragma once

#include <optional>
#include <source_location>
#include <string_view>

#include "wiz8/layouts/levels.h"

struct W8World;

// The level id of the current world context, if one exists.
std::optional<int> GetLoadedLevelID(void);
// Use only in paths whose contract requires a world context.
int GetRequiredLoadedLevelID(std::source_location caller = std::source_location::current());
W8World* ExchangeWorldLoadContext(W8World* world);

extern W8LevelFolderRecord g_level_folders[W8_LEVEL_COUNT];
const char* GetLevelFolderName(int level_id);
unsigned char GetLevelLocationCode(int level_id, char* location_code);
std::optional<int> FindLevelIdByLocationCode(const char* location_code);

extern unsigned short g_level_name_indices[W8_LEVEL_COUNT];

bool LevelBuildInfoByID(int level_id, W8LevelInfo* info);
unsigned char LoadSkyWorld(std::string_view sky_path);
unsigned char LoadLevel(int requested_level, int entrance, bool restoring_game);
void LeaveLevel();

void UnloadSkyWorld(void);
char GetLevelBand(int saved_level);

extern bool g_camera_path_active;
extern float g_default_world_height;
extern const float g_position_height_epsilon;
extern unsigned char g_environment_load_flag;

unsigned char ReloadLevelPreservingCamera(int level, int entrance);

/* 0x0042B770: start the saved level's music playlist, falling back to
   Adventure.MPL when the level-specific playlist does not exist. */
void StartLevelMusic(int fade, int replace_current);
class W8MaterialMapper;
extern W8MaterialMapper g_material_mapper;
