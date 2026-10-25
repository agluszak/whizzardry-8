#include "wiz8/level_specific_code/Monastery1.h"

#include <memory>
#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/location_variables.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/level_session.h"
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
    TriggerDecision coffinlidg(Trigger* trigger);
    TriggerDecision wheelStar(Trigger* trigger);
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
TriggerDecision OnRoachTriggerActivated(Trigger* trigger)
{
    if (GetLocationVarIDByName("RoachesSpawned") != -1) {
        return {.run_action = false};
    }
    auto first = CurrentGame().level.loaded().namedPosition("location_roach01");
    if (!first) return {.run_action = false};
    auto second = CurrentGame().level.loaded().namedPosition("location_roach02");
    if (!second) return {.run_action = false};
    auto third = CurrentGame().level.loaded().namedPosition("location_roach03");
    if (!third) return {.run_action = false};
    first->z += 1000.0f;
    second->z += 1000.0f;
    third->z += 1000.0f;
    SpawnMonsters(0x138, 3, &*first, 1, false, false);
    SpawnMonsters(0x138, 3, &*second, 1, false, false);
    SpawnMonsters(0x138, 3, &*third, 1, false, false);
    CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "RoachesSpawned", 1);
    return {.run_action = true};
}

/* Activation callback on spider_trigger: every activation spawns six spiders
   (kind 0x19c) at the spiderman entity when it exists. */
TriggerDecision OnSpiderTriggerActivated(Trigger* trigger)
{
    if (auto position = CurrentGame().level.loaded().namedPosition("spiderman")) {
        SpawnMonsters(0x19c, 6, &*position, 1, false, false);
    }
    return {.run_action = true};
}

/* Activation callback on Bartrigger: clears the armed bit of the Textforbar
   trigger so the bar text stops repeating. */
TriggerDecision OnBarTriggerActivated(Trigger* trigger)
{
    Trigger* textTrigger = FindTriggerByName("Textforbar");

    if (textTrigger != 0) {
        textTrigger->flags &= ~W8_TRIGGER_ENABLED;
    }
    return {.run_action = true};
}

/* Activation callback on Coffinlide: the first activation spawns the head
   (kind 0x12d) at the Head entity and records HeadSpawned. */
TriggerDecision OnCoffinlideActivated(Trigger* trigger)
{
    if (auto point = GetLocationVarIDByName("HeadSpawned") == -1
                         ? CurrentGame().level.loaded().namedPosition("Head")
                         : std::nullopt) {
        SpawnMonsters(0x12d, 1, &*point, 1, true, false);
        CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "HeadSpawned", 1);
    }
    return {.run_action = true};
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
TriggerDecision Monastery1Script::coffinlidg(Trigger* trigger)
{
    glm::vec3 position;

    position = world().party.eye();
    PointCastSpell(position, W8_SPELL_NOXIOUS_FUMES, 3);
    return {.run_action = true};
}

/* Activation callback on wheel_star: consumes the held star key (item 0x24c);
   anything else in hand plays level message 0x22 and leaves the trigger
   armed. */
TriggerDecision Monastery1Script::wheelStar(Trigger* trigger)
{
    if (ctx.inventory.heldItemId() != 0x24c) {
        ctx.game.queueLevelMessage(0x22);
        return {.run_action = false, .handled = true};
    }
    ctx.inventory.removeById(ctx.inventory.heldItemId(), false);
    return {.run_action = true};
}

} // namespace

std::unique_ptr<LevelScript> MakeMonastery1Script(const ScriptContext& context)
{
    return std::make_unique<Monastery1Script>(context);
}
