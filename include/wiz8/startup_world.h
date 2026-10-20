#pragma once

class Navigator;

// The party's navigator: built with the renderer, null outside that lifetime.
Navigator* PartyNavigator();

bool InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
