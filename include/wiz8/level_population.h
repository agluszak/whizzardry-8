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
    W8MonsterRoster monster_roster;
    std::vector<std::unique_ptr<W8MonsterGroup>> monster_groups;
    std::vector<std::unique_ptr<W8MonsterGroup>> encounter_groups;
    // A traversal snapshot, never a second registry to update. A formation
    // consumes one encounter slot regardless of the number of allied groups.
    std::vector<W8MonsterGroup*> activeEncounterLeaders() const
    {
        std::vector<W8MonsterGroup*> leaders;
        for (const auto* partition : {&monster_groups, &encounter_groups})
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
    // The live monster at `id`, if one is in the world.
    Monster* findEntity(int id) const
    {
        const auto* info = findMonster(id);
        return info ? info->entity.find() : nullptr;
    }
    auto activeMemberCount(const W8MonsterGroup& group) const
    {
        return std::ranges::count_if(group.MemberIds(), [this](int id) {
            const auto* member = findMonster(id);
            return member->highest_condition < W8_CONDITION_TURNCOAT &&
                   member->control_state != W8_MONSTER_CONTROL_LURED;
        });
    }
    W8MonsterGroup* findGroup(int id) const
    {
        for (const auto* partition : {&monster_groups, &encounter_groups})
            for (const auto& group : *partition)
                if (group->group_id == id) return group.get();
        return nullptr;
    }
};

W8LevelPopulation& CurrentLevelPopulation();
/* Bounded queries into the current level; absent levels resolve to null. */
W8MonsterInfo* GetMonsterListEntry(std::ptrdiff_t index);
W8MonsterInfo* GetUnbornMonsterEntry(int index);
