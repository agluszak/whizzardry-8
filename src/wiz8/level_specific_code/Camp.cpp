#include "wiz8/level_specific_code/Camp.h"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/save_game.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

/* Level Specific Code for Camp.LVL (level 0x10) — the Rapax away camp.

   Attribution evidence: this TU sits in the unanchored gap between the
   Trynnie2.cpp and Trynnie1.cpp intervals (0x004DA610..0x004DA650), after the
   RapaxMainFloor/RapaxUpperFloor clusters; the level-0x10 script binds
   prisondoor06/04/03. The original file name is not anchored by an assertion
   path string. */

namespace {

class CampScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;
};

void CampScript::enter()
{
    if (ctx.game.testFact(W8_FACT_PARTY_AT_RAC) == 0) {
        LoadAwayCampChest(world());
        SetFact(W8_FACT_PARTY_AT_RAC, 0, false);
    }
    // "prisondoor06": clear fact 0x1e9.
    bindTrigger("prisondoor06", [](Trigger*) {
        SetFact(W8_FACT_RAPAX_QUEEN_LOCKED_UP, 0, false);
        return true;
    });
    // "prisondoor04": clear fact 0x7d.
    bindTrigger("prisondoor04", [](Trigger*) {
        SetFact(W8_FACT_RODAN_LOCKED_IN_CAGE, 0, false);
        return true;
    });
    // "prisondoor03": clear fact 0x3e.
    bindTrigger("prisondoor03", [](Trigger*) {
        SetFact(W8_FACT_DRAZIC_LOCKED_IN_CAGE, 0, false);
        return true;
    });
}

} // namespace

std::unique_ptr<LevelScript> MakeCampScript(const ScriptContext& context)
{
    return std::make_unique<CampScript>(context);
}

/* Fill the away camp chest from the level-0x26 item records stored in the
   current save. Records that are not world-persistent move into the chest
   trigger's container item; persistent records are flattened back into the
   load vector (so their own groups are visited too) and then freed. */
void LoadAwayCampChest(World& world)
{
    std::vector<std::unique_ptr<W8WorldItem>> items;
    Prop* pChest;
    Trigger* pTrigger;
    W8WorldItem* pContainer;
    int index;

    pChest = FindPropByName(&world, "AwayCampChest");
    if (pChest == 0) throw std::runtime_error("camp level lacks AwayCampChest");
    pTrigger = pChest->GetTrigger();
    if (pTrigger == 0) throw std::runtime_error("AwayCampChest has no trigger");
    pContainer = pTrigger->GetOrCreateItemGroup(true);
    assert(pContainer != 0);
    if (!LoadSavedLevelItems(0x26, items)) return;
    for (index = 0; index < static_cast<int>(items.size()); ++index) {
        auto item = std::move(items[index]);
        auto contents = std::move(item->contents);
        if (ItemInfoIsWorldPersistent(item.get())) {
            for (auto& contained : contents) items.push_back(std::move(contained));
        } else {
            pContainer->contents.push_back(std::move(item));
            for (auto& contained : contents) pContainer->contents.push_back(std::move(contained));
        }
    }
}
