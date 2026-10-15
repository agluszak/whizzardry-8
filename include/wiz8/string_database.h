#pragma once
#include <string>
#include <string_view>

std::string GetStringFromStringDatabase(std::string_view path, int index, unsigned int* metadata_00,
                                        unsigned int* metadata_04);
void ShowString(std::string_view text);
