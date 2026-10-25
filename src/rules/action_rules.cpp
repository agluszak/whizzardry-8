#include "wiz8/rules/action_rules.h"

#include <algorithm>
#include <cstdlib>

#include "wiz8/data/game_data.h"
#include "wiz8/data/item_records.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/rules/character.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_formation.h"
#include "wiz8/rules/targeting.h"
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

W8TargetNeed SpellTargetNeed(const GameData& data, int spell_id, bool normalize,
                             bool needs_item_target)
{
    if (spell_id != W8_SPELL_NONE) {
        auto type = data.spells[spell_id].target_type;
        if (normalize && type == W8_TARGET_TYPE_ALLY) type = W8_TARGET_TYPE_CASTER;
        switch (type) {
        case W8_TARGET_TYPE_ALLY:
            return spell_id != W8_SPELL_RESURRECTION ? W8_TARGET_NEED_ALLY
                                                     : W8_TARGET_NEED_CHARACTER_INDIRECT;
        case W8_TARGET_TYPE_CASTER:
            return W8_TARGET_NEED_CASTER;
        case W8_TARGET_TYPE_ENEMY:
            return W8_TARGET_NEED_ENEMY;
        case W8_TARGET_TYPE_ENEMY_GROUP:
            return W8_TARGET_NEED_GROUP;
        case W8_TARGET_TYPE_CONE:
            return W8_TARGET_NEED_CONE;
        case W8_TARGET_TYPE_RADIUS:
        case W8_TARGET_TYPE_POINT:
            return W8_TARGET_NEED_PLACE;
        case W8_TARGET_TYPE_ITEM:
            if (needs_item_target) {
                return W8_TARGET_NEED_ITEM;
            }
            break;
        default:
            break;
        }
    }
    return W8_TARGET_NEED_NONE;
}

W8TargetNeed ItemTargetNeed(const GameData& data, const W8ItemInstance* item,
                            bool needs_item_target)
{
    const W8ItemDatabaseRecord* record;

    if (item == 0 || item->iItemNo == -1) {
        return W8_TARGET_NEED_NONE;
    }
    record = &data.items[item->iItemNo];
    if (record->spell_id == W8_SPELL_NONE) {
        return W8_TARGET_NEED_NONE;
    }
    return SpellTargetNeed(data, record->spell_id, ItemClassNormalizesTarget(record),
                           needs_item_target);
}

bool CanCharacterCastSpell(const GameData& data, const W8Character* character, int spell_id)
{
    if (spell_id != W8_SPELL_NONE && character->spell_learned[spell_id] == 1 &&
        data.spells[spell_id].spell_point_cost <= character->iSPLeft[data.spells[spell_id].realm]) {
        return true;
    }
    return false;
}

bool CharacterHasCastableSpell(const GameData& data, const W8Character* character)
{
    int spell_id;

    for (spell_id = W8_SPELL_NONE; spell_id < 0x72; ++spell_id) {
        if (CanCharacterCastSpell(data, character, spell_id)) {
            return true;
        }
    }
    return false;
}
