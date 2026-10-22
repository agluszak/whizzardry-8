#include "wiz8/local_code/Traps.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

#include <fmt/format.h>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/data/records.h"
#include "wiz8/entities/locks.h"
#include "wiz8/entities/prop.h"
#include "wiz8/entities/trigger.h"
#include "wiz8/game.h"
#include "wiz8/layouts/game_status.h"
#include "wiz8/loaded_game_data.h"
#include "wiz8/local_code/Magic.h"
#include "wiz8/local_code/Targeting.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/local_code/spell_database.h"
#include "wiz8/local_screens/MainGameScreen.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/rules/targeting.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/startup_world.h"
#include "wiz8/string_database.h"
#include "wiz8/world/party_camera.h"
#include "wiz8/world/world.h"

/* Per-device spell/notice table; TriggerTrapDevice reads
   the effect spell id at index device + 0xb. */
int g_table2[] = {10,  25, 35, 40, 50, 60, 70, 80,  90,  100, 110, 121, 122,
                  123, 24, 47, 36, 37, 60, 70, 124, 125, 126, 86,  127, 91};
static unsigned char g_table1[15][8] = {
    {0, 1, 0, 0, 0, 1, 0, 0}, {0, 1, 0, 1, 0, 0, 0, 0}, {0, 0, 1, 0, 0, 1, 0, 0},
    {0, 0, 0, 0, 1, 1, 0, 0}, {0, 0, 1, 1, 0, 0, 0, 1}, {1, 1, 0, 0, 1, 0, 0, 0},
    {0, 0, 1, 0, 1, 0, 1, 0}, {1, 0, 0, 1, 0, 1, 0, 0}, {0, 0, 1, 1, 1, 0, 0, 0},
    {1, 0, 0, 0, 0, 1, 0, 1}, {0, 1, 1, 0, 0, 0, 0, 1}, {0, 0, 1, 1, 0, 0, 1, 0},
    {0, 1, 0, 0, 1, 0, 1, 0}, {0, 1, 0, 0, 0, 1, 1, 0}, {1, 0, 0, 0, 0, 1, 1, 0},
};

unsigned char GetTable650434Entry(int row, int column)
{
    return g_table1[row][column];
}

/* Per-type device floor for the sprung-trap discharge: the type's entry is
   subtracted from the trigger's device count before extra targets and power
   are rolled. */
static int g_trap_difficulty[W8_TRAP_TYPE_COUNT] = {1, 1, 2, 2, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7};

/* Roll the trigger's trap type (device_id) on first interaction: rejection-
   sample the fifteen-row trap table until a type whose per-type difficulty
   lands within four of the trigger's grade (difficulty, floored at one). */
void SelectTrapType(Trigger* trigger)
{
    LockState* lock_state;
    int budget;
    int type;

    lock_state = &trigger->lock_state;
    if (lock_state == 0) {
        return;
    }
    budget = lock_state->difficulty;
    if (budget < 1) {
        budget = 1;
    }
    do {
        type = CurrentGame().rng.below(0xf);
        lock_state->device_id = type;
    } while (g_trap_difficulty[type] > budget || g_trap_difficulty[type] + 4 < budget);
}

void CompleteTrapDisarm(Trigger* trigger)
{
    int type;
    std::string text;

    trigger->CompleteItemInteraction();
    type = trigger->lock_state.device_id;
    if (CurrentGame().rng.below(100) < 40) {
        ApplyItemEffectToRandomCharacter(g_learn_sound, -1, 0);
    }
    text =
        fmt::format("{} {}", Data().strings[g_trap_name_string_ids[type]], Data().strings[0x7b2]);
    ShowString(text);
    trigger->Run(-1);
}

static void DischargeTrapSpell(glm::vec3 point, int spell_id, unsigned int power_level,
                               int num_targets)
{
    int index;
    int eligible;
    std::vector<int> targets;
    W8CombatSlot target;
    W8TargetSource source;

    assert(num_targets >= 1);
    ResetTargetSource(&source);
    source.iType = W8_TARGET_SOURCE_INDIRECT;
    source.point = point;
    if (GetSpellTargetType(spell_id, false) == W8_TARGET_TYPE_POINT) {
        target.iType = W8_TARGET_KIND_PLACE;
        target.point = PartyNavigator()->GetPosition();
        CastSpellFromSource(spell_id, &source, &target, power_level, 0, 0, false, 0,
                            W8SpellCastKind::Standard, 0, 0);
    } else {
        ResetCombatSlot(&target);
        target.iType = W8_TARGET_KIND_PARTY;
        target.point = PartyNavigator()->GetPosition();
        eligible = 0;
        for (index = 0; index < W8_PARTY_SLOT_COUNT; ++index) {
            if (CanPartySlotParticipate(index)) {
                targets.push_back(index);
                ++eligible;
            }
        }
        if (num_targets >= eligible) {
            num_targets = eligible;
        }
        while (static_cast<int>(targets.size()) > num_targets) {
            index = CurrentGame().rng.below(static_cast<int>(targets.size()));
            if (targets[index] != CurrentGame().status.selected_character) {
                targets.erase(targets.begin() + index);
            }
        }
        CastSpellFromSource(spell_id, &source, &target, power_level, 0, 0, false, 0,
                            W8SpellCastKind::Standard, &targets, 0);
    }
}

void ResolveSprungTrap(Trigger* trigger)
{
    int devices;
    int type;
    int count;
    int power;
    std::string result;
    std::string text;
    glm::vec3 point;
    glm::vec3 camera;

    devices = trigger->lock_state.difficulty;
    devices = std::clamp(devices, 1, 7);
    type = trigger->lock_state.device_id;
    if (CurrentGame().rng.below(2) == 0) {
        trigger->CompleteItemInteraction();
        result = Data().strings[0x7b3];
    } else {
        result = Data().strings[0x7b4];
    }
    text = fmt::format("{} {}", Data().strings[g_trap_name_string_ids[type]], result);
    ShowString(text);
    count = devices - static_cast<int>(CurrentGame().rng.below(devices / 2));
    power = 4;
    if (devices > g_trap_difficulty[type]) {
        devices -= g_trap_difficulty[type];
        power = devices + 4;
        if (power > 7) {
            count += static_cast<int>(CurrentGame().rng.below(devices - 3));
            power = 7;
        }
    }
    Prop* prop = trigger->GetProp();
    if (prop == 0) {
        GetCameraForwardPoint(1000.0f, &point);
    } else {
        const auto center = prop->GetCenterPosition();
        if (!center) return;
        point = *center;
    }
    camera = cameraLocation();
    if (const auto hit = GetWorldSpatialIndex()->traceSight(camera, point)) point = hit->point;
    DischargeTrapSpell(point, g_table2[type + 11], power, count);
}
