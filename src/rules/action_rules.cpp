#include "wiz8/rules/action_rules.h"

#include <algorithm>
#include <cstdlib>

#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
bool SpellTargetsEnemies(const GameData& data, int spell_id)
{
    if (static_cast<unsigned int>(spell_id) > 0x95 || spell_id == W8_SPELL_CHARM ||
        spell_id == W8_SPELL_MINDREAD)
        return false;
    const auto type = data.spells[spell_id].target_type;
    return type > W8_TARGET_TYPE_PARTY && type < W8_TARGET_TYPE_POINT;
}

bool FormationRowOccupied(const W8Party& party, int row)
{
    return std::ranges::any_of(party.formation.cells[row], [&](int slot) {
        return slot != -1 && !party.characters[slot].bonus.out_of_formation;
    });
}

bool FormationScreens(const W8Party& party, unsigned int from, unsigned int to)
{
    const int first = party.formation.rowOf(from);
    const int second = party.formation.rowOf(to);
    return first >= 0 && second >= 0 && first != 4 && second != 4 &&
           std::abs(first - second) == 2 && FormationRowOccupied(party, 4);
}

int BlockingFormationRows(const W8Party& party, unsigned int slot, int monster_quadrant)
{
    const int row = party.formation.rowOf(slot);
    if (row == monster_quadrant) return 0;
    const bool blocked = FormationRowOccupied(party, monster_quadrant);
    if (row == 4 || std::abs(monster_quadrant - row) != 2) return blocked;
    return blocked ? 2 : FormationRowOccupied(party, 4);
}

W8RangeCategory ShortenFormationReach(W8RangeCategory range, int rows)
{
    if (range < W8_RANGE_TOUCH || range >= W8_RANGE_LONG) return range;
    while (rows-- > 0 && range != W8_RANGE_NONE)
        range = range == W8_RANGE_TOUCH ? W8_RANGE_NONE : static_cast<W8RangeCategory>(range - 1);
    return range;
}
