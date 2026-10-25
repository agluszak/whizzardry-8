#pragma once

#include <memory>

class Game;
struct PartyQuoteInput;

// Gameplay screens share quote hit targets while they retain the same game.
// Rebuilt screens recover bounds from the retained quote presentation state.
std::shared_ptr<PartyQuoteInput> AcquirePartyQuoteInput(Game& game);
