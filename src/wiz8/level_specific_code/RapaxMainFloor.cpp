#include "wiz8/level_specific_code/RapaxMainFloor.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/location_variables.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

/* Level Specific Code for RapaxMainFloor.LVL (level 0x12) — the Rapax castle
   main floor altar and platform sequence.

   Attribution evidence: this TU sits in the unanchored gap between the
   Trynnie2.cpp and Trynnie1.cpp intervals (0x004DA3C0..0x004DA5D0); its
   string block (NP_Al-Sedexus, LezboDemonAppeared, RAM03/07/11 and the cursed
   item entities at 0x0061315c..0x006131b4) is emitted between the same TUs'
   literals. The level-0x12 script binds AltarBox and platformtrigger/01/02.
   The original file name is not anchored by an assertion path string. */

namespace {

void SetRamTriggerEnabled(const char* name, bool enabled)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger != 0) {
        if (enabled) {
            trigger->flags |= W8_TRIGGER_ENABLED;
        } else {
            trigger->flags &= ~W8_TRIGGER_ENABLED;
        }
    }
}

/* Spawns a pulsing, rotating item at a named entity and activates it, if the entity exists. */
void SpawnActivatedItemAt(const char* entity, int item_id)
{
    glm::vec3 entity_position;
    glm::vec3 position;

    if (FindEntityByName(entity, &entity_position, 0, 0)) {
        position = entity_position;
        auto& items = GetWorld()->items;
        items.activate(items.add(
            items.spawn(item_id, position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE)));
    }
}

/* "platformtrigger": drop the three cursed Rapax items (0x1fd robe, 0x1fe
   helm, 0x1ff dagger) at their NP_C* entities and enable RAM03. */
bool Platform(Trigger* pTrigger)
{
    SpawnActivatedItemAt("NP_CRobe", 0x1fd);
    SpawnActivatedItemAt("NP_CHelm", 0x1fe);
    SpawnActivatedItemAt("NP_CDagger", 0x1ff);
    SetRamTriggerEnabled("RAM03", true);
    return true;
}

/* "platformtrigger01": disable RAM03 and enable RAM07. */
bool Platform01(Trigger* pTrigger)
{
    SetRamTriggerEnabled("RAM03", false);
    SetRamTriggerEnabled("RAM07", true);
    return true;
}

/* "platformtrigger02": disable RAM07 and enable RAM11. */
bool Platform02(Trigger* pTrigger)
{
    SetRamTriggerEnabled("RAM07", false);
    SetRamTriggerEnabled("RAM11", true);
    return true;
}

class RapaxMainFloorScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        bindTrigger("AltarBox", &RapaxMainFloorScript::altarBox);
        bindTrigger("platformtrigger", Platform);
        bindTrigger("platformtrigger01", Platform01);
        bindTrigger("platformtrigger02", Platform02);
    }

private:
    bool altarBox(Trigger* pTrigger);
};

/* "AltarBox": the first activation while the fact-0x14c gate is clear marks
   LezboDemonAppeared, spawns Al-Sedexus (0x124) at NP_Al-Sedexus and arms the
   trigger's follow-up action 0x1fc. */
bool RapaxMainFloorScript::altarBox(Trigger* pTrigger)
{
    glm::vec3 position;
    int var_id;

    if (!ctx.status.rpc_active) {
        var_id = GetLocationVarIDByName("LezboDemonAppeared");
        if (var_id != -1) {
            var_id = GetLocationVarValueByName("LezboDemonAppeared");
            if (var_id == 1) {
                return true;
            }
            SetTriggerVariableByName("LezboDemonAppeared", 1);
        } else {
            CreateLocationVar("LezboDemonAppeared", 1);
        }
        if (FindEntityByName("NP_Al-Sedexus", &position, 0, 0)) {
            SpawnMonsters(0x124, 1, &position, 0, true, false);
        }
        pTrigger->required_item_id = 0x1fc;
    }
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeRapaxMainFloorScript(const ScriptContext& context)
{
    return std::make_unique<RapaxMainFloorScript>(context);
}
