#include "wiz8/level_specific_code/RapaxUpperFloor.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/location_variables.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/string_database.h"
#include "wiz8/world/world.h"

/* Level Specific Code for RapaxUpperFloor.LVL (level 0x13) — the Rapax
   castle upper floor treasure vault.

   Attribution evidence: this TU sits in the unanchored gap between the
   Trynnie2.cpp and Trynnie1.cpp intervals (0x004DA110..0x004DA2E0); its
   string block (NP_SlipItem, TMakeTreasure*, TDoor1, TdoorOpen at
   0x00613118..0x00613150) is emitted between the same TUs' literals. The
   level-0x13 script binds the AirBox and DoorDone triggers. The original
   file name is not anchored by an assertion path string. */

namespace {

void RunTreasureDoor()
{
    RunNamedTrigger("TDoor1", -1);
}

/* "DoorDone": when TMakeTreasure is armed and TdoorOpen is still closed,
   deliver the stashed TMakeTreasureNumber item at NP_SlipItem and run TDoor1.
   Returns 0 once the treasure is delivered. */
bool DoorDone(Trigger* pTrigger)
{
    glm::vec3 entity_position;
    glm::vec3 position;
    int item_id;

    if (GetLocationVarIDByName("TMakeTreasure") != -1) {
        if (GetLocationVarValueByName("TMakeTreasure") != 0) {
            if (GetLocationVarIDByName("TdoorOpen") != -1) {
                if (GetLocationVarValueByName("TdoorOpen") == 0) {
                    item_id = GetLocationVarValueByName("TMakeTreasureNumber");
                    if (FindEntityByName("NP_SlipItem", &entity_position, 0, 0)) {
                        position = entity_position;
                        auto& items = GetWorld()->items;
                        items.activate(items.add(items.spawn(
                            item_id, position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE)));
                    }
                    RunTreasureDoor();
                    return false;
                }
            }
        }
    }
    return true;
}

class RapaxUpperFloorScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        bindTrigger("AirBox", &RapaxUpperFloorScript::airBox);
        bindTrigger("DoorDone", DoorDone);
    }

private:
    bool airBox(Trigger* pTrigger);
};

/* "AirBox": while the cursor holds an item, placing one of the four vault
   items (0x1cb/0x1cd/0x1cc/0x1ce) converts it to a treasure item, drops it at
   the NP_SlipItem entity and runs the TDoor1 trigger. When TdoorOpen is
   already set the treasure is deferred through the TMakeTreasure/
   TMakeTreasureNumber location vars for DoorDone to deliver. */
bool RapaxUpperFloorScript::airBox(Trigger* pTrigger)
{
    glm::vec3 entity_position;
    glm::vec3 position;
    int item_id;
    int var_id;

    if ((ctx.party.held.item.iItemNo != -1)) {
        item_id = GetItemInHand(ctx.party);
        if (item_id == 0x1cb || item_id == 0x1cd || item_id == 0x1cc || item_id == 0x1ce) {
            SetFact(W8_FACT_RAPAX_TREASURY_RAIDED, 1, false);
            SetFactionDispositionBand(0x12, W8_FACTION_HOSTILE);
            (void)TakeInventoryItem(ctx.game, {W8ItemStorage::Hand});
            if (item_id == 0x1cb) {
                item_id = 0x1c7;
            } else if (item_id == 0x1cd) {
                item_id = 0x11f;
            } else {
                item_id = (item_id == 0x1cc) ? 0x1ca : 0x51;
            }
            var_id = GetLocationVarIDByName("TdoorOpen");
            if (var_id == -1 || GetLocationVarValueByName("TdoorOpen") == 0) {
                if (FindEntityByName("NP_SlipItem", &entity_position, 0, 0)) {
                    position = entity_position;
                    auto& items = GetWorld()->items;
                    items.activate(items.add(items.spawn(
                        item_id, position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE)));
                }
                RunTreasureDoor();
                return true;
            }
            RunTreasureDoor();
            var_id = GetLocationVarIDByName("TMakeTreasure");
            if (var_id == -1) {
                CreateLocationVar("TMakeTreasure", 1);
            } else {
                SetTriggerVariableByName("TMakeTreasure", 1);
            }
            var_id = GetLocationVarIDByName("TMakeTreasureNumber");
            if (var_id == -1) {
                CreateLocationVar("TMakeTreasureNumber", item_id);
                return true;
            }
            SetTriggerVariableByName("TMakeTreasureNumber", item_id);
            return true;
        }
        ShowString(Data().strings[0x96a]);
    }
    ShowString(Data().strings[0x964]);
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeRapaxUpperFloorScript(const ScriptContext& context)
{
    return std::make_unique<RapaxUpperFloorScript>(context);
}
