#pragma once


#include <cstdint>
class W8Navigator;

extern W8Navigator* g_startup_world;
extern float g_runtime_world_scale;

std::uint8_t InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
