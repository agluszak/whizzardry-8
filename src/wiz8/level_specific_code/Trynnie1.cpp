#include "wiz8/level_specific_code/Trynnie1.h"

#include <cassert>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "glm/ext/vector_float3.hpp"
#include "wiz8/audio/soundman.h"
#include "wiz8/data/fact_ids.h"
#include "wiz8/data/faction_ids.h"
#include "wiz8/data/records.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/level_specific_code/level_script.h"
#include "wiz8/live_party.h"
#include "wiz8/local_code/ConditionsAndEnchantments.h"
#include "wiz8/local_code/HealthStaminaMana.h"
#include "wiz8/local_code/MonsterGenerator.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/location_variables.h"
#include "wiz8/monster_generators.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/rules/item_instance.h"
#include "wiz8/rules/party.h"
#include "wiz8/rules/party_slot.h"
#include "wiz8/script_context.h"
#include "wiz8/story.h"
#include "wiz8/world/level_session.h"
#include "wiz8/world/world.h"

class Trigger;

/* Level Specific Code\Trynnie1.cpp (level 0x19).

   Attribution evidence: 0x004DA670 and 0x004DA850 cite this file's path
   string in their assertions; 0x004DA670 is the level-0x19 setup that reads
   the 'Trynnies'/'Dead Trynnies' encounter tables into the Trynnie1Killed
   location variable. The level-0x1a neighbors belong to Trynnie2.cpp
   (0x004D9E60 cites it). */

/* Item ids 0x1b3 and 0x1c3 route the use-item action down the origin/equip
   path instead of activating, the same as a non-usable equip class. */
bool IsSpecialItemId(const W8ItemInstance* item)
{
    return item->iItemNo == 0x1b3 || item->iItemNo == 0x1c3;
}

/* Replace every live faction-0xd (Trynnie) group with a dead-Trynnie (0x1be)
   group, then point each 'Trynnies' generator at the 'Dead Trynnies' table. */
void KillTrynnieGroups(World& world)
{
    W8MonsterGroup* group;
    const W8MonsterRecord* record;
    MonGen* generator;
    int iOldTable;
    int iNewTable;
    int index;

    for (index = 0; index < static_cast<int>(CurrentLevelPopulation().monster_groups.size());
         ++index) {
        group = CurrentLevelPopulation().monster_groups[index].get();
        if (group != 0 && group->members_active &&
            (record = MonsterGroupGetRecord(group), record->faction_id == W8_FACTION_TRYNNIE) &&
            ReplaceMonsterGroupSpecies(group, 0x1be) != 0) {
            RemoveAllGroupMembers(group);
        }
    }
    iOldTable = FindEncounterTableByName("Trynnies");
    iNewTable = FindEncounterTableByName("Dead Trynnies");
    if (iOldTable == -1 || iNewTable == -1)
        throw std::runtime_error("Trynnie encounter tables are missing");
    for (index = 0; index < static_cast<int>(world.encounters.generators.size()); ++index) {
        generator = world.encounters.generators[index].get();
        if (generator->encounter_table_index == iOldTable) {
            generator->SetEncounterTable(iNewTable);
        }
    }
}

namespace {

class Trynnie1Script final : public LevelScript {
public:
    using LevelScript::LevelScript;

    void enter() override;

private:
    bool fountRandomFX(Trigger* pTrigger);
};

/* Binds the fountain, then, once fact 0x229 reports the Trynnies dead and
   the Trynnie1Killed location variable does not exist yet, swaps the
   encounter tables and spawns the dead shaman (0xcd) at VOC_SHAMAN_DYING. */
void Trynnie1Script::enter()
{
    glm::vec3 vPos2;

    bindTrigger("Fount_randomFX", &Trynnie1Script::fountRandomFX);
    if (ctx.game.testFact(W8_FACT_TRYNNIE_SHAMAN_DIES) == 1) {
        if (GetLocationVarIDByName("Trynnie1Killed") == -1) {
            KillTrynnieGroups(world());
            const auto vPos = world().namedPosition("VOC_SHAMAN_DYING");
            if (!vPos) throw std::runtime_error("level lacks VOC_SHAMAN_DYING");
            vPos2 = *vPos;
            [[maybe_unused]] auto* group = CreateGroup(0xcd, 1, &vPos2, true, true);
            assert(group != 0);
            CurrentGame().story.ensureLocationVariable(GetLoadedLevelID(), "Trynnie1Killed", 1);
        }
    }
}

/* "Fount_randomFX": the magic fountain rolls d100 - afflict a random party
   member with condition 9 (25%), restore the party's spell points (25%),
   heal everyone (25%) or restore everyone's stamina (25%). */
bool Trynnie1Script::fountRandomFX(Trigger* pTrigger)
{
    unsigned int roll;
    int slot;
    int i;

    roll = ctx.game.rng.below(100);
    if (roll < 25) {
        slot = CurrentGame().party.randomSlot(CurrentGame().rng, 0, 0, -1, -1);
        if (slot != -1) {
            InflictCondition(ctx.game, slot,
                             {.condition = W8_CONDITION_HEXED,
                              .duration = static_cast<int>(ctx.game.rng.below(5)) + 10});
            wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
            return true;
        }
    } else if (roll < 50) {
        RestorePartySpellPoints(-1);
        wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
        return true;
    } else if (roll < 75) {
        for (i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
            if (ctx.party.members[i].has_value()) {
                HealCharacter(i, 100, false);
            }
        }
        wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
        return true;
    } else {
        for (i = 0; i < W8_PARTY_SLOT_COUNT; ++i) {
            if (ctx.party.members[i].has_value()) {
                RestoreCharacterStamina(i, 100, false);
            }
        }
    }
    wiz8::sound::play("Data\\Sound\\misc\\fountain_magic.wav");
    return true;
}

} // namespace

std::unique_ptr<LevelScript> MakeTrynnie1Script(const ScriptContext& context)
{
    return std::make_unique<Trynnie1Script>(context);
}
