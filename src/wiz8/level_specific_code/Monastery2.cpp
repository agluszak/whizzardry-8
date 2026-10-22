#include "wiz8/level_specific_code/Monastery2.h"

#include <memory>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/entities/world_item.h"
#include "wiz8/item_spawning.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/script_context.h"
#include "wiz8/world/world.h"

/* Level Specific Code\Monastery2.cpp (level 9).

   Attribution evidence: only 0x004DC7A0 cites this file's path string, in its
   two pPropTrigger assertions. 0x004DC880 is attributed because the level-9
   (Monastery2) script binds it and it lies between the Monastery2 anchor and
   the level-8 (Monastery1) callback block. The wider gap
   0x004DC390-0x004DCB10 holds contiguous per-level callback blocks for four
   level TUs - MtGigasOuter (0xe), MtGigasTop (0xf), Monastery2 (9) and
   Monastery1 (8) - which are not recovered here. */

namespace {

void DisableBellTrigger(const char* name)
{
    Trigger* pPropTrigger = FindRequiredTriggerByName(name);
    pPropTrigger->flags &= ~W8_TRIGGER_ON;
}

bool MicroDoor2(Trigger* pTrigger)
{
    glm::vec3 position;
    W8WorldItem* item;

    position = glm::vec3(13085.0f, -80.0f, -49440.0f);
    item = AddWorldItem(*GetWorld(), SpawnItem(*GetWorld(), 0x2d7, &position,
                                               W8_ITEM_ENTITY_PULSE | W8_ITEM_ENTITY_ROTATE));
    if (item != 0) {
        ActivateItem(*GetWorld(), item);
    }
    return true;
}

class Monastery2Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override
    {
        bindTrigger("bell_button", &Monastery2Script::bellButton);
        bindTrigger("micro_door2", MicroDoor2);
    }

private:
    bool bellButton(Trigger* pTrigger);
};

bool Monastery2Script::bellButton(Trigger* pTrigger)
{
    Prop* prop;
    int slot;

    prop = FindPropByName(&world(), "dial1");
    if (prop != 0) {
        slot = prop->FindCurrentAnimationSlot();
        if (slot == 0) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell1.wav");
        } else if (slot == 1) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell2.wav");
            return true;
        } else if (slot == 2) {
            wiz8::sound::play("Data\\Sound\\Ambients\\Mon2Bell3.wav");
            DisableBellTrigger("bellringswitch");
            DisableBellTrigger("bell_button");
            return true;
        }
    }
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeMonastery2Script(const ScriptContext& context)
{
    return std::make_unique<Monastery2Script>(context);
}
