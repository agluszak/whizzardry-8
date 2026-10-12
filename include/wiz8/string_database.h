#pragma once
#include <cstdint>
#include <string>
#include <string_view>

std::string GetStringFromStringDatabase(std::string_view path, std::int32_t index,
                                          std::uint32_t* metadata_00, std::uint32_t* metadata_04);
void ShowString(std::string_view text);
