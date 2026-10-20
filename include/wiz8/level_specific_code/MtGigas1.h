#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x0c (MtGigas1): the lifts, the pressure plate and the mud wall. */
std::unique_ptr<LevelScript> MakeMtGigas1Script(const ScriptContext& context);
/* The _VOC_EWAXXLIFT1 callback, which MtGigasOuter shares: outside a
   conversation, queue the lift NPC's notice with the held item, if any. */
bool QueueGigasLiftNotice(const ScriptContext& context);
