#pragma once

#include <memory>

#include "wiz8/rules/item_location.h"

class LevelScript;
struct ScriptContext;

/* Level 0x1a (Trynnie2): the vines, Hogar's meat, the Zulu and the urns. */
std::unique_ptr<LevelScript> MakeTrynnie2Script(const ScriptContext& context);

/* 0x004D9F60: consume the summon-quest items (0x1b3/0x1c3) when used. */
bool Trynnie2UseItem(const ScriptContext& context, W8ItemLocation location);
