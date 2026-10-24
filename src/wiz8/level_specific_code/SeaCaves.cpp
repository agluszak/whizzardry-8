#include "wiz8/level_specific_code/SeaCaves.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "monster_cycles.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/world.h"

struct W8NpcState;

namespace {

/* Spawns one hostile spirit at the named entity, if it exists. */
bool SpawnSpiritAt(const char* entity)
{
    glm::vec3 position;

    if (FindEntityByName(entity, &position, 0, 0)) {
        SpawnMonsters(0x1f2, 1, &position, 1, false, false);
    }
    return true;
}

/* "HigardiChest01": spawn one spirit monster (0x1f2, hostile) at the
   NP_Spirit01 entity. */
bool SeaCavesHigardiChest01(Trigger* pTrigger)
{
    return SpawnSpiritAt("NP_Spirit01");
}

/* "HigardiChest02": spawn one spirit monster at NP_Spirit02. */
bool SeaCavesHigardiChest02(Trigger* pTrigger)
{
    return SpawnSpiritAt("NP_Spirit02");
}

/* "HigardiChest03": spawn one spirit monster at NP_Spirit03. */
bool SeaCavesHigardiChest03(Trigger* pTrigger)
{
    return SpawnSpiritAt("NP_Spirit03");
}

/* "HigardiChest04": spawn one spirit monster at NP_Spirit04. */
bool SeaCavesHigardiChest04(Trigger* pTrigger)
{
    return SpawnSpiritAt("NP_Spirit04");
}

/* "HigardiChest05": spawn one spirit monster at NP_Spirit05. */
bool SeaCavesHigardiChest05(Trigger* pTrigger)
{
    return SpawnSpiritAt("NP_Spirit05");
}

/* "doortomb": NPC kind 0x64's monster entry gets a fresh group member when
   its group still exists and the monster answers query 6 false. */
bool SeaCavesDoorTomb(Trigger* pTrigger)
{
    W8NpcState* npc;
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;

    npc = CurrentGame().story.npcOfKind(0x64);
    if (npc == 0) {
        return true;
    }
    monster_info = GetNpcMonsterInfo(npc);
    if (monster_info == 0) {
        return true;
    }
    if (!monster_info->entity) {
        return true;
    }
    if (monster_info->entity->Query(W8_MONSTER_QUERY_CYCLE) != W8_MONSTER_CYCLE_BIRTH) {
        return true;
    }
    group = FindMonsterGroup(monster_info->monster_group_id);
    if (group != 0) {
        GiveBirthToMonster(group);
    }
    return true;
}

class SeaCavesScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        slope_override_active_ = true;
        bindTrigger("HigardiChest01", SeaCavesHigardiChest01);
        bindTrigger("HigardiChest02", SeaCavesHigardiChest02);
        bindTrigger("HigardiChest03", SeaCavesHigardiChest03);
        bindTrigger("HigardiChest04", SeaCavesHigardiChest04);
        bindTrigger("HigardiChest05", SeaCavesHigardiChest05);
        bindTrigger("doortomb", SeaCavesDoorTomb);
    }
};

} // namespace

std::unique_ptr<LevelScript> MakeSeaCavesScript(const ScriptContext& context)
{
    return std::make_unique<SeaCavesScript>(context);
}
