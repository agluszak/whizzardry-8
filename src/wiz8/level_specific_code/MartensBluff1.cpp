#include "wiz8/level_specific_code/MartensBluff1.h"

#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/combat/combat.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/spell_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/fact_state.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/render/particle_system.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/string_database.h"
#include "wiz8/utility.h"
#include "wiz8/world/world.h"

/* Level Specific Code\MartensBluff1.cpp (level 5, Marten's Bluff).

   Attribution evidence: the assertions in MartensBluff1TeleportState
   quote this file's path at lines 761/779/784/789/794, and the spawn lookup
   in MartensBluff1TransportSpawn passes the path to
   MonsterGetIndexByLocationID at line 509. The level-5 script binds the
   teleporter cluster (MR109, ButtonGigas, ButtonTrang, ButtonRift,
   ButtonMaten). */

namespace {

enum class MartensBluff1TeleporterPad : int {
    none = 0,
    gigas = 1,
    trang = 2,
    rift = 3,
    maten = 4,
};

/* The master command that re-arms the transporter. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

// Activation callback that only stops the trigger.
bool BlockTriggerActivation(Trigger*)
{
    return false;
}

class MartensBluff1Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void setup();
    bool fHandlock(Trigger* pTrigger);
    bool dial(int shift);
    bool gasSwitch(Trigger* pTrigger);
    bool jDoorController(Trigger* pTrigger);
    bool transportSpawn();
    bool transporter(int command);
    void captureTransporter(W8SavedLocationState& state);
    bool teleporter(Trigger* pTrigger);
    bool pressTeleporterButton(MartensBluff1TeleporterPad pad);
    bool teleportState(MartensBluff1TeleporterPad new_state);
    bool wireTrigger(Trigger* pTrigger);

    bool teleport_running_ = false;
    Trigger* door_controller_ = nullptr;
    Sound3D* gas_sound0_ = nullptr;
    Sound3D* gas_sound1_ = nullptr;
    std::optional<W8IntervalGate> transport_gate_;
};

void MartensBluff1Script::enter()
{
    setup();
    bindTrigger("MR109", &MartensBluff1Script::teleporter);
    for (const char* name : {"MR110", "MR111", "MR112"}) {
        if (Trigger* pTrigger = FindTriggerByName(name); pTrigger != 0) {
            pTrigger->activation_callback = BlockTriggerActivation;
        }
    }
    bindTrigger("F-Handlock", &MartensBluff1Script::fHandlock);
    // A button press while a teleport runs only re-arms the input.
    bindTrigger("ButtonGigas", [this](Trigger*) {
        return pressTeleporterButton(MartensBluff1TeleporterPad::gigas);
    });
    bindTrigger("ButtonTrang", [this](Trigger*) {
        return pressTeleporterButton(MartensBluff1TeleporterPad::trang);
    });
    bindTrigger("ButtonRift", [this](Trigger*) {
        return pressTeleporterButton(MartensBluff1TeleporterPad::rift);
    });
    bindTrigger("ButtonMaten", [this](Trigger*) {
        return pressTeleporterButton(MartensBluff1TeleporterPad::maten);
    });
    bindTrigger("WireTrigger", &MartensBluff1Script::wireTrigger);
    // "Controller": toggles fact 0x42.
    bindTrigger("Controller", [](Trigger*) {
        if (GetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON) == 0) {
            SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 1, false);
        } else {
            SetFact(W8_FACT_NARGISST_MONITOR_OVERFLOW_ON, 0, false);
        }
        g_trigger_feedback = true;
        return true;
    });
    // The dials advance one 4-bit digit of DialState each.
    bindTrigger("Dial-A", [this](Trigger*) { return dial(0); });
    bindTrigger("Dial-B", [this](Trigger*) { return dial(4); });
    bindTrigger("Dial-C", [this](Trigger*) { return dial(8); });
    bindTrigger("Gas-Switch", &MartensBluff1Script::gasSwitch);
    bindTrigger("J-Doorcontroller", &MartensBluff1Script::jDoorController);
    // "MartenBook": latches fact 0x268.
    bindTrigger("MartenBook", [](Trigger*) {
        SetFact(W8_FACT_QUEST_MARTEN_DIARY, 1, false);
        return true;
    });
}

/* The level-5 setup: caches the J-Doorcontroller trigger, seeds DialState, kicks the Trang
   transporter, raises the MR101 lift flag when the party is already beside it
   and starts or stops the two GasSpray particles' air-escaping loops from the
   saved gas state. */
void MartensBluff1Script::setup()
{
    glm::vec3 position;
    glm::vec3 trigger_position;
    Trigger* pTrigger;
    ParticleSystem* particle;
    int dial_state;

    FindTriggerByName("Gas-Switch");
    door_controller_ = FindTriggerByName("J-Doorcontroller");
    if (door_controller_ != 0 && GetLocationVarIDByName("DialState") == -1) {
        ctx.game.rng.below(8);
        ctx.game.rng.below(8);
        ctx.game.rng.below(8);
        CreateLocationVar("DialState", 0x470111);
    }
    transporter(kMasterToggle);
    position = cameraLocation(world());
    pTrigger = FindTriggerByName("MR101");
    if (pTrigger != 0 && (pTrigger->flags & W8_TRIGGER_POSITIONED) != 0) {
        pTrigger->GetPosition(&trigger_position);
        if (glm::length((trigger_position - position)) < 500.0) {
            FindTriggerByName("Lift2Marten2");
            if (GetLocationVarIDByName("LiftArrived") == -1) {
                CreateLocationVar("LiftArrived", 1);
                ShowString(Data().strings[0x71b]);
            }
        }
    }
    if (door_controller_ != 0) {
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
                gas_sound0_ =
                    CreateAndPlaySoundNode(world(), "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav",
                                           position, 0.3f, 30.0f, true);
            }
        }
        particle = FindParticleByName("GasSpray-01");
        if (particle != 0) {
            if ((dial_state & 0xf0000000) == 0) {
                particle->SetActive(false);
            } else {
                position = glm::vec3(particle->transform.getLocation());
                gas_sound1_ =
                    CreateAndPlaySoundNode(world(), "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav",
                                           position, 0.3f, 30.0f, true);
            }
        }
    }
}

/* The "F-Handlock" trap callback: already-unlocked or the right hand item
   (0x26f) opens it; otherwise it shocks the whole party for 2d4+1. */
bool MartensBluff1Script::fHandlock(Trigger* pTrigger)
{
    W8Dice dice;

    if (pTrigger->running) {
        return true;
    }
    if ((ctx.party.held.item.iItemNo != -1) && GetItemInHand(ctx.party) == 0x26f) {
        return true;
    }
    SetDice(&dice, 2, 4, 1);
    ApplyRolledHealthChangeToParty(&dice, 0, 1);
    wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 04.wav");
    g_trigger_feedback = true;
    return false;
}

/* The "Dial-A", "Dial-B" and "Dial-C" callbacks: advance one 4-bit digit of
   DialState (the lowest, second or third) modulo 8, leaving the other digits
   alone. */
bool MartensBluff1Script::dial(int shift)
{
    if (door_controller_ == 0) {
        return false;
    }
    int state = GetLocationVarValueByName("DialState");
    const int digit = (((state >> shift) & 0xf) + 1) % 8;

    state = (digit << shift) | (state & ~(0xf << shift));
    SetTriggerVariableByName("DialState", state);
    return true;
}

/* The "Gas-Switch" callback: toggles the 0x1000 gas-disable digit of
   DialState. Re-enabling only clears the digit; disabling also stops the two
   GasSpray particles and their looping sounds. */
bool MartensBluff1Script::gasSwitch(Trigger* pTrigger)
{
    ParticleSystem* particle;
    int state;

    if (door_controller_ == 0) {
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
    if (gas_sound0_ != 0) {
        gas_sound0_->Stop();
        gas_sound0_ = 0;
    }
    if (gas_sound1_ != 0) {
        gas_sound1_->Stop();
        gas_sound1_ = 0;
    }
    SetTriggerVariableByName("DialState", state);
    return true;
}

/* The "J-Doorcontroller" callback: opens while the three dialed digits match
   the combination stored in bits 16-27 of DialState. A wrong code sprays gas
   (unless the gas switch disabled it) and casts spell 0x25 at the party. */
bool MartensBluff1Script::jDoorController(Trigger* pTrigger)
{
    glm::vec3 position;
    ParticleSystem* particle;
    int state;

    if (door_controller_ == 0 || pTrigger->running) {
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
        gas_sound0_ = CreateAndPlaySoundNode(
            world(), "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav", position, 0.3f, 30.0f, true);
    }
    particle = FindParticleByName("GasSpray-01");
    if (particle != 0) {
        particle->SetActive(true);
        position = glm::vec3(particle->transform.getLocation());
        gas_sound1_ = CreateAndPlaySoundNode(
            world(), "Data\\Sound\\Ambients\\Air_Escaping_Loop.wav", position, 0.3f, 30.0f, true);
    }
    position = cameraLocation(world());
    PointCastSpell(position, W8_SPELL_FREEZE_FLESH, 5);
    state |= 0x10000000;
    SetTriggerVariableByName("DialState", state);
    return false;
}

/* The Trang transporter spawn: while fact 0x43 is unset and combat is off it
   finds the two ANTRHACAX entities, plays the transporter sound at the first,
   spawns monster group 0x177 there and hands the spawned monster its move
   script. Answers false only when the fact was already set. */
bool MartensBluff1Script::transportSpawn()
{
    glm::vec3 position_a;
    glm::vec3 position_b;
    W8MonsterGroup* group;
    W8MonsterInfo* info;
    int location_id;

    if (GetFact(W8_FACT_TRANG_TELEPORTER_TOAST)) {
        return false;
    }
    if (ctx.combat.has_value()) {
        return true;
    }
    if (!FindEntityByName("ANTRHACAX1", &position_a, 0, 0)) {
        return true;
    }
    if (!FindEntityByName("ANTRHACAX2", &position_b, 0, 0)) {
        return true;
    }
    CreateAndPlaySoundNode(world(), "Data\\Sound\\Ambients\\TrangTransporter.wav", position_a, 0.7f,
                           30.0f, false);
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

void MartensBluff1Script::captureTransporter(W8SavedLocationState& state)
{
    state.emplace_back("TransportSpawn",
                       transport_gate_
                           ? static_cast<int>(transport_gate_->GetProgress() * g_octree_cell_scale)
                           : 0);
}

/* The TransportSpawn master function. The 0xEFFFFFFF command re-arms the minute gate and registers the run. The command-0 run
   fires the transporter spawn each time the gate elapses until the spawn
   reports the fact already set, then deletes the gate and unregisters. */
bool MartensBluff1Script::transporter(int command)
{
    if (command != 0) {

        if (command == kMasterToggle) {
            if (GetFact(W8_FACT_TRANG_TELEPORTER_TOAST)) {
                return false;
            }
            if (transport_gate_) {
                transport_gate_->Arm();
            } else {
                transport_gate_.emplace(60.0f, false, true);
            }
            if (GetLocationVarIDByName("TransportSpawn") != -1) {
                transport_gate_->SetProgress(GetLocationVarValueByName("TransportSpawn") *
                                             kMovementSpeedStep);
            }
            addMaster(&MartensBluff1Script::transporter, &MartensBluff1Script::captureTransporter);
            return false;
        }
    }
    if (!transport_gate_) {
        return true;
    }
    if (!transport_gate_->PollFinished()) {
        return false;
    }
    if (transportSpawn()) {
        transport_gate_->Arm();
        return false;
    }
    transport_gate_.reset();
    return true;
}

/* "MR109": the teleporter pad. Sends the party to the destination indexed by
   the TeleporterState location variable, picking a random pad when none has
   been chosen yet. Always answers false so the trigger stays armed. */
bool MartensBluff1Script::teleporter(Trigger* pTrigger)
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
        state = static_cast<MartensBluff1TeleporterPad>(ctx.game.rng.below(4) + 1);
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
bool MartensBluff1Script::pressTeleporterButton(MartensBluff1TeleporterPad pad)
{
    if (!teleport_running_) {
        return teleportState(pad);
    }
    g_trigger_feedback = true;
    return true;
}

/* Shared teleporter state advance. Asserts the MR109 trigger exists, reads
   the current TeleporterState and, when it names a different live pad, runs
   that pad's button trigger before writing the new state. The re-entrancy
   flag keeps the button callbacks from recursing back in. */
bool MartensBluff1Script::teleportState(MartensBluff1TeleporterPad new_state)
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
    teleport_running_ = true;
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
    teleport_running_ = false;
    return true;
}

/* The "WireTrigger" callback: latches TeleporterState 1 (returning false when
   it is already nonzero) and replays the ButtonGigas sequence under the
   teleport-running guard. */
bool MartensBluff1Script::wireTrigger(Trigger* pTrigger)
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
    teleport_running_ = true;
    pTelTrigger = FindRequiredTriggerByName("ButtonGigas");
    pTelTrigger->Run(-1);
    teleport_running_ = false;
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeMartensBluff1Script(const ScriptContext& context)
{
    return std::make_unique<MartensBluff1Script>(context);
}
