#include "wiz8/level_specific_code/MartensBluff1.h"

#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/dice.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/PC_Item.h"
#include "wiz8/local_code/Strings.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/random.h"
#include "wiz8/render/camera.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spell_ids.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"
#include "wiz8/xstatus.h"

/* Level Specific Code\MartensBluff1.cpp (level 5, Marten's Bluff).

   Attribution evidence: the assertions in MartensBluff1TeleportState
   quote this file's path at lines 761/779/784/789/794, and the spawn lookup
   in MartensBluff1TransportSpawn passes the path to
   MonsterGetIndexByLocationID at line 509. The level-5 block of
   InitializeLevelMasterFunctions registers the teleporter cluster
   (MR109, ButtonGigas, ButtonTrang, ButtonRift, ButtonMaten). */

static bool g_teleport_running;
static Trigger* g_door_controller;
static Sound3D* g_gas_sound0;
static Sound3D* g_gas_sound1;
static std::optional<W8IntervalGate> g_transport_gate;

/* The level-5 entry point called from InitializeLevelMasterFunctions:
   caches the J-Doorcontroller trigger, seeds DialState, kicks the Trang
   transporter, raises the MR101 lift flag when the party is already beside it
   and starts or stops the two GasSpray particles' air-escaping loops from the
   saved gas state. */
void MartensBluff1Setup(void)
{
    glm::vec3 position;
    glm::vec3 trigger_position;
    Trigger* pTrigger;
    ParticleSystem* particle;
    int dial_state;

    FindTriggerByName("Gas-Switch");
    g_door_controller = FindTriggerByName("J-Doorcontroller");
    g_gas_sound0 = 0;
    g_gas_sound1 = 0;
    g_teleport_running = false;
    if (g_door_controller != 0 && GetLocationVarIDByName("DialState") == -1) {
        Random(8);
        Random(8);
        Random(8);
        CreateLocationVar("DialState", 0x470111);
    }
    MartensBluff1Transporter(static_cast<int>(0xEFFFFFFF));
    position = glm::vec3(GetWorld()->camera->transform.getLocation());
    pTrigger = FindTriggerByName("MR101");
    if (pTrigger != 0 && (pTrigger->flags & W8_TRIGGER_POSITIONED) != 0) {
        pTrigger->GetPosition(&trigger_position);
        if (glm::length((trigger_position - position)) < 500.0) {
            FindTriggerByName("Lift2Marten2");
            if (GetLocationVarIDByName("LiftArrived") == -1) {
                CreateLocationVar("LiftArrived", 1);
                ShowString(g_localized_strings[0x71b]);
            }
        }
    }
    if (g_door_controller != 0) {
        dial_state = 0;
        if (GetLocationVarIDByName("DialState") != -1) {
            dial_state = GetLocationVarValueByName("DialState");
        }
        particle = FindParticleByName("GasSpray-00");
        if (particle != 0) {
            if ((dial_state & 0xf0000000) == 0) {
                particle->SetActive(false);
            } else {
                position = glm::vec3(particle->transform.getLocation());
                g_gas_sound0 = CreateAndPlaySoundNode(
                    "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav", position, 0.3f, 30.0f, true);
            }
        }
        particle = FindParticleByName("GasSpray-01");
        if (particle != 0) {
            if ((dial_state & 0xf0000000) == 0) {
                particle->SetActive(false);
            } else {
                position = glm::vec3(particle->transform.getLocation());
                g_gas_sound1 = CreateAndPlaySoundNode(
                    "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav", position, 0.3f, 30.0f, true);
            }
        }
    }
}

/* The "F-Handlock" trap callback: already-unlocked or the right hand item
   (0x26f) opens it; otherwise it shocks the whole party for 2d4+1. */
bool MartensBluff1FHandlock(Trigger* pTrigger)
{
    W8Dice dice;

    if (pTrigger->running) {
        return true;
    }
    if ((g_status.party.held.item.iItemNo != -1) && GetItemInHand() == 0x26f) {
        return true;
    }
    SetDice(&dice, 2, 4, 1);
    ApplyRolledHealthChangeToParty(&dice, 0, 1);
    wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 04.wav");
    g_trigger_feedback = true;
    return false;
}

/* Advances one 4-bit digit of DialState modulo 8, leaving the other digits alone. */
static void advanceDialDigit(int shift)
{
    int state = GetLocationVarValueByName("DialState");
    const int digit = (((state >> shift) & 0xf) + 1) % 8;

    state = (digit << shift) | (state & ~(0xf << shift));
    SetTriggerVariableByName("DialState", state);
}

/* The "Dial-A" callback: advance the lowest DialState digit modulo 8. */
bool MartensBluff1DialA(Trigger* pTrigger)
{
    if (g_door_controller == 0) {
        return false;
    }
    advanceDialDigit(0);
    return true;
}

/* The "Dial-B" callback: advance the second DialState digit modulo 8. */
bool MartensBluff1DialB(Trigger* pTrigger)
{
    if (g_door_controller == 0) {
        return false;
    }
    advanceDialDigit(4);
    return true;
}

/* The "Dial-C" callback: advance the third DialState digit modulo 8. */
bool MartensBluff1DialC(Trigger* pTrigger)
{
    if (g_door_controller == 0) {
        return false;
    }
    advanceDialDigit(8);
    return true;
}

/* The "Gas-Switch" callback: toggles the 0x1000 gas-disable digit of
   DialState. Re-enabling only clears the digit; disabling also stops the two
   GasSpray particles and their looping sounds. */
bool MartensBluff1GasSwitch(Trigger* pTrigger)
{
    ParticleSystem* particle;
    int state;

    if (g_door_controller == 0) {
        return false;
    }
    state = GetLocationVarValueByName("DialState");
    if ((state & 0xf000) != 0) {
        state &= ~0xf000;
        SetTriggerVariableByName("DialState", state);
        return true;
    }
    state = (state & 0xfff0fff) | 0x1000;
    particle = FindParticleByName("GasSpray-00");
    if (particle != 0) {
        particle->SetActive(false);
    }
    particle = FindParticleByName("GasSpray-01");
    if (particle != 0) {
        particle->SetActive(false);
    }
    if (g_gas_sound0 != 0) {
        g_gas_sound0->Stop();
        g_gas_sound0 = 0;
    }
    if (g_gas_sound1 != 0) {
        g_gas_sound1->Stop();
        g_gas_sound1 = 0;
    }
    SetTriggerVariableByName("DialState", state);
    return true;
}

/* The "J-Doorcontroller" callback: opens while the three dialed digits match
   the combination stored in bits 16-27 of DialState. A wrong code sprays gas
   (unless the gas switch disabled it) and casts spell 0x25 at the party. */
bool MartensBluff1JDoorController(Trigger* pTrigger)
{
    glm::vec3 position;
    ParticleSystem* particle;
    int state;

    if (g_door_controller == 0 || pTrigger->running) {
        return false;
    }
    state = GetLocationVarValueByName("DialState");
    if ((state >> 0x10 & 0xfff) == (state & 0xfff)) {
        return true;
    }
    if ((state & 0xf000) != 0) {
        return false;
    }
    g_trigger_feedback = true;
    particle = FindParticleByName("GasSpray-00");
    if (particle != 0) {
        particle->SetActive(true);
        position = glm::vec3(particle->transform.getLocation());
        g_gas_sound0 = CreateAndPlaySoundNode("Data\\Sound\\Ambients\\Air_Escaping_Loop.wav",
                                              position, 0.3f, 30.0f, true);
    }
    particle = FindParticleByName("GasSpray-01");
    if (particle != 0) {
        particle->SetActive(true);
        position = glm::vec3(particle->transform.getLocation());
        g_gas_sound1 = CreateAndPlaySoundNode("Data\\Sound\\Ambients\\Air_Escaping_Loop.wav",
                                              position, 0.3f, 30.0f, true);
    }
    position = glm::vec3(GetWorld()->camera->transform.getLocation());
    PointCastSpell(position, W8_SPELL_FREEZE_FLESH, 5);
    state |= 0x10000000;
    SetTriggerVariableByName("DialState", state);
    return false;
}

/* The "Controller" callback: toggles fact 0x42. */
bool MartensBluff1Controller(Trigger* pTrigger)
{
    if (GetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON) == 0) {
        SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 1, false);
    } else {
        SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 0, false);
    }
    g_trigger_feedback = true;
    return true;
}

/* The Trang transporter spawn: while fact 0x43 is unset and combat is off it
   finds the two ANTRHACAX entities, plays the transporter sound at the first,
   spawns monster group 0x177 there and hands the spawned monster its move
   script. Answers false only when the fact was already set. */
bool MartensBluff1TransportSpawn(void)
{
    glm::vec3 position_a;
    glm::vec3 position_b;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    if (GetFact(W8_FACT_TRANG_TELEPORTER_TOAST)) {
        return false;
    }
    if (g_combat.active()) {
        return true;
    }
    if (!FindEntityByName("ANTRHACAX1", &position_a, 0, 0)) {
        return true;
    }
    if (!FindEntityByName("ANTRHACAX2", &position_b, 0, 0)) {
        return true;
    }
    CreateAndPlaySoundNode("Data\\Sound\\Ambients\\TrangTransporter.wav", position_a, 0.7f, 30.0f,
                           false);
    group = SpawnMonsters(0x177, 1, &position_a, 0, true, false, false);
    location_id = group->MemberLocationId(0);
    if (location_id != 0) {
        info = FindMonster(location_id);
        if (info != 0 && info->p3D != 0) {
            info->p3D->SetScript("M1_Trang_trans.MSF", true);
        }
    }
    return true;
}

static void CaptureTransporter(W8SavedLocationState& state)
{
    state.emplace_back("TransportSpawn",
                       g_transport_gate
                           ? static_cast<int>(g_transport_gate->GetProgress() * g_octree_cell_scale)
                           : 0);
}

/* The TransportSpawn master function. The 0xEFFFFFFF command re-arms the minute gate and registers the run. The command-0 run
   fires the transporter spawn each time the gate elapses until the spawn
   reports the fact already set, then deletes the gate and unregisters. */
bool MartensBluff1Transporter(int command)
{
    if (command != 0) {

        if (command == static_cast<int>(0xEFFFFFFF)) {
            if (GetFact(W8_FACT_TRANG_TELEPORTER_TOAST)) {
                return false;
            }
            if (g_transport_gate) {
                g_transport_gate->Arm();
            } else {
                g_transport_gate.emplace(60.0f, false, true);
            }
            if (GetLocationVarIDByName("TransportSpawn") != -1) {
                g_transport_gate->SetProgress(GetLocationVarValueByName("TransportSpawn") *
                                              kMovementSpeedStep);
            }
            g_master_functions.push_back({MartensBluff1Transporter, CaptureTransporter});
            return false;
        }
    }
    if (!g_transport_gate) {
        return true;
    }
    if (!g_transport_gate->PollFinished()) {
        return false;
    }
    if (MartensBluff1TransportSpawn()) {
        g_transport_gate->Arm();
        return false;
    }
    g_transport_gate.reset();
    return true;
}

/* "MR109": the teleporter pad. Sends the party to the destination indexed by
   the TeleporterState location variable, picking a random pad when none has
   been chosen yet. Always answers false so the trigger stays armed. */
bool MartensBluff1Teleporter(Trigger* pTrigger)
{
    MartensBluff1TeleporterPad state;

    if (GetLocationVarIDByName("TeleporterState") == -1) {
        CreateLocationVar("TeleporterState", 0);
        state = MartensBluff1TeleporterPad::none;
    } else {
        state =
            static_cast<MartensBluff1TeleporterPad>(GetLocationVarValueByName("TeleporterState"));
    }
    if (state == MartensBluff1TeleporterPad::none) {
        state = static_cast<MartensBluff1TeleporterPad>(Random(4) + 1);
    }
    switch (state) {
    case MartensBluff1TeleporterPad::gigas:
        pTrigger->RunDestination("CT611");
        return false;
    case MartensBluff1TeleporterPad::trang:
        pTrigger->RunDestination("CT107");
        return false;
    case MartensBluff1TeleporterPad::rift:
        pTrigger->RunDestination("RIF01");
        return false;
    case MartensBluff1TeleporterPad::maten:
        pTrigger->RunDestination("MR207");
        return false;
    case MartensBluff1TeleporterPad::none:
        break;
    }
    return false;
}

/* Advances the teleporter to a pad; while a teleport runs, a press only re-arms the input. */
static bool pressTeleporterButton(MartensBluff1TeleporterPad pad)
{
    if (!g_teleport_running) {
        return MartensBluff1TeleportState(pad);
    }
    g_trigger_feedback = true;
    return true;
}

/* "ButtonGigas": advances the teleporter to pad 1. While the state helper is
   running it only re-arms the consumed-input flag. */
bool MartensBluff1ButtonGigas(Trigger* pTrigger)
{
    return pressTeleporterButton(MartensBluff1TeleporterPad::gigas);
}

/* "ButtonTrang": advances the teleporter to pad 2. */
bool MartensBluff1ButtonTrang(Trigger* pTrigger)
{
    return pressTeleporterButton(MartensBluff1TeleporterPad::trang);
}

/* "ButtonRift": advances the teleporter to pad 3. */
bool MartensBluff1ButtonRift(Trigger* pTrigger)
{
    return pressTeleporterButton(MartensBluff1TeleporterPad::rift);
}

/* "ButtonMaten": advances the teleporter to pad 4. */
bool MartensBluff1ButtonMaten(Trigger* pTrigger)
{
    return pressTeleporterButton(MartensBluff1TeleporterPad::maten);
}

/* Shared teleporter state advance. Asserts the MR109 trigger exists, reads
   the current TeleporterState and, when it names a different live pad, runs
   that pad's button trigger before writing the new state. The re-entrancy
   flag keeps the button callbacks from recursing back in. */
bool MartensBluff1TeleportState(MartensBluff1TeleporterPad new_state)
{
    Trigger* pOldButtonTrigger;
    MartensBluff1TeleporterPad state;

    FindRequiredTriggerByName("MR109");
    if (GetLocationVarIDByName("TeleporterState") == -1) {
        CreateLocationVar("TeleporterState", 0);
        return false;
    }
    state = static_cast<MartensBluff1TeleporterPad>(GetLocationVarValueByName("TeleporterState"));
    if (state == MartensBluff1TeleporterPad::none) {
        return false;
    }
    if (state == new_state) {
        return false;
    }
    g_teleport_running = true;
    switch (state) {
    case MartensBluff1TeleporterPad::gigas:
        pOldButtonTrigger = FindRequiredTriggerByName("ButtonGigas");
        break;
    case MartensBluff1TeleporterPad::trang:
        pOldButtonTrigger = FindRequiredTriggerByName("ButtonTrang");
        break;
    case MartensBluff1TeleporterPad::rift:
        pOldButtonTrigger = FindRequiredTriggerByName("ButtonRift");
        break;
    case MartensBluff1TeleporterPad::maten:
        pOldButtonTrigger = FindRequiredTriggerByName("ButtonMaten");
        break;
    case MartensBluff1TeleporterPad::none:
    default:
        goto set_state;
    }
    pOldButtonTrigger->Run(-1);
set_state:
    SetTriggerVariableByName("TeleporterState", static_cast<int>(new_state));
    g_teleport_running = false;
    return true;
}

/* The "WireTrigger" callback: latches TeleporterState 1 (returning false when
   it is already nonzero) and replays the ButtonGigas sequence under the
   teleport-running guard. */
bool MartensBluff1WireTrigger(Trigger* pTrigger)
{
    Trigger* pTelTrigger;

    g_trigger_feedback = true;
    pTelTrigger = FindRequiredTriggerByName("MR109");
    if (GetLocationVarIDByName("TeleporterState") == -1) {
        CreateLocationVar("TeleporterState", static_cast<int>(MartensBluff1TeleporterPad::gigas));
    } else {
        if (GetLocationVarValueByName("TeleporterState") !=
            static_cast<int>(MartensBluff1TeleporterPad::none)) {
            return false;
        }
        SetTriggerVariableByName("TeleporterState",
                                 static_cast<int>(MartensBluff1TeleporterPad::gigas));
    }
    g_teleport_running = true;
    pTelTrigger = FindRequiredTriggerByName("ButtonGigas");
    pTelTrigger->Run(-1);
    g_teleport_running = false;
    return true;
}

/* The "MartenBook" callback: latches fact 0x268. */
bool MartensBluff1MartenBook(Trigger* pTrigger)
{
    SetFact(W8_FACT_QUEST_MARTEN_DIARY, 1, false);
    return true;
}
