#include "wiz8/level_specific_code/MtGigasOuter.h"

#include <optional>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "surrender/srCamera.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/Video2.h"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"

struct W8ItemInstance;
struct W8NpcState;

#define MTGIGASOUTER_CPP "C:\\Projects\\Wizardry 8\\Level Specific Code\\MtGigasOuter.cpp"

/* Level Specific Code\MtGigasOuter.cpp (level 0x0e).

   Attribution evidence: 0x004DC080 is this TU's hard lower bound and
   InitializeLevelMasterFunctions runs 0x004DBE70 under case 0x0e,
   alongside the _VOC_EWAXXLIFT1 trigger wiring. The assert file string at
   0x00613640 names this TU for 0x004DBEC0 and 0x004DC080. */

// GLOBAL: WIZ8 0x006834fc
static std::optional<W8IntervalGate> g_lift_gate;
// GLOBAL: WIZ8 0x00683500
static W8Prop* g_lift_prop;
// GLOBAL: WIZ8 0x00683504
static stSound3D* g_mt_gigas_outer_alarm_sound;
// GLOBAL: WIZ8 0x00683508
static std::optional<W8IntervalGate> g_mt_gigas_outer_alarm_gate;

/* Lift prop Y travel: 5228.6 at the top of the ride, -2000.0 at the bottom,
   and -5000.0 parked out of the world once the ride is spent. */
// GLOBAL: WIZ8 0x00613600
static float LIFT_TOP_Y = 5228.6f;
// GLOBAL: WIZ8 0x00613604
static float LIFT_BOTTOM_Y = -2000.0f;
// GLOBAL: WIZ8 0x00613608
static float LIFT_PARKED_Y = -5000.0f;

/* When the FlagPosition location variable exists, feed its value to the lift
   gate; values at or above 1000 stop there. Otherwise the "flag" trigger
   runs. */
// FUNCTION: WIZ8 0x004DBE70
void ProcessFlagPosition(void)
{
    if (GetLocationVarIDByName("FlagPosition")) {
        int value = GetLocationVarValueByName("FlagPosition");
        ControlLiftGate(value);
        if (value >= 1000) {
            return;
        }
    }
    Trigger* trigger = FindTriggerByName("flag");
    trigger->Run(-1);
}

/* Activation callback installed on the "crank" trigger. A fresh game creates
   FlagPosition and queues the lift operator's greeting script; a zero value
   resets the lift and flags the trigger used; a saved mid-descent value
   (100-999) snaps the lift prop to its parked depth, marks the ride done and
   spawns the sentry at NP_catwalk. */
// FUNCTION: WIZ8 0x004DBEC0
bool OnCrankTriggerActivated(Trigger* trigger)
{
    glm::vec3 position;
    W8NpcState* npc;
    Trigger* pFlagTrigger;
    W8Prop* prop;
    int value;

    if (g_status.item_in_cursor) {
        return false;
    }
    if (!GetLocationVarIDByName("FlagPosition")) {
        CreateLocationVar("FlagPosition", 0);
        npc = GetNpcStateByKind(0x2a);
        QueueNpcScriptNotice(npc, 0, 0x90, true, 0);
        g_trigger_feedback = true;
        return false;
    }
    value = GetLocationVarValueByName("FlagPosition");
    if (value == 0) {
        ControlLiftGate(0xEFFFFFFF);
        trigger->flags &= ~W8_TRIGGER_ENABLED;
        npc = GetNpcStateByKind(0x2a);
        QueueNpcScriptNotice(npc, 0, 0x16, true, 0);
        return true;
    }
    if (value >= 100 && value < 1000) {
        pFlagTrigger = FindRequiredTriggerByName("flag");
        prop = pFlagTrigger->GetProp();
        position = prop->GetPosition();
        position.y = LIFT_PARKED_Y;
        prop->SetPosition(&position);
        SetTriggerVariableByName("FlagPosition", 1000);
        ReplaceOrCreateItem(&g_status.item_in_hand, 0x290, false, false, false);
        SetItemCursor(0);
        npc = GetNpcStateByKind(0x2a);
        QueueNpcScriptNotice(npc, 0, 0x18, true, 0);
        ResumeNpc(npc, 1);
        if (FindEntityByName("NP_catwalk", &position, 0, 0)) {
            SpawnMonsters(0x14a, 1, &position, 1, true, false, false);
        }
        return true;
    }
    return false;
}

static void CaptureLiftGate(W8SavedLocationState& state)
{
    state.emplace_back("FlagPosition",
                       g_lift_gate ? static_cast<int>(g_lift_gate->GetProgress() * 100.0f) : 0);
}
static void CaptureCampAlarm(W8SavedLocationState& state)
{
    state.emplace_back("UmpaniCampAlarm",
                       g_mt_gigas_outer_alarm_gate
                           ? static_cast<int>(g_mt_gigas_outer_alarm_gate->GetElapsedSeconds())
                           : 0);
}

/* The lift's master function. Registered while the gate runs so the
   dispatcher ticks it every frame with command zero; other callers command
   it directly: a positive command moves the lift that many percent of the
   ride; 0xEFFFFFFF re-arms the ride from the top. The prop slides from
   5228.6 down to -2000.0 while the gate runs; -5000.0 parks it below. */
// FUNCTION: WIZ8 0x004DC080
bool ControlLiftGate(int command)
{
    glm::vec3 position;
    Trigger* pFlagTrigger;
    float progress;
    int value;

    if (command != 0) {
        value = 0;
        if (command != static_cast<int>(0xEFFFFFFF)) {
            value = command;
        }
        pFlagTrigger = FindRequiredTriggerByName("flag");
        g_lift_prop = pFlagTrigger->GetProp();
        position = g_lift_prop->GetPosition();
        if (value < 100) {
            g_master_functions.push_back({ControlLiftGate, CaptureLiftGate});
            if (g_lift_gate) {
                g_lift_gate->Arm();
            } else {
                g_lift_gate.emplace(3.0f, false, true);
            }
            if (value > 0) {
                g_lift_gate->SetProgress(value * kMovementSpeedStep);
            }
            position.y =
                (LIFT_TOP_Y - LIFT_BOTTOM_Y) * (1.0f - value * kMovementSpeedStep) + LIFT_BOTTOM_Y;
        } else {
            if (value == 100) {
                position.y = LIFT_BOTTOM_Y;
            } else {
                position.y = LIFT_PARKED_Y;
            }
        }
        g_lift_prop->SetPosition(&position);
        return false;
    }
    if (!g_lift_gate) return true;
    const bool finished = g_lift_gate->PollFinished();
    progress = finished ? 1.0f : g_lift_gate->GetProgress();
    if (finished) SetTriggerVariableByName("FlagPosition", 100);
    position = g_lift_prop->GetPosition();
    position.y = (1.0f - progress) * (LIFT_TOP_Y - LIFT_BOTTOM_Y) + LIFT_BOTTOM_Y;
    g_lift_prop->SetPosition(&position);
    return finished;
}

/* Activation callback installed on the "Security Button" trigger. The first
   press lowers the Umpani faction disposition and starts the camp alarm;
   while the UmpaniCampAlarm variable exists the button does nothing. */
// FUNCTION: WIZ8 0x004DC390
bool OnSecurityButtonActivated(Trigger* trigger)
{
    g_trigger_feedback = true;
    if (GetLocationVarIDByName("UmpaniCampAlarm")) {
        return false;
    }
    SetFactionDispositionBand(4, W8_FACTION_HOSTILE);
    ControlCampAlarm(0xEFFFFFFF);
    return true;
}

/* The camp alarm's master function, same shape as ControlLiftGate: command
   zero ticks the alarm gate while registered; any other command plays Alarm1.wav at
   the camera and re-arms the gate (clamped to 30 seconds). When the gate
   finishes the sound is released and the variable is reset to zero. */
// FUNCTION: WIZ8 0x004DC3D0
bool ControlCampAlarm(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (!GetLocationVarIDByName("UmpaniCampAlarm")) CreateLocationVar("UmpaniCampAlarm", 30);
        position = glm::vec3(GetWorld()->camera->getLocation());
        if (static_cast<unsigned int>(command) > 0x1e) {
            command = 0x1e;
        }
        if (g_mt_gigas_outer_alarm_sound) g_mt_gigas_outer_alarm_sound->Stop();
        g_mt_gigas_outer_alarm_sound = nullptr;
        g_mt_gigas_outer_alarm_sound = CreateAndPlaySoundNode("Data\\Sound\\Ambients\\Alarm1.wav",
                                                              position, 1.0f, 75.0f, true);
        if (g_mt_gigas_outer_alarm_gate) {
            g_mt_gigas_outer_alarm_gate.reset();
        }
        g_mt_gigas_outer_alarm_gate.emplace(static_cast<float>(static_cast<unsigned int>(command)),
                                            false, true);
        if (g_mt_gigas_outer_alarm_gate) {
            g_master_functions.push_back({ControlCampAlarm, CaptureCampAlarm});
        }
    }
    if (!g_mt_gigas_outer_alarm_gate->PollFinished()) {
        return false;
    }
    if (g_mt_gigas_outer_alarm_sound) g_mt_gigas_outer_alarm_sound->Stop();
    g_mt_gigas_outer_alarm_sound = nullptr;
    g_mt_gigas_outer_alarm_gate.reset();
    SetTriggerVariableByName("UmpaniCampAlarm", 0);
    return true;
}

/* Activation callback on VOC_EWAXXSENTRYtrig: hands the sentry NPC (kind
   0x59) the item the player is holding, if any, as script argument -1. */
// FUNCTION: WIZ8 0x004DC600
bool OnSentryTriggerActivated(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x59);
    g_trigger_feedback = true;
    return true;
}

/* Activation callback on ewaxxdoortrigger03: the door only responds while an
   item in the 0x268-0x26e key range sits in the player's hand. */
// FUNCTION: WIZ8 0x004DC640
bool OnEwaxxDoor03Activated(Trigger* trigger)
{
    if (g_status.item_in_cursor) {
        const auto item_id = GetItemInHand();
        if (item_id >= 0x268 && item_id <= 0x26e) {
            return true;
        }
    }
    return false;
}

/* Activation callback on dummytrigger: every live member of monster kind
   0x180 gives birth once. */
// FUNCTION: WIZ8 0x004DC670
bool OnDummyTriggerActivated(Trigger* trigger)
{
    W8MonsterGroup* group = FindNextExistingMonsterByID(0x180, 0);

    while (group != 0) {
        GiveBirthToMonster(group);
        group = FindNextExistingMonsterByID(0x180, group);
    }
    return true;
}
