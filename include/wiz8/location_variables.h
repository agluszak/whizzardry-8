#pragma once

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using W8SavedLocationState = std::vector<std::pair<std::string, int>>;

struct W8Chunk;

namespace wiz8 {
class File;
}

int GetLocationVarIDByName(std::string_view name);
void SetTriggerVariableByName(std::string_view name, int value);
void CreateLocationVar(std::string_view name, int value);
int GetLocationVarValueByName(std::string_view name);
void SaveLocationVariables(wiz8::File& handle,
                           std::span<const std::pair<std::string, int>> script_state = {});
bool LoadLocationVariables(W8Chunk& chunk);
bool HasLocationVariables();
void ClearLocationVariables();
