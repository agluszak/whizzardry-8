#include "wiz8/rules/action_rules.h"

#include "wiz8/data/game_data.h"
#include "wiz8/data/records.h"
#include "wiz8/data/spell_ids.h"
bool SpellTargetsEnemies(const GameData& data, int spell_id)
{
    if (static_cast<unsigned int>(spell_id) > 0x95 || spell_id == W8_SPELL_CHARM ||
        spell_id == W8_SPELL_MINDREAD)
        return false;
    const auto type = data.spells[spell_id].target_type;
    return type > W8_TARGET_TYPE_PARTY && type < W8_TARGET_TYPE_POINT;
}
