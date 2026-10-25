#include "wiz8/level_specific_code/Rift1.h"

#include <memory>
#include <optional>
#include <string>

#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/location_variables.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

/* Level Specific Code\Rift1.cpp (level 0x15).

   Attribution evidence: 0x004DAFD0 passes this file's path string to
   MonsterGetIndexByLocationID. The level-0x15 script binds the surrounding
   cluster
   (Fireantspawn, Sexspawn, Hotstuff, Gate, AshLock, TimeDorado). */

namespace {

/* "Sexspawn": on the first activation after fact 0x15f, mark the
   Al-Sedexus swap, remove the original Al-Sedexus (species 0xd9) and spawn
   the replacement (species 0x22b) at the Sexspawn entity if it is not already
   in the level. Returns false unless the swap runs, so the trigger stays
   armed until it does. */
TriggerDecision Rift1Sexspawn(Trigger* pTrigger)
{
    W8MonsterInfo* monster_info;

    if (CurrentGame().testFact(W8_FACT_TEMPLAR) != 0) {
        if (GetLocationVarIDByName("AlSedexusSwapped") == -1) {
            CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "AlSedexusSwapped", 1);
            monster_info = FindMonsterInfoBySpecies(0xd9);
            if (monster_info != 0) {
                CurrentGame().retireMonster(monster_info, W8MonsterRemoval::Despawn);
            }
            monster_info = FindMonsterInfoBySpecies(0x22b);
            if (auto point = monster_info == 0
                                 ? CurrentGame().level.loaded().namedPosition("Sexspawn")
                                 : std::nullopt) {
                SpawnMonsters(0x22b, 1, &*point, 0, true, false);
            }
            return {.run_action = true};
        }
    }
    return {.run_action = false};
}

/* "Hotstuff": until fact 0x326, spawn the lava lord (0x175) at the Hotstuff
   entity, give it the MoveLavalord movement script and begin the scripted
   world action. */
TriggerDecision Rift1Hotstuff(Trigger* pTrigger)
{
    W8MonsterGroup* group;
    Monster* monster;

    if (CurrentGame().testFact(W8_FACT_LAVALORD_DIE) == 0) {
        if (auto position = CurrentGame().level.loaded().namedPosition("Hotstuff")) {
            group = SpawnMonsters(0x175, 1, &*position, 0, true, false);
            if (group != 0) {
                monster = GetMonsterByLocationID(group->MemberLocationId(0));
                if (monster != 0 && monster->SetScript("MoveLavalord.MSF", true)) {
                    BeginScriptedWorldAction();
                }
            }
        }
    }
    return {.run_action = true};
}

/* "Fireantspawn": spawn a hostile group of six fire ants (0x12b) at the
   Fireantspawn entity. */
TriggerDecision Rift1Fireantspawn(Trigger* pTrigger)
{
    if (auto position = CurrentGame().level.loaded().namedPosition("Fireantspawn")) {
        SpawnMonsters(0x12b, 6, &*position, 1, false, false);
    }
    return {.run_action = true};
}

/* "Gate": raise fact 0x1c4. */
TriggerDecision Rift1Gate(Trigger* pTrigger)
{
    CurrentGame().setFact(W8_FACT_RIFT_RAFE_CAGE_OPEN, 1);
    return {.run_action = true};
}

/* "AshLock": spawn one hostile monster 0x222 at the NP_Hotstuff3 entity. */
TriggerDecision Rift1AshLock(Trigger* pTrigger)
{
    if (auto position = CurrentGame().level.loaded().namedPosition("NP_Hotstuff3")) {
        SpawnMonsters(0x222, 1, &*position, 1, true, false);
    }
    return {.run_action = true};
}

/* "TimeDorado": give every existing monster group of species 0xfc a fresh
   member. */
TriggerDecision Rift1TimeDorado(const ScriptContext& ctx, Trigger* pTrigger)
{
    W8MonsterGroup* previous;

    previous = ctx.world->population->findSpeciesGroup(0xfc, 0);
    while (previous != 0) {
        ctx.game.giveBirthToMonster(previous);
        previous = ctx.world->population->findSpeciesGroup(0xfc, previous);
    }
    return {.run_action = true};
}

class Rift1Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        bindTrigger("Fireantspawn", Rift1Fireantspawn);
        bindTrigger("Sexspawn", Rift1Sexspawn);
        bindTrigger("Hotstuff", Rift1Hotstuff);
        bindTrigger("Gate", Rift1Gate);
        bindTrigger("AshLock", Rift1AshLock);
        bindTrigger("TimeDorado", [this](Trigger* trigger) -> TriggerDecision {
            return Rift1TimeDorado(ctx, trigger);
        });
    }
};

} // namespace

std::unique_ptr<LevelScript> MakeRift1Script(const ScriptContext& context)
{
    return std::make_unique<Rift1Script>(context);
}
