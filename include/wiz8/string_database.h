#pragma once
#include <span>

unsigned char GetStringFromStringDatabase(const char* path, int index, std::span<char> output,
                                          unsigned int* metadata_00, unsigned int* metadata_04);
void ShowString(char* text);
