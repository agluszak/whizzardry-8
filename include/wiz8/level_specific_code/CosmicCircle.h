#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 4 (Cosmic Circle): the arena spawn and its trigger plane. */
std::unique_ptr<LevelScript> MakeCosmicCircleScript(const ScriptContext& context);
