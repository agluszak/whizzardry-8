#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 5 (Marten's Bluff): the teleporter pads, the dial-locked door and its
   gas trap, and the Trang transporter. */
std::unique_ptr<LevelScript> MakeMartensBluff1Script(const ScriptContext& context);
