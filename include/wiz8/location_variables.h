#pragma once

#include <cstdint>
#include "wiz8/filesystem.h"

#include <vector>
#include <algorithm>

extern std::vector<char*> g_location_variable_names;
extern std::vector<std::int32_t> g_location_variable_levels;
extern std::vector<std::int32_t> g_location_variable_values;

std::int32_t GetLocationVarIDByName(const char* name);
void SetTriggerVariableByName(const char* name, std::int32_t value);
void CreateLocationVar(const char* name, std::int32_t value); /* 0x00443DC0 */
std::int32_t GetLocationVarValueByName(const char* name);     /* 0x004440D0 */
void SaveLocationVariables(wiz8::File& handle);
bool LoadLocationVariables(wiz8::File& handle);
