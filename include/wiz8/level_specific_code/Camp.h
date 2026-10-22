#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;
struct World;

/* Level 0x10 (Camp, the Rapax away camp): the camp chest and the prison doors. */
std::unique_ptr<LevelScript> MakeCampScript(const ScriptContext& context);
/* Fill the away camp chest from the level-0x26 item records stored in the
   current save. */
void LoadAwayCampChest(World& world);
