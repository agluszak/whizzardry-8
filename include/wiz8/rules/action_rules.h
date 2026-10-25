#pragma once

#include "wiz8/data/records.h"
#include "wiz8/rules/targeting.h"
struct GameData;
// Charm and Mindread are friendly despite their database enemy target band.
bool SpellTargetsEnemies(const GameData& data, int spell_id);

struct W8Party;
bool FormationRowOccupied(const W8Party& party, int row);
bool FormationScreens(const W8Party& party, unsigned int from, unsigned int to);
int BlockingFormationRows(const W8Party& party, unsigned int slot, int monster_quadrant);
W8RangeCategory ShortenFormationReach(W8RangeCategory range, int rows);

W8TargetNeed SpellTargetNeed(const GameData& data, int spell_id, bool normalize,
                             bool needs_item_target);
struct W8ItemInstance;
W8TargetNeed ItemTargetNeed(const GameData& data, const W8ItemInstance* item,
                            bool needs_item_target);

struct W8Character;
bool CanCharacterCastSpell(const GameData& data, const W8Character* character, int spell_id);
bool CharacterHasCastableSpell(const GameData& data, const W8Character* character);
