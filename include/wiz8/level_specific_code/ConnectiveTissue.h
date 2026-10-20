#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x1b (ConnectiveTissue, the Arnika-Trynton road): the lich crypt. */
std::unique_ptr<LevelScript> MakeConnectiveTissueScript(const ScriptContext& context);
