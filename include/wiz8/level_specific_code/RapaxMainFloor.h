#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x12 (RapaxMainFloor): the altar and the platform sequence. */
std::unique_ptr<LevelScript> MakeRapaxMainFloorScript(const ScriptContext& context);
