#pragma once

#include <string>

/* Load the string table, fonts and gameplay databases, then request the first
   screen: the intro, or the startup quick save when asked to load one. */
bool InitializeGame(bool load_at_startup, const std::string& string_data_override);
/* Release everything InitializeGame loaded, after the screens have left. */
void ShutdownGame(bool save_configuration);
