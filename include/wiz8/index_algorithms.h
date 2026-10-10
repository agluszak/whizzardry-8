#pragma once

#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>

// These indexes store borrowed pointers or numeric IDs. Standard containers
// own only the index nodes; their values keep the existing game ownership.
// Duplicate-key traversal is observable in path selection and mesh stripping.
// Keep newest-first candidate ordering stable across standard rehashes.
template<class Index>
void InsertIndexValue(Index& index, typename Index::key_type key, typename Index::mapped_type value)
{
    auto [first, last] = index.equal_range(key);
    const auto inserted = index.emplace_hint(first, key, value);
    auto [begin, end] = index.equal_range(key);
    // A conforming implementation may ignore the hint. Explicitly rotate the
    // new value to the front rather than depend on library insertion order.
    if (inserted != begin) {
        std::vector<typename Index::mapped_type> values{value};
        for (auto item = begin; item != end; ++item) {
            if (item != inserted) values.push_back(item->second);
        }
        auto output = values.begin();
        for (auto item = begin; item != end; ++item) item->second = *output++;
    }
}

template<class Index>
void InsertUniqueIndexValue(Index& index, typename Index::key_type key,
                            typename Index::mapped_type value)
{
    const auto [first, last] = index.equal_range(key);
    if (std::find_if(first, last, [&](const auto& item) { return item.second == value; }) == last)
        InsertIndexValue(index, key, value);
}

template<class Index, class Key>
auto NextIndexValue(Index& index, Key key, typename Index::iterator previous)
{
    if (previous == index.end()) return index.equal_range(key).first;
    const auto next = std::next(previous);
    return next != index.end() && next->first == key ? next : index.end();
}

template<class Index, class Key>
auto LookupIndexValue(const Index& index, Key key)
{
    const auto [first, last] = index.equal_range(key);
    return first == last ? typename Index::mapped_type{} : first->second;
}

template<class Index, class Key, class Value>
void RemoveIndexValue(Index& index, Key key, Value value)
{
    const auto [first, last] = index.equal_range(key);
    const auto found = std::find_if(first, last, [&](const auto& item) {
        return item.second == value;
    });
    if (found != last) index.erase(found);
}

template<class Index, class Key>
void RemoveIndexValue(Index& index, Key key)
{
    const auto [first, last] = index.equal_range(key);
    if (first != last) index.erase(first);
}
