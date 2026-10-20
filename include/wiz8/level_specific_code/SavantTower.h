#pragma once

#include <memory>

class LevelScript;
struct ScriptContext;

/* Level 0x24 (SavantTower): the bomb's shape buttons and blocker. */
std::unique_ptr<LevelScript> MakeSavantTowerScript(const ScriptContext& context);
