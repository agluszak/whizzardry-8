#include "wiz8/level_specific_code/SavantTower.h"

#include <memory>

#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

/* Level Specific Code\SavantTower.cpp (level 0x24, the Savant's tower on
   Ascension Peak).

   Attribution evidence: the level-0x24 script binds the Triangle/Circle/
   Square/Star shape triggers and ButtonBlocker against these callbacks, and
   the retail string pool places "DeactivatingBomb" in its own contribution
   between Ascension.cpp's and the ConnectiveTissue Liche block's. */

namespace {

/* The shape callback (Triangle/Circle/Square/Star): on the first step latch
   DeactivatingBomb and queue the NPC-0x46 script notice. */
bool Shape(Trigger* pTrigger)
{
    if (GetLocationVarIDByName("DeactivatingBomb") == -1) {
        CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "DeactivatingBomb", 1);
        QueueNpcScriptNotice(CurrentGame().story.npcOfKind(0x46), 0, 0, false, 0);
    }
    return true;
}

class SavantTowerScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    bool buttonBlocker(Trigger* pTrigger);
};

/* The ButtonBlocker prop tracks fact 0x156. */
void SavantTowerScript::enter()
{
    Prop* prop = FindPropByName(&world(), "ButtonBlocker");
    if (prop != 0) {
        if (ctx.game.testFact(W8_FACT_DS_BOMB_DEACTIVATED) != 0) {
            prop->SetActivationState(true);
        } else {
            prop->SetActivationState(false);
        }
    }
    bindTrigger("Triangle", Shape);
    bindTrigger("Circle", Shape);
    bindTrigger("Square", Shape);
    bindTrigger("Star", Shape);
    bindTrigger("ButtonBlocker", &SavantTowerScript::buttonBlocker);
}

/* The ButtonBlocker callback: pin the blocker prop, queue the NPC-0x46
   script notice and latch fact 0x156. */
bool SavantTowerScript::buttonBlocker(Trigger* pTrigger)
{
    Prop* blocker = FindPropByName(&world(), "ButtonBlocker");

    if (blocker != 0) {
        blocker->SetActivationState(true);
    }
    QueueNpcScriptNotice(CurrentGame().story.npcOfKind(0x46), 0, 1, false, 0);
    SetFact(W8_FACT_DS_BOMB_DEACTIVATED, 1, false);
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeSavantTowerScript(const ScriptContext& context)
{
    return std::make_unique<SavantTowerScript>(context);
}
