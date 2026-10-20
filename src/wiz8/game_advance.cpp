#include <chrono>

#include "wiz8/combat/fight.h"
#include "wiz8/combat/party_movement.h"
#include "wiz8/entities/prop.h"
#include "wiz8/game.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/local_code/character_events.h"
#include "wiz8/monster_roster.h"
#include "wiz8/platform/GameTimeAccumulator.h"
#include "wiz8/rules/events.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/environment.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {
using std::chrono::milliseconds;

constexpr milliseconds kSearchSweepInterval{500};
constexpr milliseconds kSightInterval{500};
constexpr milliseconds kWorldItemsInterval{50};

// A fight starts when a living party meets a monster hostile enough to fight.
bool HostilesEngage(const Game& game)
{
    return !game.combat.has_value() && AnyCharacterActive() &&
           CurrentLevelPopulation().monster_roster.hostileCombatCount();
}

/* Whether a countdown the step has already run down is due; a due one starts again from
   `interval`, so a late step does not catch up. */
bool Due(milliseconds& due, milliseconds interval)
{
    if (due > milliseconds::zero()) return false;
    due = interval;
    return true;
}
} // namespace

void Game::advance(milliseconds elapsed, StepHold hold)
{
    // Time that passed while the world ran reaches its clock before the new hold applies.
    PassEnvironmentTime(elapsed);
    ApplyWorldAdvancement(*this, hold.world);
    if (!hold.speech) {
        speech.ProcessDeferredCharacterEvents();
        AdvancePartyVoices();
    }
    if (!activity.in_world()) return;

    search_sweep_due -= elapsed;
    sight_due -= elapsed;
    world_items_due -= elapsed;
    if (combat) combat->passTime(elapsed);
    UpdateSurpriseMode();
    ProcessMainGameAutoSave();
    GameTimeAccumulator().Update(elapsed);
    g_animated_prop_present = false;
    WorldUpdateProps(level.primary.get());
    if (level.sky) WorldUpdateProps(level.sky.get());

    ProcessMonsterManagerFrame();
    if (!combat.has_value()) {
        AccumulateMovementFatigue(*this);
        if (Due(search_sweep_due, kSearchSweepInterval)) RunSearchPulse();
    } else if (!hold.combat && !activity.conversation) {
        UpdateCombat();
    }
    if (IsSightRangeOverridden() && HostilesEngage(*this)) beginCombat(0);
    if (Due(sight_due, kSightInterval)) UpdateMonsterSight();
    if (!advancement.held) {
        UpdateMonsterGroups(true);
        if (HostilesEngage(*this) && !activity.conversation) beginCombat(0);
        if (g_navigator_position_changed) {
            g_navigator_position_changed = false;
            if (combat.has_value()) events.push(CombatantsMoved{});
            sight_refresh_pending = true;
        }
    }
    if (Due(world_items_due, kWorldItemsInterval)) {
        UpdateNearbyWorldItems();
        DetachAllWorldItems();
    }
    if (!advancement.paused || combat.has_value()) UpdateWorlds();
}
