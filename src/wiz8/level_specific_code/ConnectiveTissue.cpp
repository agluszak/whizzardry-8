#include "wiz8/level_specific_code/ConnectiveTissue.h"

#include <memory>

#include "wiz8/entities/trigger.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/location_variables.h"
#include "wiz8/script_context.h"

/* Level Specific Code\ConnectiveTissue.cpp (level 0x1b, the Arnika-Trynton
   connector map).

   Attribution evidence: the level-0x1b script binds the "Liche" trigger
   against this callback, and the retail string pool gives its
   UNDEAD01-06/LicheDead names a contiguous contribution between the
   SavantTower and Arnika translation units. Level 0x1b is the
   Arnika_Trynton map in the retail level table. */

namespace {

/* Turns on a named spawn generator when the level defines it. */
void EnableMonGenerator(const char* name)
{
    if (MonGen* generator = FindMonGenByName(name); generator != 0) {
        generator->flags |= 1;
    }
}

class ConnectiveTissueScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override { bindTrigger("Liche", &ConnectiveTissueScript::liche); }

private:
    bool liche(Trigger* pTrigger);
};

/* The "Liche" crypt callback: without item 0x15d in hand and before the
   trigger's action has fired it only answers with level message 6. Otherwise
   it answers with message 7, marks the lich dead (1000 experience the first
   time) and enables the six UNDEAD spawn generators. */
bool ConnectiveTissueScript::liche(Trigger* pTrigger)
{
    if (ctx.inventory.heldItemId() != 0x15d && pTrigger->action == 0) {
        ShowLevelMessage(6);
        g_trigger_feedback = true;
        return false;
    }
    ShowLevelMessage(7);
    if (GetLocationVarIDByName("LicheDead") == -1) {
        CreateLocationVar("LicheDead", 1);
        AwardPartyExperience(0x3e8, false);
    }
    EnableMonGenerator("UNDEAD01");
    EnableMonGenerator("UNDEAD02");
    EnableMonGenerator("UNDEAD03");
    EnableMonGenerator("UNDEAD04");
    EnableMonGenerator("UNDEAD05");
    EnableMonGenerator("UNDEAD06");
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeConnectiveTissueScript(const ScriptContext& context)
{
    return std::make_unique<ConnectiveTissueScript>(context);
}
