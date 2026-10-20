#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x0f (MtGigasTop): the cannon, the landing and the pain activator. */
std::unique_ptr<LevelScript> MakeMtGigasTopScript(const ScriptContext& context);
