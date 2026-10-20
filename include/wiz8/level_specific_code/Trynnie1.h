#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;
struct World;

/* Level 0x19 (Trynnie1): the dead-Trynnie restore and the magic fountain. */
std::unique_ptr<LevelScript> MakeTrynnie1Script(const ScriptContext& context);

/* Swap the 'Trynnies'/'Dead Trynnies' encounter tables and clear the live
   groups; Trynnie2's killed-var sync also calls it. */
void KillTrynnieGroups(World& world); /* 0x004DA850 */
