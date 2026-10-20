#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x16 (SeaCaves): the Higardi chests, the tomb door and the slope
   override item. */
std::unique_ptr<LevelScript> MakeSeaCavesScript(const ScriptContext& context);
