#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x13 (RapaxUpperFloor): the treasure vault. */
std::unique_ptr<LevelScript> MakeRapaxUpperFloorScript(const ScriptContext& context);
