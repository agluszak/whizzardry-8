#include "wiz8/level_specific_code/MtGigasTop.h"

#include "wiz8/data/dice.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/utility.h"

struct W8ItemInstance;
struct W8NpcState;

/* Level Specific Code\MtGigasTop.cpp (level 0x0f).

   Attribution evidence: InitializeLevelMasterFunctions registers all
   five callbacks under case 0x0f, the level the Levels.cpp table names
   "MtGigasTop"; the block sits between the MtGigasOuter (0x0e) callbacks and
   the Monastery2 (9) anchors. */

/* Activation callback on _VOC_EWAXXCANNON1: hands the cannon NPC (kind 0x58)
   the item the player is holding, if any, as script argument -1. */
bool OnEwaxxCannon1Activated(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x58);
    return true;
}

/* Activation callback on EwaxxLanding: same held-item handoff to the landing
   NPC (kind 0x7f). */
bool OnEwaxxLandingActivated(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x7f);
    return true;
}

/* Activation callback on catchCord: records fact 0x177 once the party grabs
   the catch cord. */
bool OnCatchCordActivated(Trigger* trigger)
{
    SetFact(W8_FACT_TMISSION_KILL_UMPANI_CORD_SEVERED, 1, false);
    return true;
}

/* Activation callback on _VOC_EWAXXTOPDOOR2: hands the top-door NPC (kind
   0x60) the held item, if any, as script argument -1. */
bool OnEwaxxTopDoor2Activated(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x60);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on painActivatorTrigger: rolls 1d40+30 health damage
   against the party; the trigger stays armed (returns false). */
bool OnPainActivatorActivated(Trigger* trigger)
{
    W8Dice dice;

    SetDice(&dice, 1, 0x28, 0x1e);
    ApplyRolledHealthChangeToParty(&dice, 0, 1);
    return false;
}
