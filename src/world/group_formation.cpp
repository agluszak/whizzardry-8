#include <algorithm>
#include <cassert>
#include <memory>
#include <stdexcept>
#include <utility>

#include "wiz8/entities/animation_types.h"
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

void World::restartGroupScripts()
{
    for (const auto& group : population->groups()) {
        auto* leader = population->findEntity(group->leader_location_id);
        if (leader && leader->script) leader->UseScript(leader->script, true);
    }
}

void W8LevelPopulation::unlinkGroup(W8MonsterGroup& group)
{
    if (group.leader_group_id == 0) return;
    auto* previous = findGroup(group.leader_group_id);
    std::ranges::replace(previous->allied_group_ids, group.group_id, 0);
    group.leader_group_id = 0;
}

W8MonsterGroup* W8LevelPopulation::alliedSuccessor(const W8MonsterGroup& outgoing) const
{
    W8MonsterGroup* leader = nullptr;
    unsigned int best = 0;
    for (int id : outgoing.allied_group_ids) {
        auto* candidate = id == 0 ? nullptr : findGroup(id);
        if (!candidate) continue;
        unsigned int rank = 0;
        for (int member_id : candidate->MemberIds())
            if (auto* body = findEntity(member_id))
                rank =
                    std::max(rank, static_cast<unsigned int>(body->nav.movement.leadership_rank));
        if (!leader || rank > best) {
            leader = candidate;
            best = rank;
        }
    }
    return leader;
}

W8MonsterGroup& W8LevelPopulation::adoptGroup(std::unique_ptr<W8MonsterGroup> group, bool encounter)
{
    auto& groups = encounter ? encounter_groups_ : monster_groups_;
    groups.push_back(std::move(group));
    return *groups.back();
}

void W8LevelPopulation::eraseEmptyGroup(W8MonsterGroup& group)
{
    if (!group.MemberIds().empty())
        throw std::logic_error("cannot erase a populated monster group");
    for (auto* partition : {&monster_groups_, &encounter_groups_}) {
        const auto found =
            std::ranges::find(*partition, &group, &std::unique_ptr<W8MonsterGroup>::get);
        if (found != partition->end()) {
            partition->erase(found);
            return;
        }
    }
}

bool W8LevelPopulation::giveBirth(W8MonsterGroup& group)
{
    const auto found =
        std::ranges::find(encounter_groups_, &group, &std::unique_ptr<W8MonsterGroup>::get);
    if (found == encounter_groups_.end()) return false;
    // Reserve storage before moving either owning partition.
    monster_groups_.reserve(monster_groups_.size() + 1);
    monster_roster.live_.reserve(monster_roster.live_.size() + group.MemberIds().size());
    monster_groups_.push_back(std::move(*found));
    encounter_groups_.erase(found);
    for (int id : group.MemberIds()) {
        auto* info = findMonster(id);
        if (!info || !monster_roster.promote(info)) continue;
        auto& body = *info->entity;
        if (body.IsCycleSupported(0)) {
            body.SetCycle(0);
            body.visual.SetBehaviour(W8_ANIMATION_PLAY_ONCE);
        } else {
            body.SetCycle(1);
            body.visual.SetBehaviour(W8_ANIMATION_NEVER_STOP);
        }
        body.visual.SetSubCycle(0);
        body.SetAnimating(true);
        body.nav.active = true;
        body.inactive = false;
    }
    return true;
}
