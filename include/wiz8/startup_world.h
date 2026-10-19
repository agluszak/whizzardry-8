#pragma once

class Navigator;

// The party's navigator: built with the renderer, null outside that lifetime.
Navigator* PartyNavigator();

unsigned char InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
