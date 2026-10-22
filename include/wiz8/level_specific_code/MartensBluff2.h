#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 6 (Marten's Bluff 2): the arrow and spikeball traps, the monster
   crusher, the raven quest and the stone idol's gas. */
std::unique_ptr<LevelScript> MakeMartensBluff2Script(const ScriptContext& context);
