#ifndef WIZ8_MONSTER_ROSTER_H
#define WIZ8_MONSTER_ROSTER_H

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

enum class W8MonsterRemoval : unsigned char;

#include "wiz8/local_code/MonsterManager.h"

// The ordered live and unborn partitions own their records. Promotion moves
// ownership without moving the monster. Only the complete gameplay removal
// operation can erase an individual record.
class W8MonsterRoster {
public:
    W8MonsterRoster() = default;
    W8MonsterRoster(const W8MonsterRoster&) = delete;
    W8MonsterRoster& operator=(const W8MonsterRoster&) = delete;
    W8MonsterInfo* add(std::unique_ptr<W8MonsterInfo> info, bool unborn)
    {
        auto& index = unborn ? unborn_ : live_;
        index.push_back(std::move(info));
        return index.back().get();
    }

private:
    friend struct W8LevelPopulation;
    // Only the population birth operation may promote a record.
    bool promote(W8MonsterInfo* info)
    {
        const auto it = std::ranges::find(unborn_, info, &std::unique_ptr<W8MonsterInfo>::get);
        if (it == unborn_.end()) {
            return false;
        }
        live_.push_back(std::move(*it));
        unborn_.erase(it);
        return true;
    }

public:
    const std::vector<std::unique_ptr<W8MonsterInfo>>& live() const { return live_; }
    const std::vector<std::unique_ptr<W8MonsterInfo>>& unborn() const { return unborn_; }
    bool isUnborn(const W8MonsterInfo* info) const
    {
        return std::ranges::contains(unborn_, info, &std::unique_ptr<W8MonsterInfo>::get);
    }

    bool hasActiveMonsters() const
    {
        const auto active = [](const auto& info) { return info->fActive; };
        return std::ranges::any_of(live_, active) || std::ranges::any_of(unborn_, active);
    }

    bool hasEngagedHostile() const
    {
        return std::ranges::any_of(live_, [](const auto& info) {
            return info->pCombat && info->ubDisposition == W8_DISPOSITION_HOSTILE &&
                   info->hp_current != 0 && info->highest_condition < W8_CONDITION_WEBBED;
        });
    }

    auto hostileCombatCount() const
    {
        return std::ranges::count_if(live_, [](const auto& info) {
            return info->fActive && info->pCombat && info->ubDisposition == W8_DISPOSITION_HOSTILE;
        });
    }

private:
    friend struct World;
    bool erase(W8MonsterInfo* info, std::vector<std::unique_ptr<W8MonsterInfo>>* retirements)
    {
        for (auto* partition : {&live_, &unborn_}) {
            const auto it =
                std::ranges::find(*partition, info, &std::unique_ptr<W8MonsterInfo>::get);
            if (it != partition->end()) {
                // Allocation happens before moving the owner. Failure leaves it retained.
                if (retirements) retirements->push_back(std::move(*it));
                partition->erase(it);
                return true;
            }
        }
        return false;
    }

    std::vector<std::unique_ptr<W8MonsterInfo>> live_;
    std::vector<std::unique_ptr<W8MonsterInfo>> unborn_;
};

#endif
