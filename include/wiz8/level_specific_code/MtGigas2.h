#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x0d (MtGigas2): the wire panel, the officers and the Umpani alarm. */
std::unique_ptr<LevelScript> MakeMtGigas2Script(const ScriptContext& context);
