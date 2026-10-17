#include "wiz8/level_specific_code/Monastery2.h"

#include "Item.h"
#include "glm/ext/vector_float3.hpp"
#include "layouts/world.h"
#include "soundman.h"
#include "surrender/srDebug.h"
#include "wiz8/engine_code/Prop.h"
#include "wiz8/engine_code/Trigger.hpp"
#include "wiz8/engine_code/World.h"
#include "wiz8/item_spawning.h"

/* Level Specific Code\Monastery2.cpp (level 9).

   Attribution evidence: only 0x004DC7A0 cites this file's path string, in its
   two pPropTrigger assertions. 0x004DC880 is attributed because
   MasterFunctionList registers it under level 9 (Monastery2) and it lies
   between the Monastery2 anchor and the level-8 (Monastery1) callback block.
   The wider gap 0x004DC390-0x004DCB10 holds contiguous per-level callback
   blocks for four level TUs - MtGigasOuter (0xe), MtGigasTop (0xf),
   Monastery2 (9) and Monastery1 (8) - which are not recovered here. */

#define MONASTERY2_CPP "C:\\Projects\\Wizardry 8\\Level Specific Code\\Monastery2.cpp"

static void DisableBellTrigger(const char* name, int line)
{
    Trigger* pPropTrigger = FindTriggerByName(name);
    if (pPropTrigger == 0) {
        srAssertFail("pPropTrigger", MONASTERY2_CPP, line, 0);
    }
    pPropTrigger->flags &= ~W8_TRIGGER_ON;
}

// FUNCTION: WIZ8 0x004dc7a0
bool Monastery2BellButton(Trigger* pTrigger)
{
    W8Prop* prop;
    int slot;

    prop = FindPropByName(GetWorld(), "dial1");
    if (prop != 0) {
        slot = prop->Rep()->FindCurrentAnimationSlot();
        if (slot == 0) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell1.wav");
        } else if (slot == 1) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell2.wav");
            return true;
        } else if (slot == 2) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell3.wav");
            DisableBellTrigger("bellringswitch", 0x21);
            DisableBellTrigger("bell_button", 0x24);
            return true;
        }
    }
    return true;
}

// FUNCTION: WIZ8 0x004dc880
bool Monastery2MicroDoor2(Trigger* pTrigger)
{
    glm::vec3 position;
    W8WorldItem* item;

    position = glm::vec3(13085.0f, -80.0f, -49440.0f);
    item = AddWorldItem(SpawnItem(0x2d7, &position, W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
    if (item != 0) {
        ActivateItem(item);
    }
    return true;
}
