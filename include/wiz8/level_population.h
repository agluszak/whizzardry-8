#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "wiz8/item_spawning.h"
#include "wiz8/local_code/MonsterGroup.h"
#include "wiz8/monster_roster.h"

// Records and borrowed traversal indexes belong to the same loaded level as
// their graphical representations. Preparation uses an unpublished instance.
struct W8LevelPopulation {
    W8LevelPopulation() = default;
    W8LevelPopulation(const W8LevelPopulation&) = delete;
    W8LevelPopulation& operator=(const W8LevelPopulation&) = delete;
    W8MonsterRoster monster_roster;
    const auto& groups() const { return monster_groups_; }
    const auto& encounterGroups() const { return encounter_groups_; }
    W8MonsterGroup& adoptGroup(std::unique_ptr<W8MonsterGroup> group, bool encounter);
    void eraseEmptyGroup(W8MonsterGroup& group);
    bool giveBirth(W8MonsterGroup& group);
    std::vector<std::unique_ptr<W8WorldItem>> world_items;
    // A traversal snapshot, never a second registry to update. A formation
    // consumes one encounter slot regardless of the number of allied groups.
    std::vector<W8MonsterGroup*> activeEncounterLeaders() const
    {
        std::vector<W8MonsterGroup*> leaders;
        for (const auto* partition : {&monster_groups_, &encounter_groups_})
            for (const auto& group : *partition)
                if (group->encounter_registered && group->leader_group_id == 0)
                    leaders.push_back(group.get());
        return leaders;
    }
    W8MonsterInfo* findMonster(int id) const
    {
        for (const auto* partition : {&monster_roster.live(), &monster_roster.unborn()})
            for (const auto& info : *partition)
                if (info->location_id == id) return info.get();
        return nullptr;
    }
    void releaseMonsterVisibility(W8MonsterInfo& removed)
    {
        removed.monster_visibility.reset();
        for (const auto& monster : monster_roster.live()) {
            if (monster->fActive && monster->monster_visibility)
                monster->monster_visibility->erase(removed.location_id);
        }
    }
    void releaseElementalSummon(int id)
    {
        for (const auto& monster : monster_roster.live()) {
            if (monster->elemental_summon == id) {
                monster->elemental_summon = -1;
                break;
            }
        }
    }
    // The live monster at `id`, if one is in the world.
    Monster* findEntity(int id) const
    {
        const auto* info = findMonster(id);
        return info ? info->entity.get() : nullptr;
    }
    W8MonsterGroup* alliedSuccessor(const W8MonsterGroup& outgoing) const;
    void unlinkGroup(W8MonsterGroup& group);
    bool hasVisibleThreat(const W8MonsterGroup& group) const;
    bool groupCanSeeGroup(const W8MonsterGroup& source, const W8MonsterGroup& target) const;
    bool allMembersDying(const W8MonsterGroup& group) const;
    auto activeMemberCount(const W8MonsterGroup& group) const
    {
        return std::ranges::count_if(group.MemberIds(), [this](int id) {
            const auto* member = findMonster(id);
            return member->highest_condition < W8_CONDITION_TURNCOAT &&
                   member->control_state != W8_MONSTER_CONTROL_LURED;
        });
    }
    W8MonsterGroup* findSpeciesGroup(int species, const W8MonsterGroup* after = nullptr) const
    {
        bool passed = after == nullptr;
        for (const auto* partition : {&monster_groups_, &encounter_groups_})
            for (const auto& group : *partition) {
                if (passed && group->monster_id == species) return group.get();
                if (group.get() == after) passed = true;
            }
        return nullptr;
    }
    W8MonsterGroup* findGroup(int id) const
    {
        for (const auto* partition : {&monster_groups_, &encounter_groups_})
            for (const auto& group : *partition)
                if (group->group_id == id) return group.get();
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<W8MonsterGroup>> monster_groups_;
    std::vector<std::unique_ptr<W8MonsterGroup>> encounter_groups_;
};

W8LevelPopulation& CurrentLevelPopulation();
/* Bounded queries into the current level; absent levels resolve to null. */
W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index);
W8MonsterInfo* GetUnbornMonsterEntry(int index);
W8WorldItem* GetWorldItemListEntry(int index);
