#include "wiz8/level_specific_code/MtGigasOuter.h"

#include <memory>
#include <optional>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/sound3d.h"
#include "wiz8/cursor.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/gameplay_constants.h"
#include "wiz8/inventory.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/MtGigas1.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Factions.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/local_screens/NPCInteractionSubscreen.h"
#include "wiz8/location_variables.h"
#include "wiz8/platform/IntervalGate.h"
#include "wiz8/render/camera.h"
#include "wiz8/rules/item_rules.h"
#include "wiz8/rules/party.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

struct W8NpcState;

/* Level Specific Code\MtGigasOuter.cpp (level 0x0e).

   Attribution evidence: 0x004DC080 is this TU's hard lower bound and the
   level-0x0e script runs 0x004DBE70 alongside the _VOC_EWAXXLIFT1 trigger
   wiring. The assert file string at 0x00613640 names this TU for 0x004DBEC0
   and 0x004DC080. */

namespace {

/* The master command that re-arms the lift ride or starts the alarm. */
constexpr int kMasterToggle = static_cast<int>(0xEFFFFFFF);

/* Lift prop Y travel: 5228.6 at the top of the ride, -2000.0 at the bottom,
   and -5000.0 parked out of the world once the ride is spent. */
constexpr float kLiftTopY = 5228.6f;
constexpr float kLiftBottomY = -2000.0f;
constexpr float kLiftParkedY = -5000.0f;

/* Activation callback on VOC_EWAXXSENTRYtrig: hands the sentry NPC (kind
   0x59) the item the player is holding, if any, as script argument -1. */
bool SentryTrigger(Trigger* trigger)
{
    QueueNpcHeldItemNotice(0x59);
    g_trigger_feedback = true;
    return true;
}

/* Activation callback on dummytrigger: every live member of monster kind
   0x180 gives birth once. */
bool DummyTrigger(Trigger* trigger)
{
    W8MonsterGroup* group = FindNextExistingMonsterByID(0x180, 0);

    while (group != 0) {
        GiveBirthToMonster(group);
        group = FindNextExistingMonsterByID(0x180, group);
    }
    return true;
}

class MtGigasOuterScript final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void processFlagPosition();
    bool crank(Trigger* trigger);
    bool liftGate(int command);
    bool securityButton(Trigger* trigger);
    bool campAlarm(int command);
    bool ewaxxDoor03(Trigger* trigger);
    void captureLiftGate(W8SavedLocationState& state);
    void captureCampAlarm(W8SavedLocationState& state);

    std::optional<W8IntervalGate> lift_gate_;
    Prop* lift_prop_ = nullptr;
    Sound3D* alarm_sound_ = nullptr;
    std::optional<W8IntervalGate> alarm_gate_;
};

void MtGigasOuterScript::enter()
{
    processFlagPosition();
    bindTrigger("_VOC_EWAXXLIFT1", [this](Trigger*) { return QueueGigasLiftNotice(ctx); });
    bindTrigger("crank", &MtGigasOuterScript::crank);
    bindTrigger("Security Button", &MtGigasOuterScript::securityButton);
    bindTrigger("VOC_EWAXXSENTRYtrig", SentryTrigger);
    bindTrigger("ewaxxdoortrigger03", &MtGigasOuterScript::ewaxxDoor03);
    bindTrigger("dummytrigger", DummyTrigger);
}

/* When the FlagPosition location variable exists, feed its value to the lift
   gate; values at or above 1000 stop there. Otherwise the "flag" trigger
   runs. */
void MtGigasOuterScript::processFlagPosition()
{
    if (GetLocationVarIDByName("FlagPosition") != -1) {
        int value = GetLocationVarValueByName("FlagPosition");
        liftGate(value);
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
bool MtGigasOuterScript::crank(Trigger* trigger)
{
    glm::vec3 position;
    W8NpcState* npc;
    Trigger* pFlagTrigger;
    Prop* prop;
    int value;

    if ((ctx.party.held.item.iItemNo != -1)) {
        return false;
    }
    if (GetLocationVarIDByName("FlagPosition") == -1) {
        CreateLocationVar("FlagPosition", 0);
        npc = GetNpcStateByKind(0x2a);
        QueueNpcScriptNotice(npc, 0, 0x90, true, 0);
        g_trigger_feedback = true;
        return false;
    }
    value = GetLocationVarValueByName("FlagPosition");
    if (value == 0) {
        liftGate(kMasterToggle);
        trigger->flags &= ~W8_TRIGGER_ENABLED;
        npc = GetNpcStateByKind(0x2a);
        QueueNpcScriptNotice(npc, 0, 0x16, true, 0);
        return true;
    }
    if (value >= 100 && value < 1000) {
        pFlagTrigger = FindRequiredTriggerByName("flag");
        prop = pFlagTrigger->GetProp();
        position = prop->GetPosition();
        position.y = kLiftParkedY;
        prop->SetPosition(&position);
        SetTriggerVariableByName("FlagPosition", 1000);
        ReplaceHeldItem(ctx.game, MakeItem(Data(), ctx.game.rng, 0x290, false, false, false));
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

void MtGigasOuterScript::captureLiftGate(W8SavedLocationState& state)
{
    state.emplace_back("FlagPosition",
                       lift_gate_ ? static_cast<int>(lift_gate_->GetProgress() * 100.0f) : 0);
}

void MtGigasOuterScript::captureCampAlarm(W8SavedLocationState& state)
{
    state.emplace_back("UmpaniCampAlarm",
                       alarm_gate_ ? static_cast<int>(alarm_gate_->GetElapsedSeconds()) : 0);
}

/* The lift's master function. Registered while the gate runs so the
   dispatcher ticks it every frame with command zero; other callers command
   it directly: a positive command moves the lift that many percent of the
   ride; the toggle re-arms the ride from the top. The prop slides from
   5228.6 down to -2000.0 while the gate runs; -5000.0 parks it below. */
bool MtGigasOuterScript::liftGate(int command)
{
    glm::vec3 position;
    Trigger* pFlagTrigger;
    float progress;
    int value;

    if (command != 0) {
        value = 0;
        if (command != kMasterToggle) {
            value = command;
        }
        pFlagTrigger = FindRequiredTriggerByName("flag");
        lift_prop_ = pFlagTrigger->GetProp();
        position = lift_prop_->GetPosition();
        if (value < 100) {
            addMaster(&MtGigasOuterScript::liftGate, &MtGigasOuterScript::captureLiftGate);
            if (lift_gate_) {
                lift_gate_->Arm();
            } else {
                lift_gate_.emplace(3.0f, false, true);
            }
            if (value > 0) {
                lift_gate_->SetProgress(value * kMovementSpeedStep);
            }
            position.y =
                (kLiftTopY - kLiftBottomY) * (1.0f - value * kMovementSpeedStep) + kLiftBottomY;
        } else {
            if (value == 100) {
                position.y = kLiftBottomY;
            } else {
                position.y = kLiftParkedY;
            }
        }
        lift_prop_->SetPosition(&position);
        return false;
    }
    if (!lift_gate_) return true;
    const bool finished = lift_gate_->PollFinished();
    progress = finished ? 1.0f : lift_gate_->GetProgress();
    if (finished) SetTriggerVariableByName("FlagPosition", 100);
    position = lift_prop_->GetPosition();
    position.y = (1.0f - progress) * (kLiftTopY - kLiftBottomY) + kLiftBottomY;
    lift_prop_->SetPosition(&position);
    return finished;
}

/* Activation callback installed on the "Security Button" trigger. The first
   press lowers the Umpani faction disposition and starts the camp alarm;
   while the UmpaniCampAlarm variable exists the button does nothing. */
bool MtGigasOuterScript::securityButton(Trigger* trigger)
{
    g_trigger_feedback = true;
    if (GetLocationVarIDByName("UmpaniCampAlarm") != -1) {
        return false;
    }
    SetFactionDispositionBand(4, W8_FACTION_HOSTILE);
    campAlarm(kMasterToggle);
    return true;
}

/* The camp alarm's master function, same shape as the lift: command zero
   ticks the alarm gate while registered; any other command plays Alarm1.wav
   at the camera and re-arms the gate (clamped to 30 seconds). When the gate
   finishes the sound is released and the variable is reset to zero. */
bool MtGigasOuterScript::campAlarm(int command)
{
    glm::vec3 position;

    if (command != 0) {
        if (GetLocationVarIDByName("UmpaniCampAlarm") == -1)
            CreateLocationVar("UmpaniCampAlarm", 30);
        position = glm::vec3(world().camera->transform.getLocation());
        if (static_cast<unsigned int>(command) > 0x1e) {
            command = 0x1e;
        }
        if (alarm_sound_) alarm_sound_->Stop();
        alarm_sound_ = nullptr;
        alarm_sound_ = CreateAndPlaySoundNode(world(), "Data\\Sound\\Ambients\\Alarm1.wav",
                                              position, 1.0f, 75.0f, true);
        const bool was_running = alarm_gate_.has_value();
        alarm_gate_.emplace(static_cast<float>(static_cast<unsigned int>(command)), false, true);
        if (!was_running) {
            addMaster(&MtGigasOuterScript::campAlarm, &MtGigasOuterScript::captureCampAlarm);
        }
    }
    if (!alarm_gate_) return true;
    if (!alarm_gate_->PollFinished()) {
        return false;
    }
    if (alarm_sound_) alarm_sound_->Stop();
    alarm_sound_ = nullptr;
    alarm_gate_.reset();
    SetTriggerVariableByName("UmpaniCampAlarm", 0);
    return true;
}

/* Activation callback on ewaxxdoortrigger03: the door only responds while an
   item in the 0x268-0x26e key range sits in the player's hand. */
bool MtGigasOuterScript::ewaxxDoor03(Trigger* trigger)
{
    if ((ctx.party.held.item.iItemNo != -1)) {
        int item_id = GetItemInHand(ctx.party);
        if (item_id >= 0x268 && item_id <= 0x26e) {
            return true;
        }
    }
    return false;
}

} // namespace

std::unique_ptr<LevelScript> MakeMtGigasOuterScript(const ScriptContext& context)
{
    return std::make_unique<MtGigasOuterScript>(context);
}
