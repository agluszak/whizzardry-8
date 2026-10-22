#include "wiz8/level_specific_code/Monastery1.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/location_variables.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

/* Level Specific Code\Monastery1.cpp (level 8).

   Attribution evidence: the level-8 script runs 0x004DC8D0 and binds the
   roach_trigger, spider_trigger, Bartrigger and Coffinlide callbacks; the
   function itself is bar-trigger housekeeping for that same set. */

namespace {

class Monastery1Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    bool coffinlidg(Trigger* trigger);
    bool wheelStar(Trigger* trigger);
};

void ClearTextForBarTrigger()
{
    Trigger* trigger = FindTriggerByName("Bartrigger");
    if (trigger != 0 && trigger->state_index == 1) {
        trigger = FindTriggerByName("Textforbar");
        if (trigger != 0) {
            trigger->flags &= ~W8_TRIGGER_ENABLED;
        }
    }
}

/* Activation callback on roach_trigger: the first activation drops three
   roaches (kind 0x138) 1000 units above each roach location and records
   RoachesSpawned so later activations do nothing. */
bool OnRoachTriggerActivated(Trigger* trigger)
{
    glm::vec3 positions[3];

    if (GetLocationVarIDByName("RoachesSpawned")) {
        return false;
    }
    if (!FindEntityByName("location_roach01", &positions[0], 0, 0)) {
        return false;
    }
    if (!FindEntityByName("location_roach02", &positions[1], 0, 0)) {
        return false;
    }
    if (!FindEntityByName("location_roach03", &positions[2], 0, 0)) {
        return false;
    }
    positions[0].z += 1e+03f;
    positions[1].z += 1e+03f;
    positions[2].z += 1e+03f;
    SpawnMonsters(0x138, 3, &positions[0], 1, false, false);
    SpawnMonsters(0x138, 3, &positions[1], 1, false, false);
    SpawnMonsters(0x138, 3, &positions[2], 1, false, false);
    CreateLocationVar("RoachesSpawned", 1);
    return true;
}

/* Activation callback on spider_trigger: every activation spawns six spiders
   (kind 0x19c) at the spiderman entity when it exists. */
bool OnSpiderTriggerActivated(Trigger* trigger)
{
    glm::vec3 position;

    if (FindEntityByName("spiderman", &position, 0, 0)) {
        SpawnMonsters(0x19c, 6, &position, 1, false, false);
    }
    return true;
}

/* Activation callback on Bartrigger: clears the armed bit of the Textforbar
   trigger so the bar text stops repeating. */
bool OnBarTriggerActivated(Trigger* trigger)
{
    Trigger* textTrigger = FindTriggerByName("Textforbar");

    if (textTrigger != 0) {
        textTrigger->flags &= ~W8_TRIGGER_ENABLED;
    }
    return true;
}

/* Activation callback on Coffinlide: the first activation spawns the head
   (kind 0x12d) at the Head entity and records HeadSpawned. */
bool OnCoffinlideActivated(Trigger* trigger)
{
    glm::vec3 position;

    if (!GetLocationVarIDByName("HeadSpawned") && FindEntityByName("Head", &position, 0, 0)) {
        SpawnMonsters(0x12d, 1, &position, 1, true, false);
        CreateLocationVar("HeadSpawned", 1);
    }
    return true;
}

void Monastery1Script::enter()
{
    ClearTextForBarTrigger();
    bindTrigger("roach_trigger", OnRoachTriggerActivated);
    bindTrigger("spider_trigger", OnSpiderTriggerActivated);
    bindTrigger("Bartrigger", OnBarTriggerActivated);
    bindTrigger("Coffinlide", OnCoffinlideActivated);
    bindTrigger("Coffinlidg", &Monastery1Script::coffinlidg);
    bindTrigger("wheel_star", &Monastery1Script::wheelStar);
}

/* Activation callback on Coffinlidg: casts spell 0x2a at power 3 onto the
   camera position. */
bool Monastery1Script::coffinlidg(Trigger* trigger)
{
    glm::vec3 position;

    position = cameraLocation(world());
    PointCastSpell(position, W8_SPELL_NOXIOUS_FUMES, 3);
    return true;
}

/* Activation callback on wheel_star: consumes the held star key (item 0x24c);
   anything else in hand plays level message 0x22 and leaves the trigger
   armed. */
bool Monastery1Script::wheelStar(Trigger* trigger)
{
    if (ctx.party.held.item.iItemNo != 0x24c) {
        ShowLevelMessage(0x22);
        g_trigger_feedback = true;
        return false;
    }
    RemovePartyItemByID(ctx.game, 0x24c, false);
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeMonastery1Script(const ScriptContext& context)
{
    return std::make_unique<Monastery1Script>(context);
}
