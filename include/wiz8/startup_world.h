#pragma once

class W8Navigator;

// The party's navigator: built with the renderer, null outside that lifetime.
W8Navigator* PartyNavigator();
extern float g_runtime_world_scale;

unsigned char InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
