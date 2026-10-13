#ifndef WIZ8_MONSTER_ROSTER_H
#define WIZ8_MONSTER_ROSTER_H

#include <algorithm>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "wiz8/local_code/MonsterManager.h"

// Sole owner of every W8MonsterInfo. The live and unborn lists are ordered
// indexes into that storage: a monster is in exactly one of them, promotion
// moves it between them without touching ownership, and removal drops the
// index entry before the storage is released. Other systems (groups,
// visibility, the octree) hold location ids or borrowed pointers and must be
// unregistered by the caller before remove() is called.
class W8MonsterRoster {
public:
    W8MonsterInfo* add(std::unique_ptr<W8MonsterInfo> info, bool unborn)
    {
        auto& index = unborn ? unborn_ : live_;
        // Reserve first so a failed allocation cannot leave an owned but unindexed record.
        index.reserve(index.size() + 1);
        owned_.reserve(owned_.size() + 1);
        W8MonsterInfo* borrowed = info.get();
        owned_.push_back(std::move(info));
        index.push_back(borrowed);
        return borrowed;
    }

    // Membership change only; returns false when the monster is not unborn.
    bool promote(W8MonsterInfo* info)
    {
        const auto it = std::ranges::find(unborn_, info);
        if (it == unborn_.end()) return false;
        unborn_.erase(it);
        live_.push_back(info);
        return true;
    }

    // Unindexes and then destroys the record. Returns false if it is not owned.
    bool remove(W8MonsterInfo* info)
    {
        const auto owner = std::ranges::find(owned_, info, &std::unique_ptr<W8MonsterInfo>::get);
        if (owner == owned_.end()) return false;
        if (!std::erase(live_, info)) std::erase(unborn_, info);
        owned_.erase(owner);
        return true;
    }

    const std::vector<W8MonsterInfo*>& live() const { return live_; }
    const std::vector<W8MonsterInfo*>& unborn() const { return unborn_; }
    bool isUnborn(const W8MonsterInfo* info) const { return std::ranges::contains(unborn_, info); }

private:
    std::vector<std::unique_ptr<W8MonsterInfo>> owned_;
    std::vector<W8MonsterInfo*> live_;
    std::vector<W8MonsterInfo*> unborn_;
};

#endif
