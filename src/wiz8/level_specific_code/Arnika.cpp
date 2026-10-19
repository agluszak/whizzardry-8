#include "wiz8/level_specific_code/Arnika.h"

#include <optional>
#include <string>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "layouts/item_instance.h"
#include "monster_actions.h"
#include "soundman.h"
#include "wiz8/cursor.h"
#include "wiz8/engine_code/GDCamera.h"
#include "wiz8/engine_code/IntervalGate.h"
#include "wiz8/engine_code/Monster.h"
#include "wiz8/engine_code/Navigator.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/engine_code/stSound3D.h"
#include "wiz8/fact_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/CombatHostility.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/xstatus.h"

struct W8NpcState;

/* Level Specific Code\Arnika.cpp (level 0, Arnika).

   Attribution evidence: the missing-trigger assertions quote this file's
   path at lines 752-764, 1026-1038, 1469, 1512, 1520 and 1543, and the
   assert expressions name the gEl01/gEl02 members. The level-0 block of
   InitializeLevelMasterFunctions registers the trigger cluster
   (ChaosMolori, Maddmook, CMbox, AstralDominae, Mookholo, MookFrontDoor,
   YellowButton, Vaultalarmdoor, Exitbutton, GenVault-2-door, ARN11,
   RedButton, El1-TopButtons, El1-BottomButtons, GreenButton, Elevator-02,
   LazerScanner, ScannerDoor). */

struct W8Elevator {
    W8ArnikaElevatorState state; /* 0x00: persisted as El0XState */
    int moving;                  /* 0x04: persisted as El0XMoving */
    int button_down;             /* 0x08: persisted as Red/GreenButtonDown */
    Trigger* pElevator;          /* 0x0c */
    Trigger* pLift;              /* 0x10 */
    Trigger* pTopDoor;           /* 0x14 */
    Trigger* pBottomDoor;        /* 0x18 */
    Trigger* pTopDoorCollide;    /* 0x1c */
    Trigger* pBottomDoorCollide; /* 0x20 */
    Trigger* pButton;            /* 0x24 */
    W8Prop* pProp;               /* 0x28: rep the moving master waits on */
};

// GLOBAL: WIZ8 0x00683570
static W8Elevator gEl01;
// GLOBAL: WIZ8 0x006835A0
static W8Elevator gEl02;

// GLOBAL: WIZ8 0x00613DCC
static bool g_red_button_armed = true;
// GLOBAL: WIZ8 0x006835EC
static W8Prop* g_el01_button_prop;
// GLOBAL: WIZ8 0x006835F0
static W8Prop* g_el02_button_prop;

// GLOBAL: WIZ8 0x006835CC
static W8Prop* g_lazer_prop;
// GLOBAL: WIZ8 0x006835D0
static W8Prop* g_exit_door_prop;
// GLOBAL: WIZ8 0x006835D4
static Trigger* g_exit_door_trigger;
// GLOBAL: WIZ8 0x006835D8
static int g_laser_scanning;
// GLOBAL: WIZ8 0x006835DC
static stSound3D* g_warning_loop;
// GLOBAL: WIZ8 0x006835E0
static stSound3D* g_warning_oneshot;
// GLOBAL: WIZ8 0x006835E4
static std::optional<W8IntervalGate> g_warning_gate;
// GLOBAL: WIZ8 0x006835E8
static W8Monster* g_mookholo_monster;

static void CaptureEl1Moving(W8SavedLocationState& state)
{
    state.emplace_back("El01Moving", 1);
}
static void CaptureEl2Moving(W8SavedLocationState& state)
{
    state.emplace_back("El02Moving", 1);
}
static void CaptureEl1Button(W8SavedLocationState& state)
{
    state.emplace_back("RedButtonDown", gEl01.button_down);
}
static void CaptureEl2Button(W8SavedLocationState& state)
{
    state.emplace_back("GreenButtonDown", gEl02.button_down);
}
static void CaptureLaserScan(W8SavedLocationState& state)
{
    state.emplace_back("LaserScanning", g_laser_scanning);
}
static void CaptureWarningSound(W8SavedLocationState& state)
{
    state.emplace_back("WarningSound", g_warning_gate
                                           ? static_cast<int>(g_warning_gate->GetElapsedSeconds())
                                           : 0x3e8);
}

struct ElevatorStateBindings {
    W8ArnikaElevatorId which;
    W8ArnikaElevatorState initial_state;
    const char* state_variable;
    const char* moving_variable;
    const char* button_variable;
    W8MasterFunction moving_master;
    W8MasterFunction button_master;
    bool open_bottom_on_first_load;
    void (*moving_state)(W8SavedLocationState&);
    void (*button_state)(W8SavedLocationState&);
};

static constexpr ElevatorStateBindings g_el01_bindings{
    W8_ARNIKA_ELEVATOR_ONE, W8_ARNIKA_ELEVATOR_STEP_1, "El01State",     "El01Moving",
    "RedButtonDown",        ArnikaEl1Moving,           ArnikaEl1Button, false,
    CaptureEl1Moving,       CaptureEl1Button};
static constexpr ElevatorStateBindings g_el02_bindings{
    W8_ARNIKA_ELEVATOR_TWO, W8_ARNIKA_ELEVATOR_STEP_7, "El02State",     "El02Moving",
    "GreenButtonDown",      ArnikaEl2Moving,           ArnikaEl2Button, true,
    CaptureEl2Moving,       CaptureEl2Button};

static void RestoreArnikaElevator(W8Elevator& elevator, const ElevatorStateBindings& bindings)
{
    elevator.state = bindings.initial_state;
    elevator.moving = 0;
    elevator.button_down = 0;
    if (!GetLocationVarIDByName(bindings.state_variable)) {
        CreateLocationVar(bindings.state_variable, elevator.state);
        if (bindings.open_bottom_on_first_load) {
            elevator.pBottomDoor->Run(-1);
            elevator.pBottomDoorCollide->Run(-1);
        }
    } else {
        elevator.state =
            static_cast<W8ArnikaElevatorState>(GetLocationVarValueByName(bindings.state_variable));
    }
    if (!GetLocationVarIDByName(bindings.moving_variable)) {
        CreateLocationVar(bindings.moving_variable, elevator.moving);
    } else {
        elevator.moving = GetLocationVarValueByName(bindings.moving_variable);
    }
    SetTriggerVariableByName(bindings.moving_variable, 0);
    if (!GetLocationVarIDByName(bindings.button_variable)) {
        CreateLocationVar(bindings.button_variable, elevator.button_down);
    } else {
        elevator.button_down = GetLocationVarValueByName(bindings.button_variable);
    }
    if (elevator.moving != 0) {
        switch (elevator.state) {
        case W8_ARNIKA_ELEVATOR_STEP_3:
        case W8_ARNIKA_ELEVATOR_STEP_7:
            break;
        case W8_ARNIKA_ELEVATOR_STEP_1:
        case W8_ARNIKA_ELEVATOR_STEP_5:
            elevator.pProp = elevator.pLift->GetProp();
            break;
        case W8_ARNIKA_ELEVATOR_STEP_2:
        case W8_ARNIKA_ELEVATOR_STEP_4:
            elevator.pProp = elevator.pTopDoor->GetProp();
            break;
        case W8_ARNIKA_ELEVATOR_STEP_6:
        case W8_ARNIKA_ELEVATOR_STEP_8:
            elevator.pProp = elevator.pBottomDoor->GetProp();
            break;
        }
        g_master_functions.push_back({bindings.moving_master, bindings.moving_state});
    }
    if (elevator.button_down != 0) {
        bindings.button_master(elevator.button_down);
    }
}

static bool RunArnikaElevatorButton(W8Elevator& elevator, W8Prop*& button_prop,
                                    const ElevatorStateBindings& bindings, int command)
{
    if (command != 0) {
        button_prop = elevator.pButton->GetProp();
        if (button_prop == 0) {
            return false;
        }
        g_master_functions.push_back({bindings.button_master, bindings.button_state});
        if (command != static_cast<int>(0xEFFFFFFF)) {
            elevator.button_down = command;
        } else {
            elevator.button_down = 1;
        }
        return false;
    }
    if (button_prop == 0) {
        return true;
    }
    if (button_prop->Rep()->animation_playing != 0) {
        return false;
    }
    if (elevator.button_down == 1) {
        elevator.button_down = 2;
        elevator.pButton->Run(-1);
        elevator.button_down = 3;
        return false;
    }
    elevator.button_down = 0;
    SetTriggerVariableByName(bindings.button_variable, 0);
    return true;
}

static bool RunArnikaElevatorMoving(W8Elevator& elevator, const ElevatorStateBindings& bindings,
                                    int command)
{
    if (elevator.pProp == 0) {
        return true;
    }
    if (elevator.pProp->Rep()->animation_playing != 0) {
        return false;
    }

    ArnikaElevatorAdvance(bindings.which);
    return true;
}

/* Level init: clears the laser-scanner and exit-door caches, runs both
   elevator setups, then restores the laser scan, the warning sound, the
   Screg flag and the inside-the-inn NPC teleport from the persisted
   location variables and fact 0xc1.  An active Screg ambush whose monster
   is still present ends setup before the teleport. */
// FUNCTION: WIZ8 0x004E06D0
void ArnikaLevelSetup(void)
{
    glm::vec3 position;
    W8NpcState* npc;
    W8MonsterInfo* monster_info;
    Trigger* pTrigger;

    g_lazer_prop = 0;
    g_exit_door_prop = 0;
    g_exit_door_trigger = 0;
    ArnikaElevator1Setup();
    ArnikaElevator2Setup();
    g_running_trigger_from_script = false;
    if (GetLocationVarIDByName("LaserScanning") &&
        GetLocationVarValueByName("LaserScanning") != 0) {
        pTrigger = FindTriggerByName("LazerScanner");
        if (pTrigger != 0) {
            g_lazer_prop = pTrigger->GetProp();
            g_laser_scanning = 1;
            BeginScriptedWorldAction();
            g_master_functions.push_back({ArnikaLaserScanMaster, CaptureLaserScan});
        }
    }
    if (GetLocationVarIDByName("WarningSound")) {
        int value = GetLocationVarValueByName("WarningSound");
        if (value != 0) {
            ArnikaWarningSound(value);
        }
    }
    if (GetLocationVarIDByName("ScregActive") && GetLocationVarValueByName("ScregActive") == 1) {
        npc = GetNpcStateByKind(0x17);
        if (npc != 0 && (monster_info = GetNpcMonsterInfo(npc)) != 0 && monster_info->p3D != 0) {
            return;
        }
        SetTriggerVariableByName("ScregActive", 0);
    }
    if (GetFact(W8_FACT_VI_RESCUED) != 0) {
        npc = GetNpcStateByKind(0x18);
        if (npc != 0) {
            monster_info = GetNpcMonsterInfo(npc);
            if (monster_info != 0 && monster_info->p3D != 0 &&
                FindEntityByName("Inside_Inn", &position, 0, 0)) {
                monster_info->p3D->SetPosition(&position);
            }
        }
    }
}

/* LazerScanner trigger: caches the scanner prop, refuses while its rep is
   still animating or the HLL door is already open, then arms the scan. */
// FUNCTION: WIZ8 0x004E0880
bool ArnikaLazerScanner(Trigger* pTrigger)
{
    g_lazer_prop = pTrigger->GetProp();
    if (g_lazer_prop->Rep()->animation_playing != 0) {
        return false;
    }
    if (GetLocationVarIDByName("HLLDoorOpen")) {
        return false;
    }
    g_laser_scanning = 1;
    BeginScriptedWorldAction();
    g_master_functions.push_back({ArnikaLaserScanMaster, CaptureLaserScan});
    return true;
}

/* Laser-scan master: a positive command re-arms the scan; a zero tick drops it once the prop's
   rep stops animating. */
// FUNCTION: WIZ8 0x004E0960
bool ArnikaLaserScanMaster(int command)
{
    if (command != 0) {
        g_laser_scanning = 1;
        BeginScriptedWorldAction();
        g_master_functions.push_back({ArnikaLaserScanMaster, CaptureLaserScan});
        return false;
    }
    if (g_lazer_prop == 0) {
        return true;
    }
    if (g_lazer_prop->Rep()->animation_playing != 0) {
        return false;
    }

    ClearMainGameTargetState();
    g_laser_scanning = 0;
    return true;
}

/* ScannerDoor trigger: the 0x27b key item opens the HLL door once. */
// FUNCTION: WIZ8 0x004E0A80
bool ArnikaScannerDoor(Trigger* pTrigger)
{
    if (!FindItemOnParty(0x27b, 0, 0, 2, 0)) {
        return false;
    }
    if (!GetLocationVarIDByName("HLLDoorOpen")) {
        CreateLocationVar("HLLDoorOpen", 1);
    }
    return true;
}

/* Warning-sound master: a positive command starts the ULLspawn alarm for
   that many seconds (or just the one-shot above 0x1e); a zero tick runs the countdown and
   stops the looped VOC when it expires. */
// FUNCTION: WIZ8 0x004E0AC0
bool ArnikaWarningSound(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (!GetLocationVarIDByName("WarningSound")) {
            CreateLocationVar("WarningSound", 0x1e);
        }
        if (!FindEntityByName("ULLspawn", &position, 0, 0)) {
            return false;
        }
        g_warning_gate.reset();
        if (command <= 0x1e) {
            g_warning_gate.emplace(static_cast<float>(command), false, true);
        } else {
            g_warning_oneshot = CreateAndPlaySoundNode(
                "Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_002.wav", position, 1.0f,
                100.0f, false);
        }
        g_warning_loop =
            CreateAndPlaySoundNode("Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_003.wav",
                                   position, 1.0f, 75.0f, true);
        if (g_warning_loop != 0 || g_warning_oneshot != 0) {
            g_master_functions.push_back({ArnikaWarningSound, CaptureWarningSound});
        }
    }
    if (g_warning_oneshot != 0) {
        if (g_warning_oneshot->IsPlaying()) {
            return false;
        }
        g_warning_oneshot = 0;
    }
    if (!g_warning_gate) {
        g_warning_gate.emplace(30.0f, false, true);
        return false;
    }
    if (!g_warning_gate->PollFinished()) {
        return false;
    }

    g_warning_loop->Stop();
    g_warning_gate.reset();
    SetTriggerVariableByName("WarningSound", 0);
    return true;
}

/* Mookholo trigger: spawns the Screg ambush once while neither ScregActive
   nor MookDoorOpen is set, then hands the spawned monster to the fade-out
   watch and queues the NPC notice. */
// FUNCTION: WIZ8 0x004E0DC0
bool ArnikaMookholo(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    if (gXStatus.fCombatMode) {
        return false;
    }
    if (GetLocationVarIDByName("ScregActive") && GetLocationVarValueByName("ScregActive") != 0) {
        return false;
    }
    if (GetLocationVarIDByName("MookDoorOpen") && GetLocationVarValueByName("MookDoorOpen") != 0) {
        return false;
    }
    if (FindEntityByName("Mookholo", &position, 0, 0)) {
        group = SpawnMonsters(0xb, 1, &position, 0, true, false, false);
        if (GetLocationVarIDByName("ScregActive")) {
            SetTriggerVariableByName("ScregActive", 1);
        } else {
            CreateLocationVar("ScregActive", 1);
        }
        location_id = group->MemberLocationId(0).value();
        if (location_id != 0) {
            info = FindMonster(location_id);
            if (info != 0) {
                g_mookholo_monster = info->p3D;
                g_npc_dialogue_closed = false;
                g_master_functions.push_back({ArnikaMookholoWatch});
            }
            QueueNpcScriptNotice(FindNpcBindingForMonster(FindMonster(location_id)), 0,
                                 std::nullopt, false, 0);
        }
    }
    return true;
}

/* Mookholo watch: the 0xEFFFFFFF toggle re-arms the wait, and a zero tick
   fades the spawned monster out once g_npc_dialogue_closed signals the NPC notice
   finished. */
// FUNCTION: WIZ8 0x004E0F70
bool ArnikaMookholoWatch(int command)
{
    if (command != 0) {
        if (command == static_cast<int>(0xEFFFFFFF)) {
            g_npc_dialogue_closed = false;
            g_master_functions.push_back({ArnikaMookholoWatch});
        }
        return false;
    }
    if (g_npc_dialogue_closed) {
        if (g_mookholo_monster != 0) {
            g_mookholo_monster->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_CLEAR_SCREG_ACTIVE);
            g_mookholo_monster = 0;
        }
        return true;
    }
    return false;
}

/* MookFrontDoor trigger: asserts the Mookholo trigger exists and opens the
   Mook door by latching MookDoorOpen. */
// FUNCTION: WIZ8 0x004E1040
bool ArnikaMookFrontDoor(Trigger* pTrigger)
{
    FindRequiredTriggerByName("Mookholo");
    if (GetLocationVarIDByName("MookDoorOpen")) {
        SetTriggerVariableByName("MookDoorOpen", 1);
    } else {
        CreateLocationVar("MookDoorOpen", 1);
    }
    return true;
}

/* YellowButton trigger: raises the "nothing happened" flag, spawns the
   bank guards at Bguards and puts the first on the guard script. */
// FUNCTION: WIZ8 0x004E10A0
bool ArnikaYellowButton(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;

    g_trigger_feedback = true;
    if (FindEntityByName("Bguards", &position, 0, 0)) {
        group = SpawnMonsters(0xc, 6, &position, 1, true, false, false);
        if (group != 0) {
            info = FindMonster(group->leader_location_id);
            info->p3D->SetScript("guard.msf", true);
        }
    }
    return true;
}

/* Vaultalarmdoor trigger: raises the "nothing happened" flag, plays the
   vault alarm, turns every guard group hostile and records fact 0xe0. */
// FUNCTION: WIZ8 0x004E1120
bool ArnikaVaultAlarmDoor(Trigger* pTrigger)
{
    W8MonsterGroup* group;

    g_trigger_feedback = true;
    wiz8::sound::play("Data\\Sound\\Ambients\\VaultAlarm.wav");
    group = FindNextExistingMonsterByID(0xc, 0);
    while (group != 0) {
        SetMonsterGroupHostility(group, W8_DISPOSITION_HOSTILE, false);
        group = FindNextExistingMonsterByID(0xc, group);
    }
    SetFact(W8_FACT_ARNIKA_VAULT_ENTERED, 1, false);
    return true;
}

/* Exitbutton trigger: caches the exit-door trigger and prop, toggles the
   Teleporting location variable, arms the teleport watch when a teleport
   starts and records facts 0xcd and 0xe0. */
// FUNCTION: WIZ8 0x004E1180
bool ArnikaExitButton(Trigger* pTrigger)
{
    g_exit_door_trigger = pTrigger;
    g_exit_door_prop = pTrigger->GetProp();
    if (GetLocationVarIDByName("Teleporting")) {
        if (GetLocationVarValueByName("Teleporting") == 0) {
            SetTriggerVariableByName("Teleporting", 1);
            g_master_functions.push_back({ArnikaTeleportWatch});
        } else {
            SetTriggerVariableByName("Teleporting", 0);
        }
    } else {
        CreateLocationVar("Teleporting", 1);
        g_master_functions.push_back({ArnikaTeleportWatch});
    }
    g_trigger_feedback = true;
    SetFact(W8_FACT_ARNIKA_VAULT_TELEPORT, 1, false);
    SetFact(W8_FACT_ARNIKA_VAULT_ENTERED, 1, false);
    return true;
}

/* Teleport watch: once the exit-door prop's rep finishes animating, runs
   the cached trigger and teleports the party to ARN11. */
// FUNCTION: WIZ8 0x004E1300
bool ArnikaTeleportWatch(int command)
{
    if (g_exit_door_prop->Rep()->animation_playing != 0) {
        return false;
    }

    g_exit_door_trigger->Run(-1);
    g_exit_door_trigger->RunDestination("ARN11");
    return true;
}

/* GenVault-2-door trigger: spawns the vault golem once at the Golem
   entity. */
// FUNCTION: WIZ8 0x004E1340
bool ArnikaGenVaultDoor(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("Golem", &position, 0, 0)) {
        if (GetLocationVarIDByName("GolemSpawned")) {
            return false;
        }
        CreateLocationVar("GolemSpawned", 1);
        SpawnMonsters(0x31, 1, &position, 1, true, false, false);
        wiz8::sound::play("Data\\Sound\\Ambients\\Temp Transporting.wav");
    }
    return true;
}

/* Elevator-1 setup: resolves the seven triggers, restores El01State,
   El01Moving and RedButtonDown from the location variables, re-arms the
   moving master against the door or lift prop that was mid-flight and
   re-arms the button master when the red button was down. */
// FUNCTION: WIZ8 0x004E13B0
void ArnikaElevator1Setup(void)
{
    gEl01.pElevator = FindRequiredTriggerByName("Elevator-1");
    gEl01.pLift = FindRequiredTriggerByName("El1-Lift");
    gEl01.pTopDoor = FindRequiredTriggerByName("El1-TopGate");
    gEl01.pBottomDoor = FindRequiredTriggerByName("El1-BottomGate");
    gEl01.pTopDoorCollide = FindRequiredTriggerByName("El1-TopGateC");
    gEl01.pBottomDoorCollide = FindRequiredTriggerByName("El1-BottomGateC");
    gEl01.pButton = FindRequiredTriggerByName("RedButton");
    RestoreArnikaElevator(gEl01, g_el01_bindings);
}

/* "RedButton": while the button press is armed it stamps the pressed fact,
   then either starts the lift (states 1/7) or re-arms the button master. */
// FUNCTION: WIZ8 0x004E1740
bool ArnikaRedButton(Trigger* pTrigger)
{
    if (!g_red_button_armed) {
        return false;
    }
    if (gEl01.button_down == 2) {
        return true;
    }
    if (gEl01.button_down != 0) {
        return false;
    }
    g_red_button_armed = false;
    SetFact(W8_FACT_ARNIKA_SAFETY_DEPOSIT_OPEN, 1, false);
    g_red_button_armed = true;
    g_trigger_feedback = true;
    if (gEl01.state != W8_ARNIKA_ELEVATOR_STEP_3) {
        if (gEl01.button_down == 2) {
            g_trigger_feedback = true;
            g_red_button_armed = true;
            return false;
        }
        if (gEl01.state != W8_ARNIKA_ELEVATOR_STEP_1 && gEl01.state != W8_ARNIKA_ELEVATOR_STEP_7) {
            g_trigger_feedback = true;
            g_red_button_armed = true;
            return false;
        }
        ArnikaElevatorAdvance(W8_ARNIKA_ELEVATOR_ONE);
    }
    ArnikaEl1Button(static_cast<int>(0xEFFFFFFF));
    return true;
}

/* The elevator-1 button master function. A nonzero command re-arms it: -1
   persists the button state, 0xEFFFFFFF presses the button, and any other
   value is the pending press. The command-0 run waits for the button prop's
   rep to finish, then runs the button trigger once and clears the press. */
// FUNCTION: WIZ8 0x004E17C0
bool ArnikaEl1Button(int command)
{
    return RunArnikaElevatorButton(gEl01, g_el01_button_prop, g_el01_bindings, command);
}

/* "El1-TopButtons": advances the lift while no button run is pending and the
   elevator is in a callable state; always reports handled. */
// FUNCTION: WIZ8 0x004E1930
bool ArnikaEl1TopButtons(Trigger* pTrigger)
{
    g_trigger_feedback = true;
    if (gEl01.button_down != 2 &&
        (gEl01.state == W8_ARNIKA_ELEVATOR_STEP_1 || gEl01.state == W8_ARNIKA_ELEVATOR_STEP_3 ||
         gEl01.state == W8_ARNIKA_ELEVATOR_STEP_7)) {
        ArnikaElevatorAdvance(W8_ARNIKA_ELEVATOR_ONE);
    }
    return true;
}

/* "El1-BottomButtons": same advance as the top call buttons. */
// FUNCTION: WIZ8 0x004E1970
bool ArnikaEl1BottomButtons(Trigger* pTrigger)
{
    return ArnikaEl1TopButtons(pTrigger);
}

/* The El01Moving master function: -1 stamps the location variable, and the
   command-0 run advances the elevator once the watched door or lift prop's
   rep has finished playing. */
// FUNCTION: WIZ8 0x004E19B0
bool ArnikaEl1Moving(int command)
{
    return RunArnikaElevatorMoving(gEl01, g_el01_bindings, command);
}

/* Elevator-02 setup: resolves its triggers, restores El02State, El02Moving
   and GreenButtonDown, opens the bottom doors on a fresh level and re-arms
   the moving and button masters. */
// FUNCTION: WIZ8 0x004E1A10
void ArnikaElevator2Setup(void)
{
    gEl02.pElevator = FindRequiredTriggerByName("Elevator-02");
    gEl02.pLift = FindRequiredTriggerByName("El-02Lift");
    gEl02.pTopDoor = FindRequiredTriggerByName("El-02TopDoor");
    gEl02.pBottomDoor = FindRequiredTriggerByName("El-02BottomDoor");
    gEl02.pTopDoorCollide = FindRequiredTriggerByName("El-02TopDoorCollide");
    gEl02.pBottomDoorCollide = FindRequiredTriggerByName("El-02BottomDoorCollide");
    gEl02.pButton = FindRequiredTriggerByName("GreenButton");
    gEl02.pProp = 0;
    RestoreArnikaElevator(gEl02, g_el02_bindings);
}

/* "GreenButton": while no press is pending it plays the acknowledgement,
   runs the elevator unless it is already at the top, and re-arms the button
   master. */
// FUNCTION: WIZ8 0x004E1DC0
bool ArnikaGreenButton(Trigger* pTrigger)
{
    if (gEl02.button_down == 2) {
        return true;
    }
    if (gEl02.button_down != 0) {
        return false;
    }
    SetFactionDispositionBand(0xa, W8_FACTION_HOSTILE);
    g_trigger_feedback = true;
    if (gEl02.state != W8_ARNIKA_ELEVATOR_STEP_3) {
        gEl02.pElevator->Run(-1);
    }
    ArnikaEl2Button(static_cast<int>(0xEFFFFFFF));
    return true;
}

/* The elevator-2 button master function, mirroring ArnikaEl1Button
   against GreenButtonDown. */
// FUNCTION: WIZ8 0x004E1E10
bool ArnikaEl2Button(int command)
{
    return RunArnikaElevatorButton(gEl02, g_el02_button_prop, g_el02_bindings, command);
}

/* "Elevator-02": steps the lift while no button run is pending and the
   elevator is in a callable state; answers false otherwise. */
// FUNCTION: WIZ8 0x004E1F80
bool ArnikaElevator02Trigger(Trigger* pTrigger)
{
    if (gEl02.button_down == 2) {
        return false;
    }
    if (gEl02.state != W8_ARNIKA_ELEVATOR_STEP_1 && gEl02.state != W8_ARNIKA_ELEVATOR_STEP_3 &&
        gEl02.state != W8_ARNIKA_ELEVATOR_STEP_7) {
        return false;
    }
    ArnikaElevatorAdvance(W8_ARNIKA_ELEVATOR_TWO);
    return true;
}

/* The El02Moving master function, mirroring ArnikaEl1Moving. */
// FUNCTION: WIZ8 0x004E1FB0
bool ArnikaEl2Moving(int command)
{
    return RunArnikaElevatorMoving(gEl02, g_el02_bindings, command);
}

/* Runs a door and its collide trigger for one elevator step and returns the door's prop. */
static W8Prop* RunElevatorDoor(Trigger* door, Trigger* collide)
{
    W8Prop* prop = door->GetProp();
    door->Run(-1);
    collide->Run(-1);
    return prop;
}

/* Runs the lift for one elevator step and returns its prop. */
static W8Prop* RunElevatorLift(Trigger* lift)
{
    W8Prop* prop = lift->GetProp();
    lift->Run(-1);
    return prop;
}

/* Shared elevator step: advances the selected elevator one state, running
   the door/lift triggers for the departing state and caching the rep the
   moving master will wait on. States 3 and 7 are the door-closed pauses that
   produce no prop. */
// FUNCTION: WIZ8 0x004E2010
void ArnikaElevatorAdvance(W8ArnikaElevatorId which)
{
    W8Elevator* el;
    /* An invalid state runs nothing and leaves no prop to wait on. */
    W8Prop* prop = nullptr;

    if (which == W8_ARNIKA_ELEVATOR_ONE) {
        el = &gEl01;
    } else {
        el = &gEl02;
    }
    switch (el->state) {
    case W8_ARNIKA_ELEVATOR_STEP_1:
        prop = RunElevatorDoor(el->pTopDoor, el->pTopDoorCollide);
        el->state = W8_ARNIKA_ELEVATOR_STEP_2;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_2:
        prop = 0;
        el->state = W8_ARNIKA_ELEVATOR_STEP_3;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_3:
        prop = RunElevatorDoor(el->pTopDoor, el->pTopDoorCollide);
        el->state = W8_ARNIKA_ELEVATOR_STEP_4;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_4:
        prop = RunElevatorLift(el->pLift);
        el->state = W8_ARNIKA_ELEVATOR_STEP_5;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_5:
        prop = RunElevatorDoor(el->pBottomDoor, el->pBottomDoorCollide);
        el->state = W8_ARNIKA_ELEVATOR_STEP_6;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_6:
        prop = 0;
        el->state = W8_ARNIKA_ELEVATOR_STEP_7;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_7:
        prop = RunElevatorDoor(el->pBottomDoor, el->pBottomDoorCollide);
        el->state = W8_ARNIKA_ELEVATOR_STEP_8;
        break;
    case W8_ARNIKA_ELEVATOR_STEP_8:
        prop = RunElevatorLift(el->pLift);
        el->state = W8_ARNIKA_ELEVATOR_STEP_1;
        break;
    }
    if (which == W8_ARNIKA_ELEVATOR_ONE) {
        SetTriggerVariableByName("El01State", el->state);
        gEl01.pProp = prop;
        if (prop != 0) {
            g_master_functions.push_back({ArnikaEl1Moving, CaptureEl1Moving});
        }
    } else {
        SetTriggerVariableByName("El02State", el->state);
        gEl02.pProp = prop;
        if (prop != 0) {
            g_master_functions.push_back({ArnikaEl2Moving, CaptureEl2Moving});
        }
    }
}

/* "ChaosMolori": rejects the activation when an item is on the cursor and
   the shared callback flag is clear; accepts when the cursor is empty or the
   flag is raised. */
// FUNCTION: WIZ8 0x004E2340
bool ArnikaChaosMolori(Trigger* pTrigger)
{
    if (!g_running_trigger_from_script && g_status.item_in_cursor) {
        return false;
    }
    return true;
}

/* "Maddmook": the first time the party has been warned, posts the mook's
   script notice and drops the faction band. */
// FUNCTION: WIZ8 0x004E2360
bool ArnikaMaddmook(Trigger* pTrigger)
{
    if (GetLocationVarIDByName("WarnedAboutEntry") &&
        GetLocationVarValueByName("WarnedAboutEntry") == 1 &&
        GetFactionDisposition(W8_FACTION_MOOK) != W8_FACTION_NEUTRAL) {
        W8NpcState* npc = GetNpcStateByKind(0x69);
        if (npc != 0) {
            QueueNpcScriptNotice(npc, 0, 7, false, 0);
            SetFactionDispositionBand(W8_FACTION_MOOK, W8_FACTION_HOSTILE);
        }
    }
    return true;
}

/* "AstralDominae": same cursor/flag gate as ChaosMolori, then forwards the
   activation to the CMBox trigger. */
// FUNCTION: WIZ8 0x004E23C0
bool ArnikaAstralDominae(Trigger* pTrigger)
{
    if (!g_running_trigger_from_script && g_status.item_in_cursor) {
        return false;
    }
    pTrigger = FindRequiredTriggerByName("CMBox");
    pTrigger->Run(-1);
    return true;
}

/* "CMbox": places the held relic on the pedestal through
   ArnikaPedestalItem, then runs the matching trigger inside the
   shared callback flag so the pedestal callbacks stay quiet. */
// FUNCTION: WIZ8 0x004E2420
bool ArnikaCMbox(Trigger* pTrigger)
{
    if (g_running_trigger_from_script) {
        return false;
    }
    const ArnikaPedestalResult result = ArnikaPedestalItem();
    if (!result.handled) return false;
    if (result.inserted_item == 0x244) {
        pTrigger = FindRequiredTriggerByName("ChaosMolori");
    } else if (result.inserted_item == 0x242 || result.inserted_item == 0x264) {
        pTrigger = FindRequiredTriggerByName("AstralDominae");
    } else {
        return true;
    }
    g_running_trigger_from_script = true;
    pTrigger->Run(-1);
    g_running_trigger_from_script = false;
    return true;
}

/* Pedestal item exchange: when a relic (0x242/0x244/0x264) is on the cursor
   it is consumed into the PedestalItem location variable and the stored item
   comes back to the cursor; on the first exchange it also aims the camera at
   the mook and posts the entry notice. The result distinguishes a handled
   exchange from no action and carries an inserted relic only when present. */
// FUNCTION: WIZ8 0x004E24E0
ArnikaPedestalResult ArnikaPedestalItem()
{
    W8NpcState* npc;
    W8MonsterInfo* info;
    glm::vec3 position;
    std::optional<int> item;
    std::optional<int> previous;

    if (g_status.item_in_cursor) {
        const auto held_item_id = GetItemInHand();
        if (!held_item_id ||
            (*held_item_id != 0x244 && *held_item_id != 0x242 && *held_item_id != 0x264)) {
            return {};
        }
        item = *held_item_id;
    }
    FindRequiredTriggerByName("ChaosMolori");
    if (item) {
        ClearHeldItemDisplay();
    }
    if (GetLocationVarIDByName("PedestalItem") && GetLocationVarIDByName("WarnedAboutEntry") &&
        GetLocationVarValueByName("WarnedAboutEntry") == 1) {
        if (!item) return {};
        SetTriggerVariableByName("PedestalItem", *item);
        if (GetLocationVarIDByName("WarnedAboutEntry")) {
            SetTriggerVariableByName("WarnedAboutEntry", 0);
        }
        return {true, item};
    }
    if (!GetLocationVarIDByName("PedestalItem")) {
        CreateLocationVar("PedestalItem", -1);
        previous = 0x244;
        if (!item) {
            ReplaceOrCreateItem(&g_status.item_in_hand, *previous, false, false, false);
        }
    } else {
        const int stored_item = GetLocationVarValueByName("PedestalItem");
        if (stored_item != -1) previous = stored_item;
        if (!previous) {
            if (!item) return {};
        } else if (!item) {
            ReplaceOrCreateItem(&g_status.item_in_hand, *previous, false, false, false);
        }
    }
    SetItemCursor(0);
    npc = GetNpcStateByKind(0x69);
    if (npc != 0) {
        info = GetNpcMonsterInfo(npc);
        if (info != 0) {
            position = info->p3D->movement.position;
            position.y += info->p3D->movement.height_offset;
            PartyCamera().LookAt(&position, false);
        }
        QueueNpcScriptNotice(npc, 0, 8, false, 0);
    }
    if (!GetLocationVarIDByName("WarnedAboutEntry")) {
        CreateLocationVar("WarnedAboutEntry", 1);
    } else {
        SetTriggerVariableByName("WarnedAboutEntry", 1);
    }
    return {true, item};
}

/* The "BallSlot" activation callback: on the first ball insert it creates
   item 0x240 and hands it to the kind-0x16 NPC's script notice queue. */
// FUNCTION: WIZ8 0x004E26F0
bool ArnikaBallSlot(Trigger* pTrigger)
{
    W8ItemInstance item;
    W8NpcState* npc;

    if (!GetLocationVarIDByName("BallInserted")) {
        CreateLocationVar("BallInserted", 1);
        npc = GetNpcStateByKind(0x16);
        ReplaceOrCreateItem(&item, 0x240, true, true, false);
        QueueNpcScriptNotice(npc, &item, std::nullopt, false, 0);
    }
    g_trigger_feedback = true;
    return true;
}

/* The "Flightrecordertrigger" activation callback: outside NPC dialogue it
   queues the kind-0x14 NPC's script notice, passing the held item when the
   cursor holds one. */
// FUNCTION: WIZ8 0x004E2760
bool ArnikaFlightRecorder(Trigger* pTrigger)
{
    W8ItemInstance* item;
    W8NpcState* npc;

    if (gXStatus.fNpcDialogueMode) {
        return false;
    }
    g_trigger_feedback = true;
    npc = GetNpcStateByKind(0x14);
    item = 0;
    if (g_status.item_in_cursor) {
        item = &g_status.item_in_hand;
    }
    QueueNpcScriptNotice(npc, item, std::nullopt, false, 0);
    return false;
}
