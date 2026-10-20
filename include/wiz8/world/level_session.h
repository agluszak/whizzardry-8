#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "wiz8/layouts/levels.h"

// The level id of the published level world, or -1 when none is loaded.
int GetLoadedLevelID(void);

extern W8LevelFolderRecord g_level_folders[W8_LEVEL_COUNT];
std::string_view GetLevelFolderName(int level_id);
std::optional<std::string> GetLevelLocationCode(int level_id);
int FindLevelIdByLocationCode(std::string_view location_code);

extern unsigned short g_level_name_indices[W8_LEVEL_COUNT];

bool LevelBuildInfoByID(int level_id, W8LevelInfo* info);
bool LoadSkyWorld(std::string_view sky_path);
bool LoadLevel(int requested_level, int entrance, bool restoring_game);
void LeaveLevel();

void UnloadSkyWorld(void);
signed char GetLevelBand(int saved_level);

extern bool g_camera_path_active;
extern float g_default_world_height;
extern const float g_position_height_epsilon;
extern unsigned char g_environment_load_flag;

bool ReloadLevelPreservingCamera(int level, int entrance);

/* 0x0042B770: start the saved level's music playlist, falling back to
   Adventure.MPL when the level-specific playlist does not exist. */
void StartLevelMusic(int fade, int replace_current);
