#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using W8SavedLocationState = std::vector<std::pair<std::string, int>>;

std::optional<int> GetLocationVarIDByName(std::string_view name);
void SetTriggerVariableByName(std::string_view name, int value);
void CreateLocationVar(std::string_view name, int value);
int GetLocationVarValueByName(std::string_view name);
