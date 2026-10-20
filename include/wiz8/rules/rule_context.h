#pragma once

class Events;
class Rng;
struct GameData;
struct W8Party;

/* What a rule acts on: the game's data, the state it changes, the game's random stream and
   its event log. Whoever applies a rule builds the context from its game; a context borrows
   those parts and lives no longer than they do. */
struct RuleContext {
    const GameData& data;
    W8Party& party;
    Rng& rng;
    Events& events;
    /* Play has begun. Before it, party creation fills hands its own way and nothing it
       does is announced. */
    bool game_started = false;
};
