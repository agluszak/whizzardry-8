#pragma once

#include <memory>

extern const float g_path_endpoint_scale;

struct W8GameData;

std::unique_ptr<W8GameData> ReadGameData(const char* path, bool secondary); /* 0x00447570 */
unsigned char InitializeGameData(W8GameData* game_data);
