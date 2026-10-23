#pragma once

#include <memory>
#include <string>
#include <utility>

#include "wiz8/game.h"
#include "wiz8/rules/random.h"
#include "wiz8/world/world.h"

// A world drawing from the current game's random stream and item counter, as a level's does.
inline std::unique_ptr<World> MakeWorld(int level_id = -1, std::string source_path = {})
{
    auto& game = CurrentGame();
    return std::make_unique<World>(game.rng, game.status.next_world_item_id, level_id,
                                   std::move(source_path));
}

// A world with a random stream and item counter of its own, for tests without a game.
struct StandaloneWorld {
    explicit StandaloneWorld(int level_id = -1, std::string source_path = {})
        : world(rng, next_item_id, level_id, std::move(source_path))
    {
    }
    Rng rng;
    int next_item_id = 1;
    World world;
};
