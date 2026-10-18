#include "wiz8/level_specific_code/Ascension.h"

#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "layouts/npc_state.h"
#include "layouts/world.h"
#include "local_code/Factions.h"
#include "soundman.h"
#include "surrender/srCamera.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/fact_state.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/NPCScripting.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/message_box.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/xstatus.h"

/* Level Specific Code\Ascension.cpp.

   Attribution evidence: 0x004DF870, 0x004DFBC0 and 0x004E0430 cite this file's
   path string in their MonsterGetIndexByLocationID assertions (lines 0x17f,
   0x2cd, 0x2da, 0x2e7). 0x004DF810, 0x004DFB40, 0x004DFB80 and 0x004E0510 are
   bounded to this TU, and InitializeLevelMasterFunctions registers the
   0x004DFE60-0x004E04F0 callbacks plus the 0x004DF870 init call under case 1,
   the Ascension Peak level. 0x004E0560 and 0x004E05A0 are registered under
   case 0x24, the Footsteps level, which is part of the Ascension Peak block. */

// GLOBAL: WIZ8 0x0068356C
static std::optional<W8IntervalGate> g_avalanche_gate;

/* Count how many of the three Ascension Peak relic items (0x242, 0x243,
   0x244) are on the party; the world-cursor "seen bodies" handler requires
   all three. */
// FUNCTION: WIZ8 0x004DF810
int CountAscensionPeakItems(void)
{
    int count = 0;

    if (FindItemOnParty(0x242, 0, 0, 2, 0)) {
        count = 1;
    }
    if (FindItemOnParty(0x243, 0, 0, 2, 0)) {
        ++count;
    }
    if (FindItemOnParty(0x244, 0, 0, 2, 0)) {
        ++count;
    }
    return count;
}

/* Ascension Peak level init (level-1 master-function case): the relic-item
   count selects the staged encounters. Two or more relics run the avalanche
   sequence and the Rapax ambush once; all three additionally spawn the Savants
   and fire the "Bodies" trigger once. */
/* Standalone JMP thunk onto AscensionPeakInit. */

// FUNCTION: WIZ8 0x004DF870
unsigned char AscensionPeakInit(void)
{
    glm::vec3 position;
    int count = CountAscensionPeakItems();

    if (count > 1) {
        if (GetLocationVarIDByName("AP_AtLeast2of3Items") == -1) {
            W8MonsterGroup* group = nullptr;

            AscensionAvalanche(true);
            if (FindEntityByName("NP_Rapax01", &position, 0, 0)) {
                group = SpawnMonsters(0xb1, 6, &position, 1, true, false, false);
            }
            if (FindEntityByName("NP_Rapax04", &position, 0, 0)) {
                SpawnMonsters(0xf, 4, &position, 1, true, false, false);
            }
            if (group != nullptr) {
                W8MonsterInfo* info = FindMonster(group->leader_location_id);
                if (info != nullptr && info->p3D != nullptr) {
                    info->p3D->SetScript("proximitylandslide.msf", true);
                }
            }
            SpawnAscensionAmbush();
            CreateLocationVar("AP_AtLeast2of3Items", 1);
        }
        if (GetLocationVarIDByName("AP_SpawnDaughter") == -1) {
            W8NpcState* npc = GetNpcStateByKind(0x31);

            if (npc != 0 && npc->greeting_pending && npc->spawned == 0 && !npc->party_noticed &&
                GetFact(W8_FACT_TEMPLAR) != 0) {
                if (FindEntityByName("NP_Daughter", &position, 0, 0)) {
                    SpawnMonsters(0x18d, 1, &position, 0, true, false, false);
                    SetFact(static_cast<W8FactId>(0x327), 1, false);
                }
                CreateLocationVar("AP_SpawnDaughter", 1);
            }
        }
    }
    if (count == 3) {
        if (GetLocationVarIDByName("AP_All3Items") == -1) {
            if (FindEntityByName("NP_Savants01", &position, 0, 0)) {
                SpawnMonsters(0x1a0, 6, &position, 1, true, false, false);
            }
            if (GetFact(W8_FACT_TMISSION_MEET_ZANT_AT_AP) != 0 ||
                GetFact(W8_FACT_UMISSION_MEET_AP_ASSIGN) != 0) {
                RunNamedTrigger("Bodies", -1);
            }
            CreateLocationVar("AP_All3Items", 1);
        }
    }
    return 1;
}

/* Spawns one Alfie monster at a named entity, if the entity is present. */
static void spawnAlfieAt(const char* entity, int monster_id)
{
    glm::vec3 position;

    if (FindEntityByName(entity, &position, 0, 0)) {
        SpawnMonsters(monster_id, 1, &position, 0, true, false, false);
    }
}

/* Spawn the Alfie "chaos" monster (0xae) at the NP_AlfieChaos entity once the
   party carries the Astral Dominae (0x244). */
// FUNCTION: WIZ8 0x004DFAE0
unsigned char SpawnAlfieChaos(int)
{
    if (!FindItemOnParty(0x244, 0, 0, 2, 0)) {
        return 0;
    }
    spawnAlfieAt("NP_AlfieChaos", 0xae);
    return 1;
}

/* Spawn the Alfie "life" monster (0xaf) at the NP_AlfieLife entity when it is
   present in the level. */
// FUNCTION: WIZ8 0x004DFB40
unsigned char SpawnAlfieLife(int)
{
    spawnAlfieAt("NP_AlfieLife", 0xaf);
    return 1;
}

/* Spawn the Alfie "know" monster (0xb0) at the NP_AlfieKnow entity when it is
   present in the level. */
// FUNCTION: WIZ8 0x004DFB80
unsigned char SpawnAlfieKnow(int)
{
    glm::vec3 position;

    if (FindEntityByName("NP_AlfieKnow", &position, 0, 0)) {
        SpawnMonsters(0xb0, 1, &position, 0, true, false, false);
    }
    return 1;
}

/* Fire the "Avalanche" trigger and, when asked, update the 0x15d/0x15e
   proximity facts by measuring the camera against the ASC40 and ASC30
   positions, then arm the periodic land-shaker master. */
// FUNCTION: WIZ8 0x004DFBC0
void AscensionAvalanche(bool command)
{
    RunNamedTrigger("Avalanche", -1);
    if (command) {
        glm::vec3 position;
        glm::vec3 trigger_position;

        position = glm::vec3(GetWorld()->camera->getLocation());
        FindTriggerByName("ASC40")->GetPosition(&trigger_position);
        if (glm::length((position - trigger_position)) < 2500.0) {
            SetFact(W8_FACT_LANDSLIDE_RAPAX_SIDE, 1, false);
            SetFact(W8_FACT_LANDSLIDE_WILD_SIDE, 0, false);
        } else {
            FindTriggerByName("ASC30")->GetPosition(&trigger_position);
            if (glm::length((position - trigger_position)) < 2500.0) {
                SetFact(W8_FACT_LANDSLIDE_RAPAX_SIDE, 0, false);
                SetFact(W8_FACT_LANDSLIDE_WILD_SIDE, 1, false);
            }
        }
        g_master_functions.push_back({AscensionLandShaker});
    }
}

/* The land-shaker master function, registered by AscensionAvalanche. The
   lazily created one-second gate runs the LandShaker trigger once it elapses;
   -1 returns early without touching the scripted-sequence flag. */
// FUNCTION: WIZ8 0x004DFD70
void AscensionLandShaker(int command)
{

    if (command == -1) {
        return;
    }
    g_remove_current_master_function = false;
    if (!g_avalanche_gate) {
        g_avalanche_gate.emplace(1.0f, false, true);
        return;
    }
    if (!g_avalanche_gate->PollFinished()) {
        return;
    }
    g_avalanche_gate.reset();
    RunNamedTrigger("LandShaker", -1);
    g_remove_current_master_function = true;
}

/* The RampUp activation callback: spawn the daughter monster (0x18d) at the
   NP_DarkSavant entity. */
// FUNCTION: WIZ8 0x004DFE60
bool AscensionRampUp(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("NP_DarkSavant", &position, 0, 0)) {
        SpawnMonsters(0x18d, 1, &position, 0, true, false, false);
    }
    return true;
}

/* Spawn the Rapax ambush and the prince once two relics are brought to the
   peak. */
// FUNCTION: WIZ8 0x004DFEA0
unsigned char SpawnAscensionAmbush(void)
{
    glm::vec3 position;

    if (FindEntityByName("NP_Rapax02", &position, 0, 0)) {
        SpawnMonsters(0xc3, 3, &position, 0, true, false, false);
    }
    if (FindEntityByName("NP_Rapax03", &position, 0, 0)) {
        SpawnMonsters(0xc4, 6, &position, 0, true, false, false);
    }
    if (FindEntityByName("NP_Rapax05", &position, 0, 0)) {
        SpawnMonsters(0xc5, 6, &position, 0, true, false, false);
    }
    if (FindEntityByName("NP_Prince", &position, 0, 0)) {
        SpawnMonsters(0xb2, 1, &position, 0, true, false, false);
    }
    return 1;
}

/* The B-side callbacks arm their A partner, and once the Aletheides flag is
   set dismiss the bound monster and clear the flag. */
static bool armAletheidesPartner(const char* partner, const char* active_var)
{
    Trigger* partner_trigger = FindTriggerByName(partner);

    if (partner_trigger != 0) {
        partner_trigger->flags |= W8_TRIGGER_ENABLED;
    }
    if (GetLocationVarIDByName(active_var) != -1 && GetLocationVarValueByName(active_var) != 0) {
        RemoveAletheides();
        SetTriggerVariableByName(active_var, 0);
    }
    return true;
}

/* The A-side callbacks are inert while the flag is set. Otherwise Aletheides
   answers, and the party carrying the relic gets the exchange and the Alfie
   spawn. */
static bool answerAletheides(Trigger* pTrigger, const char* active_var, int relic_item,
                             W8NpcStoryAction spawn)
{
    if (GetLocationVarIDByName(active_var) == -1) {
        CreateLocationVar(active_var, 0);
    }
    if (GetLocationVarValueByName(active_var) != 0) {
        return false;
    }
    g_npc_dialogue.Queue(W8NpcCommand::begin_scripted_world_action);
    g_npc_dialogue.Queue(W8NpcPortraitString{0x71e});
    if (!FindItemOnParty(relic_item, 0, 0, 2, 0)) {
        g_npc_dialogue.Queue(W8NpcPortraitString{0x71f});
    } else {
        SetTriggerVariableByName(active_var, 1);
        g_npc_dialogue.Queue(W8NpcPortraitString{0x720});
        g_npc_dialogue.Queue(spawn);
    }
    g_npc_dialogue.Queue(W8NpcCommand::clear_target_state);
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    return true;
}

/* The ChaosBTrigger activation callback: arm ChaosATrigger, and once
   AlethidiesChaosActive is set dismiss the bound Aletheides monster and clear
   the variable. */
// FUNCTION: WIZ8 0x004DFF90
bool AscensionChaosBTrigger(Trigger* pTrigger)
{
    return armAletheidesPartner("ChaosATrigger", "AlethidiesChaosActive");
}

/* The ChaosATrigger activation callback: while AlethidiesChaosActive is set
   the trigger is inert. Otherwise Aletheides answers, and the party carrying
   the Astral Dominae (0x244) gets the relic exchange and the chaos Alfie
   spawn. */
// FUNCTION: WIZ8 0x004DFFF0
bool AscensionChaosATrigger(Trigger* pTrigger)
{
    return answerAletheides(pTrigger, "AlethidiesChaosActive", 0x244,
                            W8NpcStoryAction::spawn_alfie_chaos);
}

/* The LifeBTrigger activation callback: arm LifeATrigger, and once
   AlethidiesLifeActive is set dismiss the bound Aletheides monster and clear
   the variable. */
// FUNCTION: WIZ8 0x004E00B0
bool AscensionLifeBTrigger(Trigger* pTrigger)
{
    return armAletheidesPartner("LifeATrigger", "AlethidiesLifeActive");
}

/* The LifeATrigger activation callback: while AlethidiesLifeActive is set the
   trigger is inert. Otherwise Aletheides answers, and the party carrying the
   relic (0x242) gets the relic exchange and the life Alfie spawn. */
// FUNCTION: WIZ8 0x004E0110
bool AscensionLifeATrigger(Trigger* pTrigger)
{
    return answerAletheides(pTrigger, "AlethidiesLifeActive", 0x242,
                            W8NpcStoryAction::spawn_alfie_life);
}

/* The KnowBTrigger activation callback: arm KnowATrigger, and once
   AlethidiesKnowActive is set dismiss the bound Aletheides monster and clear
   the variable. */
// FUNCTION: WIZ8 0x004E01D0
bool AscensionKnowBTrigger(Trigger* pTrigger)
{
    return armAletheidesPartner("KnowATrigger", "AlethidiesKnowActive");
}

/* The KnowATrigger activation callback: while AlethidiesKnowActive is set the
   trigger is inert. Otherwise Aletheides answers, and the party carrying the
   relic (0x243) gets the relic exchange and the know Alfie spawn. */
// FUNCTION: WIZ8 0x004E0230
bool AscensionKnowATrigger(Trigger* pTrigger)
{
    return answerAletheides(pTrigger, "AlethidiesKnowActive", 0x243,
                            W8NpcStoryAction::spawn_alfie_know);
}

/* The second RampUp activation callback: spawn the Dark Savant (0xc2) and
   Bela (0x18c) and latch AP_DSSpawned so Path1Camera knows they arrived. */
// FUNCTION: WIZ8 0x004E02F0
bool AscensionDarkSavantSpawn(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("NP_DSavant", &position, 0, 0)) {
        SpawnMonsters(0xc2, 1, &position, 0, true, false, false);
    }
    if (FindEntityByName("NP_Bela", &position, 0, 0)) {
        SpawnMonsters(0x18c, 1, &position, 0, true, false, false);
    }
    if (GetLocationVarIDByName("AP_DSSpawned") == -1) {
        CreateLocationVar("AP_DSSpawned", 1);
    }
    return true;
}

/* The Path1Camera activation callback: once the Dark Savant is spawned, kill
   every live hostile monster, end combat and hand control to the scripted
   world action. */
// FUNCTION: WIZ8 0x004E0390
bool AscensionPath1Camera(Trigger* pTrigger)
{
    unsigned int index;

    if (GetLocationVarIDByName("AP_DSSpawned") == -1) {
        return false;
    }
    for (index = 0; index < CurrentLevelPopulation().monster_roster.live().size(); ++index) {
        W8MonsterInfo* info = GetMonsterListEntry(index);

        if (info->fActive && info->fInCombat && info->ubDisposition == W8_DISPOSITION_HOSTILE &&
            !info->p3D->IsDying()) {
            MonsterStartsDying(info, true);
        }
    }
    if (gXStatus.fCombatMode) {
        EndCombat(true);
    }
    BeginScriptedWorldAction();
    pTrigger->flags &= ~W8_TRIGGER_ON;
    return true;
}

/* Remove the monster bound to whichever Aletheides NPC kind (0x2d, 0x2e or
   0x2f) is present, first match wins. */
// FUNCTION: WIZ8 0x004E0430
void RemoveAletheides(void)
{
    W8NpcState* npc = GetNpcStateByKind(0x2d);
    W8MonsterInfo* info;

    if (npc != 0) {
        info = GetNpcMonsterInfo(npc);
        if (info != 0) {
            RemoveMonster(info, W8MonsterRemoval::Despawn);
            return;
        }
    }
    npc = GetNpcStateByKind(0x2e);
    if (npc != 0) {
        info = GetNpcMonsterInfo(npc);
        if (info != 0) {
            RemoveMonster(info, W8MonsterRemoval::Despawn);
            return;
        }
    }
    npc = GetNpcStateByKind(0x2f);
    if (npc != 0) {
        info = GetNpcMonsterInfo(npc);
        if (info != 0) {
            RemoveMonster(info, W8MonsterRemoval::Despawn);
        }
    }
}

/* The Shaker activation callback: play the avalanche rumble. */
// FUNCTION: WIZ8 0x004E04F0
bool AscensionShaker(Trigger* pTrigger)
{
    wiz8::sound::play("Data\\Sound\\Ambients\\Rumble 01.wav");
    return true;
}
