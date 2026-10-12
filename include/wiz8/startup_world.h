#pragma once


class W8Navigator;

extern W8Navigator* g_startup_world;
extern float g_runtime_world_scale;

bool InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
