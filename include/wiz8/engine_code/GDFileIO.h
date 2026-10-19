#pragma once

#include <memory>

extern const float g_path_endpoint_scale;

struct W8GameData;

std::unique_ptr<W8GameData> ReadGameData(std::string_view path, bool secondary);
unsigned char InitializeGameData(W8GameData* game_data);
