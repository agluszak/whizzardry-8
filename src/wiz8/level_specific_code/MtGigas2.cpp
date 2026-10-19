#include "wiz8/level_specific_code/MtGigas2.h"

#include <optional>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "soundman.h"
#include "surrender/srCamera.h"
#include "wiz8/dice.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/fact_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/MtGigasOuter.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/utility.h"
#include "wiz8/xstatus.h"

struct W8ItemInstance;
struct W8NpcState;

/* Level Specific Code\MtGigas2.cpp (level 0x0d).

   Attribution evidence: InitializeLevelMasterFunctions runs
   0x004DB200 and registers 0x004DB380-0x004DBA90 under case 0x0d, and the
   range sits between the Trynnie1 TU and the MtGigasOuter TU which starts
   at 0x004DBE70. The UmpaniAlarm master at 0x004DB860 is the same shape as
   ControlCampAlarm (0x004DC3D0) in MtGigasOuter.cpp. */

// GLOBAL: WIZ8 0x006834e8
static stSound3D* g_mt_gigas_alarm_sound;
// GLOBAL: WIZ8 0x006834ec
static std::optional<W8IntervalGate> g_mt_gigas_alarm_gate;

static void MtGigas2WireShock(void);
static bool MtGigas2UmpaniAlarm(int command);

/* Level-load restore. Recreates the wire-panel location variable (armed but
   untouched reads back -1), re-runs the three wire triggers when the panel
   was already solved, restarts a saved UmpaniAlarm countdown, and moves the
   Sergeant Rubble NPC to his covert when the rubble teleport was pending. */
// FUNCTION: WIZ8 0x004DB200
void MtGigas2Setup(void)
{
    if (GetLocationVarIDByName("WirePanel") != -1) {
        if (GetLocationVarValueByName("WirePanel") == 3) {
            RunNamedTrigger("redwire", -1);
            RunNamedTrigger("bluewire", -1);
            RunNamedTrigger("yellowwire", -1);
        }
    } else {
        CreateLocationVar("WirePanel", -1);
    }
    if (GetLocationVarIDByName("UmpaniAlarm") != -1) {
        int alarm = GetLocationVarValueByName("UmpaniAlarm");
        if (alarm != 0) {
            MtGigas2UmpaniAlarm(alarm);
        }
    }
    if (GetFact(W8_FACT_UMISSION_IUFPASS_LEVEL4) == 0 &&
        GetLocationVarIDByName("CODESgtRubbleTeleport") != -1 &&
        GetLocationVarValueByName("CODESgtRubbleTeleport") == 2) {
        W8NpcState* npc;
        W8MonsterInfo* info;
        glm::vec3 position;

        SetTriggerVariableByName("CODESgtRubbleTeleport", 1);
        RunNamedTrigger("door08", -1);
        SetFact(W8_FACT_UMISSION_MOVE_RUBBLE_COVERT, 0, false);
        npc = GetNpcStateByKind(0x29);
        if (npc != 0) {
            info = GetNpcMonsterInfo(npc);
            if (info != 0 && FindEntityByName("RubbleCovert", &position, 0, 0)) {
                info->p3D->SetPosition(&position);
            }
        }
    }
}

/* Activation callback on _VOC_EWAXXTRAIN: queues the wire-panel NPC notice
   with the held item, and accepts the 0x269/0x26c wire items to arm the
   panel (value 0) when it was still untouched (-1). */
// FUNCTION: WIZ8 0x004DB380
bool MtGigas2Train(Trigger* pTrigger)
{
    W8NpcState* npc;
    W8ItemInstance* item;
    int item_id;
    int value;

    if (gXStatus.fNpcDialogueMode) {
        return false;
    }
    npc = GetNpcStateByKind(0x5b);
    item = 0;
    item_id = 0;
    if (g_status.item_in_cursor) {
        item = &g_status.item_in_hand;
        item_id = GetItemInHand();
    }
    QueueNpcScriptNotice(npc, item, -1, false, 0);
    g_trigger_feedback = true;
    value = GetLocationVarValueByName("WirePanel");
    if (value == 3) {
        return false;
    }
    if (item_id == 0x269 || item_id == 0x26c) {
        wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 01.wav");
        if (value == -1) {
            SetTriggerVariableByName("WirePanel", 0);
            return true;
        }
    }
    return false;
}

/* Each wire passes only from its expected panel state; 3 means already fixed. */
static bool advanceWirePanel(int expected, int next)
{
    const int value = GetLocationVarValueByName("WirePanel");

    if (value == 3) {
        return true;
    }
    g_trigger_feedback = true;
    if (value != expected) {
        MtGigas2WireShock();
        return false;
    }
    SetTriggerVariableByName("WirePanel", next);
    return true;
}

/* Activation callback on redwire: the first wire moves the panel from 0 to
   1; any other order shocks the party. */
// FUNCTION: WIZ8 0x004DB420
bool MtGigas2RedWire(Trigger* pTrigger)
{
    return advanceWirePanel(0, 1);
}

/* Activation callback on bluewire: valid only as the second wire (1 -> 2). */
// FUNCTION: WIZ8 0x004DB460
bool MtGigas2BlueWire(Trigger* pTrigger)
{
    return advanceWirePanel(1, 2);
}

/* Activation callback on yellowwire: valid only as the last wire (2 -> 3);
   on success it latches fact 0xa7 and re-arms all three wire triggers. */
// FUNCTION: WIZ8 0x004DB4A0
bool MtGigas2YellowWire(Trigger* pTrigger)
{
    int value = GetLocationVarValueByName("WirePanel");
    Trigger* wire;

    if (value == 3) {
        return true;
    }
    g_trigger_feedback = true;
    if (value != 2) {
        MtGigas2WireShock();
        return false;
    }
    SetFact(W8_FACT_UMISSION_TRAIN_MACHINE_FIXED, 1, false);
    SetTriggerVariableByName("WirePanel", 3);
    pTrigger->flags &= ~W8_TRIGGER_ENABLED;
    wire = FindTriggerByName("redwire");
    if (wire != 0) {
        wire->flags &= ~W8_TRIGGER_ENABLED;
    }
    wire = FindTriggerByName("bluewire");
    if (wire != 0) {
        wire->flags &= ~W8_TRIGGER_ENABLED;
    }
    return true;
}

/* Shared wrong-wire penalty: resets the panel and all three wire props, then
   rolls 2d4+1 electric damage into the party with the spark sound. The third
   wire prop is looked up under the misspelled name "yellowire". */
// FUNCTION: WIZ8 0x004DB530
static void MtGigas2WireShock(void)
{
    Trigger* pTrigger;
    W8Prop* prop;
    W8Dice dice;

    SetTriggerVariableByName("WirePanel", 0);
    prop = 0;
    pTrigger = FindTriggerByName("redwire");
    if (pTrigger != 0) {
        prop = pTrigger->GetProp();
    }
    if (prop != 0) {
        prop->SetPendingAnimationSubcycle(0);
    }
    pTrigger = FindTriggerByName("bluewire");
    if (pTrigger != 0) {
        prop = pTrigger->GetProp();
    }
    if (prop != 0) {
        prop->SetPendingAnimationSubcycle(0);
    }
    pTrigger = FindTriggerByName("yellowire");
    if (pTrigger != 0) {
        prop = pTrigger->GetProp();
    }
    if (prop != 0) {
        prop->SetPendingAnimationSubcycle(0);
    }
    SetDice(&dice, 2, 4, 1);
    ApplyRolledHealthChangeToParty(&dice, 0, 1);
    wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 04.wav");
}

/* Activation callback on _VOC_EWAXXLIFT3: queues the lift NPC notice with
   the held item, if any. */
// FUNCTION: WIZ8 0x004DB650
bool MtGigas2Lift3(Trigger* pTrigger)
{
    if (gXStatus.fNpcDialogueMode) {
        return false;
    }
    QueueNpcHeldItemNotice(0x5c);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on _VOC_EWAXXTOPDOOR1: queues the top-door NPC notice
   with the held item, if any. */
// FUNCTION: WIZ8 0x004DB690
bool MtGigas2TopDoor1(Trigger* pTrigger)
{
    if (gXStatus.fNpcDialogueMode) {
        return false;
    }
    QueueNpcHeldItemNotice(0x5f);
    g_trigger_feedback = true;
    return false;
}

static bool ActivateMtGigasOfficer(int door_bit, const char* door_name, int npc_kind)
{
    if (gXStatus.fNpcDialogueMode) {
        return false;
    }
    int doors = 0;
    if (GetLocationVarIDByName("ObstacleDoors") != -1) {
        doors = GetLocationVarValueByName("ObstacleDoors");
    }
    if ((doors & door_bit) != 0) {
        Trigger* door = FindTriggerByName(door_name);
        door->Run(-1);
        SetTriggerVariableByName("ObstacleDoors", doors - door_bit);
        return false;
    }
    QueueNpcHeldItemNotice(npc_kind);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on _VOC_EWAXXOFFICER1: while ObstacleDoors bit 0 is
   set, runs Door19 and clears the bit; otherwise queues the officer NPC
   notice with the held item. */
// FUNCTION: WIZ8 0x004DB6D0
bool MtGigas2Officer1(Trigger* pTrigger)
{
    return ActivateMtGigasOfficer(1, "Door19", 0x7c);
}

/* Activation callback on _VOC_EWAXXOFFICER2: while ObstacleDoors bit 1 is
   set, runs Door18 and clears the bit; otherwise queues the officer NPC
   notice with the held item. */
// FUNCTION: WIZ8 0x004DB770
bool MtGigas2Officer2(Trigger* pTrigger)
{
    return ActivateMtGigasOfficer(2, "Door18", 0x7d);
}

/* Activation callback on triggerPlaneLaserAlarm/01: once per level (fact
   0xa5 and the UmpaniAlarm variable both unset) turns the Umpani faction
   hostile and starts the intruder alarm. */
// FUNCTION: WIZ8 0x004DB810
bool MtGigas2LaserAlarm(Trigger* pTrigger)
{
    g_trigger_feedback = true;
    if (GetFact(W8_FACT_LOCATION_GIGAS_MOTION_SENSORS_OFF) != 0) {
        return false;
    }
    if (GetLocationVarIDByName("UmpaniAlarm") != -1) {
        return false;
    }
    SetFactionDispositionBand(4, W8_FACTION_HOSTILE);
    MtGigas2UmpaniAlarm(0xEFFFFFFF);
    return true;
}

static void CaptureUmpaniAlarm(W8SavedLocationState& state)
{
    state.emplace_back(
        "UmpaniAlarm",
        g_mt_gigas_alarm_gate ? static_cast<int>(g_mt_gigas_alarm_gate->GetElapsedSeconds()) : 0);
}

/* The Umpani alarm's master function, same shape as ControlCampAlarm in
   MtGigasOuter.cpp: command zero ticks the alarm gate while registered, -1
   writes the remaining seconds into UmpaniAlarm, and any other command plays
   the intruder warning at the camera and arms the gate (clamped to 30
   seconds). Unlike ControlCampAlarm the gate and sound are only created when
   both are absent, and finishing stops the sound but keeps its node. */
// FUNCTION: WIZ8 0x004DB860
static bool MtGigas2UmpaniAlarm(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (GetLocationVarIDByName("UmpaniAlarm") == -1) CreateLocationVar("UmpaniAlarm", 30);
        if (g_mt_gigas_alarm_gate || g_mt_gigas_alarm_sound != 0) {
            return false;
        }
        position = glm::vec3(GetWorld()->camera->getLocation());
        if (static_cast<unsigned int>(command) > 0x1e) {
            command = 0x1e;
        }
        g_mt_gigas_alarm_gate.emplace(static_cast<float>(static_cast<unsigned int>(command)), false,
                                      true);
        g_mt_gigas_alarm_sound =
            CreateAndPlaySoundNode("Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_003.wav",
                                   position, 1.0f, 75.0f, true);
        if (g_mt_gigas_alarm_sound != 0 && g_mt_gigas_alarm_gate) {
            g_master_functions.push_back({MtGigas2UmpaniAlarm, CaptureUmpaniAlarm});
        }
    }
    if (!g_mt_gigas_alarm_gate) return true;
    if (!g_mt_gigas_alarm_gate->PollFinished()) {
        return false;
    }

    if (g_mt_gigas_alarm_sound) g_mt_gigas_alarm_sound->Stop();
    g_mt_gigas_alarm_gate.reset();
    SetTriggerVariableByName("UmpaniAlarm", 0);
    return true;
}

/* Activation callback on wiringMalfunction: plays the short spark sound. */
// FUNCTION: WIZ8 0x004DBA70
bool MtGigas2WiringMalfunction(Trigger* pTrigger)
{
    wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 04.wav");
    return true;
}

/* Activation callback on accessHatch: plays the falling-statue sound. */
// FUNCTION: WIZ8 0x004DBA90
bool MtGigas2AccessHatch(Trigger* pTrigger)
{
    wiz8::sound::play("Data\\Sound\\Ambients\\Sign Falling Statue.wav");
    return true;
}
