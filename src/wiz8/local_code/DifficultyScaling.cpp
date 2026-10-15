#include "conditions.h"
#include "wiz8/layouts/character.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/local_code/Configuration.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MagicEffects.h"
#include "wiz8/local_code/MonsterManager.h"

static void ScaleValueForDifficulty(int* value, bool fights_for_monsters)
{
    int multiplier;
    switch (g_settings.difficulty) {
    case 0:
        multiplier = fights_for_monsters ? 3 : 7;
        break;
    case 2:
        multiplier = fights_for_monsters ? 7 : 3;
        break;
    default:
        return;
    }
    *value = (*value * multiplier * 20) / 100;
}

// FUNCTION: WIZ8 0x0055cc00
void ScaleValueForCharacterDifficulty(int party_slot, int* value)
{
    ScaleValueForDifficulty(
        value, g_status.buffers.Char[party_slot].uiCondition[W8_CONDITION_TURNCOAT] > 0);
}

// FUNCTION: WIZ8 0x0055ccb0
void ScaleValueForMonsterDifficulty(W8MonsterInfo* monster_info, int* value)
{
    switch (monster_info->ubDisposition) {
    case W8_DISPOSITION_HOSTILE:
        ScaleValueForDifficulty(value, true);
        break;
    case W8_DISPOSITION_FRIENDLY:
        ScaleValueForDifficulty(value, false);
        break;
    default:
        break;
    }
}
