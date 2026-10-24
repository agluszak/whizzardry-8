#include "wiz8/level_specific_code/Trynnie2.h"

#include <memory>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fonts.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/Trynnie1.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_screens/MGSTextBox.h"
#include "wiz8/location_variables.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/string_database.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

class Trigger;

/* Level Specific Code\Trynnie2.cpp (level 0x1a).

   Attribution evidence: 0x004D9E60 passes this file's path string to
   MonsterGetIndexByLocationID. The level-0x1a script binds the surrounding
   cluster
   (GoodaVine_A/B, Give_Zulu, Meat_Maker, Meat_Box, URN_Trigger_01-04) and runs
   the Trynnie2Killed location-variable sync. */

namespace {

bool MeatMaker(Trigger* pTrigger);
bool UrnTrigger(Trigger* pTrigger);
void EnsureKilledVar(World& world);

class Trynnie2Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    bool goodaVineA(Trigger* pTrigger);
    bool goodaVineB(Trigger* pTrigger);
    bool giveZulu(Trigger* pTrigger);
    bool meatBox(Trigger* pTrigger);
    // A new item in the hand, refused while the hand holds something.
    bool putOnCursor(int item_id);
};

void Trynnie2Script::enter()
{
    bindTrigger("GoodaVine_A", &Trynnie2Script::goodaVineA);
    bindTrigger("GoodaVine_B", &Trynnie2Script::goodaVineB);
    bindTrigger("Meat_Maker", MeatMaker);
    bindTrigger("Meat_Box", &Trynnie2Script::meatBox);
    bindTrigger("Give_Zulu", &Trynnie2Script::giveZulu);
    bindTrigger("URN_Trigger_01", UrnTrigger);
    bindTrigger("URN_Trigger_02", UrnTrigger);
    bindTrigger("URN_Trigger_03", UrnTrigger);
    bindTrigger("URN_Trigger_04", UrnTrigger);
    EnsureKilledVar(world());
}

/* Fact 0x229 records that the Trynnie2 group was wiped out; mirror it into the
   Trynnie2Killed location variable the first time this level sees it, so the
   world-side check has a stable flag to read. */
void EnsureKilledVar(World& world)
{
    if (CurrentGame().testFact(W8_FACT_TRYNNIE_SHAMAN_DIES) == 1) {
        if (GetLocationVarIDByName("Trynnie2Killed") == -1) {
            KillTrynnieGroups(world);
            CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "Trynnie2Killed", 1);
        }
    }
}

bool Trynnie2Script::putOnCursor(int item_id)
{
    if (ctx.inventory.held()) return false;
    auto item = MakeItem(Data(), ctx.game.rng, item_id, true, true, false);
    return ctx.inventory.hold(item);
}

/* "GoodaVine_A": place the vine item on the cursor, refusing while the cursor
   already holds an item. */
bool Trynnie2Script::goodaVineA(Trigger* pTrigger)
{
    return putOnCursor(0x16d);
}

/* "GoodaVine_B": place the second vine item on the cursor, refusing while the
   cursor already holds an item. */
bool Trynnie2Script::goodaVineB(Trigger* pTrigger)
{
    return putOnCursor(0x16e);
}

/* "Give_Zulu": place item 0x1b3 on the cursor, refusing while the cursor
   already holds an item. */
bool Trynnie2Script::giveZulu(Trigger* pTrigger)
{
    return putOnCursor(0x1b3);
}

/* "Meat_Maker": spawn the meat item at the Meat_Position entity. */
bool MeatMaker(Trigger* pTrigger)
{
    auto& game = CurrentGame();
    auto& owner = game.level.loaded();
    glm::vec3 entity_position;
    glm::vec3 position;
    W8WorldItem* item;

    if (FindEntityByName("Meat_Position", &entity_position, 0, 0)) {
        position = entity_position;
        item = owner.adoptItem(game.spawnWorldItem(owner, 0x1b4, &position,
                                                   W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
        if (item != 0) {
            owner.activateItem(game.data, game.rng, item);
        }
    }
    return true;
}

/* "Meat_Box": feeding Hogar (monster 0x1d4) plain meat starts MoveHogar.msf;
   drugged meat starts MoveHogarDrugged.msf and pays the 500-point reward
   once. */
bool Trynnie2Script::meatBox(Trigger* pTrigger)
{
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int item_id;

    group = FindFirstMonsterByID(0x1d4);
    if (group == 0 || !(ctx.inventory.held())) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x964]);
        return true;
    }
    item_id = ctx.inventory.heldItemId();
    if (item_id != 0x1b4 && item_id != 0x1c2) {
        ShowNotice(W8_FONT_PALETTE_TEXT_BOX, Data().strings[0x964]);
        return true;
    }
    info = FindMonster(group->leader_location_id);
    if (info->highest_condition >= W8_CONDITION_ASLEEP) {
        return true;
    }
    if (item_id == 0x1b4) {
        info->entity->SetScript("MoveHogar.msf", true);
    } else {
        info->entity->SetScript("MoveHogarDrugged.msf", true);
        if (GetLocationVarIDByName("HogarDruggedGivenExp") == -1) {
            AwardPartyExperience(500, false);
            CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "HogarDruggedGivenExp",
                                                       1);
        }
    }
    (void)ctx.inventory.take({W8ItemStorage::Hand});
    BeginScriptedWorldAction();
    return true;
}

/* "URN_Trigger_01..04": once more than two of the four urn variables are set
   the pagoda generator is shut down; either way a level message reports the
   outcome. */
bool UrnTrigger(Trigger* pTrigger)
{
    MonGen* generator;
    unsigned int count;

    count = 0;
    if (GetLocationVarValueByName("Urn1") > 0) {
        ++count;
    }
    if (GetLocationVarValueByName("Urn2") > 0) {
        ++count;
    }
    if (GetLocationVarValueByName("Urn3") > 0) {
        ++count;
    }
    if (GetLocationVarValueByName("Urn4") > 0) {
        ++count;
    }
    if (count >= 3) {
        generator = FindMonGenByName("PAGODA");
        if (generator != 0) {
            generator->generation_enabled = false;
        }
        ShowLevelMessage(0x14);
        return true;
    }
    ShowLevelMessage(0x13);
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeTrynnie2Script(const ScriptContext& context)
{
    return std::make_unique<Trynnie2Script>(context);
}

/* Use-item action for the Zulu (0x1b3) and item 0x1c3: hand the use to the
   type-7 world cursor node covering the camera position. An unhandled use
   reports the refusal string; the first handled use only latches the status
   flag, later uses show a second string and spawn the Mystical Shaman (0xec)
   at NP_MysticalShaman. Either way the used item is consumed into a scratch
   instance. The retail DispatchWorldCursorNodeCommand call passes only two
   arguments; arg is never dereferenced for command 8, so the callee reads an
   uninitialized stack slot there. */
bool Trynnie2UseItem(const ScriptContext& context, W8ItemLocation location)
{
    const W8ItemInstance* item = context.inventory.itemAt(location);
    glm::vec3 position;

    if (item->iItemNo != 0x1b3 && item->iItemNo != 0x1c3) {
        return false;
    }
    if (DispatchWorldCursorNodeCommand(0, 8, 0) == 0) {
        ShowString(Data().strings[0x966]);
        return true;
    }
    if (item->iItemNo == 0x1b3) {
        ShowString(Data().strings[0x968]);
    } else {
        ShowString(Data().strings[0x967]);
    }
    if (!context.status.use_item_latch) {
        context.status.use_item_latch = true;
    } else {
        ShowString(Data().strings[0x969]);
        if (FindEntityByName("NP_MysticalShaman", &position, 0, 0)) {
            SpawnMonsters(0xec, 1, &position, 0, true, false);
        }
    }
    context.inventory.take(location);
    return true;
}
