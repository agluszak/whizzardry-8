#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 8 (Monastery1): the roach, spider and coffin spawns and the star wheel. */
std::unique_ptr<LevelScript> MakeMonastery1Script(const ScriptContext& context);
