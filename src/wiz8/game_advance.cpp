#include <cstdint>

#include "wiz8/entities/prop.h"
#include "wiz8/game.h"
#include "wiz8/layouts/combat_state.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/Combat.h"
#include "wiz8/local_code/CombatPartyMovement.h"
#include "wiz8/local_code/GameplayCode.h"
#include "wiz8/local_code/GameplayTime.h"
#include "wiz8/local_code/ItemManager.h"
#include "wiz8/local_code/LoadSaveGame.h"
#include "wiz8/local_code/MonsterAI.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/local_code/Search.h"
#include "wiz8/local_code/Sight.h"
#include "wiz8/monster_roster.h"
#include "wiz8/platform/timer.h"
#include "wiz8/rules/events.h"
#include "wiz8/spatial/level_spatial_index.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/update.h"
#include "wiz8/world/world.h"

namespace {
constexpr std::uint64_t kSightIntervalMs = 500;
constexpr std::uint64_t kWorldItemsIntervalMs = 50;

// A fight starts when a living party meets a monster hostile enough to fight.
bool HostilesEngage(const Game& game)
{
    return !game.combat.active() && AnyCharacterActive() &&
           CurrentLevelPopulation().monster_roster.hostileCombatCount();
}
} // namespace

void Game::advance(StepHold hold)
{
    UpdateSurpriseMode();
    ProcessMainGameAutoSave();
    UpdateSharedGameDataObject();
    g_animated_prop_present = false;
    WorldUpdateProps(level.primary.get());
    if (level.sky) WorldUpdateProps(level.sky.get());

    ProcessMonsterManagerFrame();
    if (!combat.active()) {
        AccumulateMovementFatigue(*this);
        RunSearchPulse();
    } else if (!hold.combat && !activity.conversation) {
        UpdateCombat();
    }
    if (IsSightRangeOverridden() && HostilesEngage(*this)) combat.Start(0);
    if (!TimeUntilDeadline(sight_deadline)) {
        UpdateMonsterSight();
        sight_deadline = MonotonicMilliseconds() + kSightIntervalMs;
    }
    if (!advancement.held) {
        UpdateMonsterGroups(true);
        if (HostilesEngage(*this) && !activity.conversation) combat.Start(0);
        if (g_navigator_position_changed) {
            g_navigator_position_changed = false;
            if (combat.active()) events.push(CombatantsMoved{});
            sight_refresh_pending = true;
        }
    }
    if (!TimeUntilDeadline(world_items_deadline)) {
        UpdateNearbyWorldItems();
        DetachAllWorldItems();
        world_items_deadline = MonotonicMilliseconds() + kWorldItemsIntervalMs;
    }
    if (!advancement.paused || combat.active()) UpdateWorlds();
}
