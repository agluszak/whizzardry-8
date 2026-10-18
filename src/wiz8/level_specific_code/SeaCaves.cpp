#include "wiz8/level_specific_code/SeaCaves.h"

#include "glm/ext/vector_float3.hpp"
#include "monster_cycles.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"

struct W8NpcState;

/* Spawns one hostile spirit at the named entity, if it exists. */
static bool spawnSpiritAt(const char* entity)
{
    glm::vec3 position;

    if (FindEntityByName(entity, &position, 0, 0)) {
        SpawnMonsters(0x1f2, 1, &position, 1, false, false, false);
    }
    return true;
}

/* "HigardiChest01": spawn one spirit monster (0x1f2, hostile) at the
   NP_Spirit01 entity. */
bool SeaCavesHigardiChest01(Trigger* pTrigger)
{
    return spawnSpiritAt("NP_Spirit01");
}

/* "HigardiChest02": spawn one spirit monster at NP_Spirit02. */
bool SeaCavesHigardiChest02(Trigger* pTrigger)
{
    return spawnSpiritAt("NP_Spirit02");
}

/* "HigardiChest03": spawn one spirit monster at NP_Spirit03. */
bool SeaCavesHigardiChest03(Trigger* pTrigger)
{
    return spawnSpiritAt("NP_Spirit03");
}

/* "HigardiChest04": spawn one spirit monster at NP_Spirit04. */
bool SeaCavesHigardiChest04(Trigger* pTrigger)
{
    return spawnSpiritAt("NP_Spirit04");
}

/* "HigardiChest05": spawn one spirit monster at NP_Spirit05. */
bool SeaCavesHigardiChest05(Trigger* pTrigger)
{
    return spawnSpiritAt("NP_Spirit05");
}

/* "doortomb": NPC kind 0x64's monster entry gets a fresh group member when
   its group still exists and the monster answers query 6 false. */
bool SeaCavesDoorTomb(Trigger* pTrigger)
{
    W8NpcState* npc;
    W8MonsterInfo* monster_info;
    W8MonsterGroup* group;

    npc = GetNpcStateByKind(0x64);
    if (npc == 0) {
        return true;
    }
    monster_info = GetNpcMonsterInfo(npc);
    if (monster_info == 0) {
        return true;
    }
    if (monster_info->p3D == 0) {
        return true;
    }
    if (MonsterQuery(monster_info->p3D, W8_MONSTER_QUERY_CYCLE) != W8_MONSTER_CYCLE_BIRTH) {
        return true;
    }
    group = FindMonsterGroup(monster_info->monster_group_id);
    if (group != 0) {
        GiveBirthToMonster(group);
    }
    return true;
}
