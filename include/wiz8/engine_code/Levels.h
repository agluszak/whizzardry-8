#pragma once

#include <cstdint>
#include "wiz8/layouts/levels.h"

extern W8LevelFolderRecord g_level_folders[W8_LEVEL_COUNT];

std::int32_t GetLoadedLevelID(void);
const char* GetLevelFolderName(std::int32_t level_id);
std::uint8_t GetLevelLocationCode(std::int32_t level_id, char* location_code);
std::int32_t FindLevelIdByLocationCode(const char* location_code);

extern std::uint16_t g_level_name_indices[W8_LEVEL_COUNT];

bool LevelBuildInfoByID(std::int32_t level_id, W8LevelInfo* info);
std::uint8_t LoadSkyWorld(std::int32_t level, W8LevelInfo* info);
std::uint8_t LoadLevel(std::int32_t requested_level, std::int32_t entrance, bool restoring_game);
std::uint8_t UnloadLevel(const char* save_directory);

bool IsLevelCdMissing(std::int32_t level);
std::uint8_t FindGameDataPath(char* path, std::int32_t cd_number);
std::int32_t GetLevelCdNumber(std::int32_t level);
void UnloadSkyWorld(void);
char GetLevelBand(std::int32_t saved_level);

extern bool g_camera_path_active;
extern bool g_level_status_loading;
extern float g_default_world_height;
extern const float g_position_height_epsilon;
extern std::uint8_t g_environment_load_flag;
extern bool g_mipe_trigger_display;

std::uint8_t ReloadLevelPreservingCamera(std::int32_t level, std::int32_t entrance);

/* 0x0042B770: start the saved level's music playlist, falling back to
   Adventure.MPL when the level-specific playlist does not exist. */
void StartLevelMusic(std::int32_t fade, std::int32_t replace_current);
class W8MaterialMapper;
extern W8MaterialMapper g_material_mapper;
