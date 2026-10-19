#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/CosmicCircle.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

#define COSMIC_CIRCLE_CPP "C:\\Projects\\Wizardry 8\\Level Specific Code\\Cosmic Circle.cpp"

/* Level Specific Code\Cosmic Circle.cpp (level 4).

   Attribution evidence: 0x004D9AD0 and 0x004D9B40 carry this file's path in
   their assertion calls. The level-4 block in
   InitializeLevelMasterFunctions registers CC_TRIGGERPLANE1HEDRA and
   calls the setup. The leading always-false callback is installed on Arnika's
   "ARN11" trigger; it sits at the head of this unit between
   MasterFunctionList.cpp's template emissions and the CC callback. */

// FUNCTION: WIZ8 0x004D9AC0
bool CosmicCircleReturnFalse(Trigger* pTrigger)
{
    return false;
}

/* "CC_TRIGGERPLANE1HEDRA": stepping onto the arena trigger plane ends any
   running combat, posts the savant's script notice and starts CameraPath1. */
// FUNCTION: WIZ8 0x004D9AD0
bool CosmicCircleTriggerPlane1Hedra(Trigger* pTrigger)
{
    W8NpcState* pNPC;

    if (gXStatus.fCombatMode) {
        EndCombat(true);
    }
    BeginScriptedWorldAction();
    pNPC = GetNpcStateByKind(0x85);
    if (pNPC == 0) {
        srAssertFail("pNPC", COSMIC_CIRCLE_CPP, 0x19, "Cannot find VOC_SAVANT_CC1");
    } else {
        QueueNpcScriptNotice(pNPC, 0, std::nullopt, false, 0);
    }
    UpdateCameraPathStateByName(GetWorld(), "CameraPath1", 1);
    return true;
}

/* Spawn the Cosmic Circle arena: Altheides and the dark savant on their named
   positions, Bela and Phoonzang as hostile adds, the two principals aimed at
   each other's start points and the remaining trigger planes switched off. */
// FUNCTION: WIZ8 0x004D9B40
void CosmicCircleSetup(void)
{
    /* Retail dereferenced both monster infos unconditionally, leaving the
       pointer uninitialised on a missing named entity or failed id; natively
       a missing monster is skipped. */
    W8MonsterInfo* pMonsterInfoDs = nullptr;
    W8MonsterInfo* pMonsterInfoAltheides = nullptr;
    glm::vec3 positionAltheides;
    glm::vec3 positionDs;
    glm::vec3 positionBela;
    glm::vec3 positionPhoonzang;
    W8MonsterGroup* group;
    Trigger* pTrigger;
    int uiMonsterID;

    if (!g_status.world_suspended && !g_status.cc_arena_spawned) {
        if (FindEntityByName("NP_ALTHEIDESARENA", &positionAltheides, 0, 0)) {
            group = SpawnMonsters(0x1b3, 1, &positionAltheides, 0, true, false, false);
            uiMonsterID = group->MemberLocationId(0).value();
            if (uiMonsterID == 0) {
                srAssertFail("uiMonsterID", COSMIC_CIRCLE_CPP, 0x41,
                             "Invalid Monster ID for CC end");
            } else {
                pMonsterInfoAltheides = FindMonster(uiMonsterID);
            }
        }
        if (FindEntityByName("NP_DSARENA", &positionDs, 0, 0)) {
            group = SpawnMonsters(0x1b6, 1, &positionDs, 0, true, false, false);
            uiMonsterID = group->MemberLocationId(0).value();
            if (uiMonsterID == 0) {
                srAssertFail("uiMonsterID", COSMIC_CIRCLE_CPP, 0x53,
                             "Invalid Monster ID for CC end");
            } else {
                pMonsterInfoDs = FindMonster(uiMonsterID);
            }
        }
        if (FindEntityByName("NP_BELASTART", &positionBela, 0, 0)) {
            SpawnMonsters(0x1b4, 1, &positionBela, 2, true, false, false);
        }
        if (FindEntityByName("NP_PHOONZANG1", &positionPhoonzang, 0, 0)) {
            SpawnMonsters(0x197, 1, &positionPhoonzang, 2, true, false, false);
        }
        g_status.cc_arena_spawned = true;
        if (pMonsterInfoDs != nullptr) {
            pMonsterInfoDs->p3D->AimAtPosition(&positionAltheides);
        }
        if (pMonsterInfoAltheides != nullptr) {
            pMonsterInfoAltheides->p3D->AimAtPosition(&positionDs);
        }
        pTrigger = FindTriggerByName("CC_TRIGGERPLANE2");
        if (pTrigger != 0) {
            pTrigger->flags &= ~W8_TRIGGER_ON;
        }
        pTrigger = FindTriggerByName("CC_TRIGGERPLANE4");
        if (pTrigger != 0) {
            pTrigger->flags &= ~W8_TRIGGER_ON;
        }
    }
}
