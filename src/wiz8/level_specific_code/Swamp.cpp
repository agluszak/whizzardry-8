#include "wiz8/level_specific_code/Swamp.h"

#include <initializer_list>
#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

struct W8NpcState;

/* Level Specific Code\Swamp.cpp (level 0x18).

   Attribution evidence: 0x004DAA70 passes this file's path string to
   MonsterGetIndexByLocationID. The level-0x18 script binds the surrounding
   cluster (oil_pool, gas_trig_plane01-07, onelid, fire_trig_plane01-07). */

namespace {

/* The master command that re-arms the item drop's wait. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

/* "onelid": once fact 0x33 is set and an npc of kind 0x1e exists, advance the
   script facts - set 0x39 and clear 0x33. */
TriggerDecision Onelid(Trigger* pTrigger)
{
    if (CurrentGame().testFact(W8_FACT_CROCK_KIDNAPPED_PLAYER) != 0 && FindNpcOfKind(0x1e) != 0) {
        CurrentGame().setFact(W8_FACT_CROCK_PLAYER_RETURNED_TO_PARTY, 1);
        CurrentGame().setFact(W8_FACT_CROCK_KIDNAPPED_PLAYER, 0);
    }
    return {.run_action = true};
}

class SwampScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    TriggerDecision oilPool(Trigger* pTrigger);
    TriggerDecision castPlaneSpell(Trigger* pTrigger, int spell_id);
    TriggerDecision gasFireSpawn(Trigger* pTrigger);
    bool gasFireItemDrop(int command);

    // Resolved when used: the monster can be killed or removed while the party talks to it.
    int spawned_monster_id_ = 0;
};

void SwampScript::enter()
{
    /* "gas_trig_plane01-07": while fact 0x16d is unset the trigger defers to the
       one-shot spawner; once set, it casts spell 0x2a at power 4 on the camera
       position. */
    const Callback gas_plane = [this](Trigger* pTrigger) -> TriggerDecision {
        return castPlaneSpell(pTrigger, W8_SPELL_NOXIOUS_FUMES);
    };
    // "fire_trig_plane01-07": same fact gate as the gas planes, but casts spell 0x24.
    const Callback fire_plane = [this](Trigger* pTrigger) -> TriggerDecision {
        return castPlaneSpell(pTrigger, W8_SPELL_FIREBALL);
    };
    for (const char* name :
         {"gas_trig_plane01", "gas_trig_plane02", "gas_trig_plane03", "gas_trig_plane04",
          "gas_trig_plane05", "gas_trig_plane06", "gas_trig_plane07"}) {
        bindTrigger(name, gas_plane);
    }
    bindTrigger("oil_pool", &SwampScript::oilPool);
    bindTrigger("onelid", Onelid);
    for (const char* name :
         {"fire_trig_plane01", "fire_trig_plane02", "fire_trig_plane03", "fire_trig_plane04",
          "fire_trig_plane05", "fire_trig_plane06", "fire_trig_plane07"}) {
        bindTrigger(name, fire_plane);
    }
}

/* "oil_pool": item 0x2d0 held over the pool is converted to item 0x15e and the
   shared item-consumed flag is raised. Always returns 0, so the trigger stays
   armed for another try. */
TriggerDecision SwampScript::oilPool(Trigger* pTrigger)
{
    bool handled = false;
    if ((ctx.inventory.held()) && ctx.inventory.heldItemId() == 0x2d0) {
        (void)ctx.inventory.take({W8ItemStorage::Hand});
        ctx.inventory.replaceHeld(MakeItem(Data(), ctx.game.rng, 0x15e, false, true, false));
        handled = true;
    }
    return {.run_action = false, .handled = handled};
}

/* Casts a spell at the camera position once the savant party has met; before
   that, spawns the gas fire. */
TriggerDecision SwampScript::castPlaneSpell(Trigger* pTrigger, int spell_id)
{
    glm::vec3 position;

    if (ctx.game.testFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET) == 0) {
        gasFireSpawn(pTrigger);
        return {.run_action = false};
    }
    position = world().party.eye();
    PointCastSpell(position, spell_id, 4);
    return {.run_action = true};
}

/* The one-shot behind the gas and fire trigger planes, armed while fact 0x16d
   is unset: spawn monster 0x1b5 at the triggering plane's center, fade it in
   under the camera, arm the item-drop master function, and hand the
   location-0xa3 NPC binding a script notice. */
/* 0x004DAA8A is a split entry Ghidra carved out of this body: it resumes at
   the SetFact call after the fact-0x16d early return, not a separate
   authored function. */
TriggerDecision SwampScript::gasFireSpawn(Trigger* pTrigger)
{
    glm::vec3 center;
    glm::vec3 position;
    glm::vec3 mapped;
    glm::vec3 look_target;
    W8MonsterGroup* group;
    W8MonsterInfo* monster_info;
    W8NpcState* npc;
    int index;

    if (ctx.game.testFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET) != 0) {
        return {.run_action = false};
    }
    ctx.game.setFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET, 1);
    ResetInactiveLevelDataVectors();
    center =
        glm::vec3((pTrigger->representation_vectors[0].x + pTrigger->representation_vectors[1].x +
                   pTrigger->representation_vectors[2].x + pTrigger->representation_vectors[3].x) *
                      0.25,
                  (pTrigger->representation_vectors[0].y + pTrigger->representation_vectors[1].y +
                   pTrigger->representation_vectors[2].y + pTrigger->representation_vectors[3].y) *
                      0.25,
                  (pTrigger->representation_vectors[0].z + pTrigger->representation_vectors[1].z +
                   pTrigger->representation_vectors[2].z + pTrigger->representation_vectors[3].z) *
                      0.25);
    position = center;
    group = SpawnMonsters(0x1b5, 1, &position, 0, true, false);
    index = group->MemberLocationId(0);
    if (index != 0) {
        monster_info = FindMonster(index);
        if (monster_info != 0) {
            spawned_monster_id_ = index;
            monster_info->entity->visual.instance_scale = 1.0f;
            monster_info->entity->visual.apply_instance_scale = true;
            monster_info->entity->BeginFadeIn(2.0f);
            monster_info->entity->GetMappedPosition(&mapped);
            look_target = monster_info->entity->nav.movement.position;
            look_target.y += monster_info->entity->nav.movement.height_offset;
            partyCamera().LookAt(&look_target, false, CurrentGame().activity.conversation);
            npc_dialogue_closed = false;
            addMaster(&SwampScript::gasFireItemDrop);
        }
    }
    npc = (FindMonster(index))->boundNpc();
    QueueNpcScriptNotice(npc, 0, -1, false, 0);
    return {.run_action = true};
}

/* The master function the spawn arms. The toggle re-registers it and clears
   the notice flag; each zero tick waits for the NPC dialogue to close - once
   the queued NPC script notice completes - then drops an unidentified item
   0x264 at the spawned monster and fades it out for removal. */
bool SwampScript::gasFireItemDrop(int command)
{
    glm::vec3 position;
    glm::vec3 drop_position;
    W8WorldItem* item;

    if (command != 0) {
        if (command == kMasterToggle) {
            npc_dialogue_closed = false;
            addMaster(&SwampScript::gasFireItemDrop);
        }
        return false;
    }
    if (npc_dialogue_closed) {
        if (Monster* spawned = GetMonsterByLocationID(spawned_monster_id_)) {
            position = spawned->nav.GetPosition();
            drop_position = position;
            item = world().adoptItem(ctx.game.spawnWorldItem(
                world(), 0x264, &drop_position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            item->item.identified = false;
            world().activateItem(ctx.game.data, ctx.game.rng, item);
            spawned->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        return true;
    }
    return false;
}

} // namespace

std::unique_ptr<LevelScript> MakeSwampScript(const ScriptContext& context)
{
    return std::make_unique<SwampScript>(context);
}
