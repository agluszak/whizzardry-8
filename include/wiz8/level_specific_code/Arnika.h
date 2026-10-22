#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0 (Arnika): the two elevators, the laser scanner, the vault alarms and
   the Mook pedestal. */
std::unique_ptr<LevelScript> MakeArnikaScript(const ScriptContext& context);
