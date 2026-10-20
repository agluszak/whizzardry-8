#pragma once

#include "wiz8/loaded_game_data.h"

// Tests arrange the loaded game data directly; production code only reads it.
inline GameData& MutableData()
{
    return const_cast<GameData&>(Data());
}
