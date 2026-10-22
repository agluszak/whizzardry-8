#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x15 (Rift1): the Al-Sedexus swap, the lava lord and the spawns. */
std::unique_ptr<LevelScript> MakeRift1Script(const ScriptContext& context);
