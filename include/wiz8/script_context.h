#pragma once

#include <optional>

class Events;
class Game;
class Inventory;
struct Activity;
struct Combat;
struct Story;
struct GameStatus;
struct W8Party;
struct World;

/* What NPC scripting and level scripts act on. Whoever runs a script passes it in, so
   scripts do not reach the game through `CurrentGame()`. It borrows one game's parts
   and lives no longer than that game. */
struct ScriptContext {
    W8Party& party;
    Inventory& inventory;
    Story& story;
    GameStatus& status;
    Activity& activity;
    std::optional<Combat>& combat;
    Events& events;
    // The loaded level's world; null while no level is loaded (new-game fact seeding).
    World* world;
    /* For commands that still take the whole game (gold, skill and attribute changes,
       speech) until they take the parts they act on (ROADMAP.md, "Migration"). */
    Game& game;
};

// The context of `game`, acting on its loaded level's world.
ScriptContext MakeScriptContext(Game& game);
