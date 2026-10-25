#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>

#include "wiz8/combat/combat.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/CosmicCircle.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/world.h"

struct W8NpcState;

/* Level Specific Code\Cosmic Circle.cpp (level 4).

   Attribution evidence: 0x004D9AD0 and 0x004D9B40 carry this file's path in
   their assertion calls. The level-4 script runs the setup and binds
   CC_TRIGGERPLANE1HEDRA. */

namespace {

class CosmicCircleScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void setup();
    TriggerDecision triggerPlane1Hedra(Trigger* pTrigger);
};

void CosmicCircleScript::enter()
{
    setup();
    bindTrigger("CC_TRIGGERPLANE1HEDRA", &CosmicCircleScript::triggerPlane1Hedra);
}

/* "CC_TRIGGERPLANE1HEDRA": stepping onto the arena trigger plane ends any
   running combat, posts the savant's script notice and starts CameraPath1. */
TriggerDecision CosmicCircleScript::triggerPlane1Hedra(Trigger* pTrigger)
{
    W8NpcState* pNPC;

    if (ctx.combat.has_value()) {
        ctx.game.endCombat(true);
    }
    BeginScriptedWorldAction();
    pNPC = CurrentGame().story.npcOfKind(0x85);
    if (pNPC == 0) throw std::runtime_error("Savant NPC state is missing");
    QueueNpcScriptNotice(pNPC, 0, -1, false, 0);
    UpdateCameraPathStateByName(&world(), "CameraPath1", 1);
    return {.run_action = true};
}

/* Spawn the Cosmic Circle arena: Altheides and the dark savant on their named
   positions, Bela and Phoonzang as hostile adds, the two principals aimed at
   each other's start points and the remaining trigger planes switched off. */
void CosmicCircleScript::setup()
{
    /* Retail dereferenced both monster infos unconditionally, leaving the
       pointer uninitialised on a missing named entity or failed id; natively
       a missing monster is skipped. */
    W8MonsterInfo* pMonsterInfoDs = nullptr;
    W8MonsterInfo* pMonsterInfoAltheides = nullptr;
    W8MonsterGroup* group;
    Trigger* pTrigger;
    int uiMonsterID;

    if (!ctx.status.world_suspended && !ctx.status.cc_arena_spawned) {
        auto positionAltheides = world().namedPosition("NP_ALTHEIDESARENA");
        if (positionAltheides) {
            group = SpawnMonsters(0x1b3, 1, &*positionAltheides, 0, true, false);
            uiMonsterID = group->MemberLocationId(0);
            assert(uiMonsterID != 0);
            pMonsterInfoAltheides = FindMonster(uiMonsterID);
        }
        auto positionDs = world().namedPosition("NP_DSARENA");
        if (positionDs) {
            group = SpawnMonsters(0x1b6, 1, &*positionDs, 0, true, false);
            uiMonsterID = group->MemberLocationId(0);
            assert(uiMonsterID != 0);
            pMonsterInfoDs = FindMonster(uiMonsterID);
        }
        if (auto positionBela = world().namedPosition("NP_BELASTART")) {
            SpawnMonsters(0x1b4, 1, &*positionBela, 2, true, false);
        }
        if (auto positionPhoonzang = world().namedPosition("NP_PHOONZANG1")) {
            SpawnMonsters(0x197, 1, &*positionPhoonzang, 2, true, false);
        }
        ctx.status.cc_arena_spawned = true;
        if (pMonsterInfoDs != nullptr && positionAltheides) {
            pMonsterInfoDs->entity->nav.AimAtPosition(&*positionAltheides);
        }
        if (pMonsterInfoAltheides != nullptr && positionDs) {
            pMonsterInfoAltheides->entity->nav.AimAtPosition(&*positionDs);
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

} // namespace

std::unique_ptr<LevelScript> MakeCosmicCircleScript(const ScriptContext& context)
{
    return std::make_unique<CosmicCircleScript>(context);
}
