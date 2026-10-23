#include "wiz8/level_specific_code/Arnika.h"

#include <memory>
#include <optional>
#include <utility>

#include "glm/ext/vector_float3.hpp"
#include "monster_actions.h"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/combat/hostility.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/npc_interaction.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/rules/activity.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/item_location.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

struct W8NpcState;

/* Level Specific Code\Arnika.cpp (level 0, Arnika).

   Attribution evidence: the missing-trigger assertions quote this file's
   path at lines 752-764, 1026-1038, 1469, 1512, 1520 and 1543, and the
   assert expressions name the gEl01/gEl02 members. The level-0 script binds
   the trigger cluster (ChaosMolori, Maddmook, CMbox, AstralDominae, Mookholo,
   MookFrontDoor, YellowButton, Vaultalarmdoor, Exitbutton, GenVault-2-door,
   ARN11, RedButton, El1-TopButtons, El1-BottomButtons, GreenButton,
   Elevator-02, LazerScanner, ScannerDoor). */

namespace {

enum W8ArnikaElevatorId : unsigned char { W8_ARNIKA_ELEVATOR_ONE = 1, W8_ARNIKA_ELEVATOR_TWO = 2 };

enum W8ArnikaElevatorState : unsigned char {
    W8_ARNIKA_ELEVATOR_STEP_1 = 1,
    W8_ARNIKA_ELEVATOR_STEP_2 = 2,
    W8_ARNIKA_ELEVATOR_STEP_3 = 3,
    W8_ARNIKA_ELEVATOR_STEP_4 = 4,
    W8_ARNIKA_ELEVATOR_STEP_5 = 5,
    W8_ARNIKA_ELEVATOR_STEP_6 = 6,
    W8_ARNIKA_ELEVATOR_STEP_7 = 7,
    W8_ARNIKA_ELEVATOR_STEP_8 = 8
};

/* The master command that presses a button or re-arms a watch. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

struct W8Elevator {
    W8ArnikaElevatorState state = W8_ARNIKA_ELEVATOR_STEP_1; /* persisted as El0XState */
    int moving = 0;                /* persisted as El0XMoving */
    int button_down = 0;           /* persisted as Red/GreenButtonDown */
    Trigger* pElevator = nullptr;
    Trigger* pLift = nullptr;
    Trigger* pTopDoor = nullptr;
    Trigger* pBottomDoor = nullptr;
    Trigger* pTopDoorCollide = nullptr;
    Trigger* pBottomDoorCollide = nullptr;
    Trigger* pButton = nullptr;
    Prop* pProp = nullptr; /* the rep the moving master waits on */
};

class ArnikaScript;

struct ElevatorStateBindings {
    W8ArnikaElevatorId which;
    W8ArnikaElevatorState initial_state;
    const char* state_variable;
    const char* moving_variable;
    const char* button_variable;
    bool (ArnikaScript::*moving_master)(int);
    bool (ArnikaScript::*button_master)(int);
    bool open_bottom_on_first_load;
    void (ArnikaScript::*moving_state)(W8SavedLocationState&);
    void (ArnikaScript::*button_state)(W8SavedLocationState&);
};

class ArnikaScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

    bool el1Moving(int command);
    bool el1Button(int command);
    bool el2Moving(int command);
    bool el2Button(int command);
    void captureEl1Moving(W8SavedLocationState& state);
    void captureEl2Moving(W8SavedLocationState& state);
    void captureEl1Button(W8SavedLocationState& state);
    void captureEl2Button(W8SavedLocationState& state);

private:
    void setup();
    void restoreElevator(W8Elevator& elevator, const ElevatorStateBindings& bindings);
    bool runElevatorButton(W8Elevator& elevator, Prop*& button_prop,
                           const ElevatorStateBindings& bindings, int command);
    bool runElevatorMoving(W8Elevator& elevator, const ElevatorStateBindings& bindings);
    void elevator1Setup();
    void elevator2Setup();
    void elevatorAdvance(W8ArnikaElevatorId which);
    int pedestalItem(int* previous_item);

    bool lazerScanner(Trigger* pTrigger);
    bool laserScanMaster(int command);
    bool scannerDoor(Trigger* pTrigger);
    bool warningSound(int command);
    bool mookholo(Trigger* pTrigger);
    bool mookholoWatch(int command);
    bool mookFrontDoor(Trigger* pTrigger);
    bool yellowButton(Trigger* pTrigger);
    bool vaultAlarmDoor(Trigger* pTrigger);
    bool exitButton(Trigger* pTrigger);
    bool teleportWatch(int command);
    bool genVaultDoor(Trigger* pTrigger);
    bool redButton(Trigger* pTrigger);
    bool el1TopButtons(Trigger* pTrigger);
    bool greenButton(Trigger* pTrigger);
    bool elevator02Trigger(Trigger* pTrigger);
    bool chaosMolori(Trigger* pTrigger);
    bool maddmook(Trigger* pTrigger);
    bool astralDominae(Trigger* pTrigger);
    bool cmBox(Trigger* pTrigger);
    bool ballSlot(Trigger* pTrigger);
    bool flightRecorder(Trigger* pTrigger);
    void captureLaserScan(W8SavedLocationState& state);
    void captureWarningSound(W8SavedLocationState& state);

    W8Elevator el01_;
    W8Elevator el02_;
    bool red_button_armed_ = true;
    Prop* el01_button_prop_ = nullptr;
    Prop* el02_button_prop_ = nullptr;
    Prop* lazer_prop_ = nullptr;
    Prop* exit_door_prop_ = nullptr;
    Trigger* exit_door_trigger_ = nullptr;
    int laser_scanning_ = 0;
    Sound3D* warning_loop_ = nullptr;
    Sound3D* warning_oneshot_ = nullptr;
    std::optional<W8IntervalGate> warning_gate_;
    // Resolved when used: the monster can be killed or removed while the party talks to it.
    int mookholo_monster_id_ = 0;
    /* Raised while the pedestal runs a relic trigger itself, so the relic
       callbacks let it through with an item in hand. */
    bool running_trigger_from_script_ = false;
};

constexpr ElevatorStateBindings kEl01Bindings{W8_ARNIKA_ELEVATOR_ONE,
                                              W8_ARNIKA_ELEVATOR_STEP_1,
                                              "El01State",
                                              "El01Moving",
                                              "RedButtonDown",
                                              &ArnikaScript::el1Moving,
                                              &ArnikaScript::el1Button,
                                              false,
                                              &ArnikaScript::captureEl1Moving,
                                              &ArnikaScript::captureEl1Button};
constexpr ElevatorStateBindings kEl02Bindings{W8_ARNIKA_ELEVATOR_TWO,
                                              W8_ARNIKA_ELEVATOR_STEP_7,
                                              "El02State",
                                              "El02Moving",
                                              "GreenButtonDown",
                                              &ArnikaScript::el2Moving,
                                              &ArnikaScript::el2Button,
                                              true,
                                              &ArnikaScript::captureEl2Moving,
                                              &ArnikaScript::captureEl2Button};

// Activation callback that only stops the trigger.
bool BlockTriggerActivation(Trigger*)
{
    return false;
}

void ArnikaScript::enter()
{
    bindTrigger("ChaosMolori", &ArnikaScript::chaosMolori);
    bindTrigger("Maddmook", &ArnikaScript::maddmook);
    bindTrigger("CMbox", &ArnikaScript::cmBox);
    bindTrigger("AstralDominae", &ArnikaScript::astralDominae);
    bindTrigger("BallSlot", &ArnikaScript::ballSlot);
    bindTrigger("Flightrecordertrigger", &ArnikaScript::flightRecorder);
    bindTrigger("ULLspawn", [](Trigger*) { return true; });
    bindTrigger("Mookholo", &ArnikaScript::mookholo);
    bindTrigger("MookFrontDoor", &ArnikaScript::mookFrontDoor);
    bindTrigger("YellowButton", &ArnikaScript::yellowButton);
    bindTrigger("Vaultalarmdoor", &ArnikaScript::vaultAlarmDoor);
    bindTrigger("Exitbutton", &ArnikaScript::exitButton);
    bindTrigger("GenVault-2-door", &ArnikaScript::genVaultDoor);
    bindTrigger("ARN11", BlockTriggerActivation);
    bindTrigger("RedButton", &ArnikaScript::redButton);
    // The bottom call buttons do what the top ones do.
    bindTrigger("El1-TopButtons", &ArnikaScript::el1TopButtons);
    bindTrigger("El1-BottomButtons", &ArnikaScript::el1TopButtons);
    bindTrigger("GreenButton", &ArnikaScript::greenButton);
    bindTrigger("Elevator-02", &ArnikaScript::elevator02Trigger);
    bindTrigger("LazerScanner", &ArnikaScript::lazerScanner);
    Trigger* pTrigger = FindRequiredTriggerByName("ScannerDoor");
    pTrigger->m_lData1 = 0;
    pTrigger->activation_callback = callbackOf(&ArnikaScript::scannerDoor);
    setup();
}

void ArnikaScript::captureEl1Moving(W8SavedLocationState& state)
{
    state.emplace_back("El01Moving", 1);
}
void ArnikaScript::captureEl2Moving(W8SavedLocationState& state)
{
    state.emplace_back("El02Moving", 1);
}
void ArnikaScript::captureEl1Button(W8SavedLocationState& state)
{
    state.emplace_back("RedButtonDown", el01_.button_down);
}
void ArnikaScript::captureEl2Button(W8SavedLocationState& state)
{
    state.emplace_back("GreenButtonDown", el02_.button_down);
}
void ArnikaScript::captureLaserScan(W8SavedLocationState& state)
{
    state.emplace_back("LaserScanning", laser_scanning_);
}
void ArnikaScript::captureWarningSound(W8SavedLocationState& state)
{
    state.emplace_back("WarningSound", warning_gate_
                                           ? static_cast<int>(warning_gate_->GetElapsedSeconds())
                                           : 0x3e8);
}

void ArnikaScript::restoreElevator(W8Elevator& elevator, const ElevatorStateBindings& bindings)
{
    elevator.state = bindings.initial_state;
    elevator.moving = 0;
    elevator.button_down = 0;
    if (GetLocationVarIDByName(bindings.state_variable) == -1) {
        CreateLocationVar(bindings.state_variable, elevator.state);
        if (bindings.open_bottom_on_first_load) {
            elevator.pBottomDoor->Run(-1);
            elevator.pBottomDoorCollide->Run(-1);
        }
    } else {
        elevator.state =
            static_cast<W8ArnikaElevatorState>(GetLocationVarValueByName(bindings.state_variable));
    }
    if (GetLocationVarIDByName(bindings.moving_variable) == -1) {
        CreateLocationVar(bindings.moving_variable, elevator.moving);
    } else {
        elevator.moving = GetLocationVarValueByName(bindings.moving_variable);
    }
    SetTriggerVariableByName(bindings.moving_variable, 0);
    if (GetLocationVarIDByName(bindings.button_variable) == -1) {
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
        addMaster(bindings.moving_master, bindings.moving_state);
    }
    if (elevator.button_down != 0) {
        (this->*bindings.button_master)(elevator.button_down);
    }
}

bool ArnikaScript::runElevatorButton(W8Elevator& elevator, Prop*& button_prop,
                                     const ElevatorStateBindings& bindings, int command)
{
    if (command != 0) {
        button_prop = elevator.pButton->GetProp();
        if (button_prop == 0) {
            return false;
        }
        addMaster(bindings.button_master, bindings.button_state);
        if (command != kMasterToggle) {
            elevator.button_down = command;
        } else {
            elevator.button_down = 1;
        }
        return false;
    }
    if (button_prop == 0) {
        return true;
    }
    if (button_prop->animation_playing != 0) {
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

bool ArnikaScript::runElevatorMoving(W8Elevator& elevator, const ElevatorStateBindings& bindings)
{
    if (elevator.pProp == 0) {
        return true;
    }
    if (elevator.pProp->animation_playing != 0) {
        return false;
    }

    elevatorAdvance(bindings.which);
    return true;
}

/* Level setup: runs both elevator setups, then restores the laser scan, the
   warning sound, the Screg flag and the inside-the-inn NPC teleport from the
   persisted location variables and fact 0xc1. An active Screg ambush whose
   monster is still present ends setup before the teleport. */
void ArnikaScript::setup()
{
    glm::vec3 position;
    W8NpcState* npc;
    W8MonsterInfo* monster_info;
    Trigger* pTrigger;

    elevator1Setup();
    elevator2Setup();
    if (GetLocationVarIDByName("LaserScanning") != -1 &&
        GetLocationVarValueByName("LaserScanning") != 0) {
        pTrigger = FindTriggerByName("LazerScanner");
        if (pTrigger != 0) {
            lazer_prop_ = pTrigger->GetProp();
            laser_scanning_ = 1;
            BeginScriptedWorldAction();
            addMaster(&ArnikaScript::laserScanMaster, &ArnikaScript::captureLaserScan);
        }
    }
    if (GetLocationVarIDByName("WarningSound") != -1) {
        int value = GetLocationVarValueByName("WarningSound");
        if (value != 0) {
            warningSound(value);
        }
    }
    if (GetLocationVarIDByName("ScregActive") != -1 &&
        GetLocationVarValueByName("ScregActive") == 1) {
        npc = GetNpcStateByKind(0x17);
        if (npc != 0 && (monster_info = GetNpcMonsterInfo(npc)) != 0 && monster_info->entity) {
            return;
        }
        SetTriggerVariableByName("ScregActive", 0);
    }
    if (GetFact(W8_FACT_VI_RESCUED) != 0) {
        npc = GetNpcStateByKind(0x18);
        if (npc != 0) {
            monster_info = GetNpcMonsterInfo(npc);
            if (monster_info != 0 && monster_info->entity &&
                FindEntityByName("Inside_Inn", &position, 0, 0)) {
                monster_info->entity->SetPosition(&position);
            }
        }
    }
}

/* LazerScanner trigger: caches the scanner prop, refuses while its rep is
   still animating or the HLL door is already open, then arms the scan. */
bool ArnikaScript::lazerScanner(Trigger* pTrigger)
{
    lazer_prop_ = pTrigger->GetProp();
    if (lazer_prop_->animation_playing != 0) {
        return false;
    }
    if (GetLocationVarIDByName("HLLDoorOpen") != -1) {
        return false;
    }
    laser_scanning_ = 1;
    BeginScriptedWorldAction();
    addMaster(&ArnikaScript::laserScanMaster, &ArnikaScript::captureLaserScan);
    return true;
}

/* Laser-scan master: a positive command re-arms the scan; a zero tick drops it once the prop's
   rep stops animating. */
bool ArnikaScript::laserScanMaster(int command)
{
    if (command != 0) {
        laser_scanning_ = 1;
        BeginScriptedWorldAction();
        addMaster(&ArnikaScript::laserScanMaster, &ArnikaScript::captureLaserScan);
        return false;
    }
    if (lazer_prop_ == 0) {
        return true;
    }
    if (lazer_prop_->animation_playing != 0) {
        return false;
    }

    ClearMainGameTargetState();
    laser_scanning_ = 0;
    return true;
}

/* ScannerDoor trigger: the 0x27b key item opens the HLL door once. */
bool ArnikaScript::scannerDoor(Trigger* pTrigger)
{
    if (!ctx.inventory.has(0x27b)) {
        return false;
    }
    if (GetLocationVarIDByName("HLLDoorOpen") == -1) {
        CreateLocationVar("HLLDoorOpen", 1);
    }
    return true;
}

/* Warning-sound master: a positive command starts the ULLspawn alarm for
   that many seconds (or just the one-shot above 0x1e); a zero tick runs the countdown and
   stops the looped VOC when it expires. */
bool ArnikaScript::warningSound(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (GetLocationVarIDByName("WarningSound") == -1) {
            CreateLocationVar("WarningSound", 0x1e);
        }
        if (!FindEntityByName("ULLspawn", &position, 0, 0)) {
            return false;
        }
        warning_gate_.reset();
        if (command <= 0x1e) {
            warning_gate_.emplace(static_cast<float>(command), TimerClock::World, true);
        } else {
            warning_oneshot_ = CreateAndPlaySoundNode(
                world(), "Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_002.wav", position,
                1.0f, 100.0f, false);
        }
        warning_loop_ = CreateAndPlaySoundNode(
            world(), "Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_003.wav", position, 1.0f,
            75.0f, true);
        if (warning_loop_ != 0 || warning_oneshot_ != 0) {
            addMaster(&ArnikaScript::warningSound, &ArnikaScript::captureWarningSound);
        }
    }
    if (warning_oneshot_ != 0) {
        if (warning_oneshot_->IsPlaying()) {
            return false;
        }
        warning_oneshot_ = 0;
    }
    if (!warning_gate_) {
        warning_gate_.emplace(30.0f, TimerClock::World, true);
        return false;
    }
    if (!warning_gate_->PollFinished()) {
        return false;
    }

    if (warning_loop_) warning_loop_->Stop();
    warning_gate_.reset();
    SetTriggerVariableByName("WarningSound", 0);
    return true;
}

/* Mookholo trigger: spawns the Screg ambush once while neither ScregActive
   nor MookDoorOpen is set, then hands the spawned monster to the fade-out
   watch and queues the NPC notice. */
bool ArnikaScript::mookholo(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    if (ctx.combat.has_value()) {
        return false;
    }
    if (GetLocationVarIDByName("ScregActive") != -1 &&
        GetLocationVarValueByName("ScregActive") != 0) {
        return false;
    }
    if (GetLocationVarIDByName("MookDoorOpen") != -1 &&
        GetLocationVarValueByName("MookDoorOpen") != 0) {
        return false;
    }
    if (FindEntityByName("Mookholo", &position, 0, 0)) {
        group = SpawnMonsters(0xb, 1, &position, 0, true, false);
        if (GetLocationVarIDByName("ScregActive") != -1) {
            SetTriggerVariableByName("ScregActive", 1);
        } else {
            CreateLocationVar("ScregActive", 1);
        }
        location_id = group->MemberLocationId(0);
        if (location_id != 0) {
            info = FindMonster(location_id);
            if (info != 0) {
                mookholo_monster_id_ = location_id;
                npc_dialogue_closed = false;
                addMaster(&ArnikaScript::mookholoWatch);
            }
            QueueNpcScriptNotice(FindNpcBindingForMonster(FindMonster(location_id)), 0, -1, false,
                                 0);
        }
    }
    return true;
}

/* Mookholo watch: the toggle command re-arms the wait, and a zero tick fades
   the spawned monster out once the NPC dialogue has closed. */
bool ArnikaScript::mookholoWatch(int command)
{
    if (command != 0) {
        if (command == kMasterToggle) {
            npc_dialogue_closed = false;
            addMaster(&ArnikaScript::mookholoWatch);
        }
        return false;
    }
    if (npc_dialogue_closed) {
        if (Monster* mookholo = GetMonsterByLocationID(std::exchange(mookholo_monster_id_, 0)))
            mookholo->BeginFadeOutAndRemove(W8_MONSTER_REMOVAL_CLEAR_SCREG_ACTIVE);
        return true;
    }
    return false;
}

/* MookFrontDoor trigger: asserts the Mookholo trigger exists and opens the
   Mook door by latching MookDoorOpen. */
bool ArnikaScript::mookFrontDoor(Trigger* pTrigger)
{
    FindRequiredTriggerByName("Mookholo");
    if (GetLocationVarIDByName("MookDoorOpen") != -1) {
        SetTriggerVariableByName("MookDoorOpen", 1);
    } else {
        CreateLocationVar("MookDoorOpen", 1);
    }
    return true;
}

/* YellowButton trigger: raises the "nothing happened" flag, spawns the
   bank guards at Bguards and puts the first on the guard script. */
bool ArnikaScript::yellowButton(Trigger* pTrigger)
{
    glm::vec3 position;
    W8MonsterGroup* group;
    W8MonsterInfo* info;

    g_trigger_feedback = true;
    if (FindEntityByName("Bguards", &position, 0, 0)) {
        group = SpawnMonsters(0xc, 6, &position, 1, true, false);
        if (group != 0) {
            info = FindMonster(group->leader_location_id);
            info->entity->SetScript("guard.msf", true);
        }
    }
    return true;
}

/* Vaultalarmdoor trigger: raises the "nothing happened" flag, plays the
   vault alarm, turns every guard group hostile and records fact 0xe0. */
bool ArnikaScript::vaultAlarmDoor(Trigger* pTrigger)
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
bool ArnikaScript::exitButton(Trigger* pTrigger)
{
    exit_door_trigger_ = pTrigger;
    exit_door_prop_ = pTrigger->GetProp();
    if (GetLocationVarIDByName("Teleporting") != -1) {
        if (GetLocationVarValueByName("Teleporting") == 0) {
            SetTriggerVariableByName("Teleporting", 1);
            addMaster(&ArnikaScript::teleportWatch);
        } else {
            SetTriggerVariableByName("Teleporting", 0);
        }
    } else {
        CreateLocationVar("Teleporting", 1);
        addMaster(&ArnikaScript::teleportWatch);
    }
    g_trigger_feedback = true;
    SetFact(W8_FACT_ARNIKA_VAULT_TELEPORT, 1, false);
    SetFact(W8_FACT_ARNIKA_VAULT_ENTERED, 1, false);
    return true;
}

/* Teleport watch: once the exit-door prop's rep finishes animating, runs
   the cached trigger and teleports the party to ARN11. */
bool ArnikaScript::teleportWatch(int command)
{
    if (exit_door_prop_->animation_playing != 0) {
        return false;
    }

    exit_door_trigger_->Run(-1);
    exit_door_trigger_->RunDestination("ARN11");
    return true;
}

/* GenVault-2-door trigger: spawns the vault golem once at the Golem
   entity. */
bool ArnikaScript::genVaultDoor(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("Golem", &position, 0, 0)) {
        if (GetLocationVarIDByName("GolemSpawned") != -1) {
            return false;
        }
        CreateLocationVar("GolemSpawned", 1);
        SpawnMonsters(0x31, 1, &position, 1, true, false);
        wiz8::sound::play("Data\\Sound\\Ambients\\Temp Transporting.wav");
    }
    return true;
}

/* Elevator-1 setup: resolves the seven triggers, restores El01State,
   El01Moving and RedButtonDown from the location variables, re-arms the
   moving master against the door or lift prop that was mid-flight and
   re-arms the button master when the red button was down. */
void ArnikaScript::elevator1Setup()
{
    el01_.pElevator = FindRequiredTriggerByName("Elevator-1");
    el01_.pLift = FindRequiredTriggerByName("El1-Lift");
    el01_.pTopDoor = FindRequiredTriggerByName("El1-TopGate");
    el01_.pBottomDoor = FindRequiredTriggerByName("El1-BottomGate");
    el01_.pTopDoorCollide = FindRequiredTriggerByName("El1-TopGateC");
    el01_.pBottomDoorCollide = FindRequiredTriggerByName("El1-BottomGateC");
    el01_.pButton = FindRequiredTriggerByName("RedButton");
    restoreElevator(el01_, kEl01Bindings);
}

/* "RedButton": while the button press is armed it stamps the pressed fact,
   then either starts the lift (states 1/7) or re-arms the button master. */
bool ArnikaScript::redButton(Trigger* pTrigger)
{
    if (!red_button_armed_) {
        return false;
    }
    if (el01_.button_down == 2) {
        return true;
    }
    if (el01_.button_down != 0) {
        return false;
    }
    red_button_armed_ = false;
    SetFact(W8_FACT_ARNIKA_SAFETY_DEPOSIT_OPEN, 1, false);
    red_button_armed_ = true;
    g_trigger_feedback = true;
    if (el01_.state != W8_ARNIKA_ELEVATOR_STEP_3) {
        if (el01_.button_down == 2) {
            g_trigger_feedback = true;
            red_button_armed_ = true;
            return false;
        }
        if (el01_.state != W8_ARNIKA_ELEVATOR_STEP_1 && el01_.state != W8_ARNIKA_ELEVATOR_STEP_7) {
            g_trigger_feedback = true;
            red_button_armed_ = true;
            return false;
        }
        elevatorAdvance(W8_ARNIKA_ELEVATOR_ONE);
    }
    el1Button(kMasterToggle);
    return true;
}

/* The elevator-1 button master function. A nonzero command re-arms it: -1
   persists the button state, the toggle presses the button, and any other
   value is the pending press. The command-0 run waits for the button prop's
   rep to finish, then runs the button trigger once and clears the press. */
bool ArnikaScript::el1Button(int command)
{
    return runElevatorButton(el01_, el01_button_prop_, kEl01Bindings, command);
}

/* "El1-TopButtons" and "El1-BottomButtons": advances the lift while no button
   run is pending and the elevator is in a callable state; always reports
   handled. */
bool ArnikaScript::el1TopButtons(Trigger* pTrigger)
{
    g_trigger_feedback = true;
    if (el01_.button_down != 2 &&
        (el01_.state == W8_ARNIKA_ELEVATOR_STEP_1 || el01_.state == W8_ARNIKA_ELEVATOR_STEP_3 ||
         el01_.state == W8_ARNIKA_ELEVATOR_STEP_7)) {
        elevatorAdvance(W8_ARNIKA_ELEVATOR_ONE);
    }
    return true;
}

/* The El01Moving master function: the command-0 run advances the elevator
   once the watched door or lift prop's rep has finished playing. */
bool ArnikaScript::el1Moving(int command)
{
    return runElevatorMoving(el01_, kEl01Bindings);
}

/* Elevator-02 setup: resolves its triggers, restores El02State, El02Moving
   and GreenButtonDown, opens the bottom doors on a fresh level and re-arms
   the moving and button masters. */
void ArnikaScript::elevator2Setup()
{
    el02_.pElevator = FindRequiredTriggerByName("Elevator-02");
    el02_.pLift = FindRequiredTriggerByName("El-02Lift");
    el02_.pTopDoor = FindRequiredTriggerByName("El-02TopDoor");
    el02_.pBottomDoor = FindRequiredTriggerByName("El-02BottomDoor");
    el02_.pTopDoorCollide = FindRequiredTriggerByName("El-02TopDoorCollide");
    el02_.pBottomDoorCollide = FindRequiredTriggerByName("El-02BottomDoorCollide");
    el02_.pButton = FindRequiredTriggerByName("GreenButton");
    el02_.pProp = 0;
    restoreElevator(el02_, kEl02Bindings);
}

/* "GreenButton": while no press is pending it plays the acknowledgement,
   runs the elevator unless it is already at the top, and re-arms the button
   master. */
bool ArnikaScript::greenButton(Trigger* pTrigger)
{
    if (el02_.button_down == 2) {
        return true;
    }
    if (el02_.button_down != 0) {
        return false;
    }
    SetFactionDispositionBand(0xa, W8_FACTION_HOSTILE);
    g_trigger_feedback = true;
    if (el02_.state != W8_ARNIKA_ELEVATOR_STEP_3) {
        el02_.pElevator->Run(-1);
    }
    el2Button(kMasterToggle);
    return true;
}

/* The elevator-2 button master function, mirroring el1Button against
   GreenButtonDown. */
bool ArnikaScript::el2Button(int command)
{
    return runElevatorButton(el02_, el02_button_prop_, kEl02Bindings, command);
}

/* "Elevator-02": steps the lift while no button run is pending and the
   elevator is in a callable state; answers false otherwise. */
bool ArnikaScript::elevator02Trigger(Trigger* pTrigger)
{
    if (el02_.button_down == 2) {
        return false;
    }
    if (el02_.state != W8_ARNIKA_ELEVATOR_STEP_1 && el02_.state != W8_ARNIKA_ELEVATOR_STEP_3 &&
        el02_.state != W8_ARNIKA_ELEVATOR_STEP_7) {
        return false;
    }
    elevatorAdvance(W8_ARNIKA_ELEVATOR_TWO);
    return true;
}

/* The El02Moving master function, mirroring el1Moving. */
bool ArnikaScript::el2Moving(int command)
{
    return runElevatorMoving(el02_, kEl02Bindings);
}

/* Runs a door and its collide trigger for one elevator step and returns the door's prop. */
Prop* RunElevatorDoor(Trigger* door, Trigger* collide)
{
    Prop* prop = door->GetProp();
    door->Run(-1);
    collide->Run(-1);
    return prop;
}

/* Runs the lift for one elevator step and returns its prop. */
Prop* RunElevatorLift(Trigger* lift)
{
    Prop* prop = lift->GetProp();
    lift->Run(-1);
    return prop;
}

/* Shared elevator step: advances the selected elevator one state, running
   the door/lift triggers for the departing state and caching the rep the
   moving master will wait on. States 3 and 7 are the door-closed pauses that
   produce no prop. */
// FUNCTION: WIZ8 0x004E2010
void ArnikaScript::elevatorAdvance(W8ArnikaElevatorId which)
{
    W8Elevator* el;
    /* An invalid state runs nothing and leaves no prop to wait on. */
    Prop* prop = nullptr;

    if (which == W8_ARNIKA_ELEVATOR_ONE) {
        el = &el01_;
    } else {
        el = &el02_;
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
        el01_.pProp = prop;
        if (prop != 0) {
            addMaster(&ArnikaScript::el1Moving, &ArnikaScript::captureEl1Moving);
        }
    } else {
        SetTriggerVariableByName("El02State", el->state);
        el02_.pProp = prop;
        if (prop != 0) {
            addMaster(&ArnikaScript::el2Moving, &ArnikaScript::captureEl2Moving);
        }
    }
}

/* "ChaosMolori": rejects the activation when an item is on the cursor and
   the pedestal is not running it; accepts when the cursor is empty or the
   pedestal runs it. */
bool ArnikaScript::chaosMolori(Trigger* pTrigger)
{
    if (!running_trigger_from_script_ && (ctx.inventory.held())) {
        return false;
    }
    return true;
}

/* "Maddmook": the first time the party has been warned, posts the mook's
   script notice and drops the faction band. */
bool ArnikaScript::maddmook(Trigger* pTrigger)
{
    if (GetLocationVarIDByName("WarnedAboutEntry") != -1 &&
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

/* "AstralDominae": same cursor gate as ChaosMolori, then forwards the
   activation to the CMBox trigger. */
bool ArnikaScript::astralDominae(Trigger* pTrigger)
{
    if (!running_trigger_from_script_ && (ctx.inventory.held())) {
        return false;
    }
    pTrigger = FindRequiredTriggerByName("CMBox");
    pTrigger->Run(-1);
    return true;
}

/* "CMbox": places the held relic on the pedestal through pedestalItem, then
   runs the matching trigger itself so the pedestal callbacks stay quiet. */
bool ArnikaScript::cmBox(Trigger* pTrigger)
{
    int previous = -1;
    int item;

    if (running_trigger_from_script_) {
        return false;
    }
    item = pedestalItem(&previous);
    if (item < -1) {
        return false;
    }
    if (item == 0x244) {
        pTrigger = FindRequiredTriggerByName("ChaosMolori");
    } else if (item == 0x242 || item == 0x264) {
        pTrigger = FindRequiredTriggerByName("AstralDominae");
    } else {
        return previous != -1;
    }
    running_trigger_from_script_ = true;
    pTrigger->Run(-1);
    running_trigger_from_script_ = false;
    return true;
}

/* Pedestal item exchange: when a relic (0x242/0x244/0x264) is on the cursor
   it is consumed into the PedestalItem location variable and the stored item
   comes back to the cursor; on the first exchange it also aims the camera at
   the mook and posts the entry notice. Answers the item placed, -1 when the
   pedestal already held one, or -2 when the cursor item was not a relic. */
int ArnikaScript::pedestalItem(int* previous_item)
{
    W8NpcState* npc;
    W8MonsterInfo* info;
    glm::vec3 position;
    int item = -1;
    int previous;

    if ((ctx.inventory.held())) {
        item = ctx.inventory.heldItemId();
        if (item != 0x244 && item != 0x242 && item != 0x264) {
            return -2;
        }
    }
    FindRequiredTriggerByName("ChaosMolori");
    if (item > -1) {
        (void)ctx.game.inventory.take({W8ItemStorage::Hand});
    }
    if (GetLocationVarIDByName("PedestalItem") != -1 &&
        GetLocationVarIDByName("WarnedAboutEntry") != -1 &&
        GetLocationVarValueByName("WarnedAboutEntry") == 1) {
        previous = GetLocationVarValueByName("PedestalItem");
        if (item == -1) {
            return previous | item;
        }
        SetTriggerVariableByName("PedestalItem", item);
        if (GetLocationVarIDByName("WarnedAboutEntry") != -1) {
            SetTriggerVariableByName("WarnedAboutEntry", 0);
        }
        *previous_item = previous;
        return item;
    }
    if (GetLocationVarIDByName("PedestalItem") == -1) {
        CreateLocationVar("PedestalItem", -1);
        previous = 0x244;
        if (item == -1) {
            ctx.game.inventory.replaceHeld(
                MakeItem(Data(), ctx.game.rng, previous, false, false, false));
        }
    } else {
        previous = GetLocationVarValueByName("PedestalItem");
        if (previous == -1) {
            if (item == previous) {
                return -2;
            }
        } else if (item == -1) {
            ctx.game.inventory.replaceHeld(
                MakeItem(Data(), ctx.game.rng, previous, false, false, false));
        }
    }
    npc = GetNpcStateByKind(0x69);
    if (npc != 0) {
        info = GetNpcMonsterInfo(npc);
        if (info != 0) {
            position = info->entity->nav.movement.position;
            position.y += info->entity->nav.movement.height_offset;
            partyCamera().LookAt(&position, false);
        }
        QueueNpcScriptNotice(npc, 0, 8, false, 0);
    }
    if (GetLocationVarIDByName("WarnedAboutEntry") == -1) {
        CreateLocationVar("WarnedAboutEntry", 1);
    } else {
        SetTriggerVariableByName("WarnedAboutEntry", 1);
    }
    *previous_item = previous;
    return item;
}

/* The "BallSlot" activation callback: on the first ball insert it creates
   item 0x240 and hands it to the kind-0x16 NPC's script notice queue. */
bool ArnikaScript::ballSlot(Trigger* pTrigger)
{
    W8ItemInstance item;
    W8NpcState* npc;

    if (GetLocationVarIDByName("BallInserted") == -1) {
        CreateLocationVar("BallInserted", 1);
        npc = GetNpcStateByKind(0x16);
        item = MakeItem(Data(), ctx.game.rng, 0x240, true, true, false);
        QueueNpcScriptNotice(npc, &item, -1, false, 0);
    }
    g_trigger_feedback = true;
    return true;
}

/* The "Flightrecordertrigger" activation callback: outside NPC dialogue it
   queues the kind-0x14 NPC's script notice, passing the held item when the
   cursor holds one. */
bool ArnikaScript::flightRecorder(Trigger* pTrigger)
{
    const W8ItemInstance* item;
    W8NpcState* npc;

    if (ctx.activity.conversation) {
        return false;
    }
    g_trigger_feedback = true;
    npc = GetNpcStateByKind(0x14);
    item = 0;
    if (const auto* held = ctx.inventory.held()) {
        item = &held->item;
    }
    QueueNpcScriptNotice(npc, item, -1, false, 0);
    return false;
}

} // namespace

std::unique_ptr<LevelScript> MakeArnikaScript(const ScriptContext& context)
{
    return std::make_unique<ArnikaScript>(context);
}
