#include "wiz8/level_specific_code/MtGigas2.h"

#include <memory>
#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/audio/soundman.h"
#include "wiz8/data/dice.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/entities/monster.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/platform/game_timer.h"
#include "wiz8/rules/activity.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/utility.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

struct W8ItemInstance;
struct W8NpcState;

/* Level Specific Code\MtGigas2.cpp (level 0x0d).

   Attribution evidence: the level-0x0d script runs 0x004DB200 and binds
   0x004DB380-0x004DBA90, and the range sits between the Trynnie1 TU and the
   MtGigasOuter TU which starts at 0x004DBE70. The UmpaniAlarm master at
   0x004DB860 is the same shape as the camp alarm (0x004DC3D0) in
   MtGigasOuter.cpp. */

namespace {

/* The master command a trigger starts the alarm with. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

/* Shared wrong-wire penalty: resets the panel and all three wire props, then
   rolls 2d4+1 electric damage into the party with the spark sound. The third
   wire prop is looked up under the misspelled name "yellowire". */
void WireShock()
{
    Trigger* pTrigger;
    Prop* prop;
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

/* Each wire passes only from its expected panel state; 3 means already fixed. */
bool AdvanceWirePanel(int expected, int next)
{
    const int value = GetLocationVarValueByName("WirePanel");

    if (value == 3) {
        return true;
    }
    g_trigger_feedback = true;
    if (value != expected) {
        WireShock();
        return false;
    }
    SetTriggerVariableByName("WirePanel", next);
    return true;
}

/* Activation callback on yellowwire: valid only as the last wire (2 -> 3);
   on success it latches fact 0xa7 and re-arms all three wire triggers. */
bool YellowWire(Trigger* pTrigger)
{
    int value = GetLocationVarValueByName("WirePanel");
    Trigger* wire;

    if (value == 3) {
        return true;
    }
    g_trigger_feedback = true;
    if (value != 2) {
        WireShock();
        return false;
    }
    CurrentGame().setFact(W8_FACT_UMISSION_TRAIN_MACHINE_FIXED, 1);
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

class MtGigas2Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void setup();
    bool train(Trigger* pTrigger);
    bool heldItemNotice(int npc_kind);
    bool activateOfficer(int door_bit, const char* door_name, int npc_kind);
    bool laserAlarm(Trigger* pTrigger);
    bool umpaniAlarm(int command);
    void captureUmpaniAlarm(W8SavedLocationState& state);

    Sound3D* alarm_sound_ = nullptr;
    std::optional<W8IntervalGate> alarm_gate_;
};

void MtGigas2Script::enter()
{
    setup();
    bindTrigger("_VOC_EWAXXTRAIN", &MtGigas2Script::train);
    // The wires pass in order: red (0 -> 1), blue (1 -> 2), then yellow.
    bindTrigger("redwire", [](Trigger*) { return AdvanceWirePanel(0, 1); });
    bindTrigger("bluewire", [](Trigger*) { return AdvanceWirePanel(1, 2); });
    bindTrigger("yellowwire", YellowWire);
    bindTrigger("_VOC_EWAXXLIFT3", [this](Trigger*) { return heldItemNotice(0x5c); });
    bindTrigger("_VOC_EWAXXTOPDOOR1", [this](Trigger*) { return heldItemNotice(0x5f); });
    // While ObstacleDoors bit 0 (1) is set the officer runs Door19 (Door18) and clears the bit.
    bindTrigger("_VOC_EWAXXOFFICER1",
                [this](Trigger*) { return activateOfficer(1, "Door19", 0x7c); });
    bindTrigger("_VOC_EWAXXOFFICER2",
                [this](Trigger*) { return activateOfficer(2, "Door18", 0x7d); });
    bindTrigger("triggerPlaneLaserAlarm", &MtGigas2Script::laserAlarm);
    bindTrigger("triggerPlaneLaserAlarm01", &MtGigas2Script::laserAlarm);
    // The access hatch plays the falling-statue sound.
    bindTrigger("accessHatch", [](Trigger*) {
        wiz8::sound::play("Data\\Sound\\Ambients\\Sign Falling Statue.wav");
        return true;
    });
    // The wiring malfunction plays the short spark sound.
    bindTrigger("wiringMalfunction", [](Trigger*) {
        wiz8::sound::play("Data\\Sound\\Ambients\\Electricity 04.wav");
        return true;
    });
}

/* Level-load restore. Recreates the wire-panel location variable (armed but
   untouched reads back -1), re-runs the three wire triggers when the panel
   was already solved, restarts a saved UmpaniAlarm countdown, and moves the
   Sergeant Rubble NPC to his covert when the rubble teleport was pending. */
void MtGigas2Script::setup()
{
    if (GetLocationVarIDByName("WirePanel") != -1) {
        if (GetLocationVarValueByName("WirePanel") == 3) {
            RunNamedTrigger("redwire", -1);
            RunNamedTrigger("bluewire", -1);
            RunNamedTrigger("yellowwire", -1);
        }
    } else {
        CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "WirePanel", -1);
    }
    if (GetLocationVarIDByName("UmpaniAlarm") != -1) {
        int alarm = GetLocationVarValueByName("UmpaniAlarm");
        if (alarm != 0) {
            umpaniAlarm(alarm);
        }
    }
    if (ctx.game.testFact(W8_FACT_UMISSION_IUFPASS_LEVEL4) == 0 &&
        GetLocationVarIDByName("CODESgtRubbleTeleport") != -1 &&
        GetLocationVarValueByName("CODESgtRubbleTeleport") == 2) {
        W8NpcState* npc;
        W8MonsterInfo* info;
        glm::vec3 position;

        SetTriggerVariableByName("CODESgtRubbleTeleport", 1);
        RunNamedTrigger("door08", -1);
        ctx.game.setFact(W8_FACT_UMISSION_MOVE_RUBBLE_COVERT, 0);
        npc = CurrentGame().story.npcOfKind(0x29);
        if (npc != 0) {
            info = GetNpcMonsterInfo(npc);
            if (info != 0 && FindEntityByName("RubbleCovert", &position, 0, 0)) {
                info->entity->SetPosition(&position);
            }
        }
    }
}

/* Activation callback on _VOC_EWAXXTRAIN: queues the wire-panel NPC notice
   with the held item, and accepts the 0x269/0x26c wire items to arm the
   panel (value 0) when it was still untouched (-1). */
bool MtGigas2Script::train(Trigger* pTrigger)
{
    W8NpcState* npc;
    const W8ItemInstance* item;
    int item_id;
    int value;

    if (ctx.activity.conversation) {
        return false;
    }
    npc = CurrentGame().story.npcOfKind(0x5b);
    item = 0;
    item_id = 0;
    if (const auto* held = ctx.inventory.held()) {
        item = &held->item;
        item_id = held->item.iItemNo;
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

/* The lift (_VOC_EWAXXLIFT3) and top-door (_VOC_EWAXXTOPDOOR1) callbacks:
   outside a conversation, queue the NPC's notice with the held item, if any. */
bool MtGigas2Script::heldItemNotice(int npc_kind)
{
    if (ctx.activity.conversation) {
        return false;
    }
    QueueNpcHeldItemNotice(npc_kind);
    g_trigger_feedback = true;
    return false;
}

bool MtGigas2Script::activateOfficer(int door_bit, const char* door_name, int npc_kind)
{
    if (ctx.activity.conversation) {
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

/* Activation callback on triggerPlaneLaserAlarm/01: once per level (fact
   0xa5 and the UmpaniAlarm variable both unset) turns the Umpani faction
   hostile and starts the intruder alarm. */
bool MtGigas2Script::laserAlarm(Trigger* pTrigger)
{
    g_trigger_feedback = true;
    if (ctx.game.testFact(W8_FACT_LOCATION_GIGAS_MOTION_SENSORS_OFF) != 0) {
        return false;
    }
    if (GetLocationVarIDByName("UmpaniAlarm") != -1) {
        return false;
    }
    SetFactionDispositionBand(4, W8_FACTION_HOSTILE);
    umpaniAlarm(kMasterToggle);
    return true;
}

void MtGigas2Script::captureUmpaniAlarm(W8SavedLocationState& state)
{
    state.emplace_back("UmpaniAlarm",
                       alarm_gate_ ? static_cast<int>(alarm_gate_->GetElapsedSeconds()) : 0);
}

/* The Umpani alarm's master function, same shape as the camp alarm in
   MtGigasOuter.cpp: command zero ticks the alarm gate while registered, -1
   writes the remaining seconds into UmpaniAlarm, and any other command plays
   the intruder warning at the camera and arms the gate (clamped to 30
   seconds). Unlike the camp alarm the gate and sound are only created when
   both are absent, and finishing stops the sound but keeps its node. */
bool MtGigas2Script::umpaniAlarm(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (GetLocationVarIDByName("UmpaniAlarm") == -1)
            CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "UmpaniAlarm", 30);
        if (alarm_gate_ || alarm_sound_ != 0) {
            return false;
        }
        position = world().party.eye();
        if (static_cast<unsigned int>(command) > 0x1e) {
            command = 0x1e;
        }
        alarm_gate_.emplace(world().clocks, static_cast<float>(static_cast<unsigned int>(command)),
                            TimerClock::World, true);
        alarm_sound_ = CreateAndPlaySoundNode(
            world(), "Data\\Sound\\VOCs\\VOC_HLLIntruder\\VOC_HLLIntruder_003.wav", position, 1.0f,
            75.0f, true);
        if (alarm_sound_ != 0 && alarm_gate_) {
            addMaster(&MtGigas2Script::umpaniAlarm, &MtGigas2Script::captureUmpaniAlarm);
        }
    }
    if (!alarm_gate_) return true;
    if (!alarm_gate_->PollFinished()) {
        return false;
    }

    if (alarm_sound_) alarm_sound_->Stop();
    alarm_gate_.reset();
    SetTriggerVariableByName("UmpaniAlarm", 0);
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeMtGigas2Script(const ScriptContext& context)
{
    return std::make_unique<MtGigas2Script>(context);
}
