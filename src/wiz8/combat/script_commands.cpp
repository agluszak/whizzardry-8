#include <glm/ext/vector_float3.hpp>

#include "wiz8/combat/hostility.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/npc_interaction.h"

namespace {
// The two monster records whose groups the golem attack turns hostile; the second's joins the fight.
constexpr int kFirstGolem = 0x68;
constexpr int kSecondGolem = 0x13e;
} // namespace

void Game::setMonsterGroupDisposition(int location_id, W8Disposition disposition)
{
    W8MonsterInfo* monster_info = FindMonster(location_id);
    if (!monster_info) return;
    if (W8MonsterGroup* group = FindMonsterGroup(monster_info->monster_group_id, false))
        setGroupHostility(level.loaded(), group, disposition, false);
}

void Game::startGolemAttack()
{
    ClearMainGameTargetState();
    if (W8MonsterGroup* group = CurrentLevelPopulation().findSpeciesGroup(kFirstGolem))
        setGroupHostility(level.loaded(), group, W8_DISPOSITION_HOSTILE, false);
    if (W8MonsterGroup* group = CurrentLevelPopulation().findSpeciesGroup(kSecondGolem)) {
        setGroupHostility(level.loaded(), group, W8_DISPOSITION_HOSTILE, false);
        MonsterGroupEnterCombat(group);
    }
}

void Game::castTriggerSpell(glm::vec3 position, W8SpellId spell, unsigned int power_level)
{
    PointCastSpell(position, spell, power_level);
    if (spell == W8_SPELL_HEAL_ALL) LiftAllConditionsFromParty(CurrentGame());
}
