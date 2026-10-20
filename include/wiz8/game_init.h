#pragma once

#include <string>

struct GameData;
class GameSession;

/* Load the string table and gameplay databases into `data` and the fonts, hand `games` to
   the router, then request the first screen: the intro, or the startup quick save when
   asked to load one. The caller owns both and keeps them until ShutdownGame. */
bool InitializeGame(GameData& data, GameSession& games, bool load_at_startup,
                    const std::string& string_data_override);
/* End the game in progress and release everything InitializeGame loaded, after the
   screens have left. */
void ShutdownGame(GameSession& games, bool save_configuration);
