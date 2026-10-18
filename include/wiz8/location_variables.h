#pragma once

#include <span>
#include <string>
#include <utility>
#include <vector>

using W8SavedLocationState = std::vector<std::pair<std::string, int>>;

struct W8Chunk;

namespace wiz8 {
class File;
}

int GetLocationVarIDByName(const char* name);
void SetTriggerVariableByName(const char* name, int value);
void CreateLocationVar(const char* name, int value); /* 0x00443DC0 */
int GetLocationVarValueByName(const char* name);     /* 0x004440D0 */
void SaveLocationVariables(wiz8::File& handle,
                           std::span<const std::pair<std::string, int>> script_state = {});
bool LoadLocationVariables(W8Chunk& chunk);
bool HasLocationVariables();
void ClearLocationVariables();
