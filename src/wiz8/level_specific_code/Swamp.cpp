#include "wiz8/level_specific_code/Swamp.h"

#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/cursor.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/fact_state.h"
#include "wiz8/inventory.h"
#include "wiz8/item_spawning.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/render/camera.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

struct W8NpcState;

static Monster* g_swamp_spawned_monster;

/* Level Specific Code\Swamp.cpp (level 0x18).

   Attribution evidence: 0x004DAA70 passes this file's path string to
   MonsterGetIndexByLocationID. The level-0x18 block in
   InitializeLevelMasterFunctions registers the surrounding cluster
   (oil_pool, gas_trig_plane01-07, onelid, fire_trig_plane01-07). */

/* "oil_pool": item 0x2d0 held over the pool is converted to item 0x15e and the
   shared item-consumed flag is raised. Always returns 0, so the trigger stays
   armed for another try. */
bool SwampOilPool(Trigger* pTrigger)
{
    if ((g_status.party.held.item.iItemNo != -1) && GetItemInHand() == 0x2d0) {
        (void)TakeInventoryItem({W8ItemStorage::Hand});
        ReplaceHeldItem(MakeItem(0x15e, false, true, false));
        SetItemCursor(0);
        g_trigger_feedback = true;
    }
    return false;
}

static bool SwampGasFireSpawn(Trigger* pTrigger); /* 0x004DAA70 */
static bool SwampGasFireItemDrop(int command);    /* 0x004DACD0 */

/* Casts a spell at the camera position once the savant party has met; before
   that, spawns the gas fire. */
static bool CastSwampPlaneSpell(Trigger* pTrigger, int spell_id)
{
    glm::vec3 position;

    if (GetFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET) == 0) {
        SwampGasFireSpawn(pTrigger);
        return false;
    }
    position = glm::vec3(GetWorld()->camera->transform.getLocation());
    PointCastSpell(position, spell_id, 4);
    return true;
}

/* "gas_trig_plane01-07": while fact 0x16d is unset the trigger defers to the
   one-shot spawner; once set, it casts spell 0x2a at power 4 on the camera
   position. */
bool SwampGasPlane(Trigger* pTrigger)
{
    return CastSwampPlaneSpell(pTrigger, W8_SPELL_NOXIOUS_FUMES);
}

/* "fire_trig_plane01-07": same fact gate as the gas planes, but casts spell
   0x24. */
bool SwampFirePlane(Trigger* pTrigger)
{
    return CastSwampPlaneSpell(pTrigger, W8_SPELL_FIREBALL);
}

/* The one-shot behind the gas and fire trigger planes, armed while fact 0x16d
   is unset: spawn monster 0x1b5 at the triggering plane's center, fade it in
   under the camera, arm the item-drop master function, and hand the
   location-0xa3 NPC binding a script notice. */
/* 0x004DAA8A is a split entry Ghidra carved out of this body: it resumes at
   the SetFact call after the fact-0x16d early return, not a separate
   authored function. */
static bool SwampGasFireSpawn(Trigger* pTrigger)
{
    glm::vec3 center;
    glm::vec3 position;
    glm::vec3 mapped;
    glm::vec3 look_target;
    W8MonsterGroup* group;
    W8MonsterInfo* monster_info;
    W8NpcState* npc;
    int index;

    if (GetFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET) != 0) {
        return false;
    }
    SetFact(W8_FACT_SAVANT_PARTY_SWAMP_MEET, 1, false);
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
    group = SpawnMonsters(0x1b5, 1, &position, 0, true, false, false);
    index = group->MemberLocationId(0);
    if (index != 0) {
        monster_info = FindMonster(index);
        if (monster_info != 0) {
            g_swamp_spawned_monster = monster_info->p3D;
            monster_info->p3D->visual.instance_scale = 1.0f;
            monster_info->p3D->visual.apply_instance_scale = true;
            g_swamp_spawned_monster->BeginFadeIn(2.0f);
            g_swamp_spawned_monster->GetMappedPosition(&mapped);
            look_target = monster_info->p3D->nav.movement.position;
            look_target.y += monster_info->p3D->nav.movement.height_offset;
            partyCamera().LookAt(&look_target, false);
            g_npc_dialogue_closed = false;
            g_master_functions.push_back({SwampGasFireItemDrop});
        }
    }
    npc = FindNpcBindingForMonster(FindMonster(index));
    QueueNpcScriptNotice(npc, 0, -1, false, 0);
    return true;
}

/* The master function the spawn arms. 0xEFFFFFFF re-registers it and clears
   the notice flag; each zero tick waits for g_npc_dialogue_closed - raised when the
   queued NPC script notice completes - then drops an unidentified item 0x264
   at the spawned monster and fades it out for removal. */
static bool SwampGasFireItemDrop(int command)
{
    glm::vec3 position;
    glm::vec3 drop_position;
    W8WorldItem* item;

    if (command != 0) {
        if (command == static_cast<int>(0xEFFFFFFF)) {
            g_npc_dialogue_closed = false;
            g_master_functions.push_back({SwampGasFireItemDrop});
        }
        return false;
    }
    if (g_npc_dialogue_closed) {
        if (g_swamp_spawned_monster != 0) {
            position = g_swamp_spawned_monster->nav.GetPosition();
            drop_position = position;
            item = AddWorldItem(
                SpawnItem(0x264, &drop_position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
            item->item.identified = false;
            ActivateItem(item);
            g_swamp_spawned_monster->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_NONE);
        }
        return true;
    }
    return false;
}

/* "onelid": once fact 0x33 is set and an npc of kind 0x1e exists, advance the
   script facts - set 0x39 and clear 0x33. */
bool SwampOnelid(Trigger* pTrigger)
{
    if (GetFact(W8_FACT_CROCK_KIDNAPPED_PLAYER) != 0 && FindNpcOfKind(0x1e) != 0) {
        SetFact(W8_FACT_CROCK_PLAYER_RETURNED_TO_PARTY, 1, false);
        SetFact(W8_FACT_CROCK_KIDNAPPED_PLAYER, 0, false);
    }
    return true;
}
