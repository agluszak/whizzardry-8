#include "wiz8/level_specific_code/MtGigas1.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/location_variables.h"
#include "wiz8/rules/activity.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

/* Level Specific Code\MtGigas1.cpp (level 0x0c).

   Attribution evidence: the level-0x0c script runs 0x004DBAB0 and binds
   0x004DBB50-0x004DBE30, and the range sits between the MtGigas2 TU and the
   MtGigasOuter TU which starts at 0x004DBE70. */

/* Activation callback on _VOC_EWAXXLIFT1: queues the lift NPC notice with
   the held item, if any. */
bool QueueGigasLiftNotice(const ScriptContext& context)
{
    if (context.activity.conversation) {
        return false;
    }
    QueueNpcHeldItemNotice(0x5d);
    g_trigger_feedback = true;
    return false;
}

namespace {

void RunPlateTrigger(const char* name, W8AnimationDirection direction)
{
    Trigger* trigger = FindTriggerByName(name);
    if (trigger != 0) {
        trigger->Run(-1);
        Prop* prop = trigger->GetProp();
        if (prop != 0) {
            prop->SetAnimationDirection(direction);
        }
    }
}

/* Activation callback on mudWallTrigger: spawns the badass monster at the
   NP_BadassMonster entity. */
bool MudWall(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("NP_BadassMonster", &position, 0, 0)) {
        SpawnMonsters(0x157, 1, &position, 1, true, false);
    }
    return true;
}

class MtGigas1Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    void setup();
    bool lift2(Trigger* pTrigger);
    bool pressurePlate(Trigger* pTrigger);

    Prop* plate_prop_ = nullptr;
    bool plate_down_ = false;
};

void MtGigas1Script::enter()
{
    setup();
    bindTrigger("_VOC_EWAXXLIFT1", [this](Trigger*) { return QueueGigasLiftNotice(ctx); });
    bindTrigger("_VOC_EWAXXLIFT2", &MtGigas1Script::lift2);
    bindTrigger("PRESSUREPLATE", &MtGigas1Script::pressurePlate);
    bindTrigger("mudWallTrigger", MudWall);
}

/* Level-load restore: binds the "plate" trigger's prop and mirrors the saved
   PPlateDown variable into the latch flag, creating the variable when the
   level never wrote one. */
void MtGigas1Script::setup()
{
    Trigger* pTrigger;
    Prop* prop;

    pTrigger = FindTriggerByName("plate");
    if (pTrigger != 0) {
        prop = pTrigger->GetProp();
        if (prop != 0) {
            plate_prop_ = (prop->collision_ready ? prop : nullptr);
        }
    }
    if (GetLocationVarIDByName("PPlateDown") == -1) {
        CreateLocationVar("PPlateDown", 0);
        return;
    }
    if (GetLocationVarValueByName("PPlateDown") != 0) {
        plate_down_ = true;
    }
}

/* Activation callback on _VOC_EWAXXLIFT2: queues the lift NPC notice with
   the held item, if any. */
bool MtGigas1Script::lift2(Trigger* pTrigger)
{
    if (ctx.activity.conversation) {
        return false;
    }
    QueueNpcHeldItemNotice(0x5e);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on PRESSUREPLATE: ticks the secret door. The plate
   counts as held while the camera stands inside the prop's XZ bounds or the
   prop still has list entries. On the down edge it opens secretDoor-01 and
   drops the plate (forward animation, rumble + open sounds); on the release edge it
   reverses both (reverse animation, close + rumble sounds) and clears PPlateDown. */
bool MtGigas1Script::pressurePlate(Trigger* pTrigger)
{
    glm::vec3 position;

    if (plate_prop_ == 0) {
        return false;
    }
    const glm::vec3 camera_position = world().party.eye();
    position.x = camera_position.x;
    position.z = camera_position.z;
    const auto bounds = plate_prop_->computeBounds();
    if ((position.x >= bounds.minimum.x && position.x <= bounds.maximum.x &&
         position.z >= bounds.minimum.z && position.z <= bounds.maximum.z) ||
        plate_prop_->HasSupportedItems()) {
        if (plate_down_) {
            return false;
        }
        RunPlateTrigger("secretDoor-01", W8_ANIMATION_FORWARD);
        RunPlateTrigger("plate", W8_ANIMATION_FORWARD);
        plate_down_ = true;
        SetTriggerVariableByName("PPlateDown", 1);
        wiz8::sound::play("Data\\Sound\\Ambients\\Door Stone Open.wav");
        wiz8::sound::play("Data\\Sound\\Ambients\\Amb Rumble Very Low.wav");
        return false;
    }
    if (!plate_down_) {
        return false;
    }
    RunPlateTrigger("secretDoor-01", W8_ANIMATION_REVERSE);
    RunPlateTrigger("plate", W8_ANIMATION_REVERSE);
    plate_down_ = false;
    SetTriggerVariableByName("PPlateDown", 0);
    wiz8::sound::play("Data\\Sound\\Ambients\\Door Stone Close 01.wav");
    wiz8::sound::play("Data\\Sound\\Ambients\\Amb Rumble Very Low.wav");
    return false;
}

} // namespace

std::unique_ptr<LevelScript> MakeMtGigas1Script(const ScriptContext& context)
{
    return std::make_unique<MtGigas1Script>(context);
}
