#pragma once

#include "wiz8/data/records.h"
struct GameData;
// Charm and Mindread are friendly despite their database enemy target band.
bool SpellTargetsEnemies(const GameData& data, int spell_id);

struct W8Party;
bool FormationRowOccupied(const W8Party& party, int row);
bool FormationScreens(const W8Party& party, unsigned int from, unsigned int to);
int BlockingFormationRows(const W8Party& party, unsigned int slot, int monster_quadrant);
W8RangeCategory ShortenFormationReach(W8RangeCategory range, int rows);
