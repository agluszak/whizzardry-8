#include "wiz8/level_specific_code/MtGigasTop.h"

#include <memory>

#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/script_context.h"
#include "wiz8/utility.h"

/* Level Specific Code\MtGigasTop.cpp (level 0x0f).

   Attribution evidence: the level-0x0f script binds all five callbacks, the
   level the Levels.cpp table names "MtGigasTop"; the block sits between the
   MtGigasOuter (0x0e) callbacks and the Monastery2 (9) anchors. */

namespace {

/* Activation callback on _VOC_EWAXXCANNON1: hands the cannon NPC (kind 0x58)
   the item the player is holding, if any, as script argument -1. */
bool EwaxxCannon1(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x58);
    return true;
}

/* Activation callback on EwaxxLanding: same held-item handoff to the landing
   NPC (kind 0x7f). */
bool EwaxxLanding(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x7f);
    return true;
}

/* Activation callback on catchCord: records fact 0x177 once the party grabs
   the catch cord. */
bool CatchCord(Trigger* trigger)
{
    SetFact(W8_FACT_TMISSION_KILL_UMPANI_CORD_SEVERED, 1, false);
    return true;
}

/* Activation callback on _VOC_EWAXXTOPDOOR2: hands the top-door NPC (kind
   0x60) the held item, if any, as script argument -1. */
bool EwaxxTopDoor2(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x60);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on painActivatorTrigger: rolls 1d40+30 health damage
   against the party; the trigger stays armed (returns false). */
bool PainActivator(Trigger* trigger)
{
    W8Dice dice;

    SetDice(&dice, 1, 0x28, 0x1e);
    ApplyRolledHealthChangeToParty(&dice, 0, 1);
    return false;
}

class MtGigasTopScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        bindTrigger("_VOC_EWAXXCANNON1", EwaxxCannon1);
        bindTrigger("EwaxxLanding", EwaxxLanding);
        bindTrigger("catchCord", CatchCord);
        bindTrigger("painActivatorTrigger", PainActivator);
        bindTrigger("_VOC_EWAXXTOPDOOR2", EwaxxTopDoor2);
    }
};

} // namespace

std::unique_ptr<LevelScript> MakeMtGigasTopScript(const ScriptContext& context)
{
    return std::make_unique<MtGigasTopScript>(context);
}
