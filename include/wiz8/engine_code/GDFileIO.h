#pragma once

#include <filesystem>

extern const float g_path_endpoint_scale;

struct W8GameData;

W8GameData* ReadGameData(const char* path, bool secondary); /* 0x00447570 */
W8GameData* ReadHostGameData(const std::filesystem::path& path, bool secondary);
unsigned char InitializeGameData(W8GameData* game_data);
