#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x18 (Swamp): the oil pool, the gas and fire planes and the lid. */
std::unique_ptr<LevelScript> MakeSwampScript(const ScriptContext& context);
