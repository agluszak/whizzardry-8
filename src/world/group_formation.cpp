#include <cassert>

#include "wiz8/entities/monster.h"
#include "wiz8/level_population.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/local_code/MonsterManager.h"
#include "wiz8/rules/conditions.h"
#include "wiz8/spatial/navigator.h"
#include "wiz8/world/world.h"

void World::refreshGroupNavigation(const W8MonsterGroup& group, bool combat_active)
{
    Monster* leader;
    if (group.leader_group_id == 0) {
        leader = population->findEntity(group.leader_location_id);
        if (leader) leader->nav.LinkGroupNavigator(nullptr, combat_active);
    } else {
        const auto* lead_group = population->findGroup(group.leader_group_id);
        leader = population->findEntity(lead_group->leader_location_id);
    }
    if (!leader) return;
    for (int id : group.MemberIds()) {
        auto* member = population->findEntity(id);
        if (member && member != leader) member->nav.LinkGroupNavigator(&leader->nav, combat_active);
    }
}

// Removing a leader transfers its continuing script/noise and repairs the whole formation.
void World::replaceGroupLeader(W8MonsterGroup& group, bool combat_active)
{
    assert(!group.MemberIds().empty());
    const auto& outgoing = *population->findMonster(group.leader_location_id);
    int leader_id = group.MemberIds().front();
    unsigned int best = 0;
    for (int id : group.MemberIds()) {
        const auto* info = population->findMonster(id);
        const auto* body = info->entity.get();
        if (body && info->highest_condition < W8_CONDITION_TURNCOAT &&
            (body->runtime_flags & W8_MONSTER_REMOVE_NOW) == 0 &&
            best < static_cast<unsigned int>(body->nav.movement.leadership_rank)) {
            best = body->nav.movement.leadership_rank;
            leader_id = id;
        }
    }
    group.leader_location_id = leader_id;
    auto& incoming = *population->findMonster(leader_id);
    incoming.inheritLeaderState(outgoing);

    if (group.leader_group_id != 0) return;
    refreshGroupNavigation(group, combat_active);
    for (int id : group.allied_group_ids)
        if (id != 0) refreshGroupNavigation(*population->findGroup(id), combat_active);
    if (auto* leader = population->findEntity(group.leader_location_id))
        leader->nav.PropagateGroupPosition(combat_active);
}

void W8MonsterInfo::inheritLeaderState(const W8MonsterInfo& outgoing)
{
    if (outgoing.entity && outgoing.entity->script && entity)
        entity->UseScript(outgoing.entity->script, true);
    heard_noise_radius = outgoing.heard_noise_radius;
    heard_noise_position = outgoing.heard_noise_position;
}
