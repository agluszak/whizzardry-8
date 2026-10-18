#pragma once

class W8Navigator;

// The party's navigator: built with the renderer, null outside that lifetime.
W8Navigator* PartyNavigator();

unsigned char InitializeStartupNavigation(void);
void ShutdownStartupNavigation(void);
