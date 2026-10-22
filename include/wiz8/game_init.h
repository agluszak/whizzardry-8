#pragma once

struct GameData;
class GameSession;

/* Install the loaded `data` for code that does not receive it yet, load the fonts, hand
   `games` to the router, then request the first screen: the intro, or the startup quick
   save when asked to load one. The caller owns both and keeps them until ShutdownGame. */
bool InitializeGame(const GameData& data, GameSession& games, bool load_at_startup);
/* End the game in progress and release everything InitializeGame loaded, after the
   screens have left. */
void ShutdownGame(GameSession& games, bool save_configuration);
