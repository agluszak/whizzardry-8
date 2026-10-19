#ifndef WIZ8_MONSTER_ROSTER_H
#define WIZ8_MONSTER_ROSTER_H

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

enum class W8MonsterRemoval;

#include "wiz8/local_code/MonsterManager.h"

// The ordered live and unborn partitions own their records. Promotion moves
// ownership without moving the monster. Only the complete gameplay removal
// operation can erase an individual record.
class W8MonsterRoster {
public:
    W8MonsterInfo* add(std::unique_ptr<W8MonsterInfo> info, bool unborn)
    {
        auto& index = unborn ? unborn_ : live_;
        index.push_back(std::move(info));
        return index.back().get();
    }

    // Membership change only; returns false when the monster is not unborn.
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

    auto hostileCombatCount() const
    {
        return std::ranges::count_if(live_, [](const auto& info) {
            return info->fActive && info->pCombat && info->ubDisposition == W8_DISPOSITION_HOSTILE;
        });
    }

private:
    friend bool RemoveMonster(W8MonsterInfo*, W8MonsterRemoval);
    bool erase(W8MonsterInfo* info)
    {
        for (auto* partition : {&live_, &unborn_}) {
            const auto it =
                std::ranges::find(*partition, info, &std::unique_ptr<W8MonsterInfo>::get);
            if (it != partition->end()) {
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
