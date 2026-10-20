#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x0e (MtGigasOuter): the lift crank and the Umpani camp alarm. */
std::unique_ptr<LevelScript> MakeMtGigasOuterScript(const ScriptContext& context);
