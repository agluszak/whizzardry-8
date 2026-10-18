#include "wiz8/level_specific_code/Trynnie1.h"

#include <vector>

#include "conditions.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/gameplay_databases.h"
#include "random.h"
#include "soundman.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/fact_state.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/layouts/item_instance.h"
#include "wiz8/layouts/world.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_generators.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

class Trigger;

/* Level Specific Code\Trynnie1.cpp (level 0x19).

   Attribution evidence: 0x004DA670 and 0x004DA850 cite this file's path
   string in their assertions; 0x004DA670 is the case-0x19 setup that reads
   the 'Trynnies'/'Dead Trynnies' encounter tables into the Trynnie1Killed
   location variable. The level-0x1a neighbors belong to Trynnie2.cpp
   (0x004D9E60 cites it). */

/* Item ids 0x1b3 and 0x1c3 route the use-item action down the origin/equip
   path instead of activating, the same as a non-usable equip class. */
bool IsSpecialItemId(W8ItemInstance* item)
{
    return item->iItemNo == 0x1b3 || item->iItemNo == 0x1c3;
}

/* Level 0x19 setup: once fact 0x229 reports the Trynnies dead and the
   Trynnie1Killed location variable does not exist yet, swap the encounter
   tables and spawn the dead shaman (0xcd) at VOC_SHAMAN_DYING. */
void EnsureTrynnie1KilledVar(void)
{
    glm::vec3 vPos;
    glm::vec3 vPos2;
    W8MonsterGroup* group;

    if (GetFact(W8_FACT_TRYNNIE_SHAMAN_DIES) == 1) {
        if (GetLocationVarIDByName("Trynnie1Killed") == -1) {
            KillTrynnieGroups();
            if (!FindEntityByName("VOC_SHAMAN_DYING", &vPos, 0, 0)) {
                Fail("WorldGetNamedPosition(\"VOC_SHAMAN_DYING\", vPos)");
            }
            vPos2 = vPos;
            group = CreateGroup(0xcd, 1, &vPos2, true, false, true);
            if (group == 0) {
                Fail("CreateGroup(205, 1, vPos2, TRUE, FALSE, TRUE)");
            }
            CreateLocationVar("Trynnie1Killed", 1);
        }
    }
}

/* "Fount_randomFX": the magic fountain rolls d100 - afflict a random party
   member with condition 9 (25%), restore the party's spell points (25%),
   heal everyone (25%) or restore everyone's stamina (25%). */
bool Trynnie1FountRandomFX(Trigger* pTrigger)
{
    unsigned int roll;
    int slot;
    int i;

    roll = Random(100);
    if (roll < 25) {
        slot = GetRandomCharacter(0, 0, -1, -1);
        if (slot != -1) {
            SetCharacterCondition(slot, W8_CONDITION_HEXED, Random(5) + 10, 0, 0, 1);
            wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
            return true;
        }
    } else if (roll < 50) {
        RestorePartySpellPoints(-1);
        wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
        return true;
    } else if (roll < 75) {
        for (i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
            if (g_status.buffers.XChar[i].fOccupied) {
                HealCharacter(i, 100, false);
            }
        }
        wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
        return true;
    } else {
        for (i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
            if (g_status.buffers.XChar[i].fOccupied) {
                RestoreCharacterStamina(i, 100, false);
            }
        }
    }
    wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
    return true;
}

/* Replace every live faction-0xd (Trynnie) group with a dead-Trynnie (0x1be)
   group, then point each 'Trynnies' generator at the 'Dead Trynnies' table. */
void KillTrynnieGroups(void)
{
    W8MonsterGroup* group;
    W8MonsterRecord* record;
    MonGen* generator;
    int iOldTable;
    int iNewTable;
    int index;

    for (index = 0; index < static_cast<int>(CurrentLevelPopulation().monster_groups.size());
         ++index) {
        group = CurrentLevelPopulation().monster_groups[index].get();
        if (group != 0 && group->members_active &&
            (record = MonsterGroupGetRecord(group), record->faction_id == 0xd) &&
            ReplaceMonsterGroupSpecies(group, 0x1be) != 0) {
            RemoveAllGroupMembers(group);
        }
    }
    iOldTable = FindEncounterTableByName("Trynnies");
    iNewTable = FindEncounterTableByName("Dead Trynnies");
    if (iOldTable == -1) {
        Fail("iOldTable!=(-1)", "Can't find monster table 'Trynnies'");
    }
    if (iNewTable == -1) {
        Fail("iNewTable!=(-1)", "Can't find monster table 'Dead Trynnies'");
    }
    for (index = 0; index < static_cast<int>(GetWorld()->encounters.generators.size()); ++index) {
        generator = GetWorld()->encounters.generators[index].get();
        if (generator->encounter_table_index == iOldTable) {
            generator->SetEncounterTable(iNewTable);
        }
    }
}
