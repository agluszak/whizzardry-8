#pragma once

#include <algorithm>
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
    std::vector<std::unique_ptr<W8WorldItem>> world_items;
    std::vector<W8MonsterGroup*> active_encounters;
    W8MonsterInfo* findMonster(int id) const
    {
        for (const auto* partition : {&monster_roster.live(), &monster_roster.unborn()})
            for (const auto& info : *partition)
                if (info->location_id == id) return info.get();
        return nullptr;
    }
    int activeMemberCount(const W8MonsterGroup& group) const
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
