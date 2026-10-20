#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 9 (Monastery2): the bells and the micro door. */
std::unique_ptr<LevelScript> MakeMonastery2Script(const ScriptContext& context);
