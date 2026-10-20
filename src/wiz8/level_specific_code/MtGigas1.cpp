#include "wiz8/level_specific_code/MtGigas1.h"

#include "glm/ext/vector_double3.hpp"
#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/animation_types.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/level_specific_code/MasterFunctionList.h"
#include "wiz8/local_code/NPCManager.h"
#include "wiz8/location_variables.h"
#include "wiz8/render/camera.h"
#include "wiz8/world/world.h"

struct W8ItemInstance;
struct W8NpcState;

/* Level Specific Code\MtGigas1.cpp (level 0x0c).

   Attribution evidence: InitializeLevelMasterFunctions runs
   0x004DBAB0 and registers 0x004DBB50-0x004DBE30 under case 0x0c, and the
   range sits between the MtGigas2 TU and the MtGigasOuter TU which starts
   at 0x004DBE70. */

static Prop* g_plate_prop;
static bool g_plate_down;

static void RunPlateTrigger(const char* name, W8AnimationDirection direction)
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

/* Level-load restore: binds the "plate" trigger's prop to the plate Prop
   global and mirrors the saved PPlateDown variable into the latch flag,
   creating the variable when the level never wrote one. */
void MtGigas1Setup(void)
{
    Trigger* pTrigger;
    Prop* prop;

    g_plate_prop = 0;
    g_plate_down = false;
    pTrigger = FindTriggerByName("plate");
    if (pTrigger != 0) {
        prop = pTrigger->GetProp();
        if (prop != 0) {
            g_plate_prop = (prop->collision_ready ? prop : nullptr);
        }
    }
    if (GetLocationVarIDByName("PPlateDown") == -1) {
        CreateLocationVar("PPlateDown", 0);
        return;
    }
    if (GetLocationVarValueByName("PPlateDown") != 0) {
        g_plate_down = true;
    }
}

/* Activation callback on _VOC_EWAXXLIFT1: queues the lift NPC notice with
   the held item, if any. */
bool MtGigas1Lift1(Trigger* pTrigger)
{
    if (CurrentGame().activity.conversation) {
        return false;
    }
    QueueNpcHeldItemNotice(0x5d);
    g_trigger_feedback = true;
    return false;
}

/* Activation callback on _VOC_EWAXXLIFT2: queues the lift NPC notice with
   the held item, if any. */
bool MtGigas1Lift2(Trigger* pTrigger)
{
    if (CurrentGame().activity.conversation) {
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
bool MtGigas1PressurePlate(Trigger* pTrigger)
{
    glm::vec3 position;

    if (g_plate_prop == 0) {
        return false;
    }
    glm::dvec3 camera_position = GetWorld()->camera->transform.getLocation();
    position.x = static_cast<float>(camera_position.x);
    position.z = static_cast<float>(camera_position.z);
    const auto bounds = g_plate_prop->computeBounds();
    if ((position.x >= bounds.minimum.x && position.x <= bounds.maximum.x &&
         position.z >= bounds.minimum.z && position.z <= bounds.maximum.z) ||
        g_plate_prop->HasSupportedItems()) {
        if (g_plate_down) {
            return false;
        }
        RunPlateTrigger("secretDoor-01", W8_ANIMATION_FORWARD);
        RunPlateTrigger("plate", W8_ANIMATION_FORWARD);
        g_plate_down = true;
        SetTriggerVariableByName("PPlateDown", 1);
        wiz8::sound::play("Data\\Sound\\Ambients\\Door Stone Open.wav");
        wiz8::sound::play("Data\\Sound\\Ambients\\Amb Rumble Very Low.wav");
        return false;
    }
    if (!g_plate_down) {
        return false;
    }
    RunPlateTrigger("secretDoor-01", W8_ANIMATION_REVERSE);
    RunPlateTrigger("plate", W8_ANIMATION_REVERSE);
    g_plate_down = false;
    SetTriggerVariableByName("PPlateDown", 0);
    wiz8::sound::play("Data\\Sound\\Ambients\\Door Stone Close 01.wav");
    wiz8::sound::play("Data\\Sound\\Ambients\\Amb Rumble Very Low.wav");
    return false;
}

/* Activation callback on mudWallTrigger: spawns the badass monster at the
   NP_BadassMonster entity. */
bool MtGigas1MudWall(Trigger* pTrigger)
{
    glm::vec3 position;

    if (FindEntityByName("NP_BadassMonster", &position, 0, 0)) {
        SpawnMonsters(0x157, 1, &position, 1, true, false, false);
    }
    return true;
}
