#pragma once

#include <algorithm>
#include <vector>
#include "wiz8/sr_api.h"

struct W8PathHeapEntry {
    unsigned int node;
    unsigned int priority;
};

// A* compares integer priorities only. Equal priorities retain the retail
// heap's tree ordering; adding FIFO or node-ID ordering changes chosen paths.
class W8PathOpenSet {
public:
    void reserve(unsigned int capacity)
    {
        entries.reserve(capacity);
        limit = capacity;
    }
    void clear() { entries.clear(); }
    bool empty() const { return entries.empty(); }
    const W8PathHeapEntry& top() const { return entries.front(); }

    void push(W8PathHeapEntry entry)
    {
        if (entries.size() == limit) {
            srAssertFail("heapsize < maxheapsize", "path_open_set.h", __LINE__,
                         "path open set overflow");
        }
        entries.push_back(entry);
        std::push_heap(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
            return a.priority > b.priority;
        });
    }

    W8PathHeapEntry pop()
    {
        if (entries.empty()) {
            srAssertFail("heapsize > 0", "path_open_set.h", __LINE__,
                         "empty path open set");
        }
        const auto result = entries.front();
        const auto replacement = entries.back();
        entries.pop_back();
        if (entries.empty()) {
            return result;
        }
        // std::pop_heap may move equal children above the replacement. Retail
        // stops at equality and chooses the right child when siblings tie.
        // Keep that game-specific extraction rule on standard vector storage.
        std::size_t index = 0;
        for (std::size_t child = 1; child < entries.size(); child = index * 2 + 1) {
            if (child + 1 < entries.size() &&
                entries[child + 1].priority <= entries[child].priority) {
                ++child;
            }
            if (replacement.priority <= entries[child].priority) {
                break;
            }
            entries[index] = entries[child];
            index = child;
        }
        entries[index] = replacement;
        return result;
    }

private:
    std::vector<W8PathHeapEntry> entries;
    std::size_t limit = 0;
};
