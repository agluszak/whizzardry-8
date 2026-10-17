#ifndef WIZ8_MONSTER_ROSTER_H
#define WIZ8_MONSTER_ROSTER_H

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/local_code/MonsterManager.h"

// Live and unborn partitions own their records. Promotion preserves object addresses
// and appends to the live order. Callers unregister world, visibility and NPC
// references before removing a record.
class W8MonsterRoster {
public:
    W8MonsterInfo* add(std::unique_ptr<W8MonsterInfo> info, bool unborn)
    {
        auto* borrowed = info.get();
        (unborn ? unborn_ : live_).push_back(std::move(info));
        return borrowed;
    }

    // Membership change only; returns false when the monster is not unborn.
    bool promote(W8MonsterInfo* info)
    {
        const auto it = std::ranges::find(unborn_, info, &std::unique_ptr<W8MonsterInfo>::get);
        if (it == unborn_.end()) return false;
        live_.push_back(std::move(*it));
        unborn_.erase(it);
        return true;
    }

    // Destroys the record. Callers have already unregistered borrowed references.
    bool remove(W8MonsterInfo* info)
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

    const std::vector<std::unique_ptr<W8MonsterInfo>>& live() const { return live_; }
    const std::vector<std::unique_ptr<W8MonsterInfo>>& unborn() const { return unborn_; }
    bool isUnborn(const W8MonsterInfo* info) const
    {
        return std::ranges::contains(unborn_, info, &std::unique_ptr<W8MonsterInfo>::get);
    }

private:
    std::vector<std::unique_ptr<W8MonsterInfo>> live_;
    std::vector<std::unique_ptr<W8MonsterInfo>> unborn_;
};

#endif
